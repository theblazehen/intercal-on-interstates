/* tpl.c — templates (SPEC.md §4): loading, parsing, static link checking
 * (SPEC.md §2.4) and C generation. Each template becomes one static C
 * function TPLn() that appends to the response. */
#include "ick.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { I_TEXT, I_EXPR, I_INCLUDE, I_LOOP, I_COND };

struct item {
    int kind;
    char *text;          /* I_TEXT */
    size_t len;
    struct node *e, *rhs;/* I_EXPR (e), I_LOOP (e), I_COND (e [IS rhs]) */
    int filter;
    int without;         /* I_COND: ABSTAIN WITHOUT */
    int inc;             /* I_INCLUDE: template id */
    struct item *kids;   /* I_LOOP / I_COND body */
    int nkids;
};

struct tpl {
    char *name, *path;
    char *flat;          /* text with every tag replaced by '\x01' (links) */
    struct item *items;
    int nitems;
    int loading;
    int *incs;
    int nincs;
};

static const char *read_tpl(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0, r;
    char *buf = xmalloc(cap + 1);
    while ((r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap)
            buf = xrealloc(buf, (cap *= 2) + 1);
    }
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

static char *tpl_path(struct program *p, const char *name)
{
    struct stat st;
    size_t l = strlen(p->srcdir) + strlen(name) + 16;
    char *a = xmalloc(l);
    snprintf(a, l, "%s/templates/%s", p->srcdir, name);
    if (stat(a, &st) == 0 && S_ISREG(st.st_mode))
        return a;
    snprintf(a, l, "%s/%s", p->srcdir, name);
    if (stat(a, &st) == 0 && S_ISREG(st.st_mode))
        return a;
    free(a);
    return NULL;
}

/* ---- parsing --------------------------------------------------------------- */

struct tparse {
    struct program *p;
    struct tpl *t;
    const char *src;
    size_t len, pos;
    int line;            /* current line in the template */
};

static _Noreturn void terr(struct tparse *tp, int code, const char *what)
{
    g_srcname = tp->t->path;
    g_smode = 1;
    cerr_at(code, tp->line, tp->line, what);
}

static int lineat(const char *s, size_t pos)
{
    int l = 1;
    for (size_t i = 0; i < pos; i++)
        l += s[i] == '\n';
    return l;
}

static void trim(const char **s, size_t *n)
{
    while (*n && (**s == ' ' || **s == '\t' || **s == '\n' || **s == '\r'))
        (*s)++, (*n)--;
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\n' ||
                  (*s)[*n - 1] == '\r'))
        (*n)--;
}

static int word_at(const char *s, size_t n, const char *w)
{
    size_t l = strlen(w);
    return n >= l && !memcmp(s, w, l) && (n == l || !(s[l] >= 'A' && s[l] <= 'Z'));
}

static int word_end(const char *s, size_t n, const char *w)
{
    size_t l = strlen(w);
    return n >= l && !memcmp(s + n - l, w, l) && (n == l || !(s[n - l - 1] >= 'A' && s[n - l - 1] <= 'Z'));
}

static void additem(struct item **items, int *n, struct item it)
{
    *items = xrealloc(*items, (size_t)(*n + 1) * sizeof **items);
    (*items)[(*n)++] = it;
}

static int has_unkop(struct node *n)
{
    if (!n)
        return 0;
    if (n->kind == E_UNKOP)
        return 1;
    for (int i = 0; i < n->nsubs; i++)
        if (has_unkop(n->subs[i]))
            return 1;
    if (n->kind == E_DB)
        return 0;
    return has_unkop(n->l) || has_unkop(n->r);
}

static struct node *texpr(struct tparse *tp, const char *s, size_t n, int *filter,
                          struct node **rhs, int *isis)
{
    struct node *e = parse_template_expr(tp->p, s, n, tp->line, filter, rhs, isis);
    if (!e || has_unkop(e) || (rhs && *rhs && has_unkop(*rhs))) {
        char *what = xstrndup(s, n);
        terr(tp, 406, what);
    }
    return e;
}

/* Parse items until EOF or a closing tag; returns the closer ("RESUME",
 * "REINSTATE") or NULL at end of input. */
