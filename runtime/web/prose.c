/* prose.c — `AS PROSE`: a markdown subset rendered to sanitised HTML.
 *
 * Every byte of input reaches the output either HTML-escaped or as part of
 * a tag this file writes itself; there is no raw HTML passthrough. Links
 * only carry http:, https: or scheme-less (relative) URLs. See
 * runtime/web/README.md for the supported syntax. */
#include "web_internal.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_DEPTH 16      /* nested quotes / lists / block quotes */
#define MAX_INLINE 16     /* nested emphasis / links */
#define MAX_NAME 32       /* @mention and [quote="name"] link names */

struct line {
    const char *s;
    size_t n;
};

static int is_blank_line(struct line l)
{
    for (size_t i = 0; i < l.n; i++)
        if (l.s[i] != ' ')
            return 0;
    return 1;
}

static size_t indent_of(struct line l)
{
    size_t i = 0;
    while (i < l.n && l.s[i] == ' ')
        i++;
    return i;
}

static struct line trimmed(struct line l)
{
    while (l.n && *l.s == ' ')
        l.s++, l.n--;
    while (l.n && l.s[l.n - 1] == ' ')
        l.n--;
    return l;
}

static int name_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

static int is_alnum(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static int is_space(int c) { return c == ' ' || c == '\n' || c == '\t'; }

/* ---------------------------------------------------------------- */
/* Pre-pass: normalise newlines, expand leading tabs, drop NULs, and put
 * [quote...] / [/quote] tags outside fenced code on lines of their own. */

/* Length of a quote tag at s (n bytes available), 0 if none. */
static size_t quote_tag_len(const char *s, size_t n)
{
    if (n >= 8 && !strncasecmp(s, "[/quote]", 8))
        return 8;
    if (n < 7 || strncasecmp(s, "[quote", 6))
        return 0;
    if (s[6] == ']')
        return 7;
    if (s[6] != '=')
        return 0;
    for (size_t i = 7; i < n && i < 7 + MAX_NAME + 8; i++) {
        if (s[i] == ']')
            return i + 1;
        if (s[i] == '\n' || s[i] == '[')
            return 0;
    }
    return 0;
}

static int fence_line(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i] == ' ')
        i++;
    return n - i >= 3 && s[i] == '`' && s[i + 1] == '`' && s[i + 2] == '`';
}

/* Fence state after line s: openers are ``` lines, closers bare ``` runs. */
static int fence_after(int in_fence, const char *s, size_t n)
{
    if (!fence_line(s, n))
        return in_fence;
    if (!in_fence)
        return 1;
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '`'))
        i++;
    return i == n ? 0 : 1;
}

static void prepass(struct ioi_buf *o, const char *src, size_t len)
{
    int in_fence = 0;
    size_t i = 0;
    while (i < len) {
        /* one source line: up to \n, \r\n or \r */
        size_t e = i;
        while (e < len && src[e] != '\n' && src[e] != '\r')
            e++;
        size_t start = o->len;
        int leading = 1, after_tag = 0;
        for (size_t k = i; k < e; k++) {
            char c = src[k];
            if (c == 0)
                continue;
            if (c == '\t' && leading) {
                ioi_buf_puts(o, "    ");
                after_tag = 0;
                continue;
            }
            if (c != ' ' && c != '\t')
                leading = 0;
            size_t tl;
            if (!in_fence && c == '[' && (tl = quote_tag_len(src + k, e - k))) {
                /* break before the tag unless the line so far is blank */
                int blank = 1;
                for (size_t q = start; q < o->len; q++)
                    if (o->p[q] != ' ')
                        blank = 0;
                if (blank)
                    o->len = start;
                else
                    ioi_buf_putc(o, '\n');
                ioi_buf_put(o, src + k, tl);
                ioi_buf_putc(o, '\n');
                start = o->len;
                after_tag = 1;
                k += tl - 1;
                /* skip blanks that followed the tag */
                while (k + 1 < e && src[k + 1] == ' ')
                    k++;
                continue;
            }
            ioi_buf_putc(o, c);
            after_tag = 0;
        }
        in_fence = fence_after(in_fence, o->p ? o->p + start : "", o->len - start);
        /* a tag at the end already ended the line */
        if (!after_tag)
            ioi_buf_putc(o, '\n');
        if (e < len && src[e] == '\r' && e + 1 < len && src[e + 1] == '\n')
            e++;
        i = e + 1;
    }
}

