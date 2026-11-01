/* runtime.c — core INTERCAL runtime: errors, operators, storage, STASH,
 * the NEXT stack and the INTERCAL I/O model.
 *
 * Every observable behaviour here (message texts, exit statuses, Roman
 * numerals, Turing Text, digit words) follows C-INTERCAL 0.35's libick.
 */
#include "ioi_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int ioi_lineno;

static void (*error_hook)(int, const char *);
static void (*exit_hook)(void);

void ioi_set_error_hook(void (*hook)(int, const char *)) { error_hook = hook; }
void ioi_set_exit_hook(void (*hook)(void)) { exit_hook = hook; }

void ioi_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    srand((unsigned)time(NULL));
}

/* ---- errors ------------------------------------------------------------- */

struct errtext {
    int code;
    const char *text; /* after "ICLnnnI\t"; %s = arg, %d = line */
};

#define WAY "\n\tON THE WAY TO %d\n"
static const struct errtext errors[] = {
    {0, "%s" WAY},
    {17, "DO YOU EXPECT ME TO FIGURE THIS OUT?" WAY},
    {123, "PROGRAM HAS DISAPPEARED INTO THE BLACK LAGOON" WAY},
    {129, "PROGRAM HAS GOTTEN LOST\n\tON THE WAY TO WHO KNOWS WHERE\n"},
    {222, "BUMMER, DUDE!" WAY},
    {240, "ERROR HANDLER PRINTED SNIDE REMARK" WAY},
    {241, "VARIABLES MAY NOT BE STORED IN WEST HYPERSPACE" WAY},
    {252, "I'VE FORGOTTEN WHAT I WAS ABOUT TO SAY" WAY},
    {275, "DON'T BYTE OFF MORE THAN YOU CAN CHEW" WAY},
    {436, "THROW STICK BEFORE RETRIEVING!" WAY},
    {533, "YOU WANT MAYBE WE SHOULD IMPLEMENT 64-BIT VARIABLES?" WAY},
    {562, "I DO NOT COMPUTE" WAY},
    {579, "WHAT BASE AND/OR LANGUAGE INCLUDES %s?" WAY},
    {621, "ERROR TYPE 621 ENCOUNTERED" WAY},
    {632, "THE NEXT STACK RUPTURES.  ALL DIE.  OH, THE EMBARRASSMENT!" WAY},
    {633, "PROGRAM FELL OFF THE EDGE\n\tON THE WAY TO THE NEW WORLD\n"},
    {774, "RANDOM COMPILER BUG" WAY},
    {778, "UNEXPLAINED COMPILER BUG" WAY},
    {991, "YOU HAVE TOO MUCH ROPE TO HANG YOURSELF\n        ON THE WAY TO %d\n"},
    /* Interstates (see compiler/README.md) */
    {403, "YOU CAN'T GET THERE FROM HERE" WAY},
    {404, "I'M ALL OUT OF CHOICES!" WAY},
    {410, "THAT IS NOT A NUMBER, HOWEVER YOU SPELL IT" WAY},
    {411, "A WHOLE CASSETTE WILL NOT FIT IN ONE SPOT" WAY},
    {412, "A TAIL HOLDS BYTES, NOT CASSETTES" WAY},
    {420, "THE CASSETTE IS CHEWED UP: %s" WAY},
    {421, "NO SUCH SHELF IN THE CASSETTE LIBRARY: %s" WAY},
    {422, "THE SESSION HAS NO SECRET TO KEEP" WAY},
    {414, "BYTES DO NOT MAKE A LIST" WAY},
};
#undef WAY

static char *msgbuf;