static const char *parse_items(struct tparse *tp, struct item **items, int *nitems)
{
    for (;;) {
        const char *open = strstr(tp->src + tp->pos, "{{");
        size_t end = open ? (size_t)(open - tp->src) : tp->len;
        if (end > tp->pos) {
            struct item it = {0};
            it.kind = I_TEXT;
            it.text = xstrndup(tp->src + tp->pos, end - tp->pos);
            it.len = end - tp->pos;
            additem(items, nitems, it);
        }
        if (!open)
            return NULL;
        tp->line = lineat(tp->src, end);
        const char *close = strstr(open + 2, "}}");
        if (!close)
            terr(tp, 406, "{{ WITHOUT }}");
        const char *s = open + 2;
        size_t n = (size_t)(close - s);
        tp->pos = (size_t)(close + 2 - tp->src);
        trim(&s, &n);
        struct item it = {0};
        if (word_at(s, n, "NOTE")) {
            continue;
        } else if (word_at(s, n, "RESUME") && n == 6) {
            return "RESUME";
        } else if (word_at(s, n, "REINSTATE") && n == 9) {
            return "REINSTATE";
        } else if (word_at(s, n, "DO") && word_end(s, n, "NEXT") && n > 6) {
            const char *in = s + 2;
            size_t m = n - 6;
            trim(&in, &m);
            if (m >= 2 && in[0] == '"' && in[m - 1] == '"' && !memchr(in + 1, '"', m - 2)) {
                char *name = xstrndup(in + 1, m - 2);
                it.kind = I_INCLUDE;
                it.inc = tpl_load(tp->p, name, -tp->line);
                tp->t->incs = xrealloc(tp->t->incs, (size_t)(tp->t->nincs + 1) * sizeof(int));
                tp->t->incs[tp->t->nincs++] = it.inc;
                additem(items, nitems, it);
                continue;
            }
            it.kind = I_LOOP;
            it.e = texpr(tp, in, m, NULL, NULL, NULL);
            int line = tp->line;
            const char *c = parse_items(tp, &it.kids, &it.nkids);
            if (!c || strcmp(c, "RESUME")) {
                tp->line = line;
                terr(tp, 406, "{{DO ... NEXT}} WITHOUT {{RESUME}}");
            }
            additem(items, nitems, it);
        } else if (word_at(s, n, "ABSTAIN")) {
            const char *in = s + 7;
            size_t m = n - 7;
            trim(&in, &m);
            if (word_at(in, m, "FROM")) {
                in += 4, m -= 4;
            } else if (word_at(in, m, "WITHOUT")) {
                in += 7, m -= 7;
                it.without = 1;
            } else {
                terr(tp, 406, "ABSTAIN NEEDS FROM OR WITHOUT");
            }
            trim(&in, &m);
            int isis = 0;
            it.kind = I_COND;
            it.e = texpr(tp, in, m, NULL, &it.rhs, &isis);
            int line = tp->line;
            const char *c = parse_items(tp, &it.kids, &it.nkids);
            if (!c || strcmp(c, "REINSTATE")) {
                tp->line = line;
                terr(tp, 406, "{{ABSTAIN ...}} WITHOUT {{REINSTATE}}");
            }
            additem(items, nitems, it);
        } else {
            it.kind = I_EXPR;
            it.e = texpr(tp, s, n, &it.filter, NULL, NULL);
            additem(items, nitems, it);
        }
    }
}

