/* parse.c — statement and expression parser.
 *
 * Classic mode reproduces C-INTERCAL 0.35's yacc grammar (parser.y),
 * including its error recovery: a statement that does not parse becomes a
 * "splattered" tuple that dies with E000 when executed, and the scan
 * resumes at the next DO, PLEASE, MAYBE or label token. The parser is
 * recursive descent with one token of lookahead; every decision mirrors
 * the LALR(1) table's choice (including %prec conflict resolutions), and
 * side effects (E017, E200, E197, line bookkeeping) happen at the moments
 * yacc would perform the corresponding reductions.
 *
 * Interstates mode (-s) treats each source line as one statement and adds
 * string literals and the web/database statements of SPEC.md §2.
 */
#include "ick.h"

#include <stdlib.h>
#include <string.h>

struct ps {
    struct program *p;
    struct lexer *lx;
    struct token la;
    int have_la;
    int stbeginline, thisline;
    int prevtuple;
    int smode;
    int trial;          /* > 0 while trying a rabbit-ear grouping (-s) */
    int trial_err;      /* deferred compile error during a trial */
    int trial_errline;
    int badfeature;     /* newfangled construct seen in this statement */
    int insyslib;
};

static int fail_marker;
#define FAIL ((struct node *)&fail_marker)

static struct token *peek(struct ps *ps)
{
    if (!ps->have_la) {
        lex_next(ps->lx, &ps->la);
        ps->have_la = 1;
    }
    return &ps->la;
}

static struct token take(struct ps *ps)
{
    peek(ps);
    ps->have_la = 0;
    return ps->la;
}

static int iyy(struct ps *ps) { return lex_iyylineno(ps->lx); }

/* Compile-time error raised during parsing (deferred while backtracking). */
static void perr(struct ps *ps, int code)
{
    if (ps->trial) {
        if (!ps->trial_err) {
            ps->trial_err = code;
            ps->trial_errline = iyy(ps);
        }
        return;
    }
    cerr(code, iyy(ps), NULL);
}

int intern(struct program *p, int type, uint32_t num, int line)
{
    if (num < 1 || num > 65535)
        cerr(200, line, NULL);
    for (int i = 0; i < p->nvars[type]; i++)
        if (p->vars[type][i].num == num)
            return i;
    if (p->nvars[type] == p->capvars[type]) {
        p->capvars[type] = p->capvars[type] ? p->capvars[type] * 2 : 16;
        p->vars[type] = xrealloc(p->vars[type], (size_t)p->capvars[type] * sizeof(struct var));
    }
    p->vars[type][p->nvars[type]].type = type;
    p->vars[type][p->nvars[type]].num = num;
    return p->nvars[type]++;
}

static int ps_intern(struct ps *ps, int type, int num)
{
    uint32_t n = (uint32_t)num;
    if (n < 1 || n > 65535) {
        perr(ps, 200);
        return 0;
    }
    return intern(ps->p, type, n, iyy(ps));
}

/* ---- nodes ---------------------------------------------------------------- */

static struct node *mk(enum ekind k)
{
    struct node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = k;
    return n;
}

static struct node *mkconst(uint32_t v)
{
    struct node *n = mk(E_CONST);
    n->num = v;
    n->width = 16;
    return n;
}

static struct node *mkvar(int vtype, int var)
{
    struct node *n = mk(E_VAR);
    n->vtype = vtype;
    n->var = var;
    n->width = (vtype == V_ONE || vtype == V_TAIL) ? 16 : 32;
    n->isval = vtype == V_TAIL || vtype == V_HYB;
    return n;
}

static struct node *mkunary(int op, struct node *r)
{
    struct node *n = mk(E_UNARY);
    n->op = op;
    n->r = r;
    n->width = r->width;
    return n;
}

static struct node *mkbin(enum ekind k, struct node *l, struct node *r)
{
    struct node *n = mk(k);
    n->l = l;
    n->r = r;
    if (k == E_MINGLE || k == E_UNKOP)
        n->width = 32;
    else
        n->width = r->width; /* SELECT: n-bit select has an n-bit result */
    return n;
}

static struct node *mkelem(struct node *arr, struct node **subs, int n)
{
    struct node *e = mk(E_ELEM);
    e->vtype = arr->vtype;
    e->var = arr->var;
    e->width = arr->width;
    e->subs = subs;
    e->nsubs = n;
    e->isval = arr->vtype == V_HYB;
    free(arr);
    return e;
}

static int is_close(enum tok t) { return t == T_CLOSESPARK || t == T_CLOSEEARS; }
static int is_quote(enum tok t)
{
    return t == T_OPENSPARK || t == T_CLOSESPARK || t == T_OPENEARS || t == T_CLOSEEARS;
}
static int starts_unambig(enum tok t)
{
    return t == T_UNARY || t == T_MESH || t == T_ONESPOT || t == T_TWOSPOT || is_quote(t);
}

/* ---- expressions ---------------------------------------------------------- */

static struct node *p_unambig(struct ps *ps);
static struct node *p_expr(struct ps *ps);

static struct node *p_constant_after_mesh(struct ps *ps)
{
    struct token *t = peek(ps);
    if (t->type == T_NUMBER) {
        int v = take(ps).val;
        if ((unsigned)v > 65535u)
            perr(ps, 17);
        return mkconst((uint32_t)v);
    }
    if (t->type == T_UNARY) {
        int op = take(ps).val;
        if (peek(ps)->type != T_NUMBER)
            return FAIL;
        int v = take(ps).val;
        if ((unsigned)v > 65535u)
            perr(ps, 17);
        return mkunary(op, mkconst((uint32_t)v));
    }
    return FAIL;
}

static struct node *p_scalar_after_spot(struct ps *ps, int vtype)
{
    struct token *t = peek(ps);
    if (t->type == T_NUMBER) {
        int v = take(ps).val;
        return mkvar(vtype, ps_intern(ps, vtype, v));
    }
    if (t->type == T_UNARY) {
        int op = take(ps).val;
        if (peek(ps)->type != T_NUMBER)
            return FAIL;
        int v = take(ps).val;
        return mkunary(op, mkvar(vtype, ps_intern(ps, vtype, v)));
    }
    return FAIL;
}

