/* ick.h — internal interfaces of the INTERCAL on Interstates compiler. */
#ifndef ICK_H
#define ICK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- tokens --------------------------------------------------------------- */

enum tok {
    T_EOF = 0, T_NUMBER, T_NOSPOT, T_ONESPOT, T_TWOSPOT, T_TAIL, T_HYBRID,
    T_MESH, T_MINGLE, T_SELECT, T_SLAT, T_BACKSLAT, T_UNARY, T_OPENSPARK,
    T_CLOSESPARK, T_OPENEARS, T_CLOSEEARS, T_LABEL, T_DO, T_MAYBE, T_NOT,
    T_ONCE, T_AGAIN, T_OHOHSEVEN, T_SUB, T_BY, T_GETS, T_GERUND, T_FORGET,
    T_RESUME, T_STASH, T_RETRIEVE, T_IGNORE, T_REMEMBER, T_ABSTAIN,
    T_REINSTATE, T_READ_OUT, T_WRITE_IN, T_PIN, T_NEXTFROMLABEL,
    T_NEXTFROMEXPR, T_COME_FROM, T_COMPUCOME, T_NEXT, T_FROM, T_GIVE_UP,
    T_TRY_AGAIN, T_WHILE, T_GO_BACK, T_GO_AHEAD, T_CREATE, T_COMPUCREATE,
    T_INTERSECTION, T_UNKNOWNID, T_BADCHAR,
    /* Interstates only */
    T_STRING, T_WORD,
    T_WS /* internal */
};

/* unary operators (token value of T_UNARY) */
enum { U_AND = 1, U_OR, U_XOR, U_FIN, U_WHIRL, U_WHIRL2, U_WHIRL3, U_WHIRL4, U_WHIRL5 };

/* gerund classes (token value of T_GERUND, and statement classes) */
enum {
    G_NONE = 0, G_COMMENT, G_CALC, G_NEXT, G_FORGET, G_RESUME, G_STASH,
    G_RETRIEVE, G_IGNORE, G_REMEMBER, G_ABSTAIN, G_REINSTATE, G_READ,
    G_WRITE, G_COMEFROM, G_GIVEUP,
    /* newfangled classes no supported statement belongs to */
    G_NEXTFROM, G_WHILE, G_TRYAGAIN, G_GOBACK, G_GOAHEAD, G_CREATE, G_PIN,
    G_COUNT
};

/* Interstates keywords (token value of T_WORD) */
enum {
    W_WITH = 1, W_WITHOUT, W_ROUTE, W_TEMPLATE, W_REDIRECT, W_SESSION,
    W_STATUS, W_FIELD, W_BY, W_TIME, W_INTO, W_DATABASE, W_HAVING, W_SOMEWHERE,
    W_BEYOND, W_BACKWARDS, W_SKIP, W_TAKE, W_IN, W_BULK, W_HOW, W_MANY,
    W_IS, W_AS, W_ARABIC, W_ROMAN, W_PROSE, W_AGO, W_FACE
};

struct token {
    enum tok type;
    int val;          /* number, label, unary op, gerund class, keyword */
    size_t pos, len;  /* in the lexer's stream */
    int line;         /* iyylineno when the token's action ran */
    char *str;        /* T_STRING contents (NUL-terminated, unescaped) */
    size_t slen;
};

struct lexer {
    const unsigned char *s;
    size_t len, pos;
    size_t maxread;     /* furthest character the scanner has read */
    int nl_read;        /* newlines in s[0, maxread) */
    int line_base;      /* lines before this stream (syslib continues) */
    int smode;          /* Interstates keywords */
    /* spark/ears classification stack (lexer.l's sparkearsstack) */
    unsigned char *qstack;
    int qdepth, qcap;
    int *stbeginline;   /* shared with the parser */
    int *politesse;
    const char *fname;
    int (*errline)(struct lexer *); /* line for lexer-raised errors */
};

void lex_init(struct lexer *lx, const unsigned char *s, size_t len, int line_base);
/* Lex the next token (skipping whitespace). */
void lex_next(struct lexer *lx, struct token *t);
int lex_iyylineno(const struct lexer *lx);
void lex_pop2(struct lexer *lx);
/* Interstates: lex a "string literal" starting at a '"' at position pos. */
int lex_string_at(struct lexer *lx, size_t pos, struct token *t);
/* Snapshot/restore for backtracking in -s mode. */
struct lexsave {
    size_t pos, maxread;
    int nl_read, qdepth, stbeginline, politesse;
    unsigned char *q;   /* copy of the quote stack (freed by lex_restore/lex_drop) */
};
void lex_save(const struct lexer *lx, struct lexsave *sv);
void lex_drop(struct lexsave *sv);
void lex_restore(struct lexer *lx, const struct lexsave *sv);

/* Preprocess raw source like C-INTERCAL's getc(): '!' -> "'.", backspace
 * -> "^H", first CR of a run dropped, final newline ensured. Also splits
 * the raw lines (textlines). */
struct source {
    unsigned char *stream;
    size_t len;
    char **lines;     /* lines[1..nlines] raw text */
    int nlines;
};
void source_load(struct source *src, const unsigned char *raw, size_t rawlen);

/* ---- expressions ------------------------------------------------------------ */

