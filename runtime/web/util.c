/* util.c — growable buffers and HTML escaping for the web runtime. */
#include "web_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void oom(void)
{
    fputs("ICL991I YOU HAVE TOO MUCH ROPE TO HANG YOURSELF\n", stderr);
    exit(1);
}

void *ioi_xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        oom();
    return p;
}

void *ioi_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        oom();
    return q;
}

static void reserve(struct ioi_buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1)
        cap *= 2;
    b->p = ioi_xrealloc(b->p, cap);
    b->cap = cap;
}

void ioi_buf_put(struct ioi_buf *b, const void *data, size_t len)
{
    reserve(b, len);
    if (len)
        memcpy(b->p + b->len, data, len);
    b->len += len;
    b->p[b->len] = 0;
}

void ioi_buf_puts(struct ioi_buf *b, const char *s) { ioi_buf_put(b, s, strlen(s)); }

void ioi_buf_putc(struct ioi_buf *b, char c) { ioi_buf_put(b, &c, 1); }

void ioi_buf_printf(struct ioi_buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    reserve(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void ioi_buf_escape(struct ioi_buf *b, const void *data, size_t len)
{
    const unsigned char *s = data;
    size_t run = 0;
    reserve(b, len);
    for (size_t i = 0; i < len; i++) {
        const char *rep = NULL;
        switch (s[i]) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        case 0: rep = ""; break;
        default: continue;
        }
        ioi_buf_put(b, s + run, i - run);
        ioi_buf_puts(b, rep);
        run = i + 1;
    }
    ioi_buf_put(b, s + run, len - run);
}