static struct node *p_db(struct ps *ps, enum tok close);
static struct node *p_string_or_group(struct ps *ps, struct token q);

/* After an opening spark/ears: [UNARY] expr CLOSE */
static struct node *p_group(struct ps *ps, int ears)
{
    enum tok close = ears ? T_CLOSEEARS : T_CLOSESPARK;
    struct node *e;
    if (ps->smode && peek(ps)->type == T_FROM)
        return p_db(ps, close);
    if (peek(ps)->type == T_UNARY) {
        int op = take(ps).val;
        e = p_expr(ps);
        if (e == FAIL)
            return FAIL;
        e = mkunary(op, e);
    } else {
        e = p_expr(ps);
        if (e == FAIL)
            return FAIL;
    }
    if (peek(ps)->type != close)
        return FAIL;
    take(ps);
    lex_pop2(ps->lx);
    return e;
}

static struct node *p_limunambig(struct ps *ps)
{
    struct token *t = peek(ps);
    switch (t->type) {
    case T_MESH:
        take(ps);
        return p_constant_after_mesh(ps);
    case T_ONESPOT:
        take(ps);
        return p_scalar_after_spot(ps, V_ONE);
    case T_TWOSPOT:
        take(ps);
        return p_scalar_after_spot(ps, V_TWO);
    case T_OPENSPARK:
    case T_CLOSESPARK:
        take(ps);
        return p_group(ps, 0);
    case T_OPENEARS:
    case T_CLOSEEARS:
        if (ps->smode)
            return p_string_or_group(ps, take(ps));
        take(ps);
        return p_group(ps, 1);
    case T_STRING: {
        struct token s = take(ps);
        struct node *n = mk(E_STRING);
        n->str = s.str;
        n->slen = s.slen;
        n->isval = 1;
        n->width = 32;
        return n;
    }
    default:
        return FAIL;
    }
}

static struct node *p_unambig(struct ps *ps)
{
    if (peek(ps)->type == T_UNARY) {
        int op = take(ps).val;
        struct node *r = p_unambig(ps);
        if (r == FAIL)
            return FAIL;
        return mkunary(op, r);
    }
    return p_limunambig(ps);
}

/* Subscript list after SUB. nested: sublist1 (exactly one item). */
static struct node *p_arrayref_after_type(struct ps *ps, int vtype, int need_sub);

static int p_sublist(struct ps *ps, int nested, struct node ***out)
{
    struct node **items = NULL;
    int n = 0;
    for (;;) {
        struct token *t = peek(ps);
        if (t->type == T_TAIL || t->type == T_HYBRID) {
            int vt = take(ps).type == T_TAIL ? V_TAIL : V_HYB;
            struct node *e = p_arrayref_after_type(ps, vt, 2);
            if (e == FAIL)
                return -1;
            items = xrealloc(items, (size_t)(n + 1) * sizeof *items);
            items[n++] = e;
            break;
        }
        if (starts_unambig(t->type)) {
            if (n > 0 && is_close(t->type))
                break; /* %prec HIGHPREC: reduce, the quote closes a group */
            struct node *e = p_unambig(ps);
            if (e == FAIL)
                return -1;
            items = xrealloc(items, (size_t)(n + 1) * sizeof *items);
            items[n++] = e;
            if (nested)
                break;
            continue;
        }
        if (n == 0)
            return -1;
        break;
    }
    *out = items;
    return n;
}

/* need_sub: 0 bare array allowed, 1 SUB required (expr), 2 nested
 * (subscr1/osubscr1: SUB with a single-item list). */
static struct node *p_arrayref_after_type(struct ps *ps, int vtype, int need_sub)
{
    struct token *t = peek(ps);
    int op = 0;
    if (t->type == T_UNARY) {
        op = take(ps).val;
        t = peek(ps);
        if (t->type != T_NUMBER)
            return FAIL;
    } else if (t->type != T_NUMBER) {
        return FAIL;
    }
    int num = take(ps).val;
    struct node *arr = mkvar(vtype, ps_intern(ps, vtype, num));
    if (peek(ps)->type != T_SUB) {
        if (op || need_sub == 2 || (need_sub == 1 && !ps->smode))
            return FAIL;
        return arr; /* bare array (unknown atom, or -s whole-array value) */
    }
    take(ps);
    struct node **subs;
    int n = p_sublist(ps, need_sub == 2, &subs);
    if (n < 0)
        return FAIL;
    struct node *e = mkelem(arr, subs, n);
    return op ? mkunary(op, e) : e;
}

/* After an unambig: SELECT/MINGLE/unop expr, or SLAT (newfangled). */
static struct node *p_binrest(struct ps *ps, struct node *u, int bare_scalar)
{
    struct token *t = peek(ps);
    if (t->type == T_SELECT || t->type == T_MINGLE || t->type == T_BADCHAR) {
        enum tok k = take(ps).type;
        struct node *r = p_expr(ps);
        if (r == FAIL)
            return FAIL;
        return mkbin(k == T_SELECT ? E_SELECT : k == T_MINGLE ? E_MINGLE : E_UNKOP, u, r);
    }
    if (t->type == T_SLAT && bare_scalar) {
        take(ps);
        struct node *r = p_expr(ps);
        if (r == FAIL)
            return FAIL;
        ps->badfeature = ps->badfeature ? ps->badfeature : 111;
        return u;
    }
    return u;
}

static struct node *p_valsub(struct ps *ps, struct node *u)
{
    while (ps->smode && peek(ps)->type == T_SUB) {
        take(ps);
        struct node *k = p_unambig(ps);
        if (k == FAIL)
            return FAIL;
        struct node *n = mk(E_VALSUB);
        n->l = u;
        n->r = k;
        n->isval = 1;
        n->width = 32;
        u = n;
    }
    return u;
}

