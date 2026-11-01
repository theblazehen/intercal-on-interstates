/* main.c — the ick driver: options, syslib, C generation, $CC. */
#include "ick.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 65536, n = 0;
    char *buf = xmalloc(cap);
    size_t r;
    while ((r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap)
            buf = xrealloc(buf, cap *= 2);
    }
    fclose(f);
    *len = n;
    return buf;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static char *join(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *r = xmalloc(la + lb + 2);
    memcpy(r, a, la);
    r[la] = '/';
    memcpy(r + la + 1, b, lb + 1);
    return r;
}

/* Locate include/, lib/ and share/ relative to the executable: the build
 * tree keeps them beside it (build/ick -> build/include, build/lib,
 * build/share); an installed prefix has bin/ick -> include/ioi, lib/ioi,
 * share/ioi. IOI_HOME overrides (either layout). */
static void find_home(char **inc, char **lib, char **share)
{
    const char *home = getenv("IOI_HOME");
    char exe[PATH_MAX];
    char *dir;
    if (home && *home) {
        dir = xstrdup(home);
    } else {
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n <= 0)
            die("cannot locate myself (set IOI_HOME)");
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        *slash = '\0';
        dir = xstrdup(exe);
    }
    char *s1 = join(dir, "share/syslib.i");
    if (exists(s1)) {
        *inc = join(dir, "include");
        *lib = join(dir, "lib");
        *share = join(dir, "share");
    } else {
        char *base = join(dir, "..");
        if (home && *home && exists(join(dir, "share/ioi/syslib.i")))
            base = dir;
        *inc = join(base, "include/ioi");
        *lib = join(base, "lib/ioi");
        *share = join(base, "share/ioi");
    }
    free(s1);
}

static void usage(void)
{
    fprintf(stderr,
            "usage: ick [-b] [-s] [-c] [-o FILE] program.i\n"
            "  -b  disable the random compiler bug (E774)\n"
            "  -s  INTERCAL on Interstates dialect\n"
            "  -c  keep the generated C file\n"
            "  -o  name of the executable (default: source name without .i)\n");
    exit(1);
}

