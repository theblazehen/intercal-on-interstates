/* unit.c — unit tests for the web runtime (run by tests/web/run.sh).
 * Usage: unit TMPDIR   (TMPDIR receives the scratch database) */
#include "ioi_web.h"
#include "web_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int failures, checks;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                        \
    } while (0)

#define CHECK_STR(got, want)                                                     \
    do {                                                                         \
        const char *g_ = (got), *w_ = (want);                                    \
        checks++;                                                                \
        if (strcmp(g_, w_)) {                                                    \
            failures++;                                                          \
            fprintf(stderr, "%s:%d: FAILED\n  got:  [%s]\n  want: [%s]\n",       \
                    __FILE__, __LINE__, g_, w_);                                 \
        }                                                                        \
    } while (0)

static void hexs(const uint8_t *d, size_t n, char *out) { ioi_hex(d, n, out); }

/* ---------------------------------------------------------------- */

static void test_sha256(void)
{
    char hex[129];
    uint8_t h[64];
    ioi_sha256("", 0, h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ioi_sha256("abc", 3, h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ioi_sha256(m, strlen(m), h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    struct ioi_sha256 s;
    ioi_sha256_init(&s);
    char a[1000];
    memset(a, 'a', sizeof a);
    for (int i = 0; i < 1000; i++)
        ioi_sha256_update(&s, a, sizeof a);
    ioi_sha256_final(&s, h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    /* HMAC-SHA256, RFC 4231 test cases 1, 2 and 6 */
    uint8_t key[131];
    memset(key, 0x0b, 20);
    ioi_hmac_sha256(key, 20, "Hi There", 8, h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    ioi_hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(key, 0xaa, 131);
    const char *big = "Test Using Larger Than Block-Size Key - Hash Key First";
    ioi_hmac_sha256(key, 131, big, strlen(big), h);
    hexs(h, 32, hex);
    CHECK_STR(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    /* PBKDF2-HMAC-SHA256 */
    ioi_pbkdf2_sha256("password", 8, "salt", 4, 1, h, 32);
    hexs(h, 32, hex);
    CHECK_STR(hex, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    ioi_pbkdf2_sha256("password", 8, "salt", 4, 4096, h, 32);
    hexs(h, 32, hex);
    CHECK_STR(hex, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    /* RFC 7914 §11 */
    ioi_pbkdf2_sha256("passwd", 6, "salt", 4, 1, h, 64);
    hexs(h, 64, hex);
    CHECK_STR(hex, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                   "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");

    CHECK(ioi_ct_equal("abcd", "abcd", 4));
    CHECK(!ioi_ct_equal("abcd", "abce", 4));

    char *ph = ioi_password_hash("hunter22", 8);
    CHECK(ph && !strncmp(ph, "pbkdf2-sha256$", 14));
    CHECK(ioi_password_is_hash(ph, strlen(ph)));
    CHECK(ioi_password_verify("hunter22", 8, ph, strlen(ph)));
    CHECK(!ioi_password_verify("hunter23", 8, ph, strlen(ph)));
    char *ph2 = ioi_password_hash("hunter22", 8);
    CHECK(strcmp(ph, ph2) != 0); /* random salt */
    CHECK(!ioi_password_is_hash("hunter22", 8));
    CHECK(!ioi_password_is_hash("pbkdf2-sha256$x$00$00", 21));
    free(ph);
    free(ph2);
}

/* ---------------------------------------------------------------- */

static void test_decoding(void)
{
    char s[64];
    strcpy(s, "a+b%20c%2Fd%zz%4");
    size_t n = ioi_urldecode(s, strlen(s));
    s[n] = 0;
    CHECK_STR(s, "a b c/d%zz%4");

    const char *qs = "x=1&name=Ada+Lovelace&e=&flag&enc=%3Cb%3E&name=second&k%61y=v";
    char *v;
    size_t vl;
    CHECK(ioi_form_lookup(qs, strlen(qs), "name", &v, &vl) && !strcmp(v, "Ada Lovelace"));
    free(v);
    CHECK(ioi_form_lookup(qs, strlen(qs), "e", &v, &vl) && vl == 0);
    free(v);
    CHECK(ioi_form_lookup(qs, strlen(qs), "flag", &v, &vl) && vl == 0);
    free(v);
    CHECK(ioi_form_lookup(qs, strlen(qs), "enc", &v, &vl) && !strcmp(v, "<b>"));
    free(v);
    CHECK(ioi_form_lookup(qs, strlen(qs), "kay", &v, &vl) && !strcmp(v, "v"));
    free(v);
    CHECK(!ioi_form_lookup(qs, strlen(qs), "missing", &v, &vl));

    char *c = ioi_cookie_lookup("a=1; ioi_session=v1.2.3.abc; b=\"q\"", "ioi_session");
    CHECK(c && !strcmp(c, "v1.2.3.abc"));
    free(c);
    c = ioi_cookie_lookup("a=1;b=\"q\"", "b");
    CHECK(c && !strcmp(c, "q"));
    free(c);
    CHECK(ioi_cookie_lookup("xioi_session=1", "ioi_session") == NULL);

    uint32_t u;
    CHECK(ioi_web_parse_number((const uint8_t *)"ONE TWO", 7, &u) && u == 12);
    CHECK(ioi_web_parse_number((const uint8_t *)"oh niner", 8, &u) && u == 9);
    CHECK(ioi_web_parse_number((const uint8_t *)" 42 ", 4, &u) && u == 42);
    CHECK(ioi_web_parse_number((const uint8_t *)"DUO TRES", 8, &u) && u == 23);
    const char *max = "FOUR TWO NINE FOUR NINE SIX SEVEN TWO NINE FIVE";
    CHECK(ioi_web_parse_number((const uint8_t *)max, strlen(max), &u) && u == 4294967295u);
    CHECK(ioi_web_parse_number((const uint8_t *)"4294967295", 10, &u) && u == 4294967295u);
    CHECK(!ioi_web_parse_number((const uint8_t *)"4294967296", 10, &u) && u == 0);
    CHECK(!ioi_web_parse_number((const uint8_t *)"ONE TOO", 7, &u));
    CHECK(!ioi_web_parse_number((const uint8_t *)"12a", 3, &u));
    CHECK(!ioi_web_parse_number((const uint8_t *)"", 0, &u));
    CHECK(!ioi_web_parse_number((const uint8_t *)"-1", 2, &u));
}

/* ---------------------------------------------------------------- */

static char *param_str(uint32_t list)
{
    static char out[256];
    out[0] = 0;
    for (size_t i = 0; i < ioi_cas_len(list); i++) {
        size_t n;
        const uint8_t *d = ioi_cas_data(ioi_cas_get(list, i), &n);
        if (i)
            strcat(out, "|");
        strncat(out, (const char *)d, n);
    }
    return out;
}

static void test_routes(void)
{
    static const struct {
        const char *pattern, *method, *path;
        int want;
        const char *params;
    } cases[] = {
        {"/", "GET", "/", 1, ""},
        {"/", "HEAD", "/", 1, ""},
        {"/", "POST", "/", 0, ""},
        {"/home", "GET", "/home", 1, ""},
        {"/home", "GET", "/home/", 1, ""},
        {"/home", "GET", "/homer", 0, ""},
        {"/home", "GET", "/", 0, ""},
        {"/blog/post/:id", "GET", "/blog/post/1", 1, "1"},
        {"/blog/post/:id", "GET", "/blog/post", 0, ""},
        {"/blog/post/:id", "GET", "/blog/post/1/x", 0, ""},
        {"POST /t/:id/reply", "POST", "/t/42/reply", 1, "42"},
        {"POST /t/:id/reply", "GET", "/t/42/reply", 0, ""},
        {"POST /t/:id/reply", "HEAD", "/t/42/reply", 0, ""},
        {"DELETE /p/:a/:b", "DELETE", "/p/x y/z", 1, "x y|z"},
        {"/files/*", "GET", "/files/a/b/c.txt", 1, "a/b/c.txt"},
        {"/files/*", "GET", "/files", 1, ""},
        {"/files/:dir/*", "GET", "/files/d/e", 1, "d|e"},
        {"*", "GET", "/anything/at/all", 1, ""},
        {"*", "POST", "/x", 1, ""},
        {"POST *", "GET", "/x", 0, ""},
        {"POST *", "POST", "/x", 1, ""},
        {"/a/*/b", "GET", "/a/x/b", -1, ""},
        {"/a/:", "GET", "/a/x", -1, ""},
        {"get /x", "GET", "/x", -1, ""},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t params = 0;
        int got = ioi_web_match(cases[i].pattern, cases[i].method, cases[i].path, &params, NULL);
        checks++;
        if (got != cases[i].want || (got == 1 && strcmp(param_str(params), cases[i].params))) {
            failures++;
            fprintf(stderr, "route %s %s %s: got %d [%s], want %d [%s]\n", cases[i].pattern,
                    cases[i].method, cases[i].path, got, got == 1 ? param_str(params) : "",
                    cases[i].want, cases[i].params);
        }
    }
    uint32_t names;
    ioi_web_match("/x/:a/:bee/*", "GET", "/x/1/2/3", NULL, &names);
    CHECK_STR(param_str(names), "a|bee|*");

    CHECK(ioi_same_origin_check("example.org:10585", "http://example.org:10585", NULL));
    CHECK(ioi_same_origin_check("Example.org", "http://example.org:80", NULL));
    CHECK(ioi_same_origin_check("example.org", NULL, "https://example.org/t/1?x=2"));
    CHECK(ioi_same_origin_check("example.org:443", "https://example.org", NULL));
    CHECK(!ioi_same_origin_check("example.org", "http://evil.org", NULL));
    CHECK(!ioi_same_origin_check("example.org", "http://example.org.evil.org", NULL));
    CHECK(!ioi_same_origin_check("example.org:10585", "http://example.org:10586", NULL));
    CHECK(!ioi_same_origin_check("example.org", "null", "http://example.org/"));
    CHECK(!ioi_same_origin_check("example.org", NULL, NULL));
    CHECK(!ioi_same_origin_check("example.org", "", ""));
    CHECK(!ioi_same_origin_check("example.org", NULL, "javascript://example.org/"));
    CHECK(!ioi_same_origin_check("example.org", NULL, "http://evil.org/@example.org"));
    CHECK(!ioi_same_origin_check(NULL, "http://example.org", NULL));

    CHECK(ioi_host_allowed("example.org", "example.org"));
    CHECK(ioi_host_allowed("example.org", "EXAMPLE.org:8080"));
    CHECK(ioi_host_allowed("a.test, example.org:443", "example.org:443"));
    CHECK(!ioi_host_allowed("example.org:443", "example.org:80"));
    CHECK(!ioi_host_allowed("example.org", "example.org.evil.test"));
    CHECK(!ioi_host_allowed("example.org", "evil.test"));
    CHECK(!ioi_host_allowed("example.org", ""));
    CHECK(!ioi_host_allowed("example.org", NULL));
    CHECK(ioi_host_allowed("[::1]", "[::1]:10585"));
}

/* ---------------------------------------------------------------- */

static void test_session(void)
{
    const char *id = "0123456789abcdef0123456789abcdef";
    char got[33];
    char *c = ioi_session_sign(id, "s3cret");
    CHECK(strlen(c) == 97 && c[32] == '.');
    CHECK(ioi_session_check(c, "s3cret", got) && !strcmp(got, id));
    CHECK(!ioi_session_check(c, "other", got));
    CHECK(!ioi_session_check(c, "", got));
    char *t = strdup(c);
    t[0] = '1'; /* another id, same MAC */
    CHECK(!ioi_session_check(t, "s3cret", got));
    strcpy(t, c);
    t[strlen(t) - 1] ^= 1;
    CHECK(!ioi_session_check(t, "s3cret", got));
    CHECK(!ioi_session_check("v1.7.1000000.abc", "s3cret", got));
    CHECK(!ioi_session_check(id, "s3cret", got));
    free(t);
    free(c);

    const char *d = ioi_password_dummy();
    CHECK(ioi_password_is_hash(d, strlen(d)));
    CHECK(!ioi_password_verify("", 0, d, strlen(d)));
    CHECK(!ioi_password_verify("00000000", 8, d, strlen(d)));
}

/* Server-side sessions against the test database (run after test_db). */
static char cookie_from_header[256];

static void capture_cookie(void)
{
    const char *h = ioi_session_header();
    cookie_from_header[0] = 0;
    if (h) {
        size_t n = strcspn(h, ";");
        snprintf(cookie_from_header, sizeof cookie_from_header, "%.*s", (int)n, h);
    }
}

static void test_session_store(void)
{
    setenv("IOI_SECRET", "unit-secret", 1);
    unsetenv("HTTP_COOKIE");
    ioi_web_reset();
    CHECK(ioi_web_session_get() == 0);
    CHECK(ioi_web_session_set(7) == 0 && ioi_web_session_get() == 7);
    capture_cookie();
    CHECK(!strncmp(cookie_from_header, "ioi_session=", 12));
    CHECK(strstr(ioi_session_header(), "; Path=/; HttpOnly; SameSite=Lax; Max-Age=2592000") != NULL);
    char first[256];
    strcpy(first, cookie_from_header);

    /* the next request presents the cookie */
    ioi_web_reset();
    setenv("HTTP_COOKIE", first, 1);
    CHECK(ioi_web_session_get() == 7);
    /* a new value issues a new id; the old cookie stops working */
    CHECK(ioi_web_session_set(9) == 0);
    capture_cookie();
    char second[256];
    strcpy(second, cookie_from_header);
    CHECK(strcmp(first, second) != 0);
    ioi_web_reset();
    CHECK(ioi_web_session_get() == 0); /* still the first cookie */
    ioi_web_reset();
    setenv("HTTP_COOKIE", second, 1);
    CHECK(ioi_web_session_get() == 9);
    /* logout deletes the row: replaying the cookie gets nothing */
    CHECK(ioi_web_session_set(0) == 0 && ioi_web_session_get() == 0);
    CHECK(strstr(ioi_session_header(), "ioi_session=; ") && strstr(ioi_session_header(), "Max-Age=0"));
    ioi_web_reset();
    CHECK(ioi_web_session_get() == 0);

    /* expiry: IOI_NOW past the 30 days */
    ioi_web_reset();
    unsetenv("HTTP_COOKIE");
    setenv("IOI_NOW", "1800000000", 1);
    CHECK(ioi_web_session_set(5) == 0);
    capture_cookie();
    ioi_web_reset();
    setenv("HTTP_COOKIE", cookie_from_header, 1);
    setenv("IOI_NOW", "1802591999", 1);
    CHECK(ioi_web_session_get() == 5);
    ioi_web_reset();
    setenv("IOI_NOW", "1802592000", 1);
    CHECK(ioi_web_session_get() == 0);
    unsetenv("IOI_NOW");

    /* Secure cookies use the __Host- prefix and ignore the plain name */
    ioi_web_reset();
    unsetenv("HTTP_COOKIE");
    setenv("IOI_SECURE_COOKIES", "1", 1);
    CHECK(ioi_web_session_set(11) == 0);
    CHECK(!strncmp(ioi_session_header(), "__Host-ioi_session=", 19));
    CHECK(strstr(ioi_session_header(), "; Secure") != NULL);
    capture_cookie();
    ioi_web_reset();
    setenv("HTTP_COOKIE", cookie_from_header, 1);
    CHECK(ioi_web_session_get() == 11);
    ioi_web_reset();
    setenv("HTTP_COOKIE", cookie_from_header + 7, 1); /* "ioi_session=..." */
    CHECK(ioi_web_session_get() == 0);
    unsetenv("IOI_SECURE_COOKIES");

    /* no secret: nothing is stored */
    ioi_web_reset();
    unsetenv("IOI_SECRET");
    CHECK(ioi_web_session_set(3) == -1);
    ioi_web_reset();

    /* the framework table exists but ActiveCassette numbering ignores it */
    struct ioi_db_query q = {.table = 3, .shape = IOI_DB_HOW_MANY};
    uint32_t v;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_E_TABLE);
}

/* ---------------------------------------------------------------- */

/* Every '<' in prose output must open a tag from the allow-list, with only
 * allowed attributes, and every href must be http(s) or scheme-less. */
static int html_is_safe(const char *h, const char **why)
{
    static const char *tags[] = {"p", "br", "strong", "em", "code", "pre", "a", "ul", "ol",
                                 "li", "blockquote", "cite", "h3", "h4", "h5", "h6", NULL};
    for (const char *p = h; (p = strchr(p, '<')); p++) {
        const char *q = p + 1;
        if (*q == '/')
            q++;
        size_t n = 0;
        while (q[n] && ((q[n] >= 'a' && q[n] <= 'z') || (q[n] >= '0' && q[n] <= '9')))
            n++;
        int ok = 0;
        for (int i = 0; tags[i]; i++)
            if (strlen(tags[i]) == n && !strncmp(q, tags[i], n))
                ok = 1;
        if (!ok) {
            *why = p;
            return 0;
        }
        const char *end = strchr(q, '>');
        if (!end) {
            *why = p;
            return 0;
        }
        /* attributes */
        for (const char *a = q + n; a < end;) {
            while (a < end && *a == ' ')
                a++;
            if (a >= end)
                break;
            const char *eq = strchr(a, '=');
            if (!eq || eq > end || eq[1] != '"') {
                *why = a;
                return 0;
            }
            size_t an = (size_t)(eq - a);
            if (!((an == 4 && !strncmp(a, "href", 4)) || (an == 3 && !strncmp(a, "rel", 3)) ||
                  (an == 5 && !strncmp(a, "class", 5)) || (an == 5 && !strncmp(a, "start", 5)))) {
                *why = a;
                return 0;
            }
            const char *vs = eq + 2, *ve = strchr(vs, '"');
            if (!ve || ve > end) {
                *why = a;
                return 0;
            }
            if (an == 4) {
                /* a scheme is what precedes the first ':' unless a '/', '?'
                 * or '#' (after entity decoding: '&' starts "&amp;") comes first */
                size_t k = strcspn(vs, ":/?#&\"");
                if (vs[k] == ':' && strncasecmp(vs, "http:", 5) && strncasecmp(vs, "https:", 6)) {
                    *why = vs;
                    return 0;
                }
            }
            a = ve + 1;
        }
    }
    return 1;
}

static char *prose(const char *src)
{
    static struct ioi_buf b;
    b.len = 0;
    ioi_prose_render(&b, src, strlen(src));
    if (!b.p)
        ioi_buf_put(&b, "", 0);
    const char *why;
    checks++;
    if (!html_is_safe(b.p, &why)) {
        failures++;
        fprintf(stderr, "UNSAFE prose for [%s]:\n  %s\n  at: %.40s\n", src, b.p, why);
    }
    return b.p;
}

static void test_prose(void)
{
    CHECK_STR(prose(""), "");
    CHECK_STR(prose("hello"), "<p>hello</p>\n");
    CHECK_STR(prose("one\ntwo\n\nthree"), "<p>one<br>\ntwo</p>\n<p>three</p>\n");
    CHECK_STR(prose("a\r\nb"), "<p>a<br>\nb</p>\n");
    CHECK_STR(prose("**bold** and *it* and ***both***"),
              "<p><strong>bold</strong> and <em>it</em> and <strong><em>both</em></strong></p>\n");
    CHECK_STR(prose("2 * 3 * 4"), "<p>2 * 3 * 4</p>\n");
    CHECK_STR(prose("snake_case_name"), "<p>snake_case_name</p>\n");
    CHECK_STR(prose("use `a < b` here"), "<p>use <code>a &lt; b</code> here</p>\n");
    CHECK_STR(prose("``a ` b``"), "<p><code>a ` b</code></p>\n");
    CHECK_STR(prose("`**not bold**`"), "<p><code>**not bold**</code></p>\n");
    CHECK_STR(prose("\\*literal\\*"), "<p>*literal*</p>\n");
    CHECK_STR(prose("```c\nint x = 1 < 2;\n  **y**\n```\nafter"),
              "<pre><code class=\"language-c\">int x = 1 &lt; 2;\n  **y**\n</code></pre>\n"
              "<p>after</p>\n");
    CHECK_STR(prose("```\nunterminated"), "<pre><code>unterminated\n</code></pre>\n");
    CHECK_STR(prose("[site](https://example.org/a?b=1&c=2)"),
              "<p><a href=\"https://example.org/a?b=1&amp;c=2\" rel=\"nofollow ugc\">site</a></p>\n");
    CHECK_STR(prose("[topic](/t/1#p2)"),
              "<p><a href=\"/t/1#p2\" rel=\"nofollow ugc\">topic</a></p>\n");
    CHECK_STR(prose("see https://example.org/x_(y). ok"),
              "<p>see <a href=\"https://example.org/x_(y)\" rel=\"nofollow ugc\">"
              "https://example.org/x_(y)</a>. ok</p>\n");
    CHECK_STR(prose("(https://example.org/z)"),
              "<p>(<a href=\"https://example.org/z\" rel=\"nofollow ugc\">https://example.org/z</a>)</p>\n");
    CHECK_STR(prose("<https://example.org>"),
              "<p><a href=\"https://example.org\" rel=\"nofollow ugc\">https://example.org</a></p>\n");
    CHECK_STR(prose("hi @ada_l and mail a@b.c"),
              "<p>hi <a class=\"mention\" href=\"/u/ada_l\">@ada_l</a> and mail a@b.c</p>\n");
    CHECK_STR(prose("# Title\n## Sub ##\n####### seven"),
              "<h3>Title</h3>\n<h4>Sub</h4>\n<p>####### seven</p>\n");
    CHECK_STR(prose("- a\n- b *c*\n- d"), "<ul>\n<li>a\n</li>\n<li>b <em>c</em>\n</li>\n<li>d\n</li>\n</ul>\n");
    CHECK_STR(prose("1. one\n2. two"), "<ol>\n<li>one\n</li>\n<li>two\n</li>\n</ol>\n");
    CHECK_STR(prose("3. three\n4. four"),
              "<ol start=\"3\">\n<li>three\n</li>\n<li>four\n</li>\n</ol>\n");
    CHECK_STR(prose("- a\n  - nested\n- b"),
              "<ul>\n<li>a\n<ul>\n<li>nested\n</li>\n</ul>\n</li>\n<li>b\n</li>\n</ul>\n");
    CHECK_STR(prose("- a\n\n- b"), "<ul>\n<li><p>a</p>\n</li>\n<li><p>b</p>\n</li>\n</ul>\n");
    CHECK_STR(prose("Things:\n- x\nDone."), "<p>Things:</p>\n<ul>\n<li>x\n</li>\n</ul>\n<p>Done.</p>\n");
    CHECK_STR(prose("> quoted\n> more\n\nafter"),
              "<blockquote>\n<p>quoted<br>\nmore</p>\n</blockquote>\n<p>after</p>\n");
    CHECK_STR(prose("[quote=\"ada\"]hello[/quote]"),
              "<blockquote class=\"bbquote\"><cite><a href=\"/u/ada\">ada</a> wrote:</cite>\n"
              "<p>hello</p>\n</blockquote>\n");
    CHECK_STR(prose("[quote]\n[quote=bob]inner[/quote]\nouter\n[/quote]\nreply"),
              "<blockquote class=\"bbquote\">\n<blockquote class=\"bbquote\"><cite><a href=\"/u/bob\">"
              "bob</a> wrote:</cite>\n<p>inner</p>\n</blockquote>\n<p>outer</p>\n</blockquote>\n"
              "<p>reply</p>\n");
    CHECK_STR(prose("[quote=\"<b>x</b>\"]q[/quote]"),
              "<blockquote class=\"bbquote\"><cite>&lt;b&gt;x&lt;/b&gt; wrote:</cite>\n<p>q</p>\n"
              "</blockquote>\n");
    CHECK_STR(prose("[quote]never closed"), "<p>[quote]<br>\nnever closed</p>\n");
    CHECK_STR(prose("```\n[quote]inside code[/quote]\n```"),
              "<pre><code>[quote]inside code[/quote]\n</code></pre>\n");

    /* hostile input */
    static const char *xss[] = {
        "<script>alert(1)</script>",
        "<img src=x onerror=alert(1)>",
        "[click](javascript:alert(1))",
        "[click](JaVaScRiPt:alert(1))",
        "[click](java\tscript:alert(1))",
        "[click](data:text/html,<script>alert(1)</script>)",
        "[click](vbscript:msgbox)",
        "[x](http://a.com/\" onmouseover=\"alert(1))",
        "[x](/ok\"><script>alert(1)</script>)",
        "<javascript:alert(1)>",
        "javascript:alert(1)",
        "https://example.org/\"><svg/onload=alert(1)>",
        "**<iframe src=//evil>**",
        "`</code><script>x</script>`",
        "```\"><script>\n</code></pre><script>alert(1)</script>\n```",
        "[quote=\"\\\"><script>alert(1)</script>\"]x[/quote]",
        "[quote=x onmouseover=alert(1)]y[/quote]",
        "@<script>",
        "# <h1 onclick=x>",
        "- <a href=javascript:x>y</a>",
        "> <style>body{}</style>",
        "&lt;script&gt; &#60;script&#62;",
        "[a](&#106;avascript:alert(1))",
        "[a](//evil.example/x)",
        "\x01\x02<\x00script>",
    };
    for (size_t i = 0; i < sizeof xss / sizeof xss[0]; i++) {
        const char *h = prose(xss[i]);
        CHECK(!strstr(h, "<script") && !strstr(h, "<img") && !strstr(h, "<iframe") &&
              !strstr(h, "<svg") && !strstr(h, "<style") && !strcasestr(h, "href=\"javascript") &&
              !strcasestr(h, "href=\"data") && !strcasestr(h, "href=\"vbscript") &&
              !strstr(h, "onmouseover=\"") && !strstr(h, "<h1"));
    }
    CHECK_STR(prose("<b>hi</b> & \"q\" 'a'"),
              "<p>&lt;b&gt;hi&lt;/b&gt; &amp; &quot;q&quot; &#39;a&#39;</p>\n");

    /* pathological input stays fast and bounded */
    struct ioi_buf big = {0};
    for (int i = 0; i < 5000; i++)
        ioi_buf_puts(&big, "**[`*");
    for (int i = 0; i < 200; i++)
        ioi_buf_puts(&big, "\n> - [quote]");
    struct ioi_buf out = {0};
    ioi_prose_render(&out, big.p, big.len);
    const char *why;
    CHECK(out.len > 0 && html_is_safe(out.p, &why));
    free(big.p);
    free(out.p);

    /* unmatched [quote] openers are matched in one pass, not per line */
    struct ioi_buf q = {0}, qo = {0};
    for (int i = 0; i < 40000; i++)
        ioi_buf_puts(&q, "[quote]");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ioi_prose_render(&qo, q.p, q.len);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    checks++;
    if (secs > 1.0) {
        failures++;
        fprintf(stderr, "280 KB of [quote] took %.2f s\n", secs);
    }
    free(q.p);
    free(qo.p);
}

/* ---------------------------------------------------------------- */

static void test_render_helpers(void)
{
    struct ioi_buf b = {0};
    ioi_buf_escape(&b, "<a href=\"x\">'&'</a>", 19);
    CHECK_STR(b.p, "&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;");

    int64_t w;
    CHECK(ioi_parse_when("2026-11-01 10:00:00", 19, &w) && w == 1793527200);
    CHECK(ioi_parse_when("2026-11-01T10:00:00Z", 20, &w) && w == 1793527200);
    CHECK(ioi_parse_when("1793527200", 10, &w) && w == 1793527200);
    CHECK(ioi_parse_when("1970-01-01", 10, &w) && w == 0);
    CHECK(!ioi_parse_when("yesterday", 9, &w));
    CHECK(!ioi_parse_when("2026-13-01 00:00:00", 19, &w));

    static const struct { int64_t d; const char *want; } ago[] = {
        {0, "just now"},          {59, "just now"},        {60, "1 minute ago"},
        {150, "2 minutes ago"},   {3600, "1 hour ago"},    {86400 * 3, "3 days ago"},
        {86400 * 45, "1 month ago"}, {86400 * 800, "2 years ago"}, {-7200, "in 2 hours"},
    };
    for (size_t i = 0; i < sizeof ago / sizeof ago[0]; i++) {
        b.len = 0;
        ioi_ago_render(&b, 1793527200 - ago[i].d, 1793527200);
        CHECK_STR(b.p, ago[i].want);
    }

    struct ioi_buf f1 = {0}, f2 = {0}, f3 = {0};
    ioi_face_render(&f1, "Ada", 3);
    ioi_face_render(&f2, "ada", 3);
    ioi_face_render(&f3, "bob", 3);
    CHECK(strcmp(f1.p, f2.p) != 0); /* titles keep the case ... */
    CHECK(strstr(f1.p, "<title>Ada</title>") && strstr(f2.p, "<title>ada</title>"));
    /* ... but the picture is the same */
    CHECK(!strcmp(strstr(f1.p, "</title>"), strstr(f2.p, "</title>")));
    CHECK(strcmp(strstr(f1.p, "</title>"), strstr(f3.p, "</title>")));
    CHECK(!strncmp(f1.p, "<svg class=\"ick-face\"", 21) && strstr(f1.p, "role=\"img\""));
    CHECK(f1.len < 1200);
    struct ioi_buf f4 = {0};
    ioi_face_render(&f4, "<x>", 3);
    CHECK(strstr(f4.p, "<title>&lt;x&gt;</title>") != NULL);
    free(b.p);
    free(f1.p);
    free(f2.p);
    free(f3.p);
    free(f4.p);
}

/* ---------------------------------------------------------------- */

static const char *bytes(uint32_t v)
{
    static char out[512];
    size_t n;
    const uint8_t *d = ioi_cas_data(v, &n);
    if (!d)
        return "<not bytes>";
    snprintf(out, sizeof out, "%.*s", (int)n, (const char *)d);
    return out;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static void test_db(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/schema.sql", dir);
    write_file(path,
               "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT COLLATE NOCASE UNIQUE,"
               " password PASSWORD, bio TEXT);\n"
               "CREATE INDEX users_name ON users(name);\n"
               "CREATE TABLE posts (id INTEGER PRIMARY KEY, user_id INTEGER REFERENCES users(id),"
               " title TEXT, score INTEGER, created TEXT);\n"
               "CREATE VIEW post_counts AS SELECT users.id AS id, users.name AS name,"
               " count(posts.id) AS n FROM users LEFT JOIN posts ON posts.user_id = users.id"
               " GROUP BY users.id;\n");
    snprintf(path, sizeof path, "%s/seed.sql", dir);
    write_file(path,
               "INSERT INTO users (name, password, bio) VALUES ('ada', 'correct horse', NULL),"
               " ('bob', 'tr0ub4dor&3', 'I like *INTERCAL*');\n"
               "INSERT INTO posts (user_id, title, score, created) VALUES"
               " (1, '100% pure_fun', 5, '2026-01-01 00:00:00'),"
               " (1, 'COME FROM considered helpful', 9, '2026-02-01 00:00:00'),"
               " (2, 'ABSTAIN FROM sleep', 2, '2026-03-01 00:00:00'),"
               " (2, '100 percent', 7, '2026-04-01 00:00:00');\n");
    snprintf(path, sizeof path, "%s/test.db", dir);
    setenv("IOI_DATABASE", path, 1);
    setenv("IOI_APP_DIR", dir, 1);

    uint32_t v, n;
    /* PK lookup: columns 1..N */
    struct ioi_db_query q = {.table = 0, .shape = IOI_DB_PK, .pk = 2};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK);
    CHECK(ioi_cas_len(v) == 3);
    CHECK_STR(bytes(ioi_cas_get(v, 0)), "bob");
    CHECK(!strncmp(bytes(ioi_cas_get(v, 1)), "pbkdf2-sha256$", 14)); /* seed got hashed */
    CHECK_STR(bytes(ioi_cas_get(v, 2)), "I like *INTERCAL*");
    q.pk = ioi_cas_cstr("1"); /* route params arrive as BYTES */
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_cas_len(v) == 3);
    CHECK_STR(bytes(ioi_cas_get(v, 2)), ""); /* NULL -> empty BYTES */
    q.pk = 99;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_val_kind(v) == IOI_LIST && ioi_cas_len(v) == 0);

    /* table numbering: users=0, posts=1 (index skipped), post_counts=2 */
    q = (struct ioi_db_query){.table = 1, .shape = IOI_DB_PK, .pk = 2};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK);
    CHECK(ioi_cas_get(v, 0) == 1 && ioi_cas_get(v, 2) == 9);
    CHECK_STR(bytes(ioi_cas_get(v, 1)), "COME FROM considered helpful");

    /* HAVING equality, case-insensitive via COLLATE NOCASE */
    unsigned c0[] = {0};
    struct ioi_db_cond byname = {1, IOI_DB_EQ, ioi_cas_cstr("BOB")};
    q = (struct ioi_db_query){.table = 0, .cols = c0, .ncols = 1, .conds = &byname, .nconds = 1};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 2);

    /* PASSWORD verification in HAVING */
    struct ioi_db_cond login[] = {{1, IOI_DB_EQ, ioi_cas_cstr("ada")},
                                  {2, IOI_DB_EQ, ioi_cas_cstr("correct horse")}};
    q.conds = login;
    q.nconds = 2;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    login[1].value = ioi_cas_cstr("wrong horse");
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_val_kind(v) == IOI_LIST && ioi_cas_len(v) == 0);
    q.shape = IOI_DB_HOW_MANY;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 0);
    login[1].value = ioi_cas_cstr("correct horse");
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    struct ioi_db_cond bad = {2, IOI_DB_SOMEWHERE, ioi_cas_cstr("x")};
    q.conds = &bad;
    q.nconds = 1;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_E_VALUE);

    /* SOMEWHERE: case-insensitive substring, % and _ are literal */
    unsigned ctitle[] = {2};
    struct ioi_db_cond pct = {2, IOI_DB_SOMEWHERE, ioi_cas_cstr("100%")};
    q = (struct ioi_db_query){.table = 1, .cols = ctitle, .ncols = 1, .conds = &pct, .nconds = 1,
                              .shape = IOI_DB_HOW_MANY};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    pct.value = ioi_cas_cstr("e_f");
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    pct.value = ioi_cas_cstr("E_F");
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    pct.value = ioi_cas_cstr("e%f");
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 0);
    pct.value = ioi_cas_cstr("from");
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 2);

    /* BEYOND, BY, BACKWARDS, SKIP, TAKE, IN BULK */
    unsigned cts[] = {2, 3};
    struct ioi_db_cond beyond = {3, IOI_DB_BEYOND, 4};
    struct ioi_db_order bys[] = {{3, 1}};
    q = (struct ioi_db_query){.table = 1, .cols = cts, .ncols = 2, .conds = &beyond, .nconds = 1,
                              .order = bys, .norder = 1, .shape = IOI_DB_BULK};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_cas_len(v) == 3);
    CHECK(ioi_cas_get(ioi_cas_get(v, 0), 1) == 9 && ioi_cas_get(ioi_cas_get(v, 2), 1) == 5);
    q.skip = 1;
    q.take = 1;
    q.has_take = 1;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_cas_len(v) == 1);
    CHECK_STR(bytes(ioi_cas_get(ioi_cas_get(v, 0), 0)), "100 percent");
    q.shape = IOI_DB_ROW;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_cas_len(v) == 2 && ioi_cas_get(v, 1) == 7);
    q.ncols = 1;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && !strcmp(bytes(v), "100 percent"));
    q.shape = IOI_DB_HOW_MANY;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    q.has_take = 0;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 2);

    /* views count as tables; a numeric string matches an aggregate column */
    unsigned cn[] = {2};
    struct ioi_db_cond n2 = {2, IOI_DB_EQ, ioi_cas_cstr("2")};
    q = (struct ioi_db_query){.table = 2, .cols = cn, .ncols = 1, .conds = &n2, .nconds = 1,
                              .shape = IOI_DB_HOW_MANY};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 2);

    /* insert, with a PASSWORD column hashed on the way in */
    uint32_t row[] = {ioi_cas_cstr("cy"), ioi_cas_cstr("pa55word!"), ioi_cas_cstr("new here")};
    uint32_t id;
    CHECK(ioi_db_insert(0, row, 3, &id) == IOI_DB_OK && id == 3);
    struct ioi_db_cond cylogin[] = {{1, IOI_DB_EQ, ioi_cas_cstr("cy")},
                                    {2, IOI_DB_EQ, ioi_cas_cstr("pa55word!")}};
    q = (struct ioi_db_query){.table = 0, .cols = c0, .ncols = 1, .conds = cylogin, .nconds = 2};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 3);
    q = (struct ioi_db_query){.table = 0, .shape = IOI_DB_PK, .pk = 3};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK);
    CHECK(strcmp(bytes(ioi_cas_get(v, 1)), "pa55word!") != 0);
    uint32_t dup[] = {ioi_cas_cstr("CY")};
    CHECK(ioi_db_insert(0, dup, 1, &id) == IOI_DB_E_SQL); /* UNIQUE NOCASE */
    CHECK(strstr(ioi_db_errmsg(), "UNIQUE") != NULL);
    uint32_t toomany[] = {1, 2, 3, 4};
    CHECK(ioi_db_insert(0, toomany, 4, &id) == IOI_DB_E_COLUMN);
    uint32_t post[] = {3, ioi_cas_cstr("<b>hello</b>"), 1};
    CHECK(ioi_db_insert(1, post, 3, &id) == IOI_DB_OK && id == 5);
    uint32_t badfk[] = {77, ioi_cas_cstr("orphan")};
    CHECK(ioi_db_insert(1, badfk, 2, &id) == IOI_DB_E_SQL); /* foreign keys are on */

    /* update one column / several columns */
    unsigned cscore[] = {3};
    struct ioi_db_cond pid5 = {0, IOI_DB_EQ, 5};
    q = (struct ioi_db_query){.table = 1, .cols = cscore, .ncols = 1, .conds = &pid5, .nconds = 1};
    CHECK(ioi_db_update(&q, 42, &n) == IOI_DB_OK && n == 1);
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 42);
    uint32_t pair = ioi_cas_list();
    ioi_cas_push(pair, ioi_cas_cstr("edited"));
    ioi_cas_push(pair, 43);
    unsigned ct2[] = {2, 3};
    q.cols = ct2;
    q.ncols = 2;
    CHECK(ioi_db_update(&q, pair, &n) == IOI_DB_OK && n == 1);
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && !strcmp(bytes(ioi_cas_get(v, 0)), "edited") &&
          ioi_cas_get(v, 1) == 43);
    CHECK(ioi_db_update(&q, 7, &n) == IOI_DB_E_VALUE);
    /* paged update: only the two oldest of user 1's posts */
    struct ioi_db_cond u1 = {1, IOI_DB_EQ, 1};
    struct ioi_db_order oldest[] = {{4, 0}};
    q = (struct ioi_db_query){.table = 1, .cols = cscore, .ncols = 1, .conds = &u1, .nconds = 1,
                              .order = oldest, .norder = 1, .take = 1, .has_take = 1};
    CHECK(ioi_db_update(&q, 0, &n) == IOI_DB_OK && n == 1);
    q = (struct ioi_db_query){.table = 1, .shape = IOI_DB_PK, .pk = 1};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && ioi_cas_get(v, 2) == 0);
    /* updating a password re-hashes and the new one verifies */
    unsigned cpw[] = {2};
    struct ioi_db_cond cy = {1, IOI_DB_EQ, ioi_cas_cstr("cy")};
    q = (struct ioi_db_query){.table = 0, .cols = cpw, .ncols = 1, .conds = &cy, .nconds = 1};
    CHECK(ioi_db_update(&q, ioi_cas_cstr("n3w-pass"), &n) == IOI_DB_OK && n == 1);
    cylogin[1].value = ioi_cas_cstr("n3w-pass");
    q = (struct ioi_db_query){.table = 0, .cols = c0, .ncols = 1, .conds = cylogin, .nconds = 2,
                              .shape = IOI_DB_HOW_MANY};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 1);
    /* update guarded by a password condition */
    unsigned cbio[] = {3};
    q = (struct ioi_db_query){.table = 0, .cols = cbio, .ncols = 1, .conds = cylogin, .nconds = 2};
    CHECK(ioi_db_update(&q, ioi_cas_cstr("verified"), &n) == IOI_DB_OK && n == 1);
    cylogin[1].value = ioi_cas_cstr("wrong");
    CHECK(ioi_db_update(&q, ioi_cas_cstr("hacked"), &n) == IOI_DB_OK && n == 0);

    /* delete */
    q = (struct ioi_db_query){.table = 1, .conds = &pid5, .nconds = 1};
    CHECK(ioi_db_delete(&q, &n) == IOI_DB_OK && n == 1);
    q.shape = IOI_DB_HOW_MANY;
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 0);
    struct ioi_db_cond u2 = {1, IOI_DB_EQ, 2};
    q = (struct ioi_db_query){.table = 1, .conds = &u2, .nconds = 1};
    CHECK(ioi_db_delete(&q, &n) == IOI_DB_OK && n == 2);

    /* errors */
    q = (struct ioi_db_query){.table = 9, .shape = IOI_DB_PK, .pk = 1};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_E_TABLE);
    unsigned c9[] = {9};
    q = (struct ioi_db_query){.table = 0, .cols = c9, .ncols = 1};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_E_COLUMN);
    struct ioi_db_cond lst = {1, IOI_DB_EQ, ioi_cas_list()};
    q = (struct ioi_db_query){.table = 0, .cols = c0, .ncols = 1, .conds = &lst, .nconds = 1};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_E_VALUE);
    uint32_t viewrow[] = {ioi_cas_cstr("x")};
    CHECK(ioi_db_insert(2, viewrow, 1, &id) == IOI_DB_E_SQL);

    /* the data survives a reopen, and the seed is not run again */
    ioi_db_close();
    q = (struct ioi_db_query){.table = 0, .cols = c0, .ncols = 1, .shape = IOI_DB_HOW_MANY};
    CHECK(ioi_db_select(&q, &v) == IOI_DB_OK && v == 3);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: unit TMPDIR\n");
        return 2;
    }
    test_sha256();
    test_decoding();
    test_routes();
    test_session();
    test_prose();
    test_render_helpers();
    test_db(argv[1]);
    test_session_store();
    printf("web unit: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