static struct node *p_expr(struct ps *ps)
{
    struct token *t = peek(ps);
    if (t->type == T_TAIL || t->type == T_HYBRID) {
        int vt = take(ps).type == T_TAIL ? V_TAIL : V_HYB;
        struct node *e = p_arrayref_after_type(ps, vt, 1);
        if (e == FAIL)
            return FAIL;
        if (ps->smode && e->kind == E_VAR) /* whole array value: may be SUBbed */
            return e;
        return e; /* subscr/osubscr: no binary operator may follow */
    }
    int bare = (t->type == T_ONESPOT || t->type == T_TWOSPOT);
    struct node *u = p_unambig(ps);
    if (u == FAIL)
        return FAIL;
    if (bare && u->kind != E_VAR)
        bare = 0;
    u = p_valsub(ps, u);
    if (u == FAIL)
        return FAIL;
    return p_binrest(ps, u, bare);
}

/* ---- Interstates expression pieces --------------------------------------- */

static struct node *p_string_or_group(struct ps *ps, struct token q)
{
    /* q is the already-taken '"'. Try a rabbit-ear grouping first. */
    struct lexsave sv;
    lex_save(ps->lx, &sv);
    ps->trial++;
    int saved_err = ps->trial_err, saved_bad = ps->badfeature;
    ps->trial_err = 0;
    struct node *e = p_group(ps, 1);
    ps->trial--;
    if (e != FAIL) {
        lex_drop(&sv);
        int err = ps->trial_err, line = ps->trial_errline;
        ps->trial_err = saved_err;
        if (err) {
            if (ps->trial)
                ps->trial_err = ps->trial_err ? ps->trial_err : err;
            else
                cerr(err, line, NULL);
        }
        return e;
    }
    ps->trial_err = saved_err;
    ps->badfeature = saved_bad;
    lex_restore(ps->lx, &sv);
    lex_drop(&sv);
    ps->have_la = 0;
    if (ps->lx->qdepth > 0)
        ps->lx->qdepth--; /* undo the push of the opening '"' */
    struct token s;
    if (!lex_string_at(ps->lx, q.pos, &s))
        return FAIL;
    struct node *n = mk(E_STRING);
    n->str = s.str;
    n->slen = s.slen;
    n->isval = 1;
    n->width = 32;
    return n;
}

/* A string operand ("..." in a string position). */
static char *p_string_operand(struct ps *ps, size_t *len)
{
    struct lexer *lx = ps->lx;
    if (ps->have_la) {
        if (!is_quote(ps->la.type) || lx->s[ps->la.pos] != '"')
            return NULL;
        ps->have_la = 0;
        if (lx->qdepth > 0)
            lx->qdepth--;
        lx->pos = ps->la.pos;
    } else {
        while (lx->pos < lx->len && (lx->s[lx->pos] == ' ' || lx->s[lx->pos] == '\t'))
            lx->pos++;
    }
    struct token s;
    if (!lex_string_at(lx, lx->pos, &s))
        return NULL;
    if (len)
        *len = s.slen;
    return s.str;
}

static int p_word(struct ps *ps, int w)
{
    struct token *t = peek(ps);
    if (t->type == T_WORD && t->val == w) {
        take(ps);
        return 1;
    }
    return 0;
}

static int const_value(struct node *n, int *out)
{
    if (n->kind != E_CONST)
        return 0;
    *out = (int)n->num;
    return 1;
}

static int flatten_cols(struct node *n, int **cols, int *nc)
{
    int v;
    if (const_value(n, &v)) {
        *cols = xrealloc(*cols, (size_t)(*nc + 1) * sizeof **cols);
        (*cols)[(*nc)++] = v;
        return 1;
    }
    if (n->kind == E_MINGLE)
        return flatten_cols(n->l, cols, nc) && flatten_cols(n->r, cols, nc);
    return 0;
}

/* 'FROM DATABASE #t[~SEL]' CLAUSE... (the FROM is the lookahead). */
static struct node *p_db(struct ps *ps, enum tok close)
{
    take(ps); /* FROM */
    if (!p_word(ps, W_DATABASE))
        return FAIL;
    if (peek(ps)->type != T_MESH)
        return FAIL;
    take(ps);
    if (peek(ps)->type != T_NUMBER)
        return FAIL;
    struct dbq *q = xmalloc(sizeof *q);
    memset(q, 0, sizeof *q);
    q->table = take(ps).val;
    if (peek(ps)->type == T_SELECT) {
        take(ps);
        q->sel = p_expr(ps);
        if (q->sel == FAIL)
            return FAIL;
        q->has_sel = 1;
    }
    if (peek(ps)->type != close)
        return FAIL;
    take(ps);
    lex_pop2(ps->lx);
    int shape_set = 0;
    for (;;) {
        struct token *t = peek(ps);
        struct dbcl c;
        memset(&c, 0, sizeof c);
        if (t->type == T_WORD && t->val == W_HAVING) {
            take(ps);
            if (peek(ps)->type != T_MESH)
                return FAIL;
            take(ps);
            if (peek(ps)->type != T_NUMBER)
                return FAIL;
            c.kind = W_HAVING;
            c.col = take(ps).val;
            if (p_word(ps, W_SOMEWHERE))
                c.op = 1;
            else if (p_word(ps, W_BEYOND))
                c.op = 2;
            c.e = p_expr(ps);
            if (c.e == FAIL)
                return FAIL;
        } else if (t->type == T_BY) {
            take(ps);
            if (peek(ps)->type != T_MESH)
                return FAIL;
            take(ps);
            if (peek(ps)->type != T_NUMBER)
                return FAIL;
            c.kind = W_BY;
            c.col = take(ps).val;
            c.backwards = p_word(ps, W_BACKWARDS);
        } else if (t->type == T_WORD && (t->val == W_SKIP || t->val == W_TAKE)) {
            c.kind = take(ps).val;
            c.e = p_expr(ps);
            if (c.e == FAIL)
                return FAIL;
        } else if (t->type == T_WORD && t->val == W_IN) {
            take(ps);
            if (!p_word(ps, W_BULK) || shape_set)
                return FAIL;
            q->shape = 1;
            shape_set = 1;
            continue;
        } else if (t->type == T_WORD && t->val == W_HOW) {
            take(ps);
            if (!p_word(ps, W_MANY) || shape_set)
                return FAIL;
            q->shape = 2;
            shape_set = 1;
            continue;
        } else {
            break;
        }
        q->cl = xrealloc(q->cl, (size_t)(q->ncl + 1) * sizeof *q->cl);
        q->cl[q->ncl++] = c;
    }
    if (q->ncl == 0 && !shape_set) {
        q->shape = 3; /* primary-key lookup */
    } else if (q->has_sel && q->shape != 2) {
        if (!flatten_cols(q->sel, &q->cols, &q->ncols))
            cerr(413, ps->thisline, "THE COLUMNS OF A QUERY MUST BE CONSTANTS");
    }
    struct node *n = mk(E_DB);
    n->db = q;
    n->isval = 1;
    n->width = 32;
    return n;
}