/* line < 0: included from a template at line -line */
int tpl_load(struct program *p, const char *name, int line)
{
    for (int i = 0; i < p->ntpls; i++)
        if (!strcmp(p->tpls[i]->name, name)) {
            if (p->tpls[i]->loading)
                cerr_at(408, line < 0 ? -line : line, line < 0 ? -line : line, name);
            return i;
        }
    char *path = tpl_path(p, name);
    if (!path)
        cerr_at(402, line < 0 ? -line : line, line < 0 ? -line : line, name);
    size_t len;
    const char *src = read_tpl(path, &len);
    if (!src)
        cerr_at(402, line < 0 ? -line : line, line < 0 ? -line : line, name);
    struct tpl *t = xmalloc(sizeof *t);
    memset(t, 0, sizeof *t);
    t->name = xstrdup(name);
    t->path = path;
    t->loading = 1;
    p->tpls = xrealloc(p->tpls, (size_t)(p->ntpls + 1) * sizeof *p->tpls);
    int id = p->ntpls++;
    p->tpls[id] = t;
    /* flat text for the link checker */
    t->flat = xmalloc(len + 1);
    size_t o = 0;
    for (size_t i = 0; i < len;) {
        if (src[i] == '{' && i + 1 < len && src[i + 1] == '{') {
            const char *c = strstr(src + i + 2, "}}");
            size_t e = c ? (size_t)(c - src) + 2 : len;
            /* comments and block tags vanish; value tags become a marker */
            const char *s = src + i + 2;
            size_t n = (c ? (size_t)(c - s) : len - i - 2);
            trim(&s, &n);
            if (!(word_at(s, n, "NOTE") || word_at(s, n, "ABSTAIN") || word_at(s, n, "REINSTATE") ||
                  word_at(s, n, "RESUME") || (word_at(s, n, "DO") && word_end(s, n, "NEXT"))))
                t->flat[o++] = '\x01';
            i = e;
        } else {
            t->flat[o++] = src[i++];
        }
    }
    t->flat[o] = '\0';
    struct tparse tp = {p, t, src, len, 0, 1};
    const char *saved = g_srcname;
    int saved_s = g_smode;
    const char *c = parse_items(&tp, &t->items, &t->nitems);
    if (c) {
        char msg[64];
        snprintf(msg, sizeof msg, "{{%s}} WITHOUT AN OPENING TAG", c);
        terr(&tp, 406, msg);
    }
    g_srcname = saved;
    g_smode = saved_s;
    t->loading = 0;
    return id;
}

/* ---- static link checking ------------------------------------------------- */

struct route {
    const char *method;  /* "GET" or "POST" etc. */
    char **seg;
    int nseg;
    int splat, any;
    int origin;
};

static int split(char *path, char ***out)
{
    char **segs = NULL;
    int n = 0;
    for (char *s = strtok(path, "/"); s; s = strtok(NULL, "/")) {
        segs = xrealloc(segs, (size_t)(n + 1) * sizeof *segs);
        segs[n++] = s;
    }
    *out = segs;
    return n;
}

static int parse_route(const char *pat, struct route *r)
{
    memset(r, 0, sizeof *r);
    char *s = xstrdup(pat);
    if (!strcmp(s, "*")) {
        r->any = 1;
        r->method = "*";
        return 1;
    }
    r->method = "GET";
    char *path = s;
    char *sp = strchr(s, ' ');
    if (sp) {
        *sp = '\0';
        r->method = s;
        path = sp + 1;
        while (*path == ' ')
            path++;
        for (char *m = s; *m; m++)
            if (!(*m >= 'A' && *m <= 'Z'))
                return 0;
    }
    if (path[0] != '/')
        return 0;
    r->nseg = split(path, &r->seg);
    for (int i = 0; i < r->nseg; i++) {
        if (!strcmp(r->seg[i], "*")) {
            if (i != r->nseg - 1)
                return 0;
            r->splat = 1;
            r->nseg--;
        } else if (r->seg[i][0] == ':' && !r->seg[i][1]) {
            return 0;
        }
    }
    return 1;
}

static int route_matches(const struct route *r, const char *method, char **seg, int nseg)
{
    if (r->any) /* the catch-all is the 404 handler, not a destination */
        return 0;
    if (strcmp(r->method, method))
        return 0;
    if (nseg < r->nseg || (nseg > r->nseg && !r->splat))
        return 0;
    for (int i = 0; i < r->nseg; i++) {
        int dyn = strchr(seg[i], '\x01') != NULL;
        if (r->seg[i][0] == ':')
            continue;
        if (dyn || strcmp(r->seg[i], seg[i]))
            return 0;
    }
    return 1;
}

static struct route *routes;
static int nroutes;

static char *pretty(const char *link)
{
    size_t n = 0;
    for (const char *c = link; *c; c++)
        n += *c == '\x01' ? 7 : 1;
    char *o = xmalloc(n + 1), *q = o;
    for (const char *c = link; *c; c++) {
        if (*c == '\x01') {
            memcpy(q, "{{...}}", 7);
            q += 7;
        } else
            *q++ = *c;
    }
    *q = '\0';
    return o;
}