const char *ioi_error_message(int code, int line, const char *arg)
{
    const char *t = NULL;
    for (size_t i = 0; i < sizeof errors / sizeof *errors; i++)
        if (errors[i].code == code)
            t = errors[i].text;
    size_t cap = 256 + (arg ? strlen(arg) : 0);
    free(msgbuf);
    msgbuf = malloc(cap);
    if (!msgbuf)
        abort();
    size_t n = (size_t)snprintf(msgbuf, cap, "ICL%03dI\t", code);
    if (!t)
        t = "%s" "\n\tON THE WAY TO %d\n";
    for (const char *p = t; *p && n < cap - 1; p++) {
        if (p[0] == '%' && p[1] == 's') {
            const char *s = arg ? arg : "(null)";
            size_t l = strlen(s);
            if (l > cap - 1 - n)
                l = cap - 1 - n;
            memcpy(msgbuf + n, s, l);
            n += l;
            p++;
        } else if (p[0] == '%' && p[1] == 'd') {
            n += (size_t)snprintf(msgbuf + n, cap - n, "%d", line);
            p++;
        } else {
            msgbuf[n++] = *p;
        }
    }
    if (n >= cap)
        n = cap - 1;
    msgbuf[n] = '\0';
    size_t rest = strlen("        CORRECT SOURCE AND RESUBNIT\n");
    char *m = realloc(msgbuf, n + rest + 1);
    if (!m)
        abort();
    msgbuf = m;
    memcpy(msgbuf + n, "        CORRECT SOURCE AND RESUBNIT\n", rest + 1);
    return msgbuf;
}

_Noreturn void ioi_lose(int code, int line, const char *arg)
{
    const char *m = ioi_error_message(code, line, arg);
    fflush(stdout);
    fputs(m, stderr);
    if (error_hook) {
        void (*h)(int, const char *) = error_hook;
        error_hook = NULL;
        h(code, m);
    }
    exit(code);
}

_Noreturn void ioi_give_up(void)
{
    if (exit_hook) {
        void (*h)(void) = exit_hook;
        exit_hook = NULL;
        h();
    }
    exit(0);
}

static void *xmalloc(size_t n, int err)
{
    void *p = calloc(n ? n : 1, 1);
    if (!p)
        ioi_lose(err, ioi_lineno, NULL);
    return p;
}

/* ---- operators (fiddle.c, binary only) --------------------------------- */

uint32_t ioi_mingle(uint32_t r, uint32_t s)
{
    if (r > 0xffff || s > 0xffff)
        ioi_lose(533, ioi_lineno, NULL);
    r = ((r & 0x0000ff00) << 8) | (r & 0x000000ff);
    r = ((r & 0x00f000f0) << 4) | (r & 0x000f000f);
    r = ((r & 0x0c0c0c0c) << 2) | (r & 0x03030303);
    r = ((r & 0x22222222) << 1) | (r & 0x11111111);
    s = ((s & 0x0000ff00) << 8) | (s & 0x000000ff);
    s = ((s & 0x00f000f0) << 4) | (s & 0x000f000f);
    s = ((s & 0x0c0c0c0c) << 2) | (s & 0x03030303);
    s = ((s & 0x22222222) << 1) | (s & 0x11111111);
    return (r << 1) | s;
}

uint32_t ioi_select(uint32_t r, uint32_t s)
{
    uint32_t i = 1, t = 0;
    while (s) {
        if (s & i) {
            t |= r & i;
            s ^= i;
            i <<= 1;
        } else {
            s >>= 1;
            r >>= 1;
        }
    }
    return t;
}

static uint32_t rot16(uint32_t n) { return (n >> 1) | ((n & 1) ? 0x8000u : 0); }
static uint32_t rot32(uint32_t n) { return (n >> 1) | ((n & 1) ? 0x80000000u : 0); }
uint32_t ioi_and16(uint32_t n) { return rot16(n) & n; }
uint32_t ioi_or16(uint32_t n) { return rot16(n) | n; }
uint32_t ioi_xor16(uint32_t n) { return rot16(n) ^ n; }
uint32_t ioi_and32(uint32_t n) { return rot32(n) & n; }
uint32_t ioi_or32(uint32_t n) { return rot32(n) | n; }
uint32_t ioi_xor32(uint32_t n) { return rot32(n) ^ n; }

/* ---- scalars and arrays ------------------------------------------------- */

void ioi_set16(uint16_t *p, int ign, uint32_t v)
{
    if (v > 0xffff)
        ioi_lose(275, ioi_lineno, NULL);
    if (!ign)
        *p = (uint16_t)v;
}

void ioi_set32(uint32_t *p, int ign, uint32_t v)
{
    if (!ign)
        *p = v;
}

static size_t address(const ioi_array *a, int n, const uint32_t *subs)
{
    size_t addr = 0;
    if ((uint32_t)n != a->rank)
        ioi_lose(241, ioi_lineno, NULL);
    for (uint32_t i = 0; i < a->rank; i++) {
        uint32_t v = subs[i];
        if (v == 0 || v > a->dims[i])
            ioi_lose(241, ioi_lineno, NULL);
        addr = addr * a->dims[i] + v - 1;
    }
    return addr;
}