/* ---- statements ------------------------------------------------------------ */

static struct tuple *newtuple(struct ps *ps, enum stype type)
{
    struct program *p = ps->p;
    if (p->nt == p->capt) {
        p->capt = p->capt ? p->capt * 2 : 64;
        p->t = xrealloc(p->t, (size_t)p->capt * sizeof *p->t);
    }
    struct tuple *t = &p->t[p->nt++];
    memset(t, 0, sizeof *t);
    t->type = type;
    t->line = ps->thisline;
    t->cf = -1;
    t->insyslib = ps->insyslib;
    return t;
}

static void skip_to_start(struct ps *ps)
{
    for (;;) {
        enum tok t = peek(ps)->type;
        if (t == T_DO || t == T_MAYBE || t == T_LABEL || t == T_EOF)
            return;
        take(ps);
    }
}

static int p_gerunds(struct ps *ps, int **out)
{
    int *g = NULL, n = 0;
    for (;;) {
        if (peek(ps)->type != T_GERUND)
            return -1;
        g = xrealloc(g, (size_t)(n + 1) * sizeof *g);
        g[n++] = take(ps).val;
        if (peek(ps)->type != T_INTERSECTION)
            break;
        take(ps);
    }
    *out = g;
    return n;
}

static void additem(struct tuple *t, struct node *n)
{
    t->items = xrealloc(t->items, (size_t)(t->nitems + 1) * sizeof *t->items);
    t->items[t->nitems++] = n;
}

/* variable: scalar | ick_array (bare) */
static struct node *p_variable(struct ps *ps)
{
    struct token *t = peek(ps);
    int vt;
    switch (t->type) {
    case T_ONESPOT: vt = V_ONE; break;
    case T_TWOSPOT: vt = V_TWO; break;
    case T_TAIL: vt = V_TAIL; break;
    case T_HYBRID: vt = V_HYB; break;
    default: return FAIL;
    }
    take(ps);
    if (peek(ps)->type != T_NUMBER)
        return FAIL;
    return mkvar(vt, ps_intern(ps, vt, take(ps).val));
}

static int p_varlist_rest(struct ps *ps, struct tuple *t)
{
    while (peek(ps)->type == T_INTERSECTION) {
        take(ps);
        struct node *v = p_variable(ps);
        if (v == FAIL)
            return 0;
        additem(t, v);
    }
    return 1;
}

/* initem: scalar | subscr | ick_array ; outitem adds constant */
static struct node *p_ioitem(struct ps *ps, int out)
{
    struct token *t = peek(ps);
    switch (t->type) {
    case T_ONESPOT:
    case T_TWOSPOT: {
        int vt = take(ps).type == T_ONESPOT ? V_ONE : V_TWO;
        if (peek(ps)->type != T_NUMBER)
            return FAIL;
        return mkvar(vt, ps_intern(ps, vt, take(ps).val));
    }
    case T_TAIL:
    case T_HYBRID: {
        int vt = take(ps).type == T_TAIL ? V_TAIL : V_HYB;
        if (peek(ps)->type != T_NUMBER)
            return FAIL;
        struct node *arr = mkvar(vt, ps_intern(ps, vt, take(ps).val));
        if (peek(ps)->type != T_SUB)
            return arr;
        take(ps);
        struct node **subs;
        int n = p_sublist(ps, 0, &subs);
        if (n < 0)
            return FAIL;
        return mkelem(arr, subs, n);
    }
    case T_MESH:
        if (!out)
            return FAIL;
        take(ps);
        if (peek(ps)->type != T_NUMBER)
            return FAIL;
        {
            int v = take(ps).val;
            if ((unsigned)v > 65535u)
                perr(ps, 17);
            return mkconst((uint32_t)v);
        }
    default:
        return FAIL;
    }
}

static int p_iolist_rest(struct ps *ps, struct tuple *t, int out)
{
    while (peek(ps)->type == T_INTERSECTION) {
        take(ps);
        struct node *n = p_ioitem(ps, out);
        if (n == FAIL)
            return 0;
        additem(t, n);
    }
    return 1;
}

static int is_atom_start(enum tok t)
{
    return t == T_ONESPOT || t == T_TWOSPOT || t == T_TAIL || t == T_HYBRID || starts_unambig(t);
}

/* An unknown-statement atom: subscr | scalar | notanlvalue | ick_array. */
static struct node *p_atom(struct ps *ps)
{
    struct token *t = peek(ps);
    if (t->type == T_TAIL || t->type == T_HYBRID) {
        int vt = take(ps).type == T_TAIL ? V_TAIL : V_HYB;
        return p_arrayref_after_type(ps, vt, 0);
    }
    return p_expr(ps);
}

/* unknownstatement continuation; last_atom: previous item was an atom. */
static int p_unknown_rest(struct ps *ps, int last_atom)
{
    for (;;) {
        enum tok t = peek(ps)->type;
        if (t == T_UNKNOWNID) {
            take(ps);
            last_atom = 0;
            continue;
        }
        if (!last_atom && is_atom_start(t)) {
            if (p_atom(ps) == FAIL)
                return 0;
            last_atom = 1;
            continue;
        }
        return 1;
    }
}

