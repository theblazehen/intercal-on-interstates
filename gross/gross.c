/* gross — the General READ OUT Server System.
 *
 *   gross [-p PORT] [-d PUBLIC_DIR] [-b ADDR] [-t SECONDS] ./prog
 *
 * HTTP/1.1 with keep-alive, one forked process per connection. Files under
 * PUBLIC_DIR are served at /public/...; every other request runs ./prog as a
 * CGI/1.1 script (SPEC.md §3). See gross/README.md. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BODY_CAP (1u << 20)
#define HEAD_CAP (16u << 10)
#define OUTPUT_CAP (64u << 20)
#define MAX_HEADERS 100
#define MAX_CHILDREN 256
#define KEEPALIVE_IDLE_MS 5000
#define HEAD_TIMEOUT_MS 5000      /* first byte and request head, whatever -t says */
#define MAX_REQUESTS_PER_CONN 1000

extern char **environ;

static struct {
    int port;
    const char *bind;
    const char *public_dir;
    char *public_real;   /* NULL when the directory does not exist */
    char *prog;          /* absolute path */
    char *app_dir;
    char *db_path;
    char secret[129];
    int timeout_ms;
    int per_ip;          /* connections per client address; 0 = unlimited */
    int lifetime_ms;     /* no new request on a connection older than this */
    const char *hosts;   /* IOI_HOSTS allow-list, or NULL */
} cfg = {10585, NULL, "./public", NULL, NULL, NULL, NULL, "", 10000, 16, 60000, NULL};

/* Connection processes, so SIGTERM/SIGINT can take them down too, and their
 * client addresses for the per-address cap. kids[] is cleared from the
 * SIGCHLD handler; everything else is touched with SIGCHLD blocked. */
static volatile sig_atomic_t children;
static volatile pid_t kids[MAX_CHILDREN];
static char kid_addr[MAX_CHILDREN][64];

/* Is HOST (a Host header) on the comma-separated allow-list? An entry
 * without a port allows any port. */