/* ---------------------------------------------------------------- */
/* Inline                                                           */

struct inl {
    struct ioi_buf *out;
    const char *s;
    size_t n;
    size_t fail_em, fail_strong; /* searches from >= these positions fail */
};

static void render_inline(struct ioi_buf *out, const char *s, size_t n, int depth, int links);

static int punct(int c)
{
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') ||
           (c >= '{' && c <= '~');
}

/* Index just past a code span starting at s[i] (a backtick run), or 0. */
static size_t code_span_end(const char *s, size_t n, size_t i, size_t *open, size_t *close_at)
{
    size_t r = 0;
    while (i + r < n && s[i + r] == '`')
        r++;
    for (size_t j = i + r; j < n;) {
        if (s[j] != '`') {
            j++;
            continue;
        }
        size_t k = 0;
        while (j + k < n && s[j + k] == '`')
            k++;
        if (k == r) {
            *open = r;
            *close_at = j;
            return j + k;
        }
        j += k;
    }
    *open = r;
    return 0;
}

/* Find the closing delimiter for '*' (want == 1) or '**' (want == 2),
 * scanning from `from`. Returns its index or (size_t)-1. */
static size_t find_closer(struct inl *c, size_t from, int want)
{
    size_t *fail = want == 1 ? &c->fail_em : &c->fail_strong;
    if (from >= *fail)
        return (size_t)-1;
    const char *s = c->s;
    size_t n = c->n;
    for (size_t j = from; j < n;) {
        if (s[j] == '\\' && j + 1 < n) {
            j += 2;
            continue;
        }
        if (s[j] == '`') {
            size_t o, ca, end = code_span_end(s, n, j, &o, &ca);
            j = end ? end : j + o;
            continue;
        }
        if (s[j] != '*') {
            j++;
            continue;
        }
        size_t rl = 0;
        while (j + rl < n && s[j + rl] == '*')
            rl++;
        int ws_before = j == 0 || is_space((unsigned char)s[j - 1]);
        if (j > from && !ws_before) {
            if (want == 1 && rl != 2)
                return j + rl - 1;
            if (want == 2 && rl >= 2)
                return j + rl - 2;
        }
        j += rl;
    }
    *fail = from;
    return (size_t)-1;
}

/* http:, https:, or no scheme at all; no blanks, controls or quotes. */
static int safe_url(const char *u, size_t n)
{
    if (n == 0 || n > 2048)
        return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)u[i];
        if (c <= 0x20 || c == 0x7f || c == '"' || c == '<' || c == '>' || c == '\\' || c == '`')
            return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (u[i] == '/' || u[i] == '?' || u[i] == '#')
            return 1;
        if (u[i] == ':') {
            int ok = (i == 4 && !strncasecmp(u, "http", 4)) || (i == 5 && !strncasecmp(u, "https", 5));
            return ok && n > i + 3 && u[i + 1] == '/' && u[i + 2] == '/' && u[i + 3] != '/';
        }
    }
    return 1;
}

static void emit_link_open(struct ioi_buf *out, const char *u, size_t n)
{
    ioi_buf_puts(out, "<a href=\"");
    ioi_buf_escape(out, u, n);
    ioi_buf_puts(out, "\" rel=\"nofollow ugc\">");
}