static struct tuple *bad(struct ps *ps, int code)
{
    struct tuple *t = newtuple(ps, S_BADFEATURE);
    t->badcode = code;
    return t;
}

/* Interstates: after READ OUT/WRITE IN first item, WITH ... */
static struct tuple *p_s_readout(struct ps *ps, struct node *e);
static struct tuple *p_s_writein(struct ps *ps, struct node *e);

/* Returns the new tuple, or NULL on a syntax error (lookahead = offender). */
static struct tuple *p_perform(struct ps *ps)
{
    struct token *t = peek(ps);
    struct tuple *tp;
    struct node *e;
    switch (t->type) {
    case T_LABEL: {
        int lab = take(ps).val;
        if (peek(ps)->type != T_NEXT)
            return NULL;
        take(ps);
        tp = newtuple(ps, S_NEXT);
        tp->target = lab;
        return tp;
    }
    case T_FORGET:
    case T_RESUME: {
        enum tok k = take(ps).type;
        e = p_expr(ps);
        if (e == FAIL)
            return NULL;
        if (ps->smode && k == T_FORGET && e->kind == E_DB) {
            tp = newtuple(ps, S_DB_DELETE);
            tp->rhs = e;
            return tp;
        }
        tp = newtuple(ps, k == T_FORGET ? S_FORGET : S_RESUME);
        tp->rhs = e;
        return tp;
    }
    case T_STASH:
    case T_RETRIEVE:
    case T_IGNORE:
    case T_REMEMBER: {
        enum tok k = take(ps).type;
        if (ps->smode && k == T_STASH) {
            struct token *u = peek(ps);
            if (u->type == T_TAIL || u->type == T_HYBRID) {
                int vt = take(ps).type == T_TAIL ? V_TAIL : V_HYB;
                if (peek(ps)->type != T_NUMBER)
                    return NULL;
                struct node *arr = mkvar(vt, ps_intern(ps, vt, take(ps).val));
                struct node *lv = arr;
                if (peek(ps)->type == T_SUB) {
                    take(ps);
                    struct node **subs;
                    int n = p_sublist(ps, 0, &subs);
                    if (n < 0)
                        return NULL;
                    lv = mkelem(arr, subs, n);
                    if (peek(ps)->type != T_GETS)
                        return NULL;
                }
                if (peek(ps)->type == T_GETS) {
                    take(ps);
                    e = p_expr(ps);
                    if (e == FAIL)
                        return NULL;
                    tp = newtuple(ps, S_STASH_ASSIGN);
                    tp->lhs = lv;
                    tp->rhs = e;
                    return tp;
                }
                if (peek(ps)->type == T_WORD && peek(ps)->val == W_INTO && vt == V_HYB && lv == arr) {
                    take(ps);
                    if (!p_word(ps, W_DATABASE) || peek(ps)->type != T_MESH)
                        return NULL;
                    take(ps);
                    if (peek(ps)->type != T_NUMBER)
                        return NULL;
                    tp = newtuple(ps, S_DB_INSERT);
                    tp->lhs = arr;
                    tp->target = take(ps).val;
                    return tp;
                }
                tp = newtuple(ps, S_STASH);
                additem(tp, arr);
                return p_varlist_rest(ps, tp) ? tp : NULL;
            }
            if (u->type == T_ONESPOT || u->type == T_TWOSPOT) {
                struct node *v = p_variable(ps);
                if (v == FAIL)
                    return NULL;
                if (peek(ps)->type == T_GETS) {
                    take(ps);
                    e = p_expr(ps);
                    if (e == FAIL)
                        return NULL;
                    tp = newtuple(ps, S_STASH_ASSIGN);
                    tp->lhs = v;
                    tp->rhs = e;
                    return tp;
                }
                tp = newtuple(ps, S_STASH);
                additem(tp, v);
                return p_varlist_rest(ps, tp) ? tp : NULL;
            }
        }
        struct node *v = p_variable(ps);
        if (v == FAIL)
            return NULL;
        tp = newtuple(ps, k == T_STASH ? S_STASH : k == T_RETRIEVE ? S_RETRIEVE
                                       : k == T_IGNORE ? S_IGNORE : S_REMEMBER);
        additem(tp, v);
        return p_varlist_rest(ps, tp) ? tp : NULL;
    }
    case T_ABSTAIN:
        take(ps);
        if (peek(ps)->type == T_FROM) {
            take(ps);
            if (peek(ps)->type == T_LABEL) {
                int lab = take(ps).val;
                ps->stbeginline = 0;
                tp = newtuple(ps, S_ABSTAIN);
                tp->target = lab;
                return tp;
            }
            int *g;
            int n = p_gerunds(ps, &g);
            if (n < 0)
                return NULL;
            tp = newtuple(ps, S_DISABLE);
            tp->ger = g;
            tp->nger = n;
            return tp;
        }
        e = p_expr(ps); /* computed ABSTAIN (newfangled) */
        if (e == FAIL || peek(ps)->type != T_FROM)
            return NULL;
        take(ps);
        if (peek(ps)->type == T_LABEL) {
            take(ps);
            ps->stbeginline = 0;
        } else {
            int *g;
            if (p_gerunds(ps, &g) < 0)
                return NULL;
        }
        return bad(ps, 111);
    case T_REINSTATE:
        take(ps);
        if (peek(ps)->type == T_LABEL) {
            int lab = take(ps).val;
            ps->stbeginline = 0;
            tp = newtuple(ps, S_REINSTATE);
            tp->target = lab;
            return tp;
        } else {
            int *g;
            int n = p_gerunds(ps, &g);
            if (n < 0)
                return NULL;
            tp = newtuple(ps, S_ENABLE);
            tp->ger = g;
            tp->nger = n;
            return tp;
        }
    case T_WRITE_IN:
    case T_READ_OUT: {
        int out = take(ps).type == T_READ_OUT;
        if (ps->smode) {
            struct node *first;
            if (out) {
                first = p_expr(ps);
            } else {
                first = p_ioitem(ps, 0);
            }
            if (first == FAIL)
                return NULL;
            if (peek(ps)->type == T_WORD && peek(ps)->val == W_WITH)
                return out ? p_s_readout(ps, first) : p_s_writein(ps, first);
            /* classic item list: the first item must be an outitem */
            if (out && !(first->kind == E_CONST || first->kind == E_VAR ||
                         (first->kind == E_ELEM)))
                return NULL;
            tp = newtuple(ps, out ? S_READ_OUT : S_WRITE_IN);
            additem(tp, first);
            return p_iolist_rest(ps, tp, out) ? tp : NULL;
        }
        struct node *it = p_ioitem(ps, out);
        if (it == FAIL)
            return NULL;
        tp = newtuple(ps, out ? S_READ_OUT : S_WRITE_IN);
        additem(tp, it);
        return p_iolist_rest(ps, tp, out) ? tp : NULL;
    }
    case T_PIN:
        take(ps);
        if (peek(ps)->type != T_TWOSPOT)
            return NULL;
        take(ps);
        if (peek(ps)->type != T_NUMBER)
            return NULL;
        ps_intern(ps, V_TWO, take(ps).val);
        return bad(ps, 111);
    case T_GIVE_UP:
        take(ps);
        return newtuple(ps, S_GIVE_UP);
    case T_GO_AHEAD:
    case T_GO_BACK:
        take(ps);
        return bad(ps, 405);
    case T_TRY_AGAIN:
        take(ps);
        return bad(ps, 111);
    case T_COME_FROM: {
        int lab = take(ps).val;
        if (ps->smode) {
            if (peek(ps)->type == T_WORD && peek(ps)->val == W_WITH) {
                take(ps);
                if (!p_word(ps, W_ROUTE))
                    return NULL;
                char *pat = p_string_operand(ps, NULL);
                if (!pat)
                    return NULL;
                tp = newtuple(ps, S_ROUTE);
                tp->target = lab;
                tp->str = pat;
                return tp;
            }
            if (lab == 16000) {
                tp = newtuple(ps, S_ROUTE);
                tp->target = lab;
                tp->str = xstrdup("/");
                return tp;
            }
        }
        tp = newtuple(ps, S_COME_FROM);
        tp->target = lab;
        return tp;
    }
    case T_COMPUCOME:
    case T_NEXTFROMEXPR:
        take(ps);
        if (peek(ps)->type == T_GERUND) {
            int *g;
            if (p_gerunds(ps, &g) < 0)
                return NULL;
        } else if (p_expr(ps) == FAIL) {
            return NULL;
        }
        return bad(ps, 111);
    case T_NEXTFROMLABEL:
        take(ps);
        return bad(ps, 111);
    case T_CREATE:
        take(ps);
        if (peek(ps)->type == T_BADCHAR) {
            take(ps);
        } else {
            enum tok f = peek(ps)->type;
            if (f == T_UNKNOWNID) {
                if (!p_unknown_rest(ps, 0))
                    return NULL;
            } else if (is_atom_start(f)) {
                if (p_atom(ps) == FAIL || !p_unknown_rest(ps, 1))
                    return NULL;
            } else {
                return NULL;
            }
        }
        return bad(ps, 111);
    case T_COMPUCREATE:
        take(ps);
        if (p_expr(ps) == FAIL)
            return NULL;
        if (peek(ps)->type == T_BADCHAR)
            take(ps);
        else if (peek(ps)->type == T_UNKNOWNID) {
            if (!p_unknown_rest(ps, 0))
                return NULL;
        } else
            return NULL;
        return bad(ps, 111);
    case T_UNKNOWNID:
        take(ps);
        if (!p_unknown_rest(ps, 0))
            return NULL;
        return newtuple(ps, S_UNKNOWN);
    case T_ONESPOT:
    case T_TWOSPOT: {
        int vt = take(ps).type == T_ONESPOT ? V_ONE : V_TWO;
        struct token *u = peek(ps);
        if (u->type == T_NUMBER) {
            struct node *s = mkvar(vt, ps_intern(ps, vt, take(ps).val));
            if (peek(ps)->type == T_GETS) {
                take(ps);
                e = p_expr(ps);
                if (e == FAIL)
                    return NULL;
                tp = newtuple(ps, S_GETS);
                tp->lhs = s;
                tp->rhs = e;
                return tp;
            }
            struct node *n = p_valsub(ps, s);
            if (n == FAIL)
                return NULL;
            n = p_binrest(ps, n, 1);
            if (n == FAIL)
                return NULL;
            if (n == s) { /* unknown statement starting with a scalar atom */
                if (!p_unknown_rest(ps, 1))
                    return NULL;
                return newtuple(ps, S_UNKNOWN);
            }
            goto notanlvalue;
        }
        if (u->type == T_UNARY) {
            int op = take(ps).val;
            if (peek(ps)->type != T_NUMBER)
                return NULL;
            e = mkunary(op, mkvar(vt, ps_intern(ps, vt, take(ps).val)));
            e = p_binrest(ps, e, 0);
            if (e == FAIL)
                return NULL;
            goto notanlvalue;
        }
        return NULL;
    }
    case T_TAIL:
    case T_HYBRID: {
        int vt = take(ps).type == T_TAIL ? V_TAIL : V_HYB;
        struct token *u = peek(ps);
        if (u->type == T_NUMBER) {
            struct node *arr = mkvar(vt, ps_intern(ps, vt, take(ps).val));
            if (peek(ps)->type == T_GETS) {
                take(ps);
                tp = newtuple(ps, S_RESIZE);
                tp->lhs = arr;
                for (;;) {
                    e = p_expr(ps);
                    if (e == FAIL)
                        return NULL;
                    additem(tp, e);
                    if (peek(ps)->type != T_BY)
                        break;
                    take(ps);
                }
                return tp;
            }
            if (peek(ps)->type == T_SUB) {
                take(ps);
                struct node **subs;
                int n = p_sublist(ps, 0, &subs);
                if (n < 0)
                    return NULL;
                struct node *el = mkelem(arr, subs, n);
                if (peek(ps)->type == T_GETS) {
                    take(ps);
                    e = p_expr(ps);
                    if (e == FAIL)
                        return NULL;
                    tp = newtuple(ps, S_GETS);
                    tp->lhs = el;
                    tp->rhs = e;
                    return tp;
                }
            }
            if (!p_unknown_rest(ps, 1))
                return NULL;
            return newtuple(ps, S_UNKNOWN);
        }
        if (u->type == T_UNARY) {
            e = p_arrayref_after_type(ps, vt, 1);
            if (e == FAIL)
                return NULL;
            goto notanlvalue;
        }
        return NULL;
    }
    case T_MESH:
    case T_UNARY:
    case T_OPENSPARK:
    case T_CLOSESPARK:
    case T_OPENEARS:
    case T_CLOSEEARS:
        e = p_expr(ps);
        if (e == FAIL)
            return NULL;
        if (ps->smode && e->kind == E_DB && peek(ps)->type == T_GETS) {
            take(ps);
            struct node *r = p_expr(ps);
            if (r == FAIL)
                return NULL;
            tp = newtuple(ps, S_DB_UPDATE);
            tp->lhs = e;
            tp->rhs = r;
            return tp;
        }
        goto notanlvalue;
    default:
        return NULL;
    }
notanlvalue:
    if (peek(ps)->type == T_GETS) {
        /* notanlvalue GETS expr: C-INTERCAL splatters it after the parse */
        take(ps);
        if (p_expr(ps) == FAIL)
            return NULL;
        return NULL;
    }
    if (!p_unknown_rest(ps, 1))
        return NULL;
    return newtuple(ps, S_UNKNOWN);
}

