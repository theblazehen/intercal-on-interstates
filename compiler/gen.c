/* gen.c — compile-time checks (C-INTERCAL's codecheck) and C generation.
 *
 * The generated program is one dispatch loop: `for (;;) switch (pc)`.
 * Statements fall through to each other; NEXT pushes a return point and
 * jumps by setting pc; RESUME pops one; a COME FROM suck point after its
 * target statement jumps to the COME FROM's landing case. Everything
 * non-trivial is a call into libioi (include/ioi_core.h).
 */
#include "ick.h"

#include <stdlib.h>
#include <string.h>

static struct program *P;
static FILE *O;
void gen_set_out(FILE *out) { O = out; }
static int cur_tuple;       /* tuple being emitted (E000 text for unknown ops) */
static int cur_emitline;

static int gclass_of(const struct tuple *t)
{
    switch (t->type) {
    case S_SPLAT:
    case S_UNKNOWN: return G_COMMENT;
    case S_GETS:
    case S_RESIZE:
    case S_DB_UPDATE: return G_CALC;
    case S_NEXT: return G_NEXT;
    case S_FORGET:
    case S_DB_DELETE: return G_FORGET;
    case S_RESUME: return G_RESUME;
    case S_STASH:
    case S_STASH_ASSIGN:
    case S_DB_INSERT: return G_STASH;
    case S_RETRIEVE: return G_RETRIEVE;
    case S_IGNORE: return G_IGNORE;
    case S_REMEMBER: return G_REMEMBER;
    case S_ABSTAIN:
    case S_DISABLE: return G_ABSTAIN;
    case S_REINSTATE:
    case S_ENABLE: return G_REINSTATE;
    case S_READ_OUT:
    case S_TEMPLATE:
    case S_REDIRECT:
    case S_SESSION_OUT: return G_READ;
    case S_WRITE_IN:
    case S_FIELD:
    case S_SESSION_IN:
    case S_TIME: return G_WRITE;
    case S_COME_FROM:
    case S_ROUTE: return G_COMEFROM;
    case S_GIVE_UP: return G_GIVEUP;
    default: return G_NONE;
    }
}

static int find_label(int label)
{
    for (int i = 0; i < P->nt; i++)
        if (P->t[i].label == label)
            return i;
    return -1;
}

static int emitline(int i)
{
    struct tuple *t = &P->t[i];
    if (t->type == S_NEXT && t->tidx >= 0)
        return P->t[t->tidx].line;
    if (t->ncomefrom)
        return P->t[t->cf].line;
    if (i < P->nt - 1)
        return P->t[i + 1].line;
    return P->iyylineno;
}

/* codecheck(): duplicate labels, targets, COME FROM suck points. */
void check_program(struct program *p)
{
    P = p;
    for (int i = 0; i < p->nt; i++) {
        p->t[i].tidx = -1;
        p->t[i].cf = -1;
        p->t[i].ncomefrom = 0;
    }
    for (int i = 0; i < p->nt; i++)
        if (p->t[i].label)
            for (int j = 0; j < p->nt; j++)
                if (i != j && p->t[i].label == p->t[j].label)
                    cerr_at(182, i + 1, p->t[i].line, NULL);
    for (int i = 0; i < p->nt; i++) {
        struct tuple *t = &p->t[i];
        enum stype ty = t->type;
        if (ty == S_ROUTE) {
            /* origin rule: 16000 or a READ OUT ... WITH TEMPLATE line */
            if (t->target != 16000) {
                int j = find_label(t->target);
                if (j < 0 || p->t[j].type != S_TEMPLATE)
                    cerr_at(400, t->line, t->line, NULL);
            }
            t->origin = t->target;
            continue;
        }
        if (ty != S_NEXT && ty != S_ABSTAIN && ty != S_REINSTATE && ty != S_COME_FROM)
            continue;
        if ((unsigned)t->target > 65535u)
            cerr_at(197, i + 1, t->line, NULL);
        int j = find_label(t->target);
        if (j < 0) {
            if (ty == S_NEXT)
                cerr_at(129, i + 1, t->line, NULL);
            else if (ty == S_COME_FROM)
                cerr_at(444, i + 1, t->line, NULL);
            else
                cerr_at(139, i + 1, t->line, NULL);
        }
        t->tidx = j;
        if (ty == S_COME_FROM) {
            if (p->t[j].ncomefrom)
                cerr_at(555, p->iyylineno, t->line, NULL);
            p->t[j].ncomefrom++;
            p->t[j].cf = i;
        }
    }
    /* emit-time errors of C-INTERCAL, in statement order */
    for (int i = 0; i < p->nt; i++) {
        struct tuple *t = &p->t[i];
        if (t->maybe && t->type != S_COME_FROM)
            cerr_at(405, emitline(i), t->line, NULL);
        if (t->type == S_BADFEATURE)
            cerr_at(t->badcode, emitline(i), t->line, NULL);
    }
}