uint16_t *ioi_aref16(ioi_array *a, int n, const uint32_t *subs)
{
    return (uint16_t *)a->data + address(a, n, subs);
}

uint32_t *ioi_aref32(ioi_array *a, int n, const uint32_t *subs)
{
    return (uint32_t *)a->data + address(a, n, subs);
}

void ioi_resize(ioi_array *a, int wide, int ign, int n, const uint32_t *dims)
{
    uint64_t prod = 1;
    for (int i = 0; i < n; i++) {
        if (dims[i] == 0)
            ioi_lose(240, ioi_lineno, NULL);
        prod *= dims[i];
        if (prod > ((uint64_t)1 << 40))
            ioi_lose(241, ioi_lineno, NULL);
    }
    if (ign)
        return;
    size_t esz = wide ? 4 : 2;
    if (prod * esz > (size_t)-1 / 2)
        ioi_lose(241, ioi_lineno, NULL);
    void *data = calloc((size_t)prod ? (size_t)prod : 1, esz);
    uint32_t *d = malloc((size_t)n * sizeof *d);
    if (!data || !d)
        ioi_lose(241, ioi_lineno, NULL);
    memcpy(d, dims, (size_t)n * sizeof *d);
    free(a->dims);
    free(a->data);
    a->rank = (uint32_t)n;
    a->dims = d;
    a->data = data;
}

/* ---- STASH / RETRIEVE (one LIFO list, searched by variable) ------------- */

struct stashbox {
    int type, idx;
    union {
        uint32_t scalar;
        ioi_array arr;
    } u;
    struct stashbox *next;
};
static struct stashbox *stash_top;

static size_t elements(const ioi_array *a)
{
    size_t prod = a->rank ? 1 : 0;
    for (uint32_t i = 0; i < a->rank; i++)
        prod *= a->dims[i];
    return prod;
}

void ioi_stash(int type, int idx, void *var)
{
    struct stashbox *b = xmalloc(sizeof *b, 222);
    b->type = type;
    b->idx = idx;
    if (type == IOI_ONESPOT)
        b->u.scalar = *(uint16_t *)var;
    else if (type == IOI_TWOSPOT)
        b->u.scalar = *(uint32_t *)var;
    else {
        const ioi_array *a = var;
        size_t esz = type == IOI_TAIL ? 2 : 4, n = elements(a);
        b->u.arr.rank = a->rank;
        b->u.arr.dims = xmalloc(a->rank * sizeof(uint32_t), 222);
        if (a->rank)
            memcpy(b->u.arr.dims, a->dims, a->rank * sizeof(uint32_t));
        b->u.arr.data = xmalloc(n * esz, 222);
        if (n)
            memcpy(b->u.arr.data, a->data, n * esz);
    }
    b->next = stash_top;
    stash_top = b;
}

void ioi_retrieve(int type, int idx, void *var, int ign)
{
    struct stashbox **pp = &stash_top, *b = stash_top;
    while (b && (b->type != type || b->idx != idx)) {
        pp = &b->next;
        b = b->next;
    }
    if (!b)
        ioi_lose(436, ioi_lineno, NULL);
    *pp = b->next;
    if (type == IOI_ONESPOT) {
        if (!ign)
            *(uint16_t *)var = (uint16_t)b->u.scalar;
    } else if (type == IOI_TWOSPOT) {
        if (!ign)
            *(uint32_t *)var = b->u.scalar;
    } else {
        ioi_array *a = var;
        /* C-INTERCAL restores an array only if it is currently dimensioned. */
        if (!ign && a->rank) {
            free(a->dims);
            free(a->data);
            *a = b->u.arr;
        } else {
            free(b->u.arr.dims);
            free(b->u.arr.data);
        }
    }
    free(b);
}

/* ---- NEXT stack ---------------------------------------------------------- */

#define MAXNEXT 80
static int next_stack[MAXNEXT];
static int next_index;

void ioi_next_push(int ret)
{
    if (next_index < MAXNEXT)
        next_stack[next_index++] = ret;
    else
        ioi_lose(123, ioi_lineno, NULL);
}