static struct tuple *p_s_readout(struct ps *ps, struct node *e)
{
    struct tuple *tp;
    take(ps); /* WITH */
    struct token *t = peek(ps);
    if (t->type != T_WORD)
        return NULL;
    int w = take(ps).val;
    if (w == W_TEMPLATE || w == W_REDIRECT) {
        char *name = p_string_operand(ps, NULL);
        if (!name)
            return NULL;
        tp = newtuple(ps, w == W_TEMPLATE ? S_TEMPLATE : S_REDIRECT);
        tp->rhs = e;
        tp->str = name;
        tp->status = 0;
        if (w == W_TEMPLATE && peek(ps)->type == T_WORD && peek(ps)->val == W_WITH) {
            take(ps);
            if (!p_word(ps, W_STATUS))
                return NULL;
            struct node *s = p_unambig(ps);
            int v;
            if (s == FAIL || !const_value(s, &v))
                return NULL;
            tp->status = v;
        }
        return tp;
    }
    if (w == W_SESSION) {
        tp = newtuple(ps, S_SESSION_OUT);
        tp->rhs = e;
        return tp;
    }
    return NULL;
}

static struct tuple *p_s_writein(struct ps *ps, struct node *e)
{
    struct tuple *tp;
    take(ps); /* WITH */
    struct token *t = peek(ps);
    if (t->type != T_WORD)
        return NULL;
    int w = take(ps).val;
    if (w == W_FIELD) {
        char *name = p_string_operand(ps, NULL);
        if (!name)
            return NULL;
        tp = newtuple(ps, S_FIELD);
        tp->lhs = e;
        tp->str = name;
        return tp;
    }
    if (w == W_SESSION || w == W_TIME) {
        if (e->kind == E_VAR && (e->vtype == V_TAIL || e->vtype == V_HYB))
            cerr(409, ps->thisline, NULL);
        tp = newtuple(ps, w == W_SESSION ? S_SESSION_IN : S_TIME);
        tp->lhs = e;
        return tp;
    }
    return NULL;
}