static void check_link(const struct tuple *t, const struct tpl *tp, const char *val, size_t n,
                       const char *method)
{
    if (n == 0 || val[0] != '/' || (n > 1 && val[1] == '/'))
        return;
    char *link = xstrndup(val, n);
    if (!strncmp(link, "/public/", 8) || !strcmp(link, "/public"))
        return;
    char *path = xstrdup(link);
    path[strcspn(path, "?#")] = '\0';
    char **seg;
    int nseg = split(path, &seg);
    for (int i = 0; i < nroutes; i++) {
        const struct route *r = &routes[i];
        if (r->origin != 16000 && (t->label == 0 || r->origin != t->label))
            continue;
        if (route_matches(r, method, seg, nseg))
            return;
    }
    char *msg = xmalloc(strlen(link) * 8 + strlen(tp->name) + 64);
    sprintf(msg, "%s %s FROM %s", method, pretty(link), tp->name);
    g_smode = 1;
    cerr_at(401, t->line, t->line, msg);
}

static int ieq(const char *a, size_t n, const char *b)
{
    if (strlen(b) != n)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != b[i])
            return 0;
    }
    return 1;
}

struct attr {
    const char *name, *val;
    size_t nlen, vlen;
};

/* Scan the flat HTML of TP for href/action/formaction. */
static void scan_links(const struct tuple *t, const struct tpl *tp, const char **formmeth, int *depth)
{
    const char *s = tp->flat;
    for (const char *c = s; *c; c++) {
        if (*c != '<')
            continue;
        const char *q = c + 1;
        int closing = 0;
        if (*q == '/') {
            closing = 1;
            q++;
        }
        const char *nm = q;
        while (*q && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')))
            q++;
        size_t nlen = (size_t)(q - nm);
        if (!nlen)
            continue;
        if (closing) {
            if (ieq(nm, nlen, "form") && *depth > 0)
                (*depth)--;
            continue;
        }
        struct attr at[64];
        int na = 0;
        for (;;) {
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r' || *q == '/')
                q++;
            if (!*q || *q == '>')
                break;
            const char *an = q;
            while (*q && *q != '=' && *q != '>' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r')
                q++;
            struct attr a = {an, "", (size_t)(q - an), 0};
            while (*q == ' ' || *q == '\t' || *q == '\n')
                q++;
            if (*q == '=') {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\n')
                    q++;
                if (*q == '"' || *q == '\'') {
                    char quote = *q++;
                    a.val = q;
                    while (*q && *q != quote)
                        q++;
                    a.vlen = (size_t)(q - a.val);
                    if (*q)
                        q++;
                } else {
                    a.val = q;
                    while (*q && *q != '>' && *q != ' ' && *q != '\t' && *q != '\n')
                        q++;
                    a.vlen = (size_t)(q - a.val);
                }
            }
            if (na < 64)
                at[na++] = a;
            if (a.nlen == 0)
                q++;
        }
        const char *method = "GET";
        int isform = ieq(nm, nlen, "form");
        for (int i = 0; i < na; i++)
            if (isform && ieq(at[i].name, at[i].nlen, "method") && ieq(at[i].val, at[i].vlen, "post"))
                method = "POST";
        if (isform && *depth < 32)
            formmeth[(*depth)++] = method;
        const char *enclosing = *depth > 0 ? formmeth[*depth - 1] : "GET";
        for (int i = 0; i < na; i++) {
            if (ieq(at[i].name, at[i].nlen, "href"))
                check_link(t, tp, at[i].val, at[i].vlen, "GET");
            else if (ieq(at[i].name, at[i].nlen, "action"))
                check_link(t, tp, at[i].val, at[i].vlen, isform ? method : enclosing);
            else if (ieq(at[i].name, at[i].nlen, "formaction")) {
                const char *m = enclosing;
                for (int j = 0; j < na; j++)
                    if (ieq(at[j].name, at[j].nlen, "formmethod"))
                        m = ieq(at[j].val, at[j].vlen, "post") ? "POST" : "GET";
                check_link(t, tp, at[i].val, at[i].vlen, m);
            }
        }
        if (*q)
            c = q;
    }
}

static void check_tpl_tree(struct program *p, const struct tuple *t, int id, char *seen,
                           const char **formmeth, int *depth)
{
    if (seen[id])
        return;
    seen[id] = 1;
    scan_links(t, p->tpls[id], formmeth, depth);
    for (int i = 0; i < p->tpls[id]->nincs; i++)
        check_tpl_tree(p, t, p->tpls[id]->incs[i], seen, formmeth, depth);
}

void tpl_check_links(struct program *p)
{
    for (int i = 0; i < p->nt; i++) {
        struct tuple *t = &p->t[i];
        if (t->type != S_ROUTE)
            continue;
        struct route r;
        if (!parse_route(t->str, &r)) {
            g_smode = 1;
            cerr_at(407, t->line, t->line, t->str);
        }
        r.origin = t->origin;
        routes = xrealloc(routes, (size_t)(nroutes + 1) * sizeof *routes);
        routes[nroutes++] = r;
    }
    for (int i = 0; i < p->nt; i++) {
        struct tuple *t = &p->t[i];
        if (t->type == S_TEMPLATE) {
            char *seen = xmalloc((size_t)p->ntpls + 1);
            memset(seen, 0, (size_t)p->ntpls + 1);
            const char *formmeth[32];
            int depth = 0;
            check_tpl_tree(p, t, t->tpl, seen, formmeth, &depth);
            free(seen);
        } else if (t->type == S_REDIRECT) {
            /* the rendering is the Location: check it as a GET link */
            const char *f = p->tpls[t->tpl]->flat;
            size_t n = strlen(f);
            trim(&f, &n);
            check_link(t, p->tpls[t->tpl], f, n, "GET");
        }
    }
}

/* ---- C generation ------------------------------------------------------------- */

void tpl_emit_decls(struct program *p, FILE *out)
{
    for (int i = 0; i < p->ntpls; i++)
        fprintf(out, "static void TPL%d(void);\n", i);
}

static void emit_items(FILE *out, struct item *it, int n, int depth)
{
    for (int i = 0; i < n; i++) {
        struct item *x = &it[i];
        switch (x->kind) {
        case I_TEXT:
            fputs("    ioi_tpl_text(", out);
            emit_cstring(out, x->text, x->len);
            fprintf(out, ", %zu);\n", x->len);
            break;
        case I_EXPR:
            if (x->e->isval) {
                fputs("    ioi_tpl_value(", out);
                gen_val(x->e);
            } else {
                fputs("    ioi_tpl_number(", out);
                gen_num(x->e);
            }
            fprintf(out, ", %d);\n", x->filter);
            break;
        case I_INCLUDE:
            fprintf(out, "    TPL%d();\n", x->inc);
            break;
        case I_LOOP:
            fprintf(out, "    { uint32_t L%d = ioi_tpl_loop_list(", depth);
            gen_val(x->e);
            fprintf(out, ");\n    struct ioi_tpl_save *S%d = ioi_tpl_save(&TV);\n", depth);
            fprintf(out, "    for (size_t i%d = 0; i%d < ioi_cas_len(L%d); i%d++) {\n", depth, depth, depth,
                    depth);
            fprintf(out, "    ioi_tpl_item(&TV, ioi_cas_get(L%d, i%d), (uint32_t)i%d + 1);\n", depth,
                    depth, depth);
            emit_items(out, x->kids, x->nkids, depth + 1);
            fprintf(out, "    }\n    ioi_tpl_restore(&TV, S%d); }\n", depth);
            break;
        case I_COND:
            fputs("    if (", out);
            if (!x->without)
                fputs("!", out);
            if (x->rhs) {
                fputs("ioi_val_equal(", out);
                gen_val(x->e);
                fputs(", ", out);
                gen_val(x->rhs);
                fputs(")", out);
            } else {
                fputs("ioi_val_truthy(", out);
                gen_val(x->e);
                fputs(")", out);
            }
            fputs(") {\n", out);
            emit_items(out, x->kids, x->nkids, depth);
            fputs("    }\n", out);
            break;
        }
    }
}

void tpl_emit_functions(struct program *p, FILE *out)
{
    gen_set_out(out);
    for (int i = 0; i < p->ntpls; i++) {
        fprintf(out, "/* template %s */\nstatic void TPL%d(void)\n{\n", p->tpls[i]->name, i);
        emit_items(out, p->tpls[i]->items, p->tpls[i]->nitems, 0);
        fputs("}\n\n", out);
    }
}