static int popnext(uint32_t n)
{
    /* C-INTERCAL: ick_nextindex -= n with int/unsigned wraparound. */
    int idx = (int)((unsigned)next_index - n);
    if (idx < 0) {
        next_index = 0;
        return -1;
    }
    next_index = idx;
    return idx < MAXNEXT ? next_stack[idx] : 0;
}

int ioi_resume(uint32_t n)
{
    if (n == 0)
        ioi_lose(621, ioi_lineno, NULL);
    int r = popnext(n);
    if (r == -1)
        ioi_lose(632, ioi_lineno, NULL);
    return r;
}

void ioi_forget(uint32_t n) { (void)popnext(n); }

int ioi_roll(int percent) { return rand() % 100 < percent; }

/* ---- READ OUT / WRITE IN -------------------------------------------------- */

void ioi_roman(uint32_t val, char *result)
{
    static const int trans[10][5] = {
        {0, 0, 0, 0, 0}, {1, 0, 0, 0, 0}, {2, 0, 0, 0, 0}, {3, 0, 0, 0, 0},
        {2, 1, 2, 0, 0}, {1, 2, 0, 0, 0}, {2, 2, 1, 0, 0}, {3, 2, 1, 1, 0},
        {4, 2, 1, 1, 1}, {2, 1, 3, 0, 0}};
    static const char equiv[10][4] = {
        {'I', 'I', 'V', 'X'}, {'X', 'X', 'L', 'C'}, {'C', 'C', 'D', 'M'},
        {'M', 'I', 'V', 'X'}, {'X', 'X', 'L', 'C'}, {'C', 'C', 'D', 'M'},
        {'M', 'i', 'v', 'x'}, {'x', 'x', 'l', 'c'}, {'c', 'c', 'd', 'm'},
        {'m', 'i', 'v', 'x'}};
    static const char overbar[10][4] = {
        {' ', ' ', ' ', ' '}, {' ', ' ', ' ', ' '}, {' ', ' ', ' ', ' '},
        {' ', '_', '_', '_'}, {'_', '_', '_', '_'}, {'_', '_', '_', '_'},
        {'_', ' ', ' ', ' '}, {' ', ' ', ' ', ' '}, {' ', ' ', ' ', ' '},
        {' ', '_', '_', '_'}};
    enum { MAXR = 41 };
    char res[MAXR], ovb[MAXR];
    if (val == 0) {
        strcpy(result, "_\n");
        return;
    }
    int i = MAXR - 1;
    res[i] = ovb[i] = '\0';
    for (int sig = 0; sig < 10 && val > 0; sig++) {
        int d = (int)(val % 10);
        for (int j = trans[d][0]; j > 0; j--) {
            res[--i] = equiv[sig][trans[d][j]];
            ovb[i] = overbar[sig][trans[d][j]];
        }
        val /= 10;
    }
    size_t l = strlen(ovb + i);
    memcpy(result, ovb + i, l);
    result[l] = '\n';
    strcpy(result + l + 1, res + i);
}

void ioi_read_out(uint32_t v)
{
    char buf[96];
    ioi_roman(v, buf);
    fprintf(stdout, "%s\n", buf);
    fflush(stdout);
}

void ioi_read_out_array(ioi_array *a, int wide)
{
    static unsigned lastout;
    if (a->rank != 1)
        ioi_lose(241, ioi_lineno, NULL);
    for (uint32_t i = 0; i < a->dims[0]; i++) {
        unsigned c = lastout - (wide ? ((uint32_t *)a->data)[i] : ((uint16_t *)a->data)[i]);
        lastout = c;
        c = (c & 0x0f) << 4 | (c & 0xf0) >> 4;
        c = (c & 0x33) << 2 | (c & 0xcc) >> 2;
        c = (c & 0x55) << 1 | (c & 0xaa) >> 1;
        fputc((int)(c & 0xff), stdout);
        if (c == 10)
            fflush(stdout);
    }
}

void ioi_write_in_array(ioi_array *a, int wide, int ign)
{
    static int lastin;
    if (a->rank != 1)
        ioi_lose(241, ioi_lineno, NULL);
    for (uint32_t i = 0; i < a->dims[0]; i++) {
        int c = fgetc(stdin);
        int v = c == EOF ? 256 : (int)(((unsigned)c - (unsigned)lastin) % 256);
        lastin = c;
        if (!ign) {
            if (wide)
                ((uint32_t *)a->data)[i] = (uint32_t)v;
            else
                ((uint16_t *)a->data)[i] = (uint16_t)v;
        }
    }
}