static int host_allowed(const char *list, const char *host)
{
    if (!host || !*host)
        return 0;
    size_t hl = strlen(host);
    size_t bare = host[0] == '[' ? strcspn(host, "]") + 1 : strcspn(host, ":");
    if (bare > hl)
        bare = hl;
    for (const char *p = list; *p;) {
        while (*p == ',' || *p == ' ')
            p++;
        size_t n = strcspn(p, ", ");
        /* exact host:port, or the entry equals the host part (no port) */
        if (n && ((n == hl && !strncasecmp(p, host, n)) ||
                  (n == bare && !strncasecmp(p, host, n))))
            return 1;
        p += n;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* small utilities                                                  */

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("gross: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

struct buf {
    char *p;
    size_t len, cap;
};

static void buf_put(struct buf *b, const void *d, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
    if (n)
        memcpy(b->p + b->len, d, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void buf_printf(struct buf *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        buf_put(b, tmp, (size_t)n);
        return;
    }
    char *big = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_put(b, big, (size_t)n);
    free(big);
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int write_all(int fd, const void *p, size_t n)
{
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

static void http_date(time_t t, char out[64])
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, 64, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Percent-decode a path (no '+' rule). Rejects %00 and bad escapes. */
static char *path_decode(const char *s, size_t n)
{
    char *out = xmalloc(n + 1);
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%') {
            int hi, lo;
            if (i + 2 >= n) {
                free(out);
                return NULL;
            }
            hi = hexval((unsigned char)s[i + 1]);
            lo = hexval((unsigned char)s[i + 2]);
            if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) {
                free(out);
                return NULL;
            }
            out[o++] = (char)(hi << 4 | lo);
            i += 2;
        } else if (s[i] == 0) {
            free(out);
            return NULL;
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = 0;
    return out;
}

/* ---------------------------------------------------------------- */
/* connection input                                                 */

struct conn {
    int fd;
    char *in;
    size_t len, cap;
    char peer[64];
    char port[8];
    int closed;      /* peer sent EOF */
};

/* Read more bytes, waiting until deadline (absolute ms). 1 read, 0 EOF,
 * -1 timeout/error. */
static int fill(struct conn *c, int64_t deadline)
{
    if (c->len == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8192;
        c->in = xrealloc(c->in, c->cap);
    }
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0)
            return -1;
        struct pollfd p = {c->fd, POLLIN, 0};
        int r = poll(&p, 1, (int)left);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        ssize_t n = read(c->fd, c->in + c->len, c->cap - c->len);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n < 0)
            return -1;
        if (n == 0) {
            c->closed = 1;
            return 0;
        }
        c->len += (size_t)n;
        return 1;
    }
}

static void consume(struct conn *c, size_t n)
{
    memmove(c->in, c->in + n, c->len - n);
    c->len -= n;
}

/* Close politely: stop sending, then swallow what the client still sends
 * for a moment so it can read our response instead of a reset. */
static void lingering_close(struct conn *c)
{
    shutdown(c->fd, SHUT_WR);
    int64_t deadline = now_ms() + 2000;
    size_t drained = 0;
    char tmp[16384];
    while (drained < (8u << 20)) {
        int64_t left = deadline - now_ms();
        if (left <= 0)
            break;
        struct pollfd p = {c->fd, POLLIN, 0};
        if (poll(&p, 1, (int)left) <= 0)
            break;
        ssize_t n = read(c->fd, tmp, sizeof tmp);
        if (n <= 0)
            break;
        drained += (size_t)n;
    }
    close(c->fd);
}

/* ---------------------------------------------------------------- */
/* requests                                                          */

struct header {
    char *name, *value;
};

struct request {
    char method[16];
    char *target;      /* raw request-target */
    char *path;        /* raw path part (before '?'), origin-form */
    char *query;       /* raw query ("" if none) */
    int minor;         /* HTTP/1.minor */
    struct header h[MAX_HEADERS];
    int nh;
    char *body;
    size_t bodylen;
    int keepalive;
    int head;
    int64_t start;
    int status;        /* for the log */
    size_t sent;       /* body bytes, for the log */
    long long head_length; /* HEAD: Content-Length the program declared, or -1 */
};

static const char *hdr(const struct request *r, const char *name)
{
    for (int i = 0; i < r->nh; i++)
        if (!strcasecmp(r->h[i].name, name))
            return r->h[i].value;
    return NULL;
}

static void free_request(struct request *r)
{
    free(r->target);
    free(r->path);
    free(r->query);
    free(r->body);
    for (int i = 0; i < r->nh; i++) {
        free(r->h[i].name);
        free(r->h[i].value);
    }
}

static int tchar(int c)
{
    return isalnum(c) || strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static const char *reason(int s)
{
    switch (s) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Content";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default: return s < 400 ? "OK" : s < 500 ? "Client Error" : "Server Error";
    }
}

static void log_request(const struct conn *c, const struct request *r)
{
    char when[64], line[2048];
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(when, sizeof when, "%d/%b/%Y:%H:%M:%S +0000", &tm);
    char target[512];
    size_t n = 0;
    const char *src = r->target ? r->target : "-";
    for (; *src && n < sizeof target - 1; src++)
        target[n++] = (*src > 0x20 && *src < 0x7f && *src != '"') ? *src : '?';
    target[n] = 0;
    int len = snprintf(line, sizeof line, "%s - - [%s] \"%s %s HTTP/1.%d\" %d %zu %.1fms\n", c->peer,
                       when, r->method[0] ? r->method : "-", target, r->minor, r->status, r->sent,
                       (double)(now_ms() - r->start));
    if (len > 0)
        write_all(2, line, (size_t)len < sizeof line ? (size_t)len : sizeof line - 1);
}

/* Send a complete response. extra: preformatted header lines. */
static int respond(struct conn *c, struct request *r, int status, const char *extra,
                   const char *body, size_t len)
{
    char date[64];
    http_date(time(NULL), date);
    struct buf b = {0};
    buf_printf(&b, "HTTP/1.1 %d %s\r\nDate: %s\r\nServer: gross\r\n", status, reason(status), date);
    if (extra)
        buf_put(&b, extra, strlen(extra));
    int bodyless = status == 204 || status == 304;
    if (!bodyless && r->head && r->head_length >= 0)
        buf_printf(&b, "Content-Length: %lld\r\n", r->head_length);
    else if (!bodyless)
        buf_printf(&b, "Content-Length: %zu\r\n", len);
    buf_printf(&b, "Connection: %s\r\n\r\n", r->keepalive ? "keep-alive" : "close");
    if (!r->head && !bodyless)
        buf_put(&b, body, len);
    int rc = write_all(c->fd, b.p, b.len);
    free(b.p);
    r->status = status;
    r->sent = r->head || bodyless ? 0 : len;
    return rc;
}

static const char *voice(int status)
{
    switch (status) {
    case 400: return "ICL017I DO YOU EXPECT ME TO FIGURE THIS OUT?";
    case 404: return "ICL404I THERE IS NO SUCH FILE AMONG THE PUBLIC CASSETTES";
    case 405: return "ICL405I YOU MAY ONLY READ OUT A PUBLIC FILE";
    case 408: return "ICL408I YOU TOOK TOO LONG TO SAY THAT";
    case 411: return "ICL411I HOW LONG IS THAT SUPPOSED TO BE?";
    case 413: return "ICL413I THAT BODY IS TOO HEAVY TO WRITE IN";
    case 421: return "ICL421I THIS SERVER DOES NOT ANSWER TO THAT NAME";
    case 429: return "ICL429I YOU ARE ALREADY COMING FROM HERE TOO MANY TIMES";
    case 431: return "ICL431I YOUR HEADERS ARE LONGER THAN MY PATIENCE";
    case 500: return "ICL500I THE PROGRAM REFUSED TO BE STARTED";
    case 501: return "ICL501I I DO NOT KNOW HOW TO UNPACK THAT";
    case 502: return "ICL502I THE PROGRAM ANSWERED IN A LANGUAGE NOBODY SPEAKS";
    case 503: return "ICL503I TOO MANY REQUESTS ARE ALREADY COMING FROM HERE";
    case 504: return "ICL504I THE PROGRAM WOULD NOT GIVE UP, SO WE GAVE UP ON IT";
    case 505: return "ICL505I THAT IS NOT A DIALECT OF HTTP I SPEAK";
    default: return "ICL000I SOMETHING WENT WRONG IN AN UNEXPECTED WAY";
    }
}

static int respond_error(struct conn *c, struct request *r, int status, const char *extra)
{
    struct buf b = {0};
    buf_printf(&b,
               "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
               "<title>%d %s</title></head><body><h1>%d %s</h1><pre>%s\n"
               "        CORRECT SOURCE AND RESUBNIT</pre></body></html>\n",
               status, reason(status), status, reason(status), voice(status));
    struct buf h = {0};
    buf_printf(&h, "Content-Type: text/html; charset=utf-8\r\n%s", extra ? extra : "");
    int rc = respond(c, r, status, h.p, b.p, b.len);
    free(b.p);
    free(h.p);
    return rc;
}

/* Parse the head in c->in[0..headlen). 0 ok, else HTTP status to send. */
static int parse_head(struct conn *c, size_t headlen, struct request *r)
{
    char *p = c->in, *end = c->in + headlen;
    char *eol = memchr(p, '\n', (size_t)(end - p));
    if (!eol)
        return 400;
    char *le = eol > p && eol[-1] == '\r' ? eol - 1 : eol;
    /* request line: METHOD SP TARGET SP HTTP/1.x */
    char *sp1 = memchr(p, ' ', (size_t)(le - p));
    if (!sp1 || sp1 == p || (size_t)(sp1 - p) >= sizeof r->method)
        return 400;
    for (char *q = p; q < sp1; q++)
        if (!tchar((unsigned char)*q))
            return 400;
    memcpy(r->method, p, (size_t)(sp1 - p));
    r->method[sp1 - p] = 0;
    char *t = sp1 + 1;
    char *sp2 = memchr(t, ' ', (size_t)(le - t));
    if (!sp2 || sp2 == t)
        return 400;
    char *ver = sp2 + 1;
    if (le - ver != 8 || memcmp(ver, "HTTP/1.", 7) || !isdigit((unsigned char)ver[7])) {
        if (le - ver >= 5 && !memcmp(ver, "HTTP/", 5))
            return 505;
        return 400;
    }
    r->minor = ver[7] - '0';
    for (char *q = t; q < sp2; q++)
        if ((unsigned char)*q <= 0x20 || (unsigned char)*q == 0x7f)
            return 400;
    r->target = strndup(t, (size_t)(sp2 - t));
    const char *path = r->target;
    if (!strncasecmp(path, "http://", 7) || !strncasecmp(path, "https://", 8)) {
        path = strstr(path, "://") + 3;
        path += strcspn(path, "/");
        if (!*path)
            path = "/";
    }
    if (path[0] != '/')
        return 400;
    size_t pl = strcspn(path, "?#");
    r->path = strndup(path, pl);
    const char *q = path[pl] == '?' ? path + pl + 1 : "";
    r->query = strndup(q, strcspn(q, "#"));

    /* headers */
    p = eol + 1;
    while (p < end) {
        eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol)
            eol = end;
        le = eol > p && eol[-1] == '\r' ? eol - 1 : eol;
        if (le == p)
            break;
        if (*p == ' ' || *p == '\t')
            return 400; /* obsolete line folding */
        char *colon = memchr(p, ':', (size_t)(le - p));
        if (!colon || colon == p)
            return 400;
        for (char *k = p; k < colon; k++)
            if (!tchar((unsigned char)*k))
                return 400;
        char *v = colon + 1, *ve = le;
        while (v < ve && (*v == ' ' || *v == '\t'))
            v++;
        while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t'))
            ve--;
        for (char *k = v; k < ve; k++)
            if ((unsigned char)*k < 0x20 && *k != '\t')
                return 400;
        if (r->nh == MAX_HEADERS)
            return 431;
        r->h[r->nh].name = strndup(p, (size_t)(colon - p));
        r->h[r->nh].value = strndup(v, (size_t)(ve - v));
        r->nh++;
        p = eol + 1;
    }
    if (r->minor >= 1 && !hdr(r, "Host"))
        return 400;
    const char *conn = hdr(r, "Connection");
    if (r->minor >= 1)
        r->keepalive = !(conn && strcasestr(conn, "close"));
    else
        r->keepalive = conn && strcasestr(conn, "keep-alive");
    r->head = !strcmp(r->method, "HEAD");
    return 0;
}

/* Read the body per Content-Length / chunked. 0 ok, else status. */
static int read_body(struct conn *c, struct request *r, int64_t deadline)
{
    const char *te = NULL, *cl = NULL;
    for (int i = 0; i < r->nh; i++) {
        if (!strcasecmp(r->h[i].name, "Transfer-Encoding")) {
            if (te)
                return 400;
            te = r->h[i].value;
        } else if (!strcasecmp(r->h[i].name, "Content-Length")) {
            if (cl && strcmp(cl, r->h[i].value))
                return 400;
            cl = r->h[i].value;
        }
    }
    if (te && cl)
        return 400;
    const char *expect = hdr(r, "Expect");
    if (te) {
        if (strcasecmp(te, "chunked"))
            return 501;
        if (expect && !strcasecmp(expect, "100-continue"))
            write_all(c->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
        struct buf b = {0};
        for (;;) {
            char *nl;
            while (!(nl = memchr(c->in, '\n', c->len))) {
                if (c->len > 1024 || fill(c, deadline) <= 0) {
                    free(b.p);
                    return c->len > 1024 ? 400 : 408;
                }
            }
            /* chunk-size = 1*8HEXDIG, optional ";ext", then CRLF — nothing
             * else (no sign, prefix or blanks), and only on this line */
            size_t ll = (size_t)(nl - c->in);
            size_t k = 0, size = 0;
            while (k < ll && k < 9 && hexval((unsigned char)c->in[k]) >= 0)
                size = size * 16 + (size_t)hexval((unsigned char)c->in[k++]);
            if (k == 0 || k > 8 || ll == 0 || c->in[ll - 1] != '\r' ||
                (k != ll - 1 && c->in[k] != ';')) {
                free(b.p);
                return 400;
            }
            for (size_t x = k; x < ll - 1; x++)
                if ((unsigned char)c->in[x] < 0x20 || (unsigned char)c->in[x] == 0x7f) {
                    free(b.p);
                    return 400;
                }
            consume(c, ll + 1);
            if (size == 0)
                break;
            if (size > BODY_CAP || b.len > BODY_CAP - size) {
                free(b.p);
                return 413;
            }
            while (c->len < size + 2) {
                if (fill(c, deadline) <= 0) {
                    free(b.p);
                    return 408;
                }
            }
            if (c->in[size] != '\r' || c->in[size + 1] != '\n') {
                free(b.p);
                return 400;
            }
            buf_put(&b, c->in, size);
            consume(c, size + 2);
        }
        /* trailers until an empty line */
        for (;;) {
            char *nl;
            while (!(nl = memchr(c->in, '\n', c->len))) {
                if (c->len > HEAD_CAP || fill(c, deadline) <= 0) {
                    free(b.p);
                    return 400;
                }
            }
            size_t ll = (size_t)(nl - c->in);
            int empty = ll == 0 || (ll == 1 && c->in[0] == '\r');
            consume(c, ll + 1);
            if (empty)
                break;
        }
        r->body = b.p ? b.p : xstrdup("");
        r->bodylen = b.len;
        return 0;
    }
    unsigned long long n = 0;
    if (cl) {
        char *e;
        if (!*cl || !isdigit((unsigned char)*cl))
            return 400;
        errno = 0;
        n = strtoull(cl, &e, 10);
        if (*e || errno)
            return 400;
    }
    if (n > BODY_CAP)
        return 413;
    if (n && expect && !strcasecmp(expect, "100-continue"))
        write_all(c->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    while (c->len < n) {
        if (fill(c, deadline) <= 0)
            return 408;
    }
    r->body = xmalloc((size_t)n + 1);
    memcpy(r->body, c->in, (size_t)n);
    r->body[n] = 0;
    r->bodylen = (size_t)n;
    consume(c, (size_t)n);
    return 0;
}

/* ---------------------------------------------------------------- */
/* static files                                                      */

static const char *mime_type(const char *path)
{
    static const struct { const char *ext, *type; } types[] = {
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},   {"js", "text/javascript; charset=utf-8"},
        {"mjs", "text/javascript; charset=utf-8"}, {"json", "application/json"},
        {"txt", "text/plain; charset=utf-8"}, {"xml", "application/xml"},
        {"svg", "image/svg+xml"},             {"png", "image/png"},
        {"jpg", "image/jpeg"},                {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},                 {"webp", "image/webp"},
        {"avif", "image/avif"},               {"ico", "image/x-icon"},
        {"woff", "font/woff"},                {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},                  {"otf", "font/otf"},
        {"pdf", "application/pdf"},           {"wasm", "application/wasm"},
        {"map", "application/json"},          {"webmanifest", "application/manifest+json"},
        {"mp4", "video/mp4"},                 {"webm", "video/webm"},
        {"mp3", "audio/mpeg"},                {"ogg", "audio/ogg"},
        {"wav", "audio/wav"},                 {"i", "text/plain; charset=utf-8"},
    };
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(slash ? slash : path, '.');
    if (dot)
        for (size_t i = 0; i < sizeof types / sizeof types[0]; i++)
            if (!strcasecmp(dot + 1, types[i].ext))
                return types[i].type;
    return "application/octet-stream";
}

static int serve_static(struct conn *c, struct request *r)
{
    if (strcmp(r->method, "GET") && strcmp(r->method, "HEAD"))
        return respond_error(c, r, 405, "Allow: GET, HEAD\r\n");
    char *dec = path_decode(r->path, strlen(r->path));
    if (!dec)
        return respond_error(c, r, 400, NULL);
    /* "/public" itself has no file part (and is shorter than the prefix) */
    size_t declen = strlen(dec);
    const char *rel = declen > 8 ? dec + 8 : "";
    /* refuse any ".." segment outright, then confirm with realpath */
    int bad = 0;
    for (const char *s = rel; *s;) {
        size_t n = strcspn(s, "/\\");
        if (n == 2 && s[0] == '.' && s[1] == '.')
            bad = 1;
        s += n;
        if (*s)
            s++;
    }
    struct stat st;
    char full[PATH_MAX * 2], real[PATH_MAX];
    int found = 0;
    if (!bad && cfg.public_real && *rel) {
        snprintf(full, sizeof full, "%s/%s", cfg.public_real, rel);
        size_t pl = strlen(cfg.public_real);
        if (realpath(full, real) && !strncmp(real, cfg.public_real, pl) && real[pl] == '/' &&
            stat(real, &st) == 0 && S_ISREG(st.st_mode))
            found = 1;
    }
    free(dec);
    if (!found)
        return respond_error(c, r, 404, NULL);
    int fd = open(real, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return respond_error(c, r, 404, NULL);
    char lm[64], date[64];
    http_date(st.st_mtime, lm);
    http_date(time(NULL), date);
    const char *ims = hdr(r, "If-Modified-Since");
    struct buf h = {0};
    if (ims && !strcmp(ims, lm)) {
        close(fd);
        buf_printf(&h, "HTTP/1.1 304 Not Modified\r\nDate: %s\r\nServer: gross\r\n"
                       "Last-Modified: %s\r\nCache-Control: no-cache\r\nConnection: %s\r\n\r\n",
                   date, lm, r->keepalive ? "keep-alive" : "close");
        int rc = write_all(c->fd, h.p, h.len);
        free(h.p);
        r->status = 304;
        return rc;
    }
    buf_printf(&h, "HTTP/1.1 200 OK\r\nDate: %s\r\nServer: gross\r\nContent-Type: %s\r\n"
                   "Content-Length: %lld\r\nLast-Modified: %s\r\nCache-Control: no-cache\r\n"
                   "X-Content-Type-Options: nosniff\r\nConnection: %s\r\n\r\n",
               date, mime_type(real), (long long)st.st_size, lm, r->keepalive ? "keep-alive" : "close");
    int rc = write_all(c->fd, h.p, h.len);
    free(h.p);
    r->status = 200;
    if (!rc && !r->head) {
        char chunk[65536];
        off_t left = st.st_size;
        while (left > 0) {
            ssize_t n = read(fd, chunk, sizeof chunk);
            if (n <= 0 || write_all(c->fd, chunk, (size_t)n)) {
                rc = -1;
                break;
            }
            left -= n;
            r->sent += (size_t)n;
        }
        if (left > 0)
            rc = -1; /* file shrank: the framing is broken, drop the connection */
    }
    close(fd);
    return rc;
}

/* ---------------------------------------------------------------- */
/* CGI                                                              */

struct envb {
    char **v;
    size_t n, cap;
};

static void env_add(struct envb *e, const char *name, const char *value)
{
    if (e->n + 2 > e->cap) {
        e->cap = e->cap ? e->cap * 2 : 64;
        e->v = xrealloc(e->v, e->cap * sizeof *e->v);
    }
    size_t n = strlen(name) + strlen(value) + 2;
    char *s = xmalloc(n);
    snprintf(s, n, "%s=%s", name, value);
    e->v[e->n++] = s;
    e->v[e->n] = NULL;
}

static char **build_env(const struct conn *c, const struct request *r, const char *path_info)
{
    struct envb e = {0};
    /* pass through the operator's IOI_* settings except those we own */
    for (char **p = environ; *p; p++) {
        if (strncmp(*p, "IOI_", 4) || !strncmp(*p, "IOI_SECRET=", 11) ||
            !strncmp(*p, "IOI_DATABASE=", 13) || !strncmp(*p, "IOI_APP_DIR=", 12))
            continue;
        char *eq = strchr(*p, '=');
        if (!eq)
            continue;
        char name[256];
        size_t nl = (size_t)(eq - *p);
        if (nl >= sizeof name)
            continue;
        memcpy(name, *p, nl);
        name[nl] = 0;
        env_add(&e, name, eq + 1);
    }
    const char *path = getenv("PATH");
    env_add(&e, "PATH", path ? path : "/usr/local/bin:/usr/bin:/bin");
    const char *tz = getenv("TZ");
    if (tz)
        env_add(&e, "TZ", tz);
    env_add(&e, "GATEWAY_INTERFACE", "CGI/1.1");
    env_add(&e, "SERVER_SOFTWARE", "gross");
    char proto[16];
    snprintf(proto, sizeof proto, "HTTP/1.%d", r->minor);
    env_add(&e, "SERVER_PROTOCOL", proto);
    char port[16];
    snprintf(port, sizeof port, "%d", cfg.port);
    env_add(&e, "SERVER_PORT", port);
    const char *host = hdr(r, "Host");
    char sname[256] = "localhost";
    if (host && *host) {
        size_t hl = host[0] == '[' ? strcspn(host, "]") + 1 : strcspn(host, ":");
        if (hl < sizeof sname) {
            memcpy(sname, host, hl);
            sname[hl] = 0;
        }
    }
    env_add(&e, "SERVER_NAME", sname);
    env_add(&e, "REQUEST_METHOD", r->method);
    env_add(&e, "REQUEST_URI", r->target);
    env_add(&e, "SCRIPT_NAME", "");
    env_add(&e, "PATH_INFO", path_info);
    env_add(&e, "QUERY_STRING", r->query);
    env_add(&e, "REMOTE_ADDR", c->peer);
    env_add(&e, "REMOTE_PORT", c->port);
    const char *ct = hdr(r, "Content-Type");
    if (ct)
        env_add(&e, "CONTENT_TYPE", ct);
    if (r->bodylen || hdr(r, "Content-Length") || hdr(r, "Transfer-Encoding")) {
        char cl[32];
        snprintf(cl, sizeof cl, "%zu", r->bodylen);
        env_add(&e, "CONTENT_LENGTH", cl);
    }
    /* every other header as HTTP_*; repeated headers are joined */
    for (int i = 0; i < r->nh; i++) {
        const char *n = r->h[i].name;
        if (!strcasecmp(n, "Content-Type") || !strcasecmp(n, "Content-Length") ||
            !strcasecmp(n, "Proxy") || strchr(n, '_'))
            continue;
        int seen = 0;
        for (int j = 0; j < i; j++)
            if (!strcasecmp(r->h[j].name, n))
                seen = 1;
        if (seen)
            continue;
        struct buf v = {0};
        const char *sep = !strcasecmp(n, "Cookie") ? "; " : ", ";
        for (int j = i; j < r->nh; j++) {
            if (strcasecmp(r->h[j].name, n))
                continue;
            if (v.len)
                buf_put(&v, sep, strlen(sep));
            buf_put(&v, r->h[j].value, strlen(r->h[j].value));
        }
        char name[300] = "HTTP_";
        size_t k = 5;
        for (; *n && k < sizeof name - 1; n++)
            name[k++] = *n == '-' ? '_' : (char)toupper((unsigned char)*n);
        name[k] = 0;
        env_add(&e, name, v.p ? v.p : "");
        free(v.p);
    }
    env_add(&e, "IOI_SECRET", cfg.secret);
    env_add(&e, "IOI_DATABASE", cfg.db_path);
    env_add(&e, "IOI_APP_DIR", cfg.app_dir);
    return e.v;
}

static void free_env(char **v)
{
    for (char **p = v; *p; p++)
        free(*p);
    free(v);
}

/* Run the program; on success fills *out with its whole stdout. Returns 0,
 * or the HTTP status to answer with (500 start failure, 504 timeout, 502
 * oversized output). */
static int run_cgi(const struct conn *c, const struct request *r, const char *path_info,
                   struct buf *out)
{
    int in_fd;
    if (r->bodylen) {
        FILE *tf = tmpfile();
        if (!tf)
            return 500;
        if (fwrite(r->body, 1, r->bodylen, tf) != r->bodylen || fflush(tf)) {
            fclose(tf);
            return 500;
        }
        in_fd = dup(fileno(tf));
        fclose(tf);
        lseek(in_fd, 0, SEEK_SET);
    } else {
        in_fd = open("/dev/null", O_RDONLY);
    }
    if (in_fd < 0)
        return 500;
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC)) {
        close(in_fd);
        return 500;
    }
    char **envp = build_env(c, r, path_info);
    pid_t pid = fork();
    if (pid < 0) {
        close(in_fd);
        close(pfd[0]);
        close(pfd[1]);
        free_env(envp);
        return 500;
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(in_fd, 0);
        dup2(pfd[1], 1);
        if (in_fd > 2)
            close(in_fd);
        signal(SIGPIPE, SIG_DFL);
        char *argv[] = {cfg.prog, NULL};
        execve(cfg.prog, argv, envp);
        _exit(127);
    }
    setpgid(pid, pid);
    close(in_fd);
    close(pfd[1]);
    free_env(envp);

    int64_t deadline = r->start + cfg.timeout_ms;
    int status = 0, timed_out = 0, too_big = 0;
    char chunk[65536];
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) {
            timed_out = 1;
            break;
        }
        struct pollfd p = {pfd[0], POLLIN, 0};
        int rr = poll(&p, 1, (int)left);
        if (rr < 0 && errno == EINTR)
            continue;
        if (rr == 0) {
            timed_out = 1;
            break;
        }
        ssize_t n = read(pfd[0], chunk, sizeof chunk);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        if (out->len + (size_t)n > OUTPUT_CAP) {
            too_big = 1;
            break;
        }
        buf_put(out, chunk, (size_t)n);
    }
    close(pfd[0]);
    if (timed_out || too_big) {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
    }
    /* stdout closed: the program is done or about to be; don't hang on a
     * straggler that closed stdout early either */
    for (;;) {
        pid_t w = waitpid(pid, &status, timed_out || too_big ? 0 : WNOHANG);
        if (w == pid || (w < 0 && errno != EINTR))
            break;
        if (w == 0) {
            if (now_ms() >= deadline) {
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                break;
            }
            struct timespec ts = {0, 2000000};
            nanosleep(&ts, NULL);
        }
    }
    if (timed_out)
        return 504;
    if (too_big)
        return 502;
    if (out->len == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 127)
        return 500;
    return 0;
}