static void getlineno(struct ps *ps)
{
    if (ps->stbeginline < 0)
        ps->thisline = -ps->stbeginline;
    else {
        ps->thisline = ps->stbeginline;
        ps->stbeginline = 0;
    }
}

static void checklabel(struct ps *ps, int label)
{
    if (label < 1 || label > 65535)
        cerr(197, iyy(ps), NULL);
}

static void program_error(struct ps *ps)
{
    if (ps->prevtuple >= 0)
        ps->p->t[ps->prevtuple].type = S_SPLAT;
    else {
        struct tuple *t = newtuple(ps, S_SPLAT);
        t->chance = 0;
        ps->prevtuple = (int)(t - ps->p->t);
    }
    skip_to_start(ps);
}

/* command: [LABEL] please [%n] perform [ONCE|AGAIN|WHILE perform] */
static void p_command(struct ps *ps, int haslabel, int label)
{
    struct token d = take(ps); /* DO or MAYBE */
    int sign = 1, maybe = d.type == T_MAYBE;
    peek(ps);
    if (peek(ps)->type == T_NOT) {
        take(ps);
        sign = -1;
    }
    getlineno(ps);
    int chance = 100;
    if (peek(ps)->type == T_OHOHSEVEN) {
        chance = take(ps).val;
    }
    ps->badfeature = 0;
    struct tuple *tp = p_perform(ps);
    int idx;
    if (!tp) {
        tp = newtuple(ps, S_SPLAT);
        idx = (int)(tp - ps->p->t);
        skip_to_start(ps);
    } else {
        idx = (int)(tp - ps->p->t);
        if (ps->badfeature && tp->type != S_SPLAT && tp->type != S_UNKNOWN) {
            tp->type = S_BADFEATURE;
            tp->badcode = ps->badfeature;
        }
        enum tok f = peek(ps)->type;
        if (f == T_ONCE || f == T_AGAIN) {
            tp = &ps->p->t[idx];
            tp->onceagain = take(ps).type == T_ONCE ? 1 : 2;
        } else if (f == T_WHILE) {
            take(ps);
            p_perform(ps);
            cerr(405, iyy(ps), NULL);
        }
    }
    tp = &ps->p->t[idx];
    peek(ps);
    if (haslabel)
        checklabel(ps, label);
    tp->label = label;
    tp->chance = (maybe ? 101 : 1) * sign * chance;
    if (maybe)
        tp->maybe = 1;
    ps->prevtuple = idx;
    enum tok f = peek(ps)->type;
    if (!(f == T_DO || f == T_MAYBE || f == T_LABEL || f == T_EOF))
        program_error(ps);
}

