/* lex.c — a re-implementation of C-INTERCAL 0.35's lexer.l.
 *
 * C-INTERCAL's statement boundaries, line numbers and politeness count all
 * fall out of flex's longest-match scanning (and of how far ahead flex
 * reads to decide a match), so this lexer simulates every rule of lexer.l
 * as a tiny NFA: the token is the longest match (earliest rule on ties)
 * and the scanner's read position advances as far as any rule could still
 * have been matching. iyylineno is 1 + newlines read so far, exactly as
 * C-INTERCAL's getc() counts them.
 */
#include "ick.h"

#include <stdlib.h>
#include <string.h>

/* pattern mini-language: ' ' = {W} = [ \t\n]*, '\x04' = {D} =
 * [0-9][ \t\n0-9]*, '\x01' = any char but newline, '\x02' = [2-5],
 * '\x03' = [A-Z]; everything else literal. */
#define PW " "
#define PD "\x04"

enum act { A_NONE = 0, A_SETLINE = 1, A_CLEAR = 2, A_POLITE = 4, A_NEXT = 8 };

struct rule {
    const char *pat;
    enum tok tok;
    int val;
    int act;
};

#define DOACT (A_SETLINE | A_CLEAR)
#define PLEASEACT (A_SETLINE | A_CLEAR | A_POLITE)