static int valid_header_name(const char *s, size_t n)
{
    if (!n)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!tchar((unsigned char)s[i]))
            return 0;
    return 1;
}

static int serve_cgi(struct conn *c, struct request *r)
{
    char *path_info = path_decode(r->path, strlen(r->path));
    if (!path_info)
        return respond_error(c, r, 400, NULL);
    struct buf out = {0};
    int err = run_cgi(c, r, path_info, &out);
    free(path_info);
    if (err) {
        free(out.p);
        return respond_error(c, r, err, NULL);
    }
    /* split CGI headers from body */
    char *p = out.p ? out.p : "";
    char *body = NULL;
    for (size_t i = 0; i < out.len; i++) {
        if (p[i] != '\n')
            continue;
        if (i + 1 < out.len && p[i + 1] == '\n') {
            body = p + i + 2;
            break;
        }
        if (i + 2 < out.len && p[i + 1] == '\r' && p[i + 2] == '\n') {
            body = p + i + 3;
            break;
        }
    }
    if (!body) {
        free(out.p);
        return respond_error(c, r, 502, NULL);
    }
    int status = 0, have_type = 0, have_location = 0;
    struct buf h = {0};
    for (char *line = p; line < body;) {
        char *nl = memchr(line, '\n', (size_t)(body - line));
        char *le = nl > line && nl[-1] == '\r' ? nl - 1 : nl;
        if (le == line)
            break;
        char *colon = memchr(line, ':', (size_t)(le - line));
        if (!colon || !valid_header_name(line, (size_t)(colon - line))) {
            free(out.p);
            free(h.p);
            return respond_error(c, r, 502, NULL);
        }
        char *v = colon + 1;
        while (v < le && (*v == ' ' || *v == '\t'))
            v++;
        size_t nlen = (size_t)(colon - line), vlen = (size_t)(le - v);
        if (nlen == 6 && !strncasecmp(line, "Status", 6)) {
            status = atoi(v);
            if (status < 100 || status > 999) {
                free(out.p);
                free(h.p);
                return respond_error(c, r, 502, NULL);
            }
        } else if ((nlen == 10 && !strncasecmp(line, "Connection", 10)) ||
                   (nlen == 14 && !strncasecmp(line, "Content-Length", 14)) ||
                   (nlen == 17 && !strncasecmp(line, "Transfer-Encoding", 17)) ||
                   (nlen == 10 && !strncasecmp(line, "Keep-Alive", 10))) {
            /* framing is ours; a HEAD answer keeps the declared length */
            if (nlen == 14 && r->head && vlen && vlen < 19 && strspn(v, "0123456789") == vlen)
                r->head_length = strtoll(v, NULL, 10);
        } else {
            if (nlen == 12 && !strncasecmp(line, "Content-Type", 12))
                have_type = 1;
            if (nlen == 8 && !strncasecmp(line, "Location", 8))
                have_location = 1;
            buf_put(&h, line, nlen);
            buf_put(&h, ": ", 2);
            buf_put(&h, v, vlen);
            buf_put(&h, "\r\n", 2);
        }
        line = nl + 1;
    }
    if (!status)
        status = have_location ? 302 : 200;
    if (!have_type)
        buf_printf(&h, "Content-Type: text/html; charset=utf-8\r\n");
    int rc = respond(c, r, status, h.p, body, out.len - (size_t)(body - p));
    free(h.p);
    free(out.p);
    return rc;
}