/* Bare URL length at s[i] ("http://..." / "https://..."), 0 if none. */
static size_t bare_url(const char *s, size_t n, size_t i)
{
    size_t p;
    if (n - i > 7 && !strncasecmp(s + i, "http://", 7))
        p = 7;
    else if (n - i > 8 && !strncasecmp(s + i, "https://", 8))
        p = 8;
    else
        return 0;
    size_t e = i + p;
    while (e < n && !is_space((unsigned char)s[e]) && s[e] != '<' && s[e] != '>' && s[e] != '"' &&
           s[e] != '`' && (unsigned char)s[e] > 0x20)
        e++;
    /* trailing punctuation belongs to the sentence */
    for (;;) {
        if (e <= i + p)
            return 0;
        char c = s[e - 1];
        if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?' || c == '\'' ||
            c == '*' || c == ']') {
            e--;
            continue;
        }
        if (c == ')') {
            int bal = 0;
            for (size_t k = i; k < e; k++)
                bal += s[k] == '(' ? 1 : s[k] == ')' ? -1 : 0;
            if (bal < 0) {
                e--;
                continue;
            }
        }
        break;
    }
    if (s[i + p] == '/' || !safe_url(s + i, e - i))
        return 0;
    return e - i;
}

static void render_inline(struct ioi_buf *out, const char *s, size_t n, int depth, int links)
{
    struct inl c = {out, s, n, (size_t)-1, (size_t)-1};
    size_t i = 0, run = 0;
#define FLUSH() (ioi_buf_escape(out, s + run, i - run))
    while (i < n) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\\' && i + 1 < n && punct((unsigned char)s[i + 1])) {
            FLUSH();
            ioi_buf_escape(out, s + i + 1, 1);
            i += 2;
            run = i;
            continue;
        }
        if (ch == '\n') {
            FLUSH();
            ioi_buf_puts(out, "<br>\n");
            run = ++i;
            continue;
        }
        if (ch == '`') {
            size_t open, close_at, end = code_span_end(s, n, i, &open, &close_at);
            if (!end) {
                i += open;
                continue;
            }
            FLUSH();
            const char *cs = s + i + open;
            size_t cn = close_at - (i + open);
            if (cn >= 2 && cs[0] == ' ' && cs[cn - 1] == ' ')
                cs++, cn -= 2;
            ioi_buf_puts(out, "<code>");
            ioi_buf_escape(out, cs, cn);
            ioi_buf_puts(out, "</code>");
            i = run = end;
            continue;
        }
        if (ch == '*' && depth < MAX_INLINE) {
            size_t rl = 0;
            while (i + rl < n && s[i + rl] == '*')
                rl++;
            int want = rl >= 2 ? 2 : 1;
            for (; want >= 1; want--) {
                size_t from = i + (size_t)want;
                if (from >= n || is_space((unsigned char)s[from]))
                    continue;
                size_t cl = find_closer(&c, from, want);
                if (cl == (size_t)-1)
                    continue;
                FLUSH();
                const char *tag = want == 2 ? "strong" : "em";
                ioi_buf_printf(out, "<%s>", tag);
                render_inline(out, s + from, cl - from, depth + 1, links);
                ioi_buf_printf(out, "</%s>", tag);
                i = run = cl + (size_t)want;
                break;
            }
            if (want >= 1)
                continue;
            i += rl;
            continue;
        }
        if (ch == '[' && links && depth < MAX_INLINE) {
            size_t j = i + 1;
            while (j < n && s[j] != ']' && s[j] != '[')
                j++;
            if (j < n && s[j] == ']' && j + 1 < n && s[j + 1] == '(') {
                size_t u = j + 2, k = u;
                int bal = 0;
                while (k < n && !is_space((unsigned char)s[k])) {
                    if (s[k] == '(')
                        bal++;
                    else if (s[k] == ')' && bal-- == 0)
                        break;
                    k++;
                }
                if (k < n && s[k] == ')' && safe_url(s + u, k - u)) {
                    FLUSH();
                    emit_link_open(out, s + u, k - u);
                    if (j == i + 1)
                        ioi_buf_escape(out, s + u, k - u);
                    else
                        render_inline(out, s + i + 1, j - i - 1, depth + 1, 0);
                    ioi_buf_puts(out, "</a>");
                    i = run = k + 1;
                    continue;
                }
            }
            i++;
            continue;
        }
        if (ch == '<' && links) {
            size_t j = i + 1;
            while (j < n && s[j] != '>' && !is_space((unsigned char)s[j]) && s[j] != '<')
                j++;
            if (j < n && s[j] == '>' && bare_url(s + i + 1, j - i - 1, 0) == j - i - 1) {
                FLUSH();
                emit_link_open(out, s + i + 1, j - i - 1);
                ioi_buf_escape(out, s + i + 1, j - i - 1);
                ioi_buf_puts(out, "</a>");
                i = run = j + 1;
                continue;
            }
            i++;
            continue;
        }
        if ((ch == 'h' || ch == 'H') && links && (i == 0 || !is_alnum((unsigned char)s[i - 1]))) {
            size_t ul = bare_url(s, n, i);
            if (ul) {
                FLUSH();
                emit_link_open(out, s + i, ul);
                ioi_buf_escape(out, s + i, ul);
                ioi_buf_puts(out, "</a>");
                i = run = i + ul;
                continue;
            }
        }
        if (ch == '@' && links && (i == 0 || (!name_char((unsigned char)s[i - 1]) && s[i - 1] != '.'))) {
            size_t j = i + 1;
            while (j < n && j - i - 1 < MAX_NAME && name_char((unsigned char)s[j]))
                j++;
            if (j > i + 1 && (j == n || !name_char((unsigned char)s[j]))) {
                FLUSH();
                ioi_buf_puts(out, "<a class=\"mention\" href=\"/u/");
                ioi_buf_put(out, s + i + 1, j - i - 1);
                ioi_buf_puts(out, "\">@");
                ioi_buf_put(out, s + i + 1, j - i - 1);
                ioi_buf_puts(out, "</a>");
                i = run = j;
                continue;
            }
        }
        i++;
    }
    FLUSH();