static const struct {
    const char *name;
    int value;
} numerals[] = {
    {"OH", 0}, {"ZERO", 0}, {"ONE", 1}, {"TWO", 2}, {"THREE", 3}, {"FOUR", 4},
    {"FIVE", 5}, {"SIX", 6}, {"SEVEN", 7}, {"EIGHT", 8}, {"NINE", 9}, {"NINER", 9},
    {"SUTYA", 0}, {"SHUTYA", 0}, {"EKA", 1}, {"DVI", 2}, {"TRI", 3}, {"CHATUR", 4},
    {"PANCHAN", 5}, {"SHASH", 6}, {"SAPTAM", 7}, {"ASHTAN", 8}, {"NAVAN", 9},
    {"ZEROA", 0}, {"BAT", 1}, {"BI", 2}, {"HIRO", 3}, {"LAU", 4}, {"BORTZ", 5},
    {"SEI", 6}, {"ZAZPI", 7}, {"ZORTZI", 8}, {"BEDERATZI", 9},
    {"WALA", 0}, {"ISA", 1}, {"DALAWA", 2}, {"TATLO", 3}, {"APAT", 4}, {"LIMA", 5},
    {"ANIM", 6}, {"PITO", 7}, {"WALO", 8}, {"SIYAM", 9},
    {"AHTLE", 0}, {"CE", 1}, {"OME", 2}, {"IEI", 3}, {"NAUI", 4}, {"NACUILI", 5},
    {"CHIQUACE", 6}, {"CHICOME", 7}, {"CHICUE", 8}, {"CHICUNAUI", 9},
    {"NULI", 0}, {"ERTI", 1}, {"ORI", 2}, {"SAMI", 3}, {"OTXI", 4}, {"XUTI", 5},
    {"EKSVI", 6}, {"SHVIDI", 7}, {"RVA", 8}, {"CXRA", 9},
    {"KE'YOS", 0}, {"'NEM", 1}, {"MAL'H", 2}, {"YUDEXW", 3}, {"MU", 4}, {"SEK'A", 5},
    {"Q'ETL'A", 6}, {"ETLEBU", 7}, {"MALHGWENALH", 8}, {"'NA'NE'MA", 9},
    {"NOS", 0}, {"BAL", 1}, {"TEL", 2}, {"KIL", 3}, {"FOL", 4}, {"LUL", 5},
    {"M\\\"AL", 6}, {"M\xC4L", 6}, {"M\xA3\xA4L", 6}, {"VEL", 7},
    {"J\\\"OL", 8}, {"J\xD6L", 8}, {"J\xA3\x96L", 8},
    {"Z\\\"UL", 9}, {"Z\xDCL", 9}, {"Z\xA3\x9CL", 9},
    {"NIL", 0}, {"NIHIL", 0}, {"UNUS", 1}, {"UNA", 1}, {"UNUM", 1}, {"DUO", 2},
    {"DUAE", 2}, {"TRES", 3}, {"QUATTUOR", 4}, {"QUATUOR", 4}, {"QUINQUE", 5},
    {"SEX", 6}, {"SEPTEM", 7}, {"OCTO", 8}, {"NOVEM", 9},
};

uint32_t ioi_write_in(void)
{
    char buf[BUFSIZ];
    uint32_t result = 0;
    size_t n;
    if (fgets(buf, (int)sizeof buf, stdin) == NULL)
        ioi_lose(562, ioi_lineno, NULL);
    n = strlen(buf) - 1;
    if (n > 0 && buf[n - 1] == '\r')
        --n;
    buf[n] = '\0';
    n = 0;
    for (char *cp = strtok(buf, " "); cp; cp = strtok(NULL, " "), n++) {
        int digit = -1;
        for (size_t i = 0; i < sizeof numerals / sizeof *numerals; i++)
            if (strcmp(numerals[i].name, cp) == 0) {
                digit = numerals[i].value;
                break;
            }
        if (digit == -1)
            ioi_lose(579, ioi_lineno, cp);
        if (result < 429496729u || (result == 429496729u && digit < 6))
            result = result * 10 + (uint32_t)digit;
        else
            ioi_lose(533, ioi_lineno, NULL);
    }
    if (!n)
        ioi_lose(562, ioi_lineno, NULL);
    return result;
}
