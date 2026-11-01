/* response.c — buffered CGI response, redirects, error pages, AGO, FACE. */
#include "ioi_web.h"
#include "web_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HTML_TYPE "text/html; charset=utf-8"

static struct {
    int begun, finished;
    int status;
    char *ctype;
    char *location;
    struct ioi_buf body;
} rs;

void ioi_web_response_reset(void)
{
    free(rs.ctype);
    free(rs.location);
    free(rs.body.p);
    memset(&rs, 0, sizeof rs);
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(ioi_xmalloc(n), s, n);
}

/* Header values must not carry CR/LF. */
static char *header_safe(const char *s)
{
    char *p = xstrdup(s);
    for (char *q = p; *q; q++)
        if (*q == '\r' || *q == '\n')
            *q = ' ';
    return p;
}

void ioi_web_set_status(int status) { rs.status = status; }

void ioi_web_set_content_type(const char *ct)
{
    free(rs.ctype);
    rs.ctype = header_safe(ct ? ct : HTML_TYPE);
}

void ioi_web_begin(int status, const char *ct)
{
    if (rs.begun)
        return;
    rs.begun = 1;
    if (!rs.status)
        rs.status = status ? status : 200;
    if (!rs.ctype)
        ioi_web_set_content_type(ct);
}

const char *ioi_web_type_for(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot && !strcmp(dot, ".txt"))
        return "text/plain; charset=utf-8";
    if (dot && !strcmp(dot, ".xml"))
        return "application/xml; charset=utf-8";
    if (dot && !strcmp(dot, ".json"))
        return "application/json; charset=utf-8";
    return HTML_TYPE;
}

void ioi_web_append_raw(const void *data, size_t len) { ioi_buf_put(&rs.body, data, len); }

void ioi_web_append_escaped(const void *data, size_t len) { ioi_buf_escape(&rs.body, data, len); }

void ioi_web_append_prose(const void *data, size_t len)
{
    ioi_prose_render(&rs.body, data, len);
}

void ioi_web_append_face(const void *name, size_t len) { ioi_face_render(&rs.body, name, len); }

void ioi_web_append_ago(uint32_t v)
{
    int64_t when;
    switch (ioi_val_kind(v)) {
    case IOI_NUMBER:
        ioi_ago_render(&rs.body, ioi_val_to_number(v, NULL), ioi_web_time());
        return;
    case IOI_BYTES: {
        size_t len;
        const uint8_t *d = ioi_cas_data(v, &len);
        if (ioi_parse_when((const char *)d, len, &when))
            ioi_ago_render(&rs.body, when, ioi_web_time());
        else
            ioi_buf_escape(&rs.body, d, len);
        return;
    }
    case IOI_LIST:
        for (size_t i = 0; i < ioi_cas_len(v); i++)
            ioi_web_append_ago(ioi_cas_get(v, i));
        return;
    }
}

size_t ioi_web_mark(void) { return rs.body.len; }

void ioi_web_redirect_since(size_t mark)
{
    if (mark > rs.body.len)
        mark = rs.body.len;
    const char *s = rs.body.p ? rs.body.p + mark : "";
    size_t n = rs.body.len - mark;
    while (n && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
        s++, n--;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    static const struct { const char *ent; char c; } ents[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''},
    };
    struct ioi_buf loc = {0};
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        size_t step = 1;
        if (c == '&') {
            for (size_t e = 0; e < sizeof ents / sizeof ents[0]; e++) {
                size_t el = strlen(ents[e].ent);
                if (n - i >= el && !memcmp(s + i, ents[e].ent, el)) {
                    c = (unsigned char)ents[e].c;
                    step = el;
                    break;
                }
            }
        }
        i += step;
        if (c == '\r' || c == '\n')
            continue;
        if (c <= 0x20 || c >= 0x7f)
            ioi_buf_printf(&loc, "%%%02X", c);
        else
            ioi_buf_putc(&loc, (char)c);
    }
    rs.body.len = mark;
    if (rs.body.p)
        rs.body.p[mark] = 0;
    free(rs.location);
    rs.location = loc.p ? loc.p : xstrdup("/");
    rs.status = 303;
    if (!rs.ctype)
        ioi_web_set_content_type(NULL);
    rs.begun = 1;
}