/* ---------------------------------------------------------------- */
/* connection loop                                                   */

static void handle_connection(struct conn *c)
{
    int64_t end_of_life = now_ms() + cfg.lifetime_ms;
    int64_t head_ms = cfg.timeout_ms < HEAD_TIMEOUT_MS ? cfg.timeout_ms : HEAD_TIMEOUT_MS;
    for (int served = 0; served < MAX_REQUESTS_PER_CONN; served++) {
        struct request r;
        memset(&r, 0, sizeof r);
        r.minor = 1;
        r.head_length = -1;
        /* wait for the first byte of the next request: briefly, and never
         * past the connection's lifetime */
        int64_t idle = now_ms() + (served ? KEEPALIVE_IDLE_MS : head_ms);
        if (idle > end_of_life)
            idle = end_of_life;
        while (c->len == 0) {
            if (fill(c, idle) <= 0)
                goto out;
        }
        r.start = now_ms();
        /* the head must arrive within head_ms; the body within -t */
        int64_t deadline = r.start + head_ms;
        /* skip stray CRLFs between requests (RFC 9112 §2.2) */
        while (c->len && (c->in[0] == '\r' || c->in[0] == '\n')) {
            consume(c, 1);
            if (!c->len && fill(c, deadline) <= 0)
                goto out;
        }
        size_t headlen = 0;
        int status = 0;
        for (;;) {
            char *e = NULL;
            for (size_t i = 0; i + 1 < c->len; i++) {
                if (c->in[i] == '\n' && (c->in[i + 1] == '\n' ||
                                         (c->in[i + 1] == '\r' && i + 2 < c->len && c->in[i + 2] == '\n'))) {
                    e = c->in + i + (c->in[i + 1] == '\n' ? 2 : 3);
                    break;
                }
            }
            if (e) {
                headlen = (size_t)(e - c->in);
                if (headlen > HEAD_CAP)
                    status = 431;
                break;
            }
            if (c->len > HEAD_CAP) {
                status = 431;
                break;
            }
            int f = fill(c, deadline);
            if (f < 0) {
                status = 408;
                break;
            }
            if (f == 0) {
                status = c->len ? 400 : -1;
                break;
            }
        }
        if (status == -1)
            goto out;
        if (!status) {
            status = parse_head(c, headlen, &r);
            if (headlen <= c->len)
                consume(c, headlen);
        }
        if (!status && cfg.hosts && !host_allowed(cfg.hosts, hdr(&r, "Host")))
            status = 421;
        deadline = r.start + cfg.timeout_ms;
        if (!status)
            status = read_body(c, &r, deadline);
        if (status) {
            r.keepalive = 0;
            respond_error(c, &r, status, NULL);
            log_request(c, &r);
            free_request(&r);
            lingering_close(c);
            return;
        }
        int rc;
        if (now_ms() >= end_of_life)
            r.keepalive = 0;
        if (!strncmp(r.path, "/public/", 8) || !strcmp(r.path, "/public"))
            rc = serve_static(c, &r);
        else
            rc = serve_cgi(c, &r);
        log_request(c, &r);
        int keep = r.keepalive && rc == 0 && now_ms() < end_of_life;
        free_request(&r);
        if (!keep)
            break;
    }
out:
    close(c->fd);
}