/* ---- E000 text (C-INTERCAL's nice_text) --------------------------------- */

static char *nice_text(int line, int nlines)
{
    enum { MAXNICEBUF = 512 };
    char buf[MAXNICEBUF + 16];
    char *cp = buf;
    if (nlines < 1)
        nlines = 1;
    for (int i = 0; i < nlines; i++) {
        if (cp > buf + MAXNICEBUF - 10) {
            memcpy(cp, "...", 3);
            cp += 3;
            goto done;
        }
        if (i) {
            memcpy(cp, "\\n\\\n\t", 5);
            cp += 5;
        }
        int l = line + i;
        const char *text = (l >= 0 && l <= P->nlines) ? P->lines[l] : "";
        for (; *text; text++) {
            if (cp > buf + MAXNICEBUF - 10) {
                memcpy(cp, "...", 3);
                cp += 3;
                goto done;
            }
            if (*text == '"' || *text == '\\')
                *cp++ = '\\';
            if (*text == 'K') {
                *cp++ = '"';
                *cp++ = '"';
            }
            *cp++ = *text;
        }
    }
done:
    *cp = '\0';
    /* decode the C string literal C-INTERCAL would have written */
    char *out = xmalloc((size_t)(cp - buf) + 1), *o = out;
    for (char *s = buf; *s;) {
        if (s[0] == '\\' && s[1] == 'n' && s[2] == '\\' && s[3] == '\n') {
            *o++ = '\n';
            s += 4;
        } else if (s[0] == '\\' && (s[1] == '"' || s[1] == '\\')) {
            *o++ = s[1];
            s += 2;
        } else if (s[0] == '"' && s[1] == '"') {
            s += 2;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
    return out;
}

static char *e000_text(int i)
{
    struct tuple *t = &P->t[i];
    int dim = (i < P->nt - 1 ? P->t[i + 1].line : P->iyylineno) - t->line;
    return nice_text(t->line, dim);
}

/* ---- expressions ---------------------------------------------------------- */

static const char *vname(int vt)
{
    switch (vt) {
    case V_ONE: return "V1";
    case V_TWO: return "V2";
    case V_TAIL: return "VT";
    default: return "VH";
    }
}
static const char *iname(int vt)
{
    switch (vt) {
    case V_ONE: return "I1";
    case V_TWO: return "I2";
    case V_TAIL: return "IT";
    default: return "IH";
    }
}


static void gen_subs(struct node *n)
{
    fprintf(O, "%d, (const uint32_t[]){", n->nsubs);
    for (int i = 0; i < n->nsubs; i++) {
        if (i)
            fputs(", ", O);
        gen_num(n->subs[i]);
    }
    fputs("})", O);
}

static void gen_aref(struct node *n)
{
    fprintf(O, "ioi_aref%s(&%s[%d], ", n->vtype == V_TAIL ? "16" : "32", vname(n->vtype), n->var);
    gen_subs(n);
}

static int dbcount;
static void gen_db_select(struct node *n);

void gen_num(struct node *n)
{
    switch (n->kind) {
    case E_CONST:
        fprintf(O, "%uu", n->num);
        break;
    case E_VAR:
        if (n->vtype == V_ONE || n->vtype == V_TWO)
            fprintf(O, "%s[%d]", vname(n->vtype), n->var);
        else {
            fputs("ioi_num(", O);
            gen_val(n);
            fputs(")", O);
        }
        break;
    case E_ELEM:
        if (n->vtype == V_HYB && P->smode) {
            fputs("ioi_num(*", O);
            gen_aref(n);
            fputs(")", O);
        } else {
            fputs("*", O);
            gen_aref(n);
        }
        break;
    case E_MINGLE:
    case E_SELECT:
        fprintf(O, "ioi_%s(", n->kind == E_MINGLE ? "mingle" : "select");
        gen_num(n->l);
        fputs(", ", O);
        gen_num(n->r);
        fputs(")", O);
        break;
    case E_UNARY: {
        const char *op = n->op == U_AND ? "and" : n->op == U_OR ? "or" : n->op == U_XOR ? "xor" : NULL;
        if (!op)
            cerr_at(997, cur_emitline, P->t[cur_tuple].line, NULL);
        fprintf(O, "ioi_%s%d(", op, n->width);
        gen_num(n->r);
        fputs(")", O);
        break;
    }
    case E_UNKOP: {
        char *s = e000_text(cur_tuple);
        fputs("(ioi_lose(0, ioi_lineno, ", O);
        emit_cstring(O, s, strlen(s));
        fputs("), 0u)", O);
        free(s);
        break;
    }
    default:
        fputs("ioi_num(", O);
        gen_val(n);
        fputs(")", O);
        break;
    }
}

void gen_val(struct node *n)
{
    switch (n->kind) {
    case E_STRING:
        fputs("ioi_cas_bytes(", O);
        emit_cstring(O, n->str, n->slen);
        fprintf(O, ", %zu)", n->slen);
        break;
    case E_VAR:
        if (n->vtype == V_TAIL)
            fprintf(O, "ioi_tail_value(&VT[%d])", n->var);
        else if (n->vtype == V_HYB)
            fprintf(O, "ioi_hyb_value(&VH[%d])", n->var);
        else {
            fputs("ioi_box(", O);
            gen_num(n);
            fputs(")", O);
        }
        break;
    case E_ELEM:
        if (n->vtype == V_HYB) {
            fputs("*", O);
            gen_aref(n);
        } else {
            gen_num(n);
        }
        break;
    case E_VALSUB:
        fputs("ioi_val_sub(", O);
        gen_val(n->l);
        fputs(", ", O);
        gen_num(n->r);
        fputs(")", O);
        break;
    case E_DB:
        gen_db_select(n);
        break;
    default:
        fputs("ioi_box(", O);
        gen_num(n);
        fputs(")", O);
        break;
    }
}

/* ---- ActiveCassette helpers ----------------------------------------------- */

/* Each query becomes a static function returning its value or performing
 * its write; they are emitted before main(). */
struct dbfn {
    int id;
    int kind; /* 0 select, 1 update, 2 delete */
    struct node *n;
    struct node *rhs;
};
static struct dbfn *dbfns;
static int ndbfns;

static int add_dbfn(int kind, struct node *n, struct node *rhs)
{
    dbfns = xrealloc(dbfns, (size_t)(ndbfns + 1) * sizeof *dbfns);
    dbfns[ndbfns].id = dbcount++;
    dbfns[ndbfns].kind = kind;
    dbfns[ndbfns].n = n;
    dbfns[ndbfns].rhs = rhs;
    return dbfns[ndbfns++].id;
}

static void gen_db_select(struct node *n)
{
    fprintf(O, "DB%d()", add_dbfn(0, n, NULL));
}

static void emit_dbfn(struct dbfn *f)
{
    struct dbq *q = f->n->db;
    fprintf(O, "static uint32_t DB%d(%s)\n{\n", f->id, f->kind == 1 ? "uint32_t value" : "void");
    if (q->ncols) {
        fprintf(O, "    static const unsigned cols[] = {");
        for (int i = 0; i < q->ncols; i++)
            fprintf(O, "%s%du", i ? ", " : "", q->cols[i]);
        fputs("};\n", O);
    }
    int nconds = 0, norder = 0;
    for (int i = 0; i < q->ncl; i++)
        nconds += q->cl[i].kind == W_HAVING, norder += q->cl[i].kind == W_BY;
    if (nconds)
        fprintf(O, "    struct ioi_db_cond conds[%d];\n", nconds);
    if (norder) {
        fprintf(O, "    static const struct ioi_db_order order[] = {");
        for (int i = 0; i < q->ncl; i++)
            if (q->cl[i].kind == W_BY)
                fprintf(O, "{%du, %d}, ", q->cl[i].col, q->cl[i].backwards);
        fputs("};\n", O);
    }
    fputs("    struct ioi_db_query q;\n    uint32_t out = 0;\n", O);
    fputs("    memset(&q, 0, sizeof q);\n", O);
    fprintf(O, "    q.table = %du;\n", q->table);
    if (q->ncols)
        fprintf(O, "    q.cols = cols;\n    q.ncols = %d;\n", q->ncols);
    int ci = 0;
    for (int i = 0; i < q->ncl; i++) {
        struct dbcl *c = &q->cl[i];
        if (c->kind == W_HAVING) {
            fprintf(O, "    conds[%d].col = %du;\n    conds[%d].op = %s;\n    conds[%d].value = ",
                    ci, c->col, ci, c->op == 0 ? "IOI_DB_EQ" : c->op == 1 ? "IOI_DB_SOMEWHERE" : "IOI_DB_BEYOND", ci);
            gen_val(c->e);
            fputs(";\n", O);
            ci++;
        } else if (c->kind == W_SKIP) {
            fputs("    q.skip = ", O);
            gen_num(c->e);
            fputs(";\n", O);
        } else if (c->kind == W_TAKE) {
            fputs("    q.take = ", O);
            gen_num(c->e);
            fputs(";\n    q.has_take = 1;\n", O);
        }
    }
    if (nconds)
        fprintf(O, "    q.conds = conds;\n    q.nconds = %d;\n", nconds);
    if (norder)
        fprintf(O, "    q.order = order;\n    q.norder = %d;\n", norder);
    static const char *shapes[] = {"IOI_DB_ROW", "IOI_DB_BULK", "IOI_DB_HOW_MANY", "IOI_DB_PK"};
    fprintf(O, "    q.shape = %s;\n", shapes[q->shape]);
    if (q->shape == 3) {
        if (q->has_sel) {
            fputs("    q.pk = ", O);
            gen_num(q->sel);
            fputs(";\n", O);
        }
    }
    if (f->kind == 0)
        fputs("    ioi_s_db_check(ioi_db_select(&q, &out));\n", O);
    else if (f->kind == 1)
        fputs("    ioi_s_db_check(ioi_db_update(&q, value, &out));\n", O);
    else
        fputs("    ioi_s_db_check(ioi_db_delete(&q, &out));\n", O);
    fputs("    return out;\n}\n\n", O);
}

/* ---- statements ------------------------------------------------------------ */

static int N; /* number of tuples: landing cases are N + i */

/* ONCE/AGAIN: the abstention a statement leaves behind (C-INTERCAL feh2.c) */
static void gen_onceagain(const struct tuple *t, int i)
{
    fprintf(O, "    OLD = AB[%d]; AB[%d] = %d;\n", i, i, t->onceagain == 1);
}

/* NEXT and unknown statements flip ONCE/AGAIN before they run */
static int oa_before(const struct tuple *t)
{
    return t->onceagain && (t->type == S_NEXT || t->type == S_UNKNOWN);
}

static void gen_guard_open(const struct tuple *t, int i)
{
    if (oa_before(t))
        gen_onceagain(t, i);
    fputs("    if (", O);
    if (t->chance < 100)
        fprintf(O, "ioi_roll(%d) && ", t->chance);
    if (oa_before(t))
        fputs("!OLD) {\n", O);
    else
        fprintf(O, "!AB[%d]) {\n", i);
}

static void gen_assign_scalar(struct node *lhs, struct node *rhs, int grow)
{
    if (lhs->kind == E_VAR) {
        int vt = lhs->vtype;
        if (vt == V_ONE || vt == V_TWO) {
            fprintf(O, "\tioi_set%s(&%s[%d], %s[%d], ", vt == V_ONE ? "16" : "32", vname(vt), lhs->var,
                    iname(vt), lhs->var);
            gen_num(rhs);
            fputs(");\n", O);
        } else if (rhs->isval || !P->smode) {
            fprintf(O, "\tioi_%s_assign(&%s[%d], %s[%d], ", vt == V_TAIL ? "tail" : "hyb", vname(vt),
                    lhs->var, iname(vt), lhs->var);
            gen_val(rhs);
            fputs(");\n", O);
        } else {
            fprintf(O, "\tioi_resize(&%s[%d], %d, %s[%d], 1, (const uint32_t[]){", vname(vt), lhs->var,
                    vt == V_HYB, iname(vt), lhs->var);
            gen_num(rhs);
            fputs("});\n", O);
        }
        return;
    }
    /* element */
    int vt = lhs->vtype;
    const char *w = vt == V_TAIL ? "16" : "32";
    if (vt == V_TAIL && P->smode && rhs->isval)
        fputs("\tioi_tail_elem_assign(", O);
    else
        fprintf(O, "\tioi_set%s(", w);
    if (grow && lhs->nsubs == 1) {
        fprintf(O, "ioi_aref%s_grow(&%s[%d], ", w, vname(vt), lhs->var);
        gen_num(lhs->subs[0]);
        fputs(")", O);
    } else {
        gen_aref(lhs);
    }
    fprintf(O, ", %s[%d], ", iname(vt), lhs->var);
    if (vt == V_HYB && P->smode)
        gen_val(rhs);
    else if (vt == V_TAIL && P->smode && rhs->isval)
        gen_val(rhs);
    else
        gen_num(rhs);
    fputs(");\n", O);
}

/* store a runtime C expression (number) into an lvalue (WRITE IN etc.) */
static void gen_store_num(struct node *lhs, const char *cexpr)
{
    int vt = lhs->vtype;
    if (lhs->kind == E_VAR) {
        fprintf(O, "\tioi_set%s(&%s[%d], %s[%d], %s);\n", vt == V_ONE ? "16" : "32", vname(vt), lhs->var,
                iname(vt), lhs->var, cexpr);
        return;
    }
    fprintf(O, "\tioi_set%s(", vt == V_TAIL ? "16" : "32");
    gen_aref(lhs);
    if (vt == V_HYB && P->smode)
        fprintf(O, ", %s[%d], ioi_box(%s));\n", iname(vt), lhs->var, cexpr);
    else
        fprintf(O, ", %s[%d], %s);\n", iname(vt), lhs->var, cexpr);
}

static void gen_gerunds(const struct tuple *t, int set)
{
    for (int g = 0; g < t->nger; g++) {
        int cls = t->ger[g];
        int any = 0;
        for (int j = 0; j < P->nt; j++)
            if (gclass_of(&P->t[j]) == cls) {
                if (!any)
                    fputs("\t{ static const int g[] = {", O);
                fprintf(O, "%s%d", any ? "," : "", j);
                any++;
            }
        if (any)
            fprintf(O, "}; ioi_gerund(AB, g, %d, %d); }\n", any, set);
    }
}

static void gen_template_call(const struct tuple *t)
{
    int k49221 = -1;
    for (int i = 0; i < P->nvars[V_HYB]; i++)
        if (P->vars[V_HYB][i].num == 49221)
            k49221 = i;
    fputs("\tioi_tpl_extract(&TV, ", O);
    if (k49221 >= 0)
        fprintf(O, "&VH[%d], ", k49221);
    else
        fputs("NULL, ", O);
    gen_num(t->rhs);
    fputs(");\n", O);
}

static int find_var(int vt, uint32_t num)
{
    for (int i = 0; i < P->nvars[vt]; i++)
        if (P->vars[vt][i].num == num)
            return i;
    return -1;
}

static void gen_stmt(int i, int bugline)
{
    struct tuple *t = &P->t[i];
    int el = emitline(i);
    cur_tuple = i;
    cur_emitline = el;
    if (t->label || i == 0)
        fprintf(O, "case %d:\n", i);
    fprintf(O, "    ioi_lineno = %d;\n", el);
    if (t->type == S_COME_FROM || t->type == S_ROUTE) {
        fprintf(O, "case %d:;\n", N + i);
    } else {
        gen_guard_open(t, i);
        switch (t->type) {
        case S_SPLAT:
        case S_UNKNOWN:
        case S_BADFEATURE: {
            char *s = e000_text(i);
            fprintf(O, "\tioi_lose(0, %d, ", el);
            emit_cstring(O, s, strlen(s));
            fputs(");\n", O);
            free(s);
            break;
        }
        case S_GETS:
            gen_assign_scalar(t->lhs, t->rhs, 0);
            break;
        case S_STASH_ASSIGN:
            gen_assign_scalar(t->lhs, t->rhs, 1);
            break;
        case S_RESIZE:
            if (P->smode && t->nitems == 1 && t->items[0]->isval) {
                gen_assign_scalar(t->lhs, t->items[0], 0);
                break;
            }
            fprintf(O, "\tioi_resize(&%s[%d], %d, %s[%d], %d, (const uint32_t[]){", vname(t->lhs->vtype),
                    t->lhs->var, t->lhs->vtype == V_HYB, iname(t->lhs->vtype), t->lhs->var, t->nitems);
            for (int k = 0; k < t->nitems; k++) {
                if (k)
                    fputs(", ", O);
                gen_num(t->items[k]);
            }
            fputs("});\n", O);
            break;
        case S_NEXT:
            fprintf(O, "\tioi_next_push(%d); pc = %d; continue;\n", N + i, t->tidx);
            break;
        case S_FORGET:
            fputs("\tioi_forget(", O);
            gen_num(t->rhs);
            fputs(");\n", O);
            break;
        case S_RESUME:
            fputs("\tpc = ioi_resume(", O);
            gen_num(t->rhs);
            fputs("); continue;\n", O);
            break;
        case S_STASH:
        case S_RETRIEVE:
        case S_IGNORE:
        case S_REMEMBER:
            for (int k = 0; k < t->nitems; k++) {
                struct node *v = t->items[k];
                if (t->type == S_STASH)
                    fprintf(O, "\tioi_stash(%d, %d, &%s[%d]);\n", v->vtype, v->var, vname(v->vtype), v->var);
                else if (t->type == S_RETRIEVE)
                    fprintf(O, "\tioi_retrieve(%d, %d, &%s[%d], %s[%d]);\n", v->vtype, v->var,
                            vname(v->vtype), v->var, iname(v->vtype), v->var);
                else
                    fprintf(O, "\t%s[%d] = %d;\n", iname(v->vtype), v->var, t->type == S_IGNORE);
            }
            break;
        case S_ABSTAIN:
            fprintf(O, "\tAB[%d] = 1;\n", t->tidx);
            break;
        case S_REINSTATE:
            if (P->t[t->tidx].type != S_GIVE_UP)
                fprintf(O, "\tAB[%d] = 0;\n", t->tidx);
            break;
        case S_DISABLE:
        case S_ENABLE:
            gen_gerunds(t, t->type == S_DISABLE);
            break;
        case S_GIVE_UP:
            fputs("\tioi_give_up();\n", O);
            break;
        case S_READ_OUT:
            for (int k = 0; k < t->nitems; k++) {
                struct node *v = t->items[k];
                if (v->kind == E_VAR && (v->vtype == V_TAIL || v->vtype == V_HYB))
                    fprintf(O, "\tioi_read_out_array(&%s[%d], %d);\n", vname(v->vtype), v->var,
                            v->vtype == V_HYB);
                else {
                    fputs("\tioi_read_out(", O);
                    gen_num(v);
                    fputs(");\n", O);
                }
            }
            break;
        case S_WRITE_IN:
            for (int k = 0; k < t->nitems; k++) {
                struct node *v = t->items[k];
                if (v->kind == E_VAR && (v->vtype == V_TAIL || v->vtype == V_HYB))
                    fprintf(O, "\tioi_write_in_array(&%s[%d], %d, %s[%d]);\n", vname(v->vtype), v->var,
                            v->vtype == V_HYB, iname(v->vtype), v->var);
                else
                    gen_store_num(v, "ioi_write_in()");
            }
            break;
        case S_TEMPLATE:
            gen_template_call(t);
            fprintf(O, "\tioi_web_begin(%d, ioi_web_type_for(", t->status ? t->status : 200);
            emit_cstring(O, t->str, strlen(t->str));
            fprintf(O, "));\n\tTPL%d();\n", t->tpl);
            break;
        case S_REDIRECT:
            fputs("\t{ size_t m = ioi_web_mark();\n", O);
            gen_template_call(t);
            fprintf(O, "\tTPL%d();\n\tioi_web_redirect_since(m); }\n", t->tpl);
            break;
        case S_SESSION_OUT:
            fputs("\tioi_s_session_set(", O);
            gen_num(t->rhs);
            fputs(");\n", O);
            break;
        case S_SESSION_IN:
            gen_store_num(t->lhs, "ioi_web_session_get()");
            break;
        case S_TIME:
            gen_store_num(t->lhs, "ioi_web_time()");
            break;
        case S_FIELD: {
            struct node *v = t->lhs;
            fputs("\t{ const uint8_t *d; size_t n; ioi_web_field(", O);
            emit_cstring(O, t->str, strlen(t->str));
            fputs(", &d, &n);\n", O);
            int k = find_var(V_ONE, 4401);
            if (k >= 0)
                fprintf(O, "\t  V1[%d] = (uint16_t)(n > 65535 ? 65535 : n);\n", k);
            if (v->kind == E_VAR && v->vtype == V_TAIL)
                fprintf(O, "\t  ioi_tail_set_bytes(&VT[%d], IT[%d], d, n); }\n", v->var, v->var);
            else if (v->kind == E_VAR && v->vtype == V_HYB)
                fprintf(O, "\t  ioi_hyb_assign(&VH[%d], IH[%d], ioi_s_list1(ioi_cas_bytes(d, n))); }\n",
                        v->var, v->var);
            else if (v->kind == E_ELEM && v->vtype == V_HYB) {
                fputs("\t  ioi_set32(", O);
                gen_aref(v);
                fprintf(O, ", IH[%d], ioi_cas_bytes(d, n)); }\n", v->var);
            } else {
                fputs("\t  uint32_t x; ioi_web_parse_number(d, n, &x);\n", O);
                gen_store_num(v, "x");
                fputs("\t}\n", O);
            }
            break;
        }
        case S_DB_INSERT: {
            fprintf(O, "\tioi_s_db_insert(%d, &VH[%d], ", t->target, t->lhs->var);
            int k = find_var(V_TWO, 4400);
            if (k >= 0)
                fprintf(O, "&V2[%d]);\n", k);
            else
                fputs("NULL);\n", O);
            break;
        }
        case S_DB_UPDATE: {
            int id = add_dbfn(1, t->lhs, NULL);
            fprintf(O, "\tDB%d(", id);
            gen_val(t->rhs);
            fputs(");\n", O);
            break;
        }
        case S_DB_DELETE: {
            int id = add_dbfn(2, t->rhs, NULL);
            fprintf(O, "\tDB%d();\n", id);
            break;
        }
        default:
            cerr_at(778, el, t->line, NULL);
        }
        fputs("    }\n", O);
        if (t->type == S_NEXT)
            fprintf(O, "case %d:;\n", N + i);
        if (t->onceagain && !oa_before(t))
            gen_onceagain(t, i);
    }
    if (t->ncomefrom) {
        struct tuple *cf = &P->t[t->cf];
        if (cf->onceagain)
            gen_onceagain(cf, t->cf);
        fputs("    if (", O);
        if (cf->chance < 100)
            fprintf(O, "ioi_roll(%d) && ", cf->chance);
        if (cf->onceagain)
            fprintf(O, "!OLD) { pc = %d; continue; }\n", N + t->cf);
        else
            fprintf(O, "!AB[%d]) { pc = %d; continue; }\n", t->cf, N + t->cf);
    }
    if (i == bugline)
        fputs("    ioi_lose(774, ioi_lineno, 0);\n", O);
}

void generate(struct program *p, FILE *out, int bugline)
{
    P = p;
    O = out;
    N = p->nt;
    /* initial abstentions; chance normalisation (perpet.c $C) */
    for (int i = 0; i < p->nt; i++) {
        struct tuple *t = &p->t[i];
        if (t->chance >= 101 || t->chance <= -101)
            t->chance = t->chance > 0 ? t->chance / 101 : t->chance / 101;
    }
    fputs("/* Generated by ick (INTERCAL on Interstates). Do not edit. */\n", out);
    fputs("#include <string.h>\n#include \"ioi_core.h\"\n", out);
    if (p->smode)
        fputs("#include \"ioi_web.h\"\n", out);
    for (int v = 1; v <= 4; v++) {
        int n = p->nvars[v] ? p->nvars[v] : 1;
        const char *ty = v == V_ONE ? "uint16_t" : v == V_TWO ? "uint32_t" : "ioi_array";
        fprintf(out, "static %s %s[%d];\nstatic unsigned char %s[%d];\n", ty, vname(v), n, iname(v), n);
    }
    fprintf(out, "static unsigned char AB[%d] = {", p->nt ? p->nt : 1);
    for (int i = 0; i < p->nt; i++) {
        struct tuple *t = &p->t[i];
        fputs(t->chance <= 0 ? "1," : "0,", out);
        if (t->chance <= 0 && t->onceagain) /* perpet.c swaps them */
            t->onceagain = 3 - t->onceagain;
        if (t->chance < 0)
            t->chance = -t->chance;
        if (i % 32 == 31)
            fputc('\n', out);
    }
    fputs("};\nstatic unsigned char OLD;\n", out);
    fputs("static void ioi_gerund(unsigned char *ab, const int *g, int n, int set)\n"
          "{\n    for (int i = 0; i < n; i++)\n        ab[g[i]] = (unsigned char)set;\n}\n",
          out);
    if (p->smode) {
        /* template variable context */
        fputs("static ioi_array *const TVT[145] = {", out);
        for (int k = 1; k <= 144; k++) {
            int j = find_var(V_TAIL, (uint32_t)k);
            if (j >= 0)
                fprintf(out, "[%d] = &VT[%d], ", k, j);
        }
        fputs("0};\nstatic ioi_array *const TVH[145] = {", out);
        for (int k = 1; k <= 144; k++) {
            int j = find_var(V_HYB, (uint32_t)k);
            if (j >= 0)
                fprintf(out, "[%d] = &VH[%d], ", k, j);
        }
        int d = find_var(V_ONE, 144);
        fputs("0};\n", out);
        if (d >= 0)
            fprintf(out, "static const struct ioi_tpl_vars TV = {TVT, TVH, &V1[%d]};\n", d);
        else
            fputs("static const struct ioi_tpl_vars TV = {TVT, TVH, 0};\n", out);
    }
    /* main body goes to a buffer first: DB helpers and templates must be
     * declared before it */
    char *body = NULL;
    size_t bodylen = 0;
    FILE *bo = open_memstream(&body, &bodylen);
    if (!bo)
        die("out of memory");
    O = bo;
    for (int i = 0; i < p->nt; i++)
        gen_stmt(i, bugline);
    fclose(bo);
    O = out;
    if (p->smode) {
        tpl_emit_decls(p, out);
        /* DB helpers may be added while templates are emitted */
        char *tbody = NULL;
        size_t tlen = 0;
        FILE *to = open_memstream(&tbody, &tlen);
        O = to;
        tpl_emit_functions(p, to);
        fclose(to);
        O = out;
        for (int k = 0; k < ndbfns; k++)
            fprintf(out, "static uint32_t DB%d(%s);\n", dbfns[k].id, dbfns[k].kind == 1 ? "uint32_t" : "void");
        for (int k = 0; k < ndbfns; k++)
            emit_dbfn(&dbfns[k]);
        fwrite(tbody, 1, tlen, out);
        free(tbody);
    }
    fputs("int main(int argc, char **argv)\n{\n    int pc = 0;\n    ioi_init(argc, argv);\n", out);
    if (p->smode) {
        fputs("    ioi_s_init();\n    if (ioi_web_active()) {\n        uint32_t params = 0;\n", out);
        int k4400 = find_var(V_HYB, 4400);
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < p->nt; i++) {
                struct tuple *t = &p->t[i];
                if (t->type != S_ROUTE || (strcmp(t->str, "*") == 0) != pass)
                    continue;
                fprintf(out, "        if (!AB[%d] && ioi_web_route(", i);
                emit_cstring(out, t->str, strlen(t->str));
                fputs(", &params)) {\n", out);
                if (k4400 >= 0)
                    fprintf(out, "            ioi_hyb_assign(&VH[%d], 0, params);\n", k4400);
                fprintf(out, "            ioi_s_check_origin(%d);\n            pc = %d;\n            goto run;\n        }\n",
                        t->origin, N + i);
            }
        fputs("        ioi_s_no_route();\n    }\nrun:\n", out);
    }
    fputs("    for (;;) switch (pc) {\n", out);
    fwrite(body, 1, bodylen, out);
    free(body);
    fputs("    ioi_lose(129, 0, 0);\n    default:\n    ioi_lose(129, 0, 0);\n    }\n}\n", out);
}