static const char *reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 413: return "Content Too Large";
    case 422: return "Unprocessable Content";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return status < 400 ? "OK" : status < 500 ? "Client Error" : "Server Error";
    }
}

static void write_all(const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(1, p, n);
        if (w <= 0)
            return;
        p += w;
        n -= (size_t)w;
    }
}

void ioi_web_finish(void)
{
    if (rs.finished)
        return;
    rs.finished = 1;
    fflush(stdout);
    if (!ioi_web_active()) {
        write_all(rs.body.p ? rs.body.p : "", rs.body.len);
        return;
    }
    int status = rs.status ? rs.status : 200;
    struct ioi_buf h = {0};
    ioi_buf_printf(&h, "Status: %d %s\r\n", status, reason(status));
    ioi_buf_printf(&h, "Content-Type: %s\r\n", rs.ctype ? rs.ctype : HTML_TYPE);
    if (rs.location)
        ioi_buf_printf(&h, "Location: %s\r\n", rs.location);
    const char *cookie = ioi_session_header();
    if (cookie)
        ioi_buf_printf(&h, "Set-Cookie: %s\r\n", cookie);
    ioi_buf_puts(&h, "Cache-Control: private, no-cache\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "X-Frame-Options: DENY\r\n"
                     "Referrer-Policy: same-origin\r\n"
                     "Content-Security-Policy: default-src 'self'; script-src 'self'; "
                     "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                     "form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n");
    int head = !strcmp(ioi_web_method(), "HEAD");
    ioi_buf_printf(&h, "Content-Length: %zu\r\n\r\n", rs.body.len);
    if (!head)
        ioi_buf_put(&h, rs.body.p ? rs.body.p : "", rs.body.len);
    write_all(h.p, h.len);
    free(h.p);
}

void ioi_web_error_page(int status, const char *message)
{
    if (!ioi_web_active() || rs.finished)
        return;
    rs.body.len = 0;
    free(rs.location);
    rs.location = NULL;
    rs.status = status;
    rs.begun = 1;
    ioi_web_set_content_type(NULL);
    struct ioi_buf *b = &rs.body;
    /* Title: the ICLnnnI code when the message starts with one. */
    char code[16] = "";
    if (message && !strncmp(message, "ICL", 3) && strlen(message) >= 7)
        snprintf(code, sizeof code, "%.7s", message);
    ioi_buf_puts(b, "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    "<title>");
    if (*code)
        ioi_buf_printf(b, "%s \xe2\x80\x94 ", code);
    ioi_buf_printf(b, "%d %s</title><style>", status, reason(status));
    ioi_buf_puts(b,
        "body{margin:0;font:16px/1.5 system-ui,sans-serif;background:#f3f5f9;color:#1b2333}"
        ".ick-error{max-width:46rem;margin:12vh auto;padding:0 1.25rem}"
        ".ick-error h1{margin:0 0 .25rem;color:#0b2a5b;font-size:1.6rem}"
        ".ick-error .ick-sub{margin:0 0 1rem;color:#4a5568}"
        ".ick-error pre{white-space:pre-wrap;word-break:break-word;background:#0b2a5b;color:#e8f0ff;"
        "padding:1rem 1.25rem;border-radius:6px;border-left:6px solid #1f8a4c;font-size:.95rem}"
        ".ick-error a{color:#1f6f43}"
        "@media (prefers-color-scheme:dark){body{background:#0d1320;color:#dbe3f0}"
        ".ick-error h1{color:#9cc3ff}.ick-error .ick-sub{color:#a0aec0}"
        ".ick-error pre{background:#111c33}.ick-error a{color:#7fd6a4}}"
        "</style></head><body><main class=\"ick-error\">");
    ioi_buf_printf(b, "<h1>%d %s</h1>", status, reason(status));
    const char *sub = status == 404   ? "THE PROGRAM CAME FROM NOWHERE AND FOUND NOTHING THERE."
                      : status == 403 ? "THIS PAGE CAN ONLY BE COME FROM ELSEWHERE ON THIS SITE."
                                      : "THE PROGRAM HAS GIVEN UP, AND NOT POLITELY.";
    ioi_buf_printf(b, "<p class=\"ick-sub\">%s</p><pre>", sub);
    if (message)
        ioi_buf_escape(b, message, strlen(message));
    ioi_buf_puts(b, "</pre><p><a href=\"/\">PLEASE COME FROM (16000)</a></p></main></body></html>\n");
    ioi_web_finish();
}

/* ---------------------------------------------------------------- */
/* AGO                                                              */

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int digits(const char *s, size_t n, int *out)
{
    int v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 1;
}

int ioi_parse_when(const char *s, size_t len, int64_t *when)
{
    while (len && (*s == ' ' || *s == '\t'))
        s++, len--;
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        len--;
    if (len == 0)
        return 0;
    if (len <= 12) {
        int64_t v = 0;
        size_t i = 0;
        for (; i < len && s[i] >= '0' && s[i] <= '9'; i++)
            v = v * 10 + (s[i] - '0');
        if (i == len) {
            *when = v;
            return 1;
        }
    }
    int y, mo, d, h = 0, mi = 0, se = 0;
    if (len < 10 || s[4] != '-' || s[7] != '-' || !digits(s, 4, &y) || !digits(s + 5, 2, &mo) ||
        !digits(s + 8, 2, &d))
        return 0;
    size_t rest = len - 10;
    const char *t = s + 10;
    if (rest && t[rest - 1] == 'Z')
        rest--;
    if (rest) {
        if (rest != 9 || (t[0] != ' ' && t[0] != 'T') || t[3] != ':' || t[6] != ':' ||
            !digits(t + 1, 2, &h) || !digits(t + 4, 2, &mi) || !digits(t + 7, 2, &se))
            return 0;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60)
        return 0;
    *when = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
    return 1;
}

void ioi_ago_render(struct ioi_buf *out, int64_t when, int64_t now)
{
    int64_t diff = now - when;
    int future = diff < 0;
    int64_t a = future ? -diff : diff;
    if (a < 60) {
        ioi_buf_puts(out, "just now");
        return;
    }
    static const struct { int64_t secs; const char *unit; } units[] = {
        {365 * 86400, "year"}, {30 * 86400, "month"}, {86400, "day"}, {3600, "hour"}, {60, "minute"},
    };
    for (size_t i = 0; i < sizeof units / sizeof units[0]; i++) {
        if (a < units[i].secs)
            continue;
        int64_t n = a / units[i].secs;
        const char *pl = n == 1 ? "" : "s";
        if (future)
            ioi_buf_printf(out, "in %lld %s%s", (long long)n, units[i].unit, pl);
        else
            ioi_buf_printf(out, "%lld %s%s ago", (long long)n, units[i].unit, pl);
        return;
    }
}

/* ---------------------------------------------------------------- */
/* FACE                                                             */

void ioi_face_render(struct ioi_buf *out, const char *name, size_t len)
{
    uint8_t h[32];
    struct ioi_sha256 s;
    ioi_sha256_init(&s);
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        ioi_sha256_update(&s, &c, 1);
    }
    ioi_sha256_final(&s, h);
    unsigned hue = ((unsigned)h[30] << 8 | h[31]) % 360;
    unsigned sat = 45 + h[29] % 20;
    ioi_buf_puts(out, "<svg class=\"ick-face\" xmlns=\"http://www.w3.org/2000/svg\" "
                      "viewBox=\"0 0 5 5\" width=\"48\" height=\"48\" role=\"img\" "
                      "shape-rendering=\"crispEdges\"><title>");
    ioi_buf_escape(out, name, len);
    ioi_buf_printf(out, "</title><rect width=\"5\" height=\"5\" fill=\"hsl(%u,%u%%,92%%)\"/>"
                        "<path fill=\"hsl(%u,%u%%,42%%)\" d=\"",
                   hue, sat - 15, hue, sat);
    int any = 0;
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 3; x++) {
            int bit = y * 3 + x;
            if (!(h[bit / 8] >> (bit % 8) & 1))
                continue;
            any = 1;
            ioi_buf_printf(out, "M%d %dh1v1h-1z", x, y);
            if (x < 2)
                ioi_buf_printf(out, "M%d %dh1v1h-1z", 4 - x, y);
        }
    }
    if (!any)
        ioi_buf_puts(out, "M2 2h1v1h-1z");
    ioi_buf_puts(out, "\"/></svg>");
}