static const struct rule rules[] = {
    {PD, T_NUMBER, 0, 0},
    {"_", T_NOSPOT, 0, 0},
    {".", T_ONESPOT, 0, 0},
    {":", T_TWOSPOT, 0, 0},
    {",", T_TAIL, 0, 0},
    {";", T_HYBRID, 0, 0},
    {"#", T_MESH, 0, 0},
    {"\xBD", T_MINGLE, 0, 0},
    {"c^H/", T_MINGLE, 0, 0},
    {"c^H|", T_MINGLE, 0, 0},
    {"$", T_MINGLE, 0, 0},
    {"\xA2", T_MINGLE, 0, 0},
    {"\xA3", T_MINGLE, 0, 0},
    {"\xA4", T_MINGLE, 0, 0},
    {"\xC2\xA2", T_MINGLE, 0, 0},
    {"\xC2\xA3", T_MINGLE, 0, 0},
    {"\xC2\xA4", T_MINGLE, 0, 0},
    {"\xE2\x82\xA0", T_MINGLE, 0, 0},
    {"\xE2\x82\xA1", T_MINGLE, 0, 0},
    {"\xE2\x82\xA2", T_MINGLE, 0, 0},
    {"\xE2\x82\xA3", T_MINGLE, 0, 0},
    {"\xE2\x82\xA4", T_MINGLE, 0, 0},
    {"\xE2\x82\xA5", T_MINGLE, 0, 0},
    {"\xE2\x82\xA6", T_MINGLE, 0, 0},
    {"\xE2\x82\xA7", T_MINGLE, 0, 0},
    {"\xE2\x82\xA8", T_MINGLE, 0, 0},
    {"\xE2\x82\xA9", T_MINGLE, 0, 0},
    {"\xE2\x82\xAA", T_MINGLE, 0, 0},
    {"\xE2\x82\xAB", T_MINGLE, 0, 0},
    {"\xE2\x82\xAC", T_MINGLE, 0, 0},
    {"\xE0\xA7\xB2", T_MINGLE, 0, 0},
    {"\xE0\xA7\xB3", T_MINGLE, 0, 0},
    {"\xE0\xB8\xBF", T_MINGLE, 0, 0},
    {"~", T_SELECT, 0, 0},
    {"/", T_SLAT, 0, 0},
    {"\\", T_BACKSLAT, 0, 0},
    {"&", T_UNARY, U_AND, 0},
    {"V", T_UNARY, U_OR, 0},
    {"\xA5", T_UNARY, U_XOR, 0},
    {"\xBE", T_UNARY, U_XOR, 0},
    {"V^H-", T_UNARY, U_XOR, 0},
    {"\xE2\x88\x80", T_UNARY, U_XOR, 0},
    {"?", T_UNARY, U_XOR, 0},
    {"|", T_UNARY, U_FIN, 0},
    {"^", T_UNARY, U_FIN, 0},
    {"@", T_UNARY, U_WHIRL, 0},
    {"\x02" PW "@", T_UNARY, -1, 0}, /* [2-5]{W}@ */
    {"'", T_CLOSESPARK, 0, 0},       /* classified by the stack */
    {"\"", T_OPENEARS, 0, 0},
    {"(" PW PD ")", T_LABEL, 0, A_SETLINE},
    {"DO", T_DO, 0, DOACT},
    {"FAC", T_DO, 0, DOACT},
    {"PLEASE", T_DO, 0, PLEASEACT},
    {"PLACET", T_DO, 0, PLEASEACT},
    {"PLEASE" PW "DO", T_DO, 0, PLEASEACT},
    {"PLACET" PW "FACERE", T_DO, 0, PLEASEACT},
    {"MAYBE", T_MAYBE, 0, DOACT},
    {"MAYBE" PW "DO", T_MAYBE, 0, DOACT},
    {"MAYBE" PW "PLEASE", T_MAYBE, 0, PLEASEACT},
    {"MAYBE" PW "PLEASE" PW "DO", T_MAYBE, 0, PLEASEACT},
    {"NOT", T_NOT, 0, 0},
    {"N'T", T_NOT, 0, 0},
    {"NON", T_NOT, 0, 0},
    {"\xAA", T_NOT, 0, 0},
    {"\xAC", T_NOT, 0, 0},
    {"ONCE", T_ONCE, 0, 0},
    {"QUONDAM", T_ONCE, 0, 0},
    {"AGAIN", T_AGAIN, 0, 0},
    {"ITERUM", T_AGAIN, 0, 0},
    {"%" PW PD, T_OHOHSEVEN, 0, 0},
    {"SUB", T_SUB, 0, 0},
    {"MULTIPLICATUS" PW "A", T_BY, 0, 0},
    {"BY", T_BY, 0, 0},
    {"<-", T_GETS, 0, 0},
    {"CALCULANDUM", T_GERUND, G_CALC, 0},
    {"CALCULATING", T_GERUND, G_CALC, 0},
    {"ALIENERE", T_FORGET, 0, 0},
    {"FORGET", T_FORGET, 0, 0},
    {"ALIENENDUM", T_GERUND, G_FORGET, 0},
    {"FORGETTING", T_GERUND, G_FORGET, 0},
    {"RECOLERE", T_RESUME, 0, 0},
    {"RESUME", T_RESUME, 0, 0},
    {"RECOLERENDUM", T_GERUND, G_RESUME, 0},
    {"RESUMING", T_GERUND, G_RESUME, 0},
    {"EXUERE", T_STASH, 0, 0},
    {"STASH", T_STASH, 0, 0},
    {"EXUENDUM", T_GERUND, G_STASH, 0},
    {"STASHING", T_GERUND, G_STASH, 0},
    {"INUERE", T_RETRIEVE, 0, 0},
    {"RETRIEVE", T_RETRIEVE, 0, 0},
    {"INUENDUM", T_GERUND, G_RETRIEVE, 0},
    {"RETRIEVING", T_GERUND, G_RETRIEVE, 0},
    {"DISSIMULARE", T_IGNORE, 0, 0},
    {"IGNORE", T_IGNORE, 0, 0},
    {"DISSIMULANDUM", T_GERUND, G_IGNORE, 0},
    {"IGNORING", T_GERUND, G_IGNORE, 0},
    {"MEMINISSE", T_REMEMBER, 0, 0},
    {"REMEMBER", T_REMEMBER, 0, 0},
    {"MEMINISSENDUM", T_GERUND, G_REMEMBER, 0},
    {"REMEMBERING", T_GERUND, G_REMEMBER, 0},
    {"ABSTINERE", T_ABSTAIN, 0, 0},
    {"ABSTAIN", T_ABSTAIN, 0, 0},
    {"ABSTINENDUM", T_GERUND, G_ABSTAIN, 0},
    {"ABSTAINING", T_GERUND, G_ABSTAIN, 0},
    {"REINSTARE", T_REINSTATE, 0, 0},
    {"REINSTATE", T_REINSTATE, 0, 0},
    {"REINSTATANDUM", T_GERUND, G_REINSTATE, 0},
    {"REINSTATING", T_GERUND, G_REINSTATE, 0},
    {"LEGERE" PW "EX", T_READ_OUT, 0, 0},
    {"READ" PW "OUT", T_READ_OUT, 0, 0},
    {"LEGENDUM", T_GERUND, G_READ, 0},
    {"READING" PW "OUT", T_GERUND, G_READ, 0},
    {"SCRIBERE" PW "IN", T_WRITE_IN, 0, 0},
    {"WRITE" PW "IN", T_WRITE_IN, 0, 0},
    {"SCRIBENDUM", T_GERUND, G_WRITE, 0},
    {"WRITING" PW "IN", T_GERUND, G_WRITE, 0},
    {"COMMEMERO", T_GERUND, G_COMMENT, 0},
    {"COMMENTS", T_GERUND, G_COMMENT, 0},
    {"COMMENTING", T_GERUND, G_COMMENT, 0},
    {"COMMENT", T_GERUND, G_COMMENT, 0},
    {"PIN", T_PIN, 0, 0},
    {"PINNING", T_GERUND, G_PIN, 0},
    {"DEINDERE" PW "A" PW "(" PW PD ")", T_NEXTFROMLABEL, 0, 0},
    {"NEXT" PW "FROM" PW "(" PW PD ")", T_NEXTFROMLABEL, 0, 0},
    {"DEINDERE" PW "A", T_NEXTFROMEXPR, 0, 0},
    {"NEXT" PW "FROM", T_NEXTFROMEXPR, 0, 0},
    {"DEINDENDUM", T_GERUND, G_NEXTFROM, 0},
    {"NEXTING" PW "FROM", T_GERUND, G_NEXTFROM, 0},
    {"ADVENIRE" PW "DE" PW "(" PW PD ")", T_COME_FROM, 0, 0},
    {"COME" PW "FROM" PW "(" PW PD ")", T_COME_FROM, 0, 0},
    {"ADVENIRE" PW "DE", T_COMPUCOME, 0, 0},
    {"COME" PW "FROM", T_COMPUCOME, 0, 0},
    {"ADVENENDUM", T_GERUND, G_COMEFROM, 0},
    {"COMING" PW "FROM", T_GERUND, G_COMEFROM, 0},
    {"DEINDE", T_NEXT, 0, A_NEXT},
    {"NEXT", T_NEXT, 0, A_NEXT},
    {"PROXIMANDUM", T_GERUND, G_NEXT, 0},
    {"NEXTING", T_GERUND, G_NEXT, 0},
    {"FROM", T_FROM, 0, 0},
    {"CONCEDERE", T_GIVE_UP, 0, 0},
    {"DESPERARE", T_GIVE_UP, 0, 0},
    {"GIVE" PW "UP", T_GIVE_UP, 0, 0},
    {"CONOR" PW "ITERUM", T_TRY_AGAIN, 0, 0},
    {"TRY" PW "AGAIN", T_TRY_AGAIN, 0, 0},
    {"WHILE", T_WHILE, 0, 0},
    {"WHILING", T_GERUND, G_WHILE, 0},
    {"LOOPING", T_GERUND, G_WHILE, 0},
    {"TRYING" PW "AGAIN", T_GERUND, G_TRYAGAIN, 0},
    {"GO" PW "BACK", T_GO_BACK, 0, 0},
    {"REDIRE", T_GO_BACK, 0, 0},
    {"GOING" PW "BACK", T_GERUND, G_GOBACK, 0},
    {"REDENDUM", T_GERUND, G_GOBACK, 0},
    {"GO" PW "AHEAD", T_GO_AHEAD, 0, 0},
    {"GRASSOR", T_GO_AHEAD, 0, 0},
    {"GOING" PW "AHEAD", T_GERUND, G_GOAHEAD, 0},
    {"CREATE" PW "(" PW PD ")", T_CREATE, 0, 0},
    {"CONFICE" PW "(" PW PD ")", T_CREATE, 0, 0},
    {"CREATE", T_COMPUCREATE, 0, 0},
    {"CONFICE", T_COMPUCREATE, 0, 0},
    {"CREATING", T_GERUND, G_CREATE, 0},
    {"CREATION", T_GERUND, G_CREATE, 0},
    {"CONFICENDUM", T_GERUND, G_CREATE, 0},
    {"+", T_INTERSECTION, 0, 0},
    {PW, T_WS, 0, 0},
    {"\x03", T_UNKNOWNID, 0, 0},
    {"\x01^H\x01", T_BADCHAR, 0, 0},
    {"\x01", T_BADCHAR, 0, 0},
};
#define NRULES (sizeof rules / sizeof *rules)

