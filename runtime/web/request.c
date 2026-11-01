/* request.c — CGI request parsing, routing, fields, digit words, origin check. */
#include "ioi_web.h"
#include "web_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define BODY_CAP (1u << 20)

static struct {
    int inited, active;
    char *method, *path;
    char *query;               /* raw QUERY_STRING */
    char *body;                /* raw body when urlencoded, else NULL */
    size_t bodylen;
    uint32_t route_names;      /* LIST of BYTES from the matched pattern */
    uint32_t route_values;     /* LIST of BYTES */
    int routed;
} rq;

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = ioi_xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static const char *env(const char *name)
{
    const char *v = getenv(name);
    return v ? v : "";
}

void ioi_web_reset(void)
{
    free(rq.method);
    free(rq.path);
    free(rq.query);
    free(rq.body);
    memset(&rq, 0, sizeof rq);
    ioi_web_response_reset();
    ioi_web_session_reset();
}

static int is_urlencoded(const char *ct)
{
    static const char want[] = "application/x-www-form-urlencoded";
    while (*ct == ' ' || *ct == '\t')
        ct++;
    size_t n = sizeof want - 1;
    if (strncasecmp(ct, want, n))
        return 0;
    return ct[n] == 0 || ct[n] == ';' || ct[n] == ' ' || ct[n] == '\t';
}

int ioi_web_init(void)
{
    if (rq.inited)
        return rq.active;
    rq.inited = 1;
    const char *m = getenv("REQUEST_METHOD");
    rq.active = m && *m;
    rq.method = xstrdup(rq.active ? m : "");
    for (char *p = rq.method; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    const char *path = env("PATH_INFO");
    rq.path = xstrdup(*path ? path : "/");
    rq.query = xstrdup(env("QUERY_STRING"));
    rq.route_names = ioi_cas_list();
    rq.route_values = ioi_cas_list();
    if (!rq.active)
        return 0;

    const char *cl = env("CONTENT_LENGTH");
    char *end;
    errno = 0;
    unsigned long long want = strtoull(cl, &end, 10);
    if (!*cl || *end || errno)
        want = 0;
    if (want > BODY_CAP)
        want = BODY_CAP;
    if (want && is_urlencoded(env("CONTENT_TYPE"))) {
        rq.body = ioi_xmalloc((size_t)want + 1);
        size_t got = 0;
        while (got < want) {
            ssize_t n = read(0, rq.body + got, (size_t)want - got);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            got += (size_t)n;
        }
        rq.body[got] = 0;
        rq.bodylen = got;
    }
    return 1;
}

int ioi_web_active(void)
{
    ioi_web_init();
    return rq.active;
}

const char *ioi_web_method(void)
{
    ioi_web_init();
    return rq.method;
}

const char *ioi_web_path(void)
{
    ioi_web_init();
    return rq.path;
}

/* ---------------------------------------------------------------- */
/* URL decoding, forms, cookies                                     */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t ioi_urldecode(char *s, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        int hi, lo;
        if (s[i] == '+')
            s[o++] = ' ';
        else if (s[i] == '%' && i + 2 < len &&
                 (hi = hexval((unsigned char)s[i + 1])) >= 0 &&
                 (lo = hexval((unsigned char)s[i + 2])) >= 0) {
            s[o++] = (char)(hi << 4 | lo);
            i += 2;
        } else
            s[o++] = s[i];
    }
    return o;
}

int ioi_form_lookup(const char *qs, size_t qslen, const char *name, char **value, size_t *vlen)
{
    size_t namelen = strlen(name);
    size_t i = 0;
    while (i < qslen) {
        size_t start = i;
        while (i < qslen && qs[i] != '&')
            i++;
        size_t pairlen = i - start;
        i++; /* skip '&' */
        if (!pairlen)
            continue;
        const char *eq = memchr(qs + start, '=', pairlen);
        size_t klen = eq ? (size_t)(eq - (qs + start)) : pairlen;
        char *key = ioi_xmalloc(klen + 1);
        memcpy(key, qs + start, klen);
        klen = ioi_urldecode(key, klen);
        int hit = klen == namelen && memcmp(key, name, namelen) == 0;
        free(key);
        if (!hit)
            continue;
        const char *v = eq ? eq + 1 : qs + start + pairlen;
        size_t vl = (size_t)(qs + start + pairlen - v);
        char *out = ioi_xmalloc(vl + 1);
        memcpy(out, v, vl);
        vl = ioi_urldecode(out, vl);
        out[vl] = 0;
        *value = out;
        *vlen = vl;
        return 1;
    }
    return 0;
}