#undef FLUSH
}

/* ---------------------------------------------------------------- */
/* Blocks                                                           */

static int heading(struct line l, struct line *content)
{
    size_t i = indent_of(l);
    if (i > 3)
        return 0;
    int level = 0;
    while (i < l.n && l.s[i] == '#' && level < 7)
        i++, level++;
    if (level == 0 || level > 6 || (i < l.n && l.s[i] != ' '))
        return 0;
    struct line c = {l.s + i, l.n - i};
    c = trimmed(c);
    /* optional closing #s */
    size_t e = c.n;
    while (e && c.s[e - 1] == '#')
        e--;
    if (e < c.n && (e == 0 || c.s[e - 1] == ' '))
        c.n = e;
    *content = trimmed(c);
    return level;
}

static int is_quote_close(struct line l)
{
    l = trimmed(l);
    return l.n == 8 && !strncasecmp(l.s, "[/quote]", 8);
}

/* [quote], [quote=name], [quote="name"]: 1 and the name (may be empty). */
static int is_quote_open(struct line l, struct line *name)
{
    l = trimmed(l);
    if (l.n < 7 || strncasecmp(l.s, "[quote", 6) || l.s[l.n - 1] != ']')
        return 0;
    name->s = l.s;
    name->n = 0;
    if (l.n == 7)
        return 1;
    if (l.s[6] != '=')
        return 0;
    struct line v = {l.s + 7, l.n - 8};
    if (v.n >= 2 && (v.s[0] == '"' || v.s[0] == '\'') && v.s[v.n - 1] == v.s[0])
        v.s++, v.n -= 2;
    *name = v;
    return 1;
}