static const struct {
    const char *w;
    int v;
} swords[] = {
    {"WITH", W_WITH}, {"WITHOUT", W_WITHOUT}, {"ROUTE", W_ROUTE},
    {"TEMPLATE", W_TEMPLATE}, {"REDIRECT", W_REDIRECT}, {"SESSION", W_SESSION},
    {"STATUS", W_STATUS}, {"FIELD", W_FIELD}, {"TIME", W_TIME}, {"INTO", W_INTO},
    {"DATABASE", W_DATABASE}, {"HAVING", W_HAVING}, {"SOMEWHERE", W_SOMEWHERE},
    {"BEYOND", W_BEYOND}, {"BACKWARDS", W_BACKWARDS}, {"SKIP", W_SKIP},
    {"TAKE", W_TAKE}, {"IN", W_IN}, {"BULK", W_BULK}, {"HOW", W_HOW},
    {"MANY", W_MANY}, {"IS", W_IS}, {"AS", W_AS}, {"ARABIC", W_ARABIC},
    {"ROMAN", W_ROMAN}, {"PROSE", W_PROSE}, {"AGO", W_AGO}, {"FACE", W_FACE},
};

static int isws(int c) { return c == ' ' || c == '\t' || c == '\n'; }
static int isdig(int c) { return c >= '0' && c <= '9'; }