/* ---------------------------------------------------------------- */
/* startup                                                           */

static void load_secret(void)
{
    const char *path = ".gross_secret";
    for (int attempt = 0; attempt < 2; attempt++) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) == 0 && (st.st_mode & 077))
                fchmod(fd, 0600);
            char buf[256];
            ssize_t n = read(fd, buf, sizeof buf - 1);
            close(fd);
            if (n < 0)
                die("cannot read %s", path);
            buf[n] = 0;
            size_t l = strcspn(buf, "\r\n \t");
            if (l < 32 || l >= sizeof cfg.secret)
                die("%s is too short or too long; delete it to make a new one", path);
            memcpy(cfg.secret, buf, l);
            cfg.secret[l] = 0;
            return;
        }
        if (errno != ENOENT)
            die("cannot open %s: %s", path, strerror(errno));
        unsigned char raw[32];
        int rfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (rfd < 0 || read(rfd, raw, sizeof raw) != (ssize_t)sizeof raw)
            die("cannot read /dev/urandom");
        close(rfd);
        static const char hx[] = "0123456789abcdef";
        char hex[66];
        for (int i = 0; i < 32; i++) {
            hex[2 * i] = hx[raw[i] >> 4];
            hex[2 * i + 1] = hx[raw[i] & 15];
        }
        hex[64] = '\n';
        hex[65] = 0;
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            die("cannot create %s: %s", path, strerror(errno));
        }
        if (write_all(fd, hex, 65))
            die("cannot write %s", path);
        close(fd);
    }
    die("cannot settle on a secret");
}