static void parse_stream(struct ps *ps)
{
    for (;;) {
        struct token *t = peek(ps);
        if (t->type == T_EOF)
            return;
        if (t->type == T_DO || t->type == T_MAYBE) {
            p_command(ps, 0, 0);
        } else if (t->type == T_LABEL) {
            int lab = take(ps).val;
            enum tok f = peek(ps)->type;
            if (f == T_DO || f == T_MAYBE)
                p_command(ps, 1, lab);
            else
                program_error(ps);
        } else {
            program_error(ps);
        }
    }
}

/* state shared between the main program and the appended syslib */
static struct ps g_ps;

void parse_classic(struct program *p, struct lexer *lx)
{
    struct ps *ps = &g_ps;
    if (!ps->p) {
        ps->p = p;
        ps->prevtuple = -1;
    } else {
        ps->insyslib = 1;
    }
    ps->lx = lx;
    ps->have_la = 0;
    ps->smode = 0;
    lx->stbeginline = &ps->stbeginline;
    lx->politesse = &p->politesse;
    parse_stream(ps);
    p->iyylineno = lex_iyylineno(lx);
}

/* ---- Interstates: one statement per line ---------------------------------- */

void parse_interstates(struct program *p, const unsigned char *src, size_t len)
{
    struct ps *ps = &g_ps;
    ps->p = p;
    ps->prevtuple = -1;
    ps->smode = 1;
    int line = 0;
    size_t i = 0;
    int dummy_polite = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && src[j] != '\n')
            j++;
        line++;
        size_t k = i;
        while (k < j && (src[k] == ' ' || src[k] == '\t' || src[k] == '\r'))
            k++;
        if (k < j) {
            /* lex this line on its own */
            size_t l = j - i;
            unsigned char *buf = xmalloc(l + 2);
            memcpy(buf, src + i, l);
            if (l && buf[l - 1] == '\r')
                l--;
            buf[l] = '\n';
            struct lexer *lx = xmalloc(sizeof *lx);
            lex_init(lx, buf, l + 1, line - 1);
            lx->smode = 1;
            lx->stbeginline = &ps->stbeginline;
            dummy_polite = 0;
            lx->politesse = &dummy_polite;
            ps->lx = lx;
            ps->have_la = 0;
            ps->stbeginline = 0;
            ps->thisline = line;
            int label = 0, haslabel = 0;
            if (peek(ps)->type == T_LABEL) {
                label = take(ps).val;
                haslabel = 1;
            }
            int sign = 1, maybe = 0;
            enum tok f = peek(ps)->type;
            if (f == T_DO || f == T_MAYBE) {
                maybe = f == T_MAYBE;
                take(ps);
            }
            if (dummy_polite)
                p->politesse++;
            if (peek(ps)->type == T_NOT) {
                take(ps);
                sign = -1;
            }
            int chance = 100;
            if (peek(ps)->type == T_OHOHSEVEN)
                chance = take(ps).val;
            ps->thisline = line;
            ps->badfeature = 0;
            struct tuple *tp = p_perform(ps);
            if (tp && (peek(ps)->type == T_ONCE || peek(ps)->type == T_AGAIN))
                tp->onceagain = take(ps).type == T_ONCE ? 1 : 2;
            if (tp && peek(ps)->type != T_EOF)
                tp->type = S_SPLAT;
            if (!tp)
                tp = newtuple(ps, S_SPLAT);
            if (ps->badfeature && tp->type != S_SPLAT && tp->type != S_UNKNOWN) {
                tp->type = S_BADFEATURE;
                tp->badcode = ps->badfeature;
            }
            tp->line = line;
            if (haslabel)
                checklabel(ps, label);
            tp->label = label;
            tp->chance = (maybe ? 101 : 1) * sign * chance;
            tp->maybe = maybe;
        }
        i = j < len ? j + 1 : j;
    }
    p->nlines = line;
    p->iyylineno = line + 1;
}

/* ---- template expressions ------------------------------------------------- */

/* Parse the inside of {{ }}: EXPR [FILTER] or COND (EXPR [IS EXPR]). */
struct node *parse_template_expr(struct program *p, const char *s, size_t len, int line,
                                 int *filter, struct node **rhs, int *isis)
{
    struct ps tps;
    memset(&tps, 0, sizeof tps);
    tps.p = p;
    tps.smode = 1;
    tps.prevtuple = -1;
    tps.thisline = line;
    unsigned char *buf = xmalloc(len + 2);
    memcpy(buf, s, len);
    buf[len] = '\n';
    struct lexer *lx = xmalloc(sizeof *lx);
    lex_init(lx, buf, len + 1, line - 1);
    lx->smode = 1;
    int sb = 0, pol = 0;
    lx->stbeginline = &sb;
    lx->politesse = &pol;
    tps.lx = lx;
    struct node *e = p_expr(&tps);
    if (e == FAIL)
        return NULL;
    if (filter)
        *filter = 0;
    if (rhs)
        *rhs = NULL;
    if (isis)
        *isis = 0;
    struct token *t = peek(&tps);
    if (isis && t->type == T_WORD && t->val == W_IS) {
        take(&tps);
        struct node *r = p_expr(&tps);
        if (r == FAIL)
            return NULL;
        *rhs = r;
        *isis = 1;
    } else if (filter && t->type == T_WORD) {
        int w = take(&tps).val;
        if (w == W_IN && p_word(&tps, W_ARABIC))
            *filter = 1;
        else if (w == W_IN && p_word(&tps, W_ROMAN))
            *filter = 2;
        else if (w == W_AS && peek(&tps)->type == T_WORD && peek(&tps)->val == W_IS) {
            take(&tps);
            *filter = 3;
        } else if (w == W_AS && p_word(&tps, W_PROSE))
            *filter = 4;
        else if (w == W_AGO)
            *filter = 5;
        else if (w == W_AS && p_word(&tps, W_FACE))
            *filter = 6;
        else
            return NULL;
    }
    if (peek(&tps)->type != T_EOF)
        return NULL;
    return e;
}
