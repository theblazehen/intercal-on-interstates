/* util.c — allocation helpers and compile-time error reporting. */
#include "ick.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const char *g_srcname;
int g_smode;

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        cerr(345, 0, NULL);
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        cerr(345, 0, NULL);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

#define WAY "\n\tON THE WAY TO %d\n"
static const struct {
    int code;
    const char *text;
} msgs[] = {
    {17, "DO YOU EXPECT ME TO FIGURE THIS OUT?" WAY},
    {79, "PROGRAMMER IS INSUFFICIENTLY POLITE" WAY},
    {99, "PROGRAMMER IS OVERLY POLITE" WAY},
    {111, "COMMUNIST PLOT DETECTED, COMPILER IS SUICIDING" WAY},
    {127, "SAYING 'ABRACADABRA' WITHOUT A MAGIC WAND WON'T DO YOU ANY GOOD\n"
          "        ON THE WAY TO THE CLOSET\n"},
    {129, "PROGRAM HAS GOTTEN LOST\n\tON THE WAY TO WHO KNOWS WHERE\n"},
    {139, "I WASN'T PLANNING TO GO THERE ANYWAY" WAY},
    {182, "YOU MUST LIKE THIS LABEL A LOT!" WAY},
    {197, "SO!  65535 LABELS AREN'T ENOUGH FOR YOU?" WAY},
    {200, "NOTHING VENTURED, NOTHING GAINED" WAY},
    {281, "THAT MUCH QUOTATION AMOUNTS TO PLAGIARISM" WAY},
    {345, "THAT'S TOO COMPLEX FOR ME TO GRASP\n        ON THE WAY TO SOMEWHERE\n"},
    {405, "PROGRAM REJECTED FOR MENTAL HEALTH REASONS" WAY},
    {444, "IT CAME FROM BEYOND SPACE" WAY},
    {555, "FLOW DIAGRAM IS EXCESSIVELY CONNECTED" WAY},
    {666, "COMPILER HAS INDIGESTION" WAY},
    {777, "A SOURCE IS A SOURCE, OF COURSE, OF COURSE" WAY},
    {778, "UNEXPLAINED COMPILER BUG" WAY},
    {888, "I HAVE NO FILE AND I MUST SCREAM" WAY},
    {997, "ILLEGAL POSSESSION OF A CONTROLLED UNARY OPERATOR." WAY},
    {998, "EXCUSE ME,\n\tYOU MUST HAVE ME CONFUSED WITH SOME OTHER COMPILER\n"},
    {999, "NO SKELETON IN MY CLOSET, WOE IS ME!" WAY},
    /* Interstates (compiler/README.md) */
    {400, "YOU CAN ONLY COME FROM A PAGE" WAY},
    {401, "NO ROAD LEADS TO %s" WAY},
    {402, "THERE IS NO TEMPLATE CALLED %s IN THE CLOSET" WAY},
    {406, "THE TEMPLATE IS GRUMPY: %s" WAY},
    {407, "THAT ROUTE IS NOT ON ANY MAP: %s" WAY},
    {408, "A TEMPLATE MAY NOT BE ITS OWN GRANDFATHER: %s" WAY},
    {409, "YOU CANNOT WRITE THAT IN THERE" WAY},
    {413, "%s" WAY},
};
#undef WAY

_Noreturn void cerr_at(int code, int line, int srcline, const char *arg)
{
    const char *t = "%s\n\tON THE WAY TO %d\n";
    for (size_t i = 0; i < sizeof msgs / sizeof *msgs; i++)
        if (msgs[i].code == code)
            t = msgs[i].text;
    fflush(stdout);
    if (g_smode && g_srcname)
        fprintf(stderr, "%s:%d: ", g_srcname, srcline > 0 ? srcline : line);
    fprintf(stderr, "ICL%03dI\t", code);
    for (const char *p = t; *p; p++) {
        if (p[0] == '%' && p[1] == 's') {
            fputs(arg ? arg : "(null)", stderr);
            p++;
        } else if (p[0] == '%' && p[1] == 'd') {
            fprintf(stderr, "%d", line);
            p++;
        } else
            fputc(*p, stderr);
    }
    fputs("        CORRECT SOURCE AND RESUBNIT\n", stderr);
    exit(code);
}

_Noreturn void cerr(int code, int line, const char *arg) { cerr_at(code, line, line, arg); }

_Noreturn void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ick: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

/* A C string literal for arbitrary bytes. */
void emit_cstring(FILE *out, const char *s, size_t n)
{
    fputc('"', out);
    int col = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\')
            col += fprintf(out, "\\%c", c);
        else if (c == '\n')
            col += fprintf(out, "\\n");
        else if (c >= 0x20 && c < 0x7f && c != '?')
            col += fprintf(out, "%c", c);
        else
            col += fprintf(out, "\\%03o", c);
        if (col > 100 && i + 1 < n) {
            fputs("\"\n\"", out);
            col = 0;
        }
    }
    fputc('"', out);
}