/* State bit 2k: before element k; bit 2k+1: inside the tail of {D} k. */
typedef uint64_t nset;

static nset closure(const char *pat, size_t plen, nset s)
{
    for (size_t k = 0; k < plen; k++) {
        if (((s >> (2 * k)) & 1) && pat[k] == ' ')
            s |= (nset)1 << (2 * (k + 1));
        if ((s >> (2 * k + 1)) & 1)
            s |= (nset)1 << (2 * (k + 1));
    }
    return s;
}

static int elem_match(char e, int c)
{
    switch (e) {
    case '\x01': return c != '\n';
    case '\x02': return c >= '2' && c <= '5';
    case '\x03': return c >= 'A' && c <= 'Z';
    default: return (unsigned char)e == (unsigned)c;
    }
}

static nset step(const char *pat, size_t plen, nset s, int c)
{
    nset r = 0;
    for (size_t k = 0; k < plen; k++) {
        if ((s >> (2 * k)) & 1) {
            char e = pat[k];
            if (e == ' ') {
                if (isws(c))
                    r |= (nset)1 << (2 * k);
            } else if (e == '\x04') {
                if (isdig(c))
                    r |= (nset)1 << (2 * k + 1);
            } else if (elem_match(e, c)) {
                r |= (nset)1 << (2 * (k + 1));
            }
        }
        if (((s >> (2 * k + 1)) & 1) && (isws(c) || isdig(c)))
            r |= (nset)1 << (2 * k + 1);
    }
    return closure(pat, plen, r);
}

static int has_trans(size_t plen, nset s)
{
    nset accept = (nset)1 << (2 * plen);
    return (s & ~accept) != 0;
}

/* Longest match of PAT at s[p..]; *extent = characters the scanner reads. */
static long match_rule(const char *pat, const unsigned char *s, size_t len,
                       size_t p, size_t *extent)
{
    size_t plen = strlen(pat);
    nset S = closure(pat, plen, 1), accept = (nset)1 << (2 * plen);
    long best = -1;
    size_t i = 0;
    for (;;) {
        if (S & accept)
            best = (long)i;
        if (!has_trans(plen, S) || p + i >= len)
            break;
        int c = s[p + i];
        i++;
        S = step(pat, plen, S, c);
        if (!S)
            break;
    }
    *extent = i;
    return best;
}

void lex_init(struct lexer *lx, const unsigned char *s, size_t len, int line_base)
{
    memset(lx, 0, sizeof *lx);
    lx->s = s;
    lx->len = len;
    lx->line_base = line_base;
    lx->qcap = 64;
    lx->qstack = xmalloc((size_t)lx->qcap);
}

int lex_iyylineno(const struct lexer *lx) { return lx->line_base + 1 + lx->nl_read; }

static void advance_read(struct lexer *lx, size_t to)
{
    if (to > lx->len)
        to = lx->len;
    while (lx->maxread < to) {
        if (lx->s[lx->maxread] == '\n')
            lx->nl_read++;
        lx->maxread++;
    }
}

void lex_pop2(struct lexer *lx)
{
    lx->qdepth -= 2;
    if (lx->qdepth < 0)
        lx->qdepth = 0;
}