static int count_main_labels(struct program *p, int lo, int hi)
{
    for (int i = 0; i < p->nt; i++)
        if (p->t[i].label >= lo && p->t[i].label <= hi)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    int nobug = 0, smode = 0, keepc = 0;
    const char *outname = NULL, *src = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            if (!strcmp(a, "-o")) {
                if (++i >= argc)
                    usage();
                outname = argv[i];
                continue;
            }
            for (const char *f = a + 1; *f; f++) {
                switch (*f) {
                case 'b': nobug = 1; break;
                case 's': smode = 1; break;
                case 'c': keepc = 1; break;
                case 'O': case 'f': case 'F': case 'w': break; /* optimiser flags: no-ops */
                case 'h': usage(); break;
                default:
                    fprintf(stderr, "ick: unknown option -%c\n", *f);
                    usage();
                }
            }
        } else {
            if (src)
                usage();
            src = a;
        }
    }
    if (!src)
        usage();
    const char *dot = strrchr(src, '.');
    const char *slash = strrchr(src, '/');
    if (!dot || (slash && dot < slash))
        cerr(998, 1, NULL);
    size_t rawlen;
    char *raw = read_file(src, &rawlen);
    if (!raw)
        cerr(777, 1, NULL);
    char *stem = xstrndup(src, (size_t)(dot - src));
    char *srcdir = slash ? xstrndup(src, (size_t)(slash - src)) : xstrdup(".");
    char *inc, *lib, *share;
    find_home(&inc, &lib, &share);

    if (!nobug)
        srand((unsigned)time(NULL));

    static struct program prog;
    struct program *p = &prog;
    p->smode = smode;
    p->srcpath = src;
    p->srcdir = srcdir;
    g_smode = smode;
    g_srcname = src;
    intern(p, V_ONE, 1, 1); /* C-INTERCAL always mentions .1 */

    struct source s;
    source_load(&s, (const unsigned char *)raw, rawlen);
    if (smode) {
        parse_interstates(p, (const unsigned char *)raw, rawlen);
    } else {
        struct lexer *lx = xmalloc(sizeof *lx);
        lex_init(lx, s.stream, s.len, 0);
        parse_classic(p, lx);
    }
    p->lines = s.lines;
    p->nlines = s.nlines;
    p->main_nt = p->nt;

    /* Miss Manners lives. */
    if (p->nt > 2) {
        int n = p->nt, pol = p->politesse;
        if (pol == 0 || (n - 1) / pol >= 5)
            cerr_at(79, p->iyylineno, p->nlines, NULL);
        else if (n / pol < 3)
            cerr_at(99, p->iyylineno, p->nlines, NULL);
    }

    /* the magically included libraries, in C-INTERCAL's order (floatlib's
     * own NEXTs into syslib are seen by the second pass) */
    static const struct {
        int lo, hi;
        const char *file;
    } libs[] = {{5000, 5699, "floatlib.i"}, {1000, 1999, "syslib.i"}};
    for (size_t L = 0; L < sizeof libs / sizeof *libs; L++) {
        if (count_main_labels(p, libs[L].lo, libs[L].hi))
            continue;
        int need = 0;
        for (int i = 0; i < p->nt; i++)
            if (p->t[i].type == S_NEXT && p->t[i].target >= libs[L].lo && p->t[i].target <= libs[L].hi)
                need = 1;
        if (!need)
            continue;
        char *path = join(share, libs[L].file);
        size_t sl;
        char *sraw = read_file(path, &sl);
        if (!sraw)
            cerr(127, 1, NULL);
        struct source ss;
        source_load(&ss, (const unsigned char *)sraw, sl);
        int base = p->iyylineno - 1;
        struct lexer *lx = xmalloc(sizeof *lx);
        lex_init(lx, ss.stream, ss.len, base);
        parse_classic(p, lx);
        /* the library's lines continue the program's numbering */
        int total = base + ss.nlines;
        char **lines = xmalloc((size_t)(total + 2) * sizeof *lines);
        for (int i = 0; i <= total; i++)
            lines[i] = "";
        for (int i = 0; i <= p->nlines && i <= base; i++)
            lines[i] = p->lines[i];
        for (int i = 1; i <= ss.nlines; i++)
            lines[base + i] = ss.lines[i];
        p->lines = lines;
        p->nlines = total;
    }

    if (smode)
        for (int i = 0; i < p->nt; i++) {
            struct tuple *t = &p->t[i];
            if (t->type == S_TEMPLATE || t->type == S_REDIRECT)
                t->tpl = tpl_load(p, t->str, t->line);
        }

    check_program(p);
    if (smode)
        tpl_check_links(p);

    int bugline = -1;
    if (!nobug && rand() % 10 == 0 && p->nt > 0)
        bugline = rand() % p->nt;

    char *cfile = xmalloc(strlen(stem) + 3);
    sprintf(cfile, "%s.c", stem);
    FILE *out = fopen(cfile, "w");
    if (!out)
        cerr(888, 1, NULL);
    generate(p, out, bugline);
    if (fclose(out) != 0)
        cerr(888, 1, NULL);

    const char *cc = getenv("CC");
    if (!cc || !*cc)
        cc = "cc";
    char *libioi = join(lib, "libioi.a");
    const char *exe = outname ? outname : stem;
    size_t cmdlen = strlen(cc) + strlen(cfile) + strlen(inc) + strlen(libioi) + strlen(exe) + 128;
    char *cmd = xmalloc(cmdlen);
    snprintf(cmd, cmdlen, "%s -std=gnu11 -O1 -w -I'%s' -o '%s' '%s' '%s' -lm", cc, inc, exe, cfile, libioi);
    int rc = system(cmd);
    if (!keepc)
        remove(cfile);
    if (rc != 0) {
        /* One message, naming the cause; there is no source line to blame. */
        int status = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        fflush(stdout);
        fprintf(stderr, "ICL778I\tUNEXPLAINED COMPILER BUG: THE C COMPILER %s\n"
                        "\tON THE WAY TO %s\n        CORRECT SOURCE AND RESUBNIT\n",
                status == 127 ? "WAS NOT FOUND (SET $CC OR PATH)" : "FAILED", cmd);
        exit(778);
    }
    return 0;
}