static int is_bq(struct line l)
{
    size_t i = indent_of(l);
    return i <= 3 && i < l.n && l.s[i] == '>';
}

struct marker {
    int ordered;
    long start;
    size_t indent;  /* spaces before the marker */
    size_t content; /* offset of the item text */
};

static int list_marker(struct line l, struct marker *m)
{
    size_t i = indent_of(l);
    m->indent = i;
    if (i >= l.n)
        return 0;
    if (l.s[i] == '-' || l.s[i] == '*' || l.s[i] == '+') {
        m->ordered = 0;
        m->start = 1;
        i++;
    } else {
        size_t d = i;
        long v = 0;
        while (i < l.n && l.s[i] >= '0' && l.s[i] <= '9' && i - d < 9)
            v = v * 10 + (l.s[i++] - '0');
        if (i == d || i >= l.n || (l.s[i] != '.' && l.s[i] != ')'))
            return 0;
        m->ordered = 1;
        m->start = v;
        i++;
    }
    if (i < l.n && l.s[i] != ' ')
        return 0;
    size_t sp = 0;
    while (i + sp < l.n && l.s[i + sp] == ' ' && sp < 4)
        sp++;
    m->content = i + sp;
    return 1;
}

static void render_blocks(struct ioi_buf *out, struct line *ls, size_t n, int depth, int tight,
                          const size_t *qm);

/* For every line, the distance to the [/quote] closing the [quote] it opens
 * (0 when it opens none), found in one pass with a stack. Distances are
 * relative, so the table stays valid for any slice starting at a line. */
static size_t *quote_matches(struct line *ls, size_t n)
{
    size_t *dist = ioi_xmalloc(sizeof *dist * (n + 1));
    size_t *stack = ioi_xmalloc(sizeof *stack * (n + 1));
    size_t top = 0;
    int fence = 0;
    struct line nm;
    for (size_t j = 0; j < n; j++) {
        dist[j] = 0;
        int was = fence;
        fence = fence_after(fence, ls[j].s, ls[j].n);
        if (was || fence)
            continue;
        if (is_quote_open(ls[j], &nm))
            stack[top++] = j;
        else if (is_quote_close(ls[j]) && top) {
            size_t o = stack[--top];
            dist[o] = j - o;
        }
    }
    free(stack);
    return dist;
}

/* Does line j start a block that interrupts a paragraph? */
static int interrupts(struct line *ls, size_t j, int depth, const size_t *qm)
{
    struct line l = ls[j], tmp;
    struct marker m;
    if (fence_line(l.s, l.n) || heading(l, &tmp))
        return 1;
    if (depth >= MAX_DEPTH)
        return 0;
    if (is_bq(l))
        return 1;
    if (list_marker(l, &m) && m.content < l.n && (!m.ordered || m.start == 1))
        return 1;
    if (qm[j] && is_quote_open(l, &tmp))
        return 1;
    return 0;
}

static void render_fence(struct ioi_buf *out, struct line *ls, size_t *ip, size_t n)
{
    size_t i = *ip;
    struct line open = trimmed(ls[i]);
    size_t ticks = 0;
    while (ticks < open.n && open.s[ticks] == '`')
        ticks++;
    struct line info = {open.s + ticks, open.n - ticks};
    info = trimmed(info);
    size_t il = 0;
    while (il < info.n && il < 24 && (name_char((unsigned char)info.s[il]) || info.s[il] == '+'))
        il++;
    size_t strip = indent_of(ls[i]);
    ioi_buf_puts(out, "<pre><code");
    if (il) {
        ioi_buf_puts(out, " class=\"language-");
        ioi_buf_put(out, info.s, il);
        ioi_buf_putc(out, '"');
    }
    ioi_buf_putc(out, '>');
    size_t j = i + 1;
    for (; j < n; j++) {
        struct line t = trimmed(ls[j]);
        size_t k = 0;
        while (k < t.n && t.s[k] == '`')
            k++;
        if (k >= ticks && k == t.n)
            break;
        struct line l = ls[j];
        size_t d = indent_of(l);
        d = d < strip ? d : strip;
        ioi_buf_escape(out, l.s + d, l.n - d);
        ioi_buf_putc(out, '\n');
    }
    ioi_buf_puts(out, "</code></pre>\n");
    *ip = j < n ? j + 1 : n;
}