static void on_child(int sig)
{
    (void)sig;
    int saved = errno;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_CHILDREN; i++)
            if (kids[i] == pid) {
                kids[i] = 0;
                children--;
                break;
            }
    }
    errno = saved;
}

static void on_term(int sig)
{
    for (int i = 0; i < MAX_CHILDREN; i++)
        if (kids[i] > 0)
            kill(kids[i], sig);
    _exit(0);
}

static int listen_on(const char *addr, int port)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    const char *tries[2] = {addr, NULL};
    if (!addr) {
        tries[0] = "::";
        tries[1] = "0.0.0.0";
    }
    for (int t = 0; t < 2 && tries[t]; t++) {
        if (getaddrinfo(tries[t], ps, &hints, &res))
            continue;
        int fd = socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0) {
            int one = 1, zero = 0;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (res->ai_family == AF_INET6)
                setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
            if (bind(fd, res->ai_addr, res->ai_addrlen) == 0 && listen(fd, 128) == 0) {
                freeaddrinfo(res);
                return fd;
            }
            close(fd);
        }
        freeaddrinfo(res);
    }
    die("cannot listen on %s port %d: %s", addr ? addr : "*", port, strerror(errno));
    return -1;
}

static void usage(void)
{
    fputs("usage: gross [-p PORT] [-d PUBLIC_DIR] [-b ADDRESS] [-t SECONDS] [-c N] [-l SECONDS] ./prog\n"
          "  -p PORT        listen port (default 10585)\n"
          "  -d PUBLIC_DIR  files served at /public/... (default ./public)\n"
          "  -b ADDRESS     bind address (default all interfaces)\n"
          "  -t SECONDS     per-request time limit (default 10)\n"
          "  -c N           connections per client address (default 16, 0 = no limit)\n"
          "  -l SECONDS     keep-alive connection lifetime (default 60)\n"
          "IOI_HOSTS=host[:port],...  answer only these Host headers (421 otherwise)\n",
          stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "p:d:b:t:c:l:h")) != -1) {
        switch (opt) {
        case 'p':
            cfg.port = atoi(optarg);
            if (cfg.port <= 0 || cfg.port > 65535)
                die("bad port %s", optarg);
            break;
        case 'd': cfg.public_dir = optarg; break;
        case 'b': cfg.bind = optarg; break;
        case 't': {
            int s = atoi(optarg);
            if (s <= 0 || s > 3600)
                die("bad timeout %s", optarg);
            cfg.timeout_ms = s * 1000;
            break;
        }
        case 'c':
            cfg.per_ip = atoi(optarg);
            if (cfg.per_ip < 0 || cfg.per_ip > MAX_CHILDREN)
                die("bad per-client limit %s", optarg);
            break;
        case 'l': {
            int s = atoi(optarg);
            if (s <= 0 || s > 86400)
                die("bad lifetime %s", optarg);
            cfg.lifetime_ms = s * 1000;
            break;
        }
        default: usage();
        }
    }
    if (optind != argc - 1)
        usage();
    char real[PATH_MAX];
    if (!realpath(argv[optind], real) || access(real, X_OK))
        die("cannot run %s: %s", argv[optind], strerror(errno ? errno : EACCES));
    cfg.prog = xstrdup(real);
    if (realpath(cfg.public_dir, real))
        cfg.public_real = xstrdup(real);
    const char *hosts = getenv("IOI_HOSTS");
    if (hosts && *hosts)
        cfg.hosts = hosts;
    const char *app = getenv("IOI_APP_DIR");
    if (app && *app) {
        cfg.app_dir = xstrdup(app);
    } else {
        cfg.app_dir = xstrdup(cfg.prog);
        *strrchr(cfg.app_dir, '/') = 0;
        if (!*cfg.app_dir)
            strcpy(cfg.app_dir, "/");
    }
    const char *dbp = getenv("IOI_DATABASE");
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        die("cannot find the current directory");
    if (dbp && *dbp) {
        cfg.db_path = xstrdup(dbp);
    } else {
        size_t n = strlen(cwd) + sizeof "/cassette.db";
        cfg.db_path = xmalloc(n);
        snprintf(cfg.db_path, n, "%s/cassette.db", cwd);
    }
    load_secret();

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = {0};
    sa.sa_handler = on_child;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGINT, on_term);
    signal(SIGTERM, on_term);

    int lfd = listen_on(cfg.bind, cfg.port);
    fprintf(stderr, "gross: READ OUT %s on port %d (public: %s)\n", cfg.prog, cfg.port,
            cfg.public_real ? cfg.public_real : "none");

    for (;;) {
        struct sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        int fd = accept4(lfd, (struct sockaddr *)&ss, &sl, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED || errno == EMFILE || errno == ENFILE) {
                if (errno == EMFILE || errno == ENFILE)
                    sleep(1);
                continue;
            }
            die("accept: %s", strerror(errno));
        }
        struct conn c = {0};
        c.fd = fd;
        char host[INET6_ADDRSTRLEN + 1] = "-", serv[8] = "0";
        getnameinfo((struct sockaddr *)&ss, sl, host, sizeof host, serv, sizeof serv,
                    NI_NUMERICHOST | NI_NUMERICSERV);
        const char *h = host;
        if (!strncmp(h, "::ffff:", 7) && strchr(h, '.'))
            h += 7;
        snprintf(c.peer, sizeof c.peer, "%s", h);
        snprintf(c.port, sizeof c.port, "%s", serv);
        sigset_t set, old;
        sigemptyset(&set);
        sigaddset(&set, SIGCHLD);
        sigprocmask(SIG_BLOCK, &set, &old);
        int same = 0;
        for (int i = 0; i < MAX_CHILDREN; i++)
            if (kids[i] && !strcmp(kid_addr[i], c.peer))
                same++;
        int busy = children >= MAX_CHILDREN ? 503 : cfg.per_ip && same >= cfg.per_ip ? 429 : 0;
        if (busy) {
            sigprocmask(SIG_SETMASK, &old, NULL);
            struct request r;
            memset(&r, 0, sizeof r);
            r.start = now_ms();
            r.minor = 1;
            respond_error(&c, &r, busy, "Retry-After: 1\r\n");
            log_request(&c, &r);
            /* swallow what already arrived so close() doesn't reset the
             * connection under the client before it reads the answer */
            char junk[4096];
            while (recv(fd, junk, sizeof junk, MSG_DONTWAIT) > 0)
                ;
            shutdown(fd, SHUT_WR);
            close(fd);
            continue;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        pid_t pid = fork();
        if (pid == 0) {
            close(lfd);
            signal(SIGCHLD, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            sigprocmask(SIG_SETMASK, &old, NULL);
            handle_connection(&c);
            _exit(0);
        }
        if (pid > 0) {
            for (int i = 0; i < MAX_CHILDREN; i++)
                if (kids[i] == 0) {
                    kids[i] = pid;
                    snprintf(kid_addr[i], sizeof kid_addr[i], "%s", c.peer);
                    children++;
                    break;
                }
        }
        sigprocmask(SIG_SETMASK, &old, NULL);
        close(fd);
    }
}