static int qtop(const struct lexer *lx) { return lx->qdepth ? lx->qstack[lx->qdepth - 1] : 0; }

static void qpush(struct lexer *lx, int v)
{
    if (lx->qdepth >= 3200)
        cerr(281, lex_iyylineno(lx), NULL);
    if (lx->qdepth == lx->qcap) {
        lx->qcap *= 2;
        lx->qstack = xrealloc(lx->qstack, (size_t)lx->qcap);
    }
    lx->qstack[lx->qdepth++] = (unsigned char)v;
}

/* C-INTERCAL's myatoi(): all digits concatenated, then atoi(). */
static int myatoi(const unsigned char *s, size_t n)
{
    unsigned long long v = 0;
    int nd = 0, sat = 0;
    for (size_t i = 0; i < n && nd < 99; i++)
        if (isdig(s[i])) {
            nd++;
            if (!sat) {
                v = v * 10 + (unsigned)(s[i] - '0');
                if (v > 0x7fffffffffffffffULL) {
                    v = 0x7fffffffffffffffULL;
                    sat = 1;
                }
            }
        }
    return (int)(uint32_t)v;
}

void lex_next(struct lexer *lx, struct token *t)
{
    for (;;) {
        memset(t, 0, sizeof *t);
        t->pos = lx->pos;
        if (lx->pos >= lx->len) {
            t->type = T_EOF;
            t->line = lex_iyylineno(lx);
            return;
        }
        const unsigned char *s = lx->s;
        size_t p = lx->pos;
        /* Interstates keywords are whole upper-case words. */
        if (lx->smode && s[p] >= 'A' && s[p] <= 'Z' &&
            (p == 0 || !(s[p - 1] >= 'A' && s[p - 1] <= 'Z'))) {
            size_t q = p;
            while (q < lx->len && s[q] >= 'A' && s[q] <= 'Z')
                q++;
            for (size_t i = 0; i < sizeof swords / sizeof *swords; i++)
                if (strlen(swords[i].w) == q - p && !memcmp(swords[i].w, s + p, q - p)) {
                    advance_read(lx, q + 1);
                    lx->pos = q;
                    t->type = T_WORD;
                    t->val = swords[i].v;
                    t->len = q - p;
                    t->line = lex_iyylineno(lx);
                    return;
                }
        }
        long best = -1;
        size_t bestrule = 0, maxext = 0;
        for (size_t r = 0; r < NRULES; r++) {
            const char *pat = rules[r].pat;
            unsigned char c0 = (unsigned char)pat[0];
            /* quick reject on a literal first element */
            if (c0 != ' ' && c0 != '\x01' && c0 != '\x02' && c0 != '\x03' &&
                c0 != '\x04' && c0 != s[p]) {
                if (maxext < 1)
                    maxext = 1;
                continue;
            }
            size_t ext;
            long m = match_rule(pat, s, lx->len, p, &ext);
            if (ext > maxext)
                maxext = ext;
            if (m > best) {
                best = m;
                bestrule = r;
            }
        }
        if (best <= 0) { /* cannot happen: '.' matches any non-newline */
            best = 1;
            bestrule = NRULES - 1;
        }
        advance_read(lx, p + maxext);
        lx->pos = p + (size_t)best;
        const struct rule *ru = &rules[bestrule];
        if (ru->tok == T_WS)
            continue;
        t->type = ru->tok;
        t->val = ru->val;
        t->len = (size_t)best;
        int line = lex_iyylineno(lx);
        t->line = line;
        if (ru->act & A_SETLINE) {
            if (*lx->stbeginline == 0)
                *lx->stbeginline = line;
            else if (*lx->stbeginline < 0)
                *lx->stbeginline = 0;
        }
        if (ru->act & A_CLEAR)
            lx->qdepth = 0;
        if (ru->act & A_POLITE)
            (*lx->politesse)++;
        if (ru->act & A_NEXT)
            *lx->stbeginline = 0;
        switch (ru->tok) {
        case T_NUMBER:
        case T_LABEL:
        case T_COME_FROM:
        case T_NEXTFROMLABEL:
        case T_CREATE:
            t->val = myatoi(s + p, (size_t)best);
            break;
        case T_OHOHSEVEN:
            t->val = myatoi(s + p, (size_t)best);
            if (!(t->val && t->val < 100))
                cerr(17, line, NULL);
            break;
        case T_UNARY:
            if (t->val == -1)
                t->val = U_WHIRL + (s[p] - '0') - 1;
            break;
        case T_CLOSESPARK: { /* ' */
            int top = qtop(lx);
            qpush(lx, 0);
            t->type = top ? T_OPENSPARK : T_CLOSESPARK;
            break;
        }
        case T_OPENEARS: { /* " */
            int top = qtop(lx);
            qpush(lx, 1);
            t->type = top ? T_CLOSEEARS : T_OPENEARS;
            break;
        }
        default:
            break;
        }
        return;
    }
}