static void render_list(struct ioi_buf *out, struct line *ls, size_t *ip, size_t n, int depth)
{
    size_t i = *ip;
    struct marker first;
    list_marker(ls[i], &first);
    /* collect items: each item is a run of dedented lines */
    struct line *buf = ioi_xmalloc(sizeof *buf * (n - i));
    size_t *starts = ioi_xmalloc(sizeof *starts * (n - i + 1));
    size_t nb = 0, items = 0;
    int loose = 0;
    while (i < n) {
        struct marker m;
        if (!list_marker(ls[i], &m) || m.ordered != first.ordered || m.indent >= first.content)
            break;
        starts[items++] = nb;
        buf[nb++] = (struct line){ls[i].s + m.content, ls[i].n - m.content};
        size_t j = i + 1;
        int blank_inside = 0;
        while (j < n) {
            if (is_blank_line(ls[j])) {
                size_t k = j;
                while (k < n && is_blank_line(ls[k]))
                    k++;
                if (k < n && indent_of(ls[k]) >= 2 && indent_of(ls[k]) > m.indent) {
                    for (; j < k; j++)
                        buf[nb++] = (struct line){ls[j].s, 0};
                    blank_inside = 1;
                    continue;
                }
                break;
            }
            struct marker m2;
            size_t ind = indent_of(ls[j]);
            if (list_marker(ls[j], &m2) && m2.indent < m.content)
                break;
            if (ind < 2 || ind <= m.indent)
                break;
            size_t d = ind < m.content ? ind : m.content;
            buf[nb++] = (struct line){ls[j].s + d, ls[j].n - d};
            j++;
        }
        if (blank_inside)
            loose = 1;
        /* blank lines between items make the list loose */
        size_t k = j;
        while (k < n && is_blank_line(ls[k]))
            k++;
        struct marker m3;
        if (k < n && list_marker(ls[k], &m3) && m3.ordered == first.ordered &&
            m3.indent < first.content) {
            if (k > j)
                loose = 1;
            i = k;
        } else {
            i = j;
            break;
        }
    }
    starts[items] = nb;
    if (first.ordered && first.start != 1)
        ioi_buf_printf(out, "<ol start=\"%ld\">\n", first.start);
    else
        ioi_buf_puts(out, first.ordered ? "<ol>\n" : "<ul>\n");
    for (size_t it = 0; it < items; it++) {
        ioi_buf_puts(out, "<li>");
        render_blocks(out, buf + starts[it], starts[it + 1] - starts[it], depth + 1, !loose, NULL);
        ioi_buf_puts(out, "</li>\n");
    }
    ioi_buf_puts(out, first.ordered ? "</ol>\n" : "</ul>\n");
    free(buf);
    free(starts);
    *ip = i;
}