enum ekind {
    E_CONST, E_VAR,        /* .n :n (scalar), ,n ;n (whole array, -s value) */
    E_ELEM,                /* array SUB subs */
    E_MINGLE, E_SELECT, E_UNARY, E_UNKOP,
    E_STRING,              /* -s: BYTES literal */
    E_VALSUB,              /* -s: value SUB k */
    E_DB                   /* -s: FROM DATABASE read */
};

/* variable kinds */
enum { V_ONE = 1, V_TWO = 2, V_TAIL = 3, V_HYB = 4 };

struct dbq;

struct node {
    enum ekind kind;
    int op;             /* unary op */
    int width;          /* 16 or 32 */
    int isval;          /* -s: yields a cassette-capable value */
    uint32_t num;       /* constant value */
    int vtype, var;     /* variable kind and interned index */
    struct node *l, *r;
    struct node **subs;
    int nsubs;
    char *str;          /* E_STRING */
    size_t slen;
    struct dbq *db;     /* E_DB */
};

/* ActiveCassette query (parsed form) */
struct dbcl {
    int kind;           /* W_HAVING (op 0/1/2), W_BY, W_SKIP, W_TAKE */
    int col, op, backwards;
    struct node *e;
};
struct dbq {
    int table;
    struct node *sel;   /* raw SEL expression (PK lookup) */
    int *cols;
    int ncols;
    struct dbcl *cl;
    int ncl;
    int shape;          /* 0 row, 1 bulk, 2 how many, 3 pk */
    int has_sel;
};

/* ---- statements -------------------------------------------------------------- */

enum stype {
    S_SPLAT = 0, S_UNKNOWN, S_GETS, S_RESIZE, S_NEXT, S_FORGET, S_RESUME,
    S_STASH, S_RETRIEVE, S_IGNORE, S_REMEMBER, S_ABSTAIN, S_REINSTATE,
    S_DISABLE, S_ENABLE, S_GIVE_UP, S_READ_OUT, S_WRITE_IN, S_COME_FROM,
    S_BADFEATURE,      /* parsed newfangled C-INTERCAL feature: E111/E405 */
    /* Interstates */
    S_ROUTE, S_TEMPLATE, S_REDIRECT, S_SESSION_OUT, S_FIELD, S_SESSION_IN,
    S_TIME, S_STASH_ASSIGN, S_DB_INSERT, S_DB_UPDATE, S_DB_DELETE
};

struct tuple {
    enum stype type;
    int gclass;         /* gerund class for ABSTAIN/REINSTATE gerunds */
    int label;
    int chance;         /* exechance as in C-INTERCAL (100, -100, n, 0) */
    int line;           /* ick_lineno of the statement (thisline) */
    int badcode;        /* S_BADFEATURE: 111 or 405 */
    int maybe;
    int onceagain;      /* 0, 1 ONCE, 2 AGAIN (C-INTERCAL extension) */
    struct node *lhs, *rhs;
    struct node **items;   /* STASH lists, READ OUT/WRITE IN items, BY dims */
    int nitems;
    int *ger;              /* gerund classes */
    int nger;
    int target;            /* label for NEXT/ABSTAIN/REINSTATE/COME FROM */
    int tidx;              /* resolved target tuple index */
    int cf;                /* index of the COME FROM aimed here, -1 */
    int ncomefrom;
    /* Interstates */
    char *str;             /* route pattern / template name / field name */
    int status;            /* WITH STATUS */
    int tpl;               /* template id */
    int origin;            /* route origin label */
    int insyslib;
    int stashgrow;         /* STASH-assign */
};

/* ---- program -------------------------------------------------------------- */

struct var { int type; uint32_t num; };

struct program {
    struct tuple *t;
    int nt, capt;
    struct var *vars[5];
    int nvars[5], capvars[5];
    char **lines;        /* textlines, 1-based */
    int nlines;          /* iyylineno - 1 at the end */
    int iyylineno;       /* final */
    int politesse;
    int smode;
    const char *srcpath;
    const char *srcdir;
    int main_nt;         /* tuples before syslib */
    /* templates */
    struct tpl **tpls;
    int ntpls;
};

/* errors */
extern const char *g_srcname; /* for -s file:line prefixes */
extern int g_smode;
_Noreturn void cerr(int code, int line, const char *arg);
_Noreturn void cerr_at(int code, int line, int srcline, const char *arg);
_Noreturn void die(const char *fmt, ...);

/* parse.c */
int intern(struct program *p, int type, uint32_t num, int line);
void parse_classic(struct program *p, struct lexer *lx);
void parse_interstates(struct program *p, const unsigned char *src, size_t len);
struct node *parse_template_expr(struct program *p, const char *s, size_t len,
                                 int line, int *filter, struct node **rhs, int *isis);

/* gen.c */
void check_program(struct program *p);
void generate(struct program *p, FILE *out, int bugline);

/* tpl.c */
struct tpl;
int tpl_load(struct program *p, const char *name, int line);
void tpl_check_links(struct program *p);
void tpl_emit_decls(struct program *p, FILE *out);
void tpl_emit_functions(struct program *p, FILE *out);

/* gen.c: expression emitters (write to the stream set with gen_set_out) */
void gen_set_out(FILE *out);
void gen_num(struct node *n);
void gen_val(struct node *n);

/* util */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void emit_cstring(FILE *out, const char *s, size_t n);

#endif