int lex_string_at(struct lexer *lx, size_t pos, struct token *t)
{
    const unsigned char *s = lx->s;
    if (pos >= lx->len || s[pos] != '"')
        return 0;
    size_t q = pos + 1, n = 0;
    char *buf = xmalloc(lx->len - pos + 1);
    while (q < lx->len && s[q] != '"' && s[q] != '\n') {
        if (s[q] == '\\' && q + 1 < lx->len && (s[q + 1] == '"' || s[q + 1] == '\\'))
            q++;
        buf[n++] = (char)s[q++];
    }
    if (q >= lx->len || s[q] != '"') {
        free(buf);
        return 0;
    }
    buf[n] = '\0';
    memset(t, 0, sizeof *t);
    t->type = T_STRING;
    t->pos = pos;
    t->len = q + 1 - pos;
    t->str = buf;
    t->slen = n;
    lx->pos = q + 1;
    advance_read(lx, q + 2);
    t->line = lex_iyylineno(lx);
    return 1;
}

void lex_save(const struct lexer *lx, struct lexsave *sv)
{
    sv->pos = lx->pos;
    sv->maxread = lx->maxread;
    sv->nl_read = lx->nl_read;
    sv->qdepth = lx->qdepth;
    sv->q = xmalloc((size_t)lx->qdepth + 1);
    memcpy(sv->q, lx->qstack, (size_t)sv->qdepth);
    sv->stbeginline = *lx->stbeginline;
    sv->politesse = *lx->politesse;
}

void lex_drop(struct lexsave *sv)
{
    free(sv->q);
    sv->q = NULL;
}

/* The quote stack only grows, so the saved depth always fits. */
void lex_restore(struct lexer *lx, const struct lexsave *sv)
{
    lx->pos = sv->pos;
    lx->maxread = sv->maxread;
    lx->nl_read = sv->nl_read;
    lx->qdepth = sv->qdepth;
    memcpy(lx->qstack, sv->q, (size_t)sv->qdepth);
    *lx->stbeginline = sv->stbeginline;
    *lx->politesse = sv->politesse;
}

void source_load(struct source *src, const unsigned char *raw, size_t rawlen)
{
    unsigned char *out = xmalloc(rawlen * 2 + 2);
    size_t n = 0;
    int cap = 64, nl = 0;
    char **lines = xmalloc((size_t)cap * sizeof *lines);
    unsigned char *cur = xmalloc(rawlen + 2);
    size_t curlen = 0;
    size_t i = 0;
    int lastnl = 0; /* C-INTERCAL's eolflag: even an empty file gets a line */
    for (;;) {
        int c;
        if (i >= rawlen) {
            if (lastnl)
                break;
            c = '\n'; /* final newline is supplied */
        } else {
            c = raw[i++];
            if (c == '\r') { /* the first CR of a run is ignored */
                if (i >= rawlen) {
                    if (lastnl)
                        break;
                    c = '\n';
                } else {
                    c = raw[i++];
                }
            }
        }
        lastnl = 0;
        if (c == '!') {
            cur[curlen++] = '!';
            out[n++] = '\'';
            out[n++] = '.';
        } else if (c == '\b') {
            cur[curlen++] = '\b';
            out[n++] = '^';
            out[n++] = 'H';
        } else if (c == '\n') {
            if (nl + 2 >= cap) {
                cap *= 2;
                lines = xrealloc(lines, (size_t)cap * sizeof *lines);
            }
            lines[++nl] = xstrndup((const char *)cur, curlen);
            curlen = 0;
            out[n++] = '\n';
            lastnl = 1;
        } else {
            cur[curlen++] = (unsigned char)c;
            out[n++] = (unsigned char)c;
        }
    }
    free(cur);
    lines[0] = xstrdup("");
    src->stream = out;
    src->len = n;
    src->lines = lines;
    src->nlines = nl;
}