char *ioi_cookie_lookup(const char *header, const char *name)
{
    size_t namelen = strlen(name);
    const char *p = header;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';')
            p++;
        const char *start = p;
        while (*p && *p != ';')
            p++;
        const char *eq = memchr(start, '=', (size_t)(p - start));
        if (eq && (size_t)(eq - start) == namelen && memcmp(start, name, namelen) == 0) {
            const char *v = eq + 1, *e = p;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t'))
                e--;
            if (e - v >= 2 && *v == '"' && e[-1] == '"') {
                v++;
                e--;
            }
            char *out = ioi_xmalloc((size_t)(e - v) + 1);
            memcpy(out, v, (size_t)(e - v));
            out[e - v] = 0;
            return out;
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------- */
/* Same-origin check                                                 */

/* Extract "host[:port]" from "scheme://host[:port][/...]", lower-cased,
 * with the scheme's default port removed. Returns 0 on success. */
static int origin_hostport(const char *url, char *out, size_t cap, int *https)
{
    const char *sep = strstr(url, "://");
    if (!sep)
        return -1;
    size_t slen = (size_t)(sep - url);
    if (slen == 4 && !strncasecmp(url, "http", 4))
        *https = 0;
    else if (slen == 5 && !strncasecmp(url, "https", 5))
        *https = 1;
    else
        return -1;
    const char *h = sep + 3, *e = h;
    while (*e && *e != '/' && *e != '?' && *e != '#')
        e++;
    const char *at = memchr(h, '@', (size_t)(e - h));
    if (at)
        h = at + 1;
    size_t n = (size_t)(e - h);
    if (n == 0 || n >= cap)
        return -1;
    for (size_t i = 0; i < n; i++)
        out[i] = (char)tolower((unsigned char)h[i]);
    out[n] = 0;
    const char *dflt = *https ? ":443" : ":80";
    size_t dl = strlen(dflt);
    if (n > dl && !strcmp(out + n - dl, dflt))
        out[n - dl] = 0;
    return 0;
}

int ioi_same_origin_check(const char *host, const char *origin, const char *referer)
{
    char want[512], got[512];
    int https = 0;
    const char *src = NULL;
    if (origin && *origin)
        src = origin; /* "null" fails to parse, which is what we want */
    else if (referer && *referer)
        src = referer;
    if (!src || !host || !*host)
        return 0;
    if (origin_hostport(src, got, sizeof got, &https))
        return 0;
    size_t n = strlen(host);
    if (n >= sizeof want)
        return 0;
    for (size_t i = 0; i <= n; i++)
        want[i] = (char)tolower((unsigned char)host[i]);
    const char *dflt = https ? ":443" : ":80";
    size_t dl = strlen(dflt);
    if (n > dl && !strcmp(want + n - dl, dflt))
        want[n - dl] = 0;
    return strcmp(want, got) == 0;
}

int ioi_web_same_origin(void)
{
    ioi_web_init();
    return ioi_same_origin_check(getenv("HTTP_HOST"), getenv("HTTP_ORIGIN"),
                                 getenv("HTTP_REFERER"));
}

/* ---------------------------------------------------------------- */
/* Routing                                                          */

/* Next non-empty '/'-separated segment of *p; returns length, 0 at end. */
static size_t next_seg(const char **p, const char **seg)
{
    while (**p == '/')
        (*p)++;
    *seg = *p;
    while (**p && **p != '/')
        (*p)++;
    return (size_t)(*p - *seg);
}

int ioi_web_match(const char *pattern, const char *method, const char *path,
                  uint32_t *params, uint32_t *param_names)
{
    const char *p = pattern;
    const char *want_method = "GET";
    char mbuf[32];
    while (*p == ' ')
        p++;
    if (*p != '/' && *p != '*') {
        size_t n = 0;
        while (p[n] >= 'A' && p[n] <= 'Z')
            n++;
        if (n == 0 || n >= sizeof mbuf || p[n] != ' ')
            return -1;
        memcpy(mbuf, p, n);
        mbuf[n] = 0;
        want_method = mbuf;
        p += n;
        while (*p == ' ')
            p++;
    }
    int method_ok = !strcmp(method, want_method) ||
                    (!strcmp(want_method, "GET") && !strcmp(method, "HEAD"));
    if (*p == '*' && p[1] == 0) {
        /* A bare "*" answers every method; "POST *" only POST. */
        if (want_method == mbuf && !method_ok)
            return 0;
        if (params)
            *params = ioi_cas_list();
        if (param_names)
            *param_names = ioi_cas_list();
        return 1;
    }
    if (*p != '/')
        return -1;

    /* Validate the pattern before deciding anything. */
    for (const char *q = p, *seg;;) {
        size_t n = next_seg(&q, &seg);
        if (n == 0)
            break;
        if (seg[0] == ':' && n == 1)
            return -1;
        if (seg[0] == '*') {
            const char *rest = q, *s2;
            if (n != 1 || next_seg(&rest, &s2))
                return -1;
        }
    }

    if (!method_ok)
        return 0;

    uint32_t vals = ioi_cas_list(), names = ioi_cas_list();
    const char *q = path;
    for (;;) {
        const char *ps, *qs;
        size_t pn = next_seg(&p, &ps);
        if (pn == 1 && ps[0] == '*') {
            while (*q == '/')
                q++;
            const char *e = q + strlen(q);
            while (e > q && e[-1] == '/')
                e--;
            ioi_cas_push(vals, ioi_cas_bytes(q, (size_t)(e - q)));
            ioi_cas_push(names, ioi_cas_cstr("*"));
            break;
        }
        size_t qn = next_seg(&q, &qs);
        if (pn == 0 || qn == 0) {
            if (pn || qn)
                return 0;
            break;
        }
        if (ps[0] == ':') {
            ioi_cas_push(vals, ioi_cas_bytes(qs, qn));
            ioi_cas_push(names, ioi_cas_bytes(ps + 1, pn - 1));
        } else if (pn != qn || memcmp(ps, qs, pn)) {
            return 0;
        }
    }
    if (params)
        *params = vals;
    if (param_names)
        *param_names = names;
    return 1;
}

int ioi_web_route(const char *pattern, uint32_t *params)
{
    ioi_web_init();
    uint32_t vals, names;
    if (ioi_web_match(pattern, rq.method, rq.path, &vals, &names) != 1)
        return 0;
    rq.route_values = vals;
    rq.route_names = names;
    rq.routed = 1;
    if (params)
        *params = vals;
    return 1;
}

/* ---------------------------------------------------------------- */
/* Fields                                                           */

int ioi_web_field(const char *name, const uint8_t **data, size_t *len)
{
    ioi_web_init();
    size_t namelen = strlen(name);
    size_t n = ioi_cas_len(rq.route_names);
    for (size_t i = 0; i < n; i++) {
        size_t kl;
        const uint8_t *k = ioi_cas_data(ioi_cas_get(rq.route_names, i), &kl);
        if (kl == namelen && !memcmp(k, name, kl)) {
            *data = ioi_cas_data(ioi_cas_get(rq.route_values, i), len);
            return 1;
        }
    }
    char *v;
    size_t vl;
    if ((rq.body && ioi_form_lookup(rq.body, rq.bodylen, name, &v, &vl)) ||
        ioi_form_lookup(rq.query, strlen(rq.query), name, &v, &vl)) {
        *data = (const uint8_t *)v;
        *len = vl;
        return 1;
    }
    *data = (const uint8_t *)"";
    *len = 0;
    return 0;
}

/* C-INTERCAL's WRITE IN digit table (src/numerals.c), matched
 * case-insensitively here because form input is typed by humans. */
static const struct {
    const char *name;
    int value;
} numerals[] = {
    /* English */
    {"OH", 0}, {"ZERO", 0}, {"ONE", 1}, {"TWO", 2}, {"THREE", 3}, {"FOUR", 4},
    {"FIVE", 5}, {"SIX", 6}, {"SEVEN", 7}, {"EIGHT", 8}, {"NINE", 9}, {"NINER", 9},
    /* Sanskrit */
    {"SUTYA", 0}, {"SHUTYA", 0}, {"EKA", 1}, {"DVI", 2}, {"TRI", 3}, {"CHATUR", 4},
    {"PANCHAN", 5}, {"SHASH", 6}, {"SAPTAM", 7}, {"ASHTAN", 8}, {"NAVAN", 9},
    /* Basque */
    {"ZEROA", 0}, {"BAT", 1}, {"BI", 2}, {"HIRO", 3}, {"LAU", 4}, {"BORTZ", 5},
    {"SEI", 6}, {"ZAZPI", 7}, {"ZORTZI", 8}, {"BEDERATZI", 9},
    /* Tagalog */
    {"WALA", 0}, {"ISA", 1}, {"DALAWA", 2}, {"TATLO", 3}, {"APAT", 4}, {"LIMA", 5},
    {"ANIM", 6}, {"PITO", 7}, {"WALO", 8}, {"SIYAM", 9},
    /* Classical Nahuatl */
    {"AHTLE", 0}, {"CE", 1}, {"OME", 2}, {"IEI", 3}, {"NAUI", 4}, {"NACUILI", 5},
    {"CHIQUACE", 6}, {"CHICOME", 7}, {"CHICUE", 8}, {"CHICUNAUI", 9},
    /* Georgian */
    {"NULI", 0}, {"ERTI", 1}, {"ORI", 2}, {"SAMI", 3}, {"OTXI", 4}, {"XUTI", 5},
    {"EKSVI", 6}, {"SHVIDI", 7}, {"RVA", 8}, {"CXRA", 9},
    /* Kwak'wala */
    {"KE'YOS", 0}, {"'NEM", 1}, {"MAL'H", 2}, {"YUDEXW", 3}, {"MU", 4}, {"SEK'A", 5},
    {"Q'ETL'A", 6}, {"ETLEBU", 7}, {"MALHGWENALH", 8}, {"'NA'NE'MA", 9},
    /* Volapuk */
    {"NOS", 0}, {"BAL", 1}, {"TEL", 2}, {"KIL", 3}, {"FOL", 4}, {"LUL", 5},
    {"M\\\"AL", 6}, {"M\xC4L", 6}, {"M\xA3\xA4L", 6}, {"VEL", 7},
    {"J\\\"OL", 8}, {"J\xD6L", 8}, {"J\xA3\x96L", 8},
    {"Z\\\"UL", 9}, {"Z\xDCL", 9}, {"Z\xA3\x9CL", 9},
    /* Latin */
    {"NIL", 0}, {"NIHIL", 0}, {"UNUS", 1}, {"UNA", 1}, {"UNUM", 1}, {"DUO", 2},
    {"DUAE", 2}, {"TRES", 3}, {"QUATTUOR", 4}, {"QUATUOR", 4}, {"QUINQUE", 5},
    {"SEX", 6}, {"SEPTEM", 7}, {"OCTO", 8}, {"NOVEM", 9},
};

static int word_digit(const uint8_t *w, size_t n)
{
    for (size_t i = 0; i < sizeof numerals / sizeof numerals[0]; i++) {
        const char *s = numerals[i].name;
        size_t j = 0;
        while (j < n && s[j] && toupper(w[j]) == (unsigned char)s[j])
            j++;
        if (j == n && s[j] == 0)
            return numerals[i].value;
    }
    return -1;
}

static int is_blank(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int ioi_web_parse_number(const uint8_t *s, size_t len, uint32_t *out)
{
    size_t i = 0, e = len;
    *out = 0;
    while (i < e && is_blank(s[i]))
        i++;
    while (e > i && is_blank(s[e - 1]))
        e--;
    if (i == e)
        return 0;
    uint64_t n = 0;
    if (s[i] >= '0' && s[i] <= '9') {
        for (; i < e; i++) {
            if (s[i] < '0' || s[i] > '9')
                return 0;
            n = n * 10 + (uint64_t)(s[i] - '0');
            if (n > UINT32_MAX)
                return 0;
        }
        *out = (uint32_t)n;
        return 1;
    }
    while (i < e) {
        size_t w = i;
        while (i < e && !is_blank(s[i]))
            i++;
        int d = word_digit(s + w, i - w);
        if (d < 0)
            return 0;
        n = n * 10 + (uint64_t)d;
        if (n > UINT32_MAX)
            return 0;
        while (i < e && is_blank(s[i]))
            i++;
    }
    *out = (uint32_t)n;
    return 1;
}

uint32_t ioi_web_time(void)
{
    const char *now = getenv("IOI_NOW");
    if (now && *now) {
        uint32_t v;
        if (ioi_web_parse_number((const uint8_t *)now, strlen(now), &v))
            return v;
    }
    return (uint32_t)time(NULL);
}