/* QM: quote_matches for these lines, or NULL to compute it here. */
static void render_blocks(struct ioi_buf *out, struct line *ls, size_t n, int depth, int tight,
                          const size_t *qm)
{
    size_t i = 0;
    struct line tmp;
    struct marker m;
    size_t *own = NULL;
    if (!qm)
        qm = own = quote_matches(ls, n);
    while (i < n) {
        struct line l = ls[i];
        if (is_blank_line(l)) {
            i++;
            continue;
        }
        if (fence_line(l.s, l.n)) {
            render_fence(out, ls, &i, n);
            continue;
        }
        int level = heading(l, &tmp);
        if (level) {
            int h = level + 2 > 6 ? 6 : level + 2;
            ioi_buf_printf(out, "<h%d>", h);
            render_inline(out, tmp.s, tmp.n, 0, 1);
            ioi_buf_printf(out, "</h%d>\n", h);
            i++;
            continue;
        }
        if (depth < MAX_DEPTH && qm[i] && is_quote_open(l, &tmp)) {
            size_t j = i + qm[i];
            {
                ioi_buf_puts(out, "<blockquote class=\"bbquote\">");
                if (tmp.n) {
                    int linkable = tmp.n <= MAX_NAME;
                    for (size_t k = 0; k < tmp.n; k++)
                        linkable &= name_char((unsigned char)tmp.s[k]);
                    ioi_buf_puts(out, "<cite>");
                    if (linkable) {
                        ioi_buf_puts(out, "<a href=\"/u/");
                        ioi_buf_put(out, tmp.s, tmp.n);
                        ioi_buf_puts(out, "\">");
                        ioi_buf_put(out, tmp.s, tmp.n);
                        ioi_buf_puts(out, "</a>");
                    } else {
                        ioi_buf_escape(out, tmp.s, tmp.n);
                    }
                    ioi_buf_puts(out, " wrote:</cite>\n");
                } else {
                    ioi_buf_putc(out, '\n');
                }
                render_blocks(out, ls + i + 1, j - i - 1, depth + 1, 0, qm + i + 1);
                ioi_buf_puts(out, "</blockquote>\n");
                i = j + 1;
                continue;
            }
        }
        if (depth < MAX_DEPTH && is_bq(l)) {
            size_t j = i;
            while (j < n && is_bq(ls[j]))
                j++;
            struct line *sub = ioi_xmalloc(sizeof *sub * (j - i));
            for (size_t k = i; k < j; k++) {
                size_t p = indent_of(ls[k]) + 1;
                if (p < ls[k].n && ls[k].s[p] == ' ')
                    p++;
                sub[k - i] = (struct line){ls[k].s + p, ls[k].n - p};
            }
            ioi_buf_puts(out, "<blockquote>\n");
            render_blocks(out, sub, j - i, depth + 1, 0, NULL);
            ioi_buf_puts(out, "</blockquote>\n");
            free(sub);
            i = j;
            continue;
        }
        if (depth < MAX_DEPTH && list_marker(l, &m)) {
            render_list(out, ls, &i, n, depth);
            continue;
        }
        /* paragraph */
        struct ioi_buf para = {0};
        size_t j = i;
        while (j < n && !is_blank_line(ls[j]) && (j == i || !interrupts(ls, j, depth, qm))) {
            struct line t = trimmed(ls[j]);
            if (j > i)
                ioi_buf_putc(&para, '\n');
            ioi_buf_put(&para, t.s, t.n);
            j++;
        }
        if (!tight)
            ioi_buf_puts(out, "<p>");
        render_inline(out, para.p ? para.p : "", para.len, 0, 1);
        ioi_buf_puts(out, tight ? "\n" : "</p>\n");
        free(para.p);
        i = j;
    }
    free(own);
}

void ioi_prose_render(struct ioi_buf *out, const char *src, size_t len)
{
    struct ioi_buf norm = {0};
    prepass(&norm, src, len);
    size_t nl = 0;
    for (size_t i = 0; i < norm.len; i++)
        nl += norm.p[i] == '\n';
    struct line *ls = ioi_xmalloc(sizeof *ls * (nl + 1));
    size_t n = 0, start = 0;
    for (size_t i = 0; i < norm.len; i++) {
        if (norm.p[i] == '\n') {
            size_t e = i;
            while (e > start && norm.p[e - 1] == ' ')
                e--;
            ls[n++] = (struct line){norm.p + start, e - start};
            start = i + 1;
        }
    }
    render_blocks(out, ls, n, 0, 0, NULL);
    free(ls);
    free(norm.p);
}
