/* notes.c — a hand-written CGI against libioi.a, doing what ick -s output
 * does: match routes, read fields, keep a session, use ActiveCassette and
 * render prose. Used by tests/web/run.sh under gross.
 *
 * Tables (schema.sql): #0 users(id, name, password PASSWORD)
 *                      #1 notes(id, user_id, body, created) */
#include "ioi_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void out(const char *s) { ioi_web_append_raw(s, strlen(s)); }

static void out_value(uint32_t v)
{
    size_t n;
    const uint8_t *d = ioi_cas_data(v, &n);
    if (d)
        ioi_web_append_escaped(d, n);
    else {
        char buf[16];
        snprintf(buf, sizeof buf, "%u", ioi_val_to_number(v, NULL));
        out(buf);
    }
}

static void fail_db(void)
{
    char msg[700];
    snprintf(msg, sizeof msg, "ICL455I THE CASSETTE IS JAMMED: %s\n        CORRECT SOURCE AND RESUBNIT",
             ioi_db_errmsg());
    ioi_web_error_page(500, msg);
    exit(1);
}

static uint32_t field(const char *name)
{
    const uint8_t *d;
    size_t n;
    ioi_web_field(name, &d, &n);
    return ioi_cas_bytes(d, n);
}

static void redirect(const char *to)
{
    size_t m = ioi_web_mark();
    out(to);
    ioi_web_redirect_since(m);
}

static void page_index(void)
{
    uint32_t me = ioi_web_session_get();
    ioi_web_begin(200, ioi_web_type_for("index.html"));
    out("<!DOCTYPE html><title>notes</title>");
    if (me) {
        struct ioi_db_query q = {.table = 0, .shape = IOI_DB_PK, .pk = me};
        uint32_t user;
        if (ioi_db_select(&q, &user))
            fail_db();
        size_t nl;
        const uint8_t *name = ioi_cas_data(ioi_cas_get(user, 0), &nl);
        out("<p id=\"me\">signed in as ");
        ioi_web_append_escaped(name, nl);
        out("</p>");
        ioi_web_append_face(name, nl);
    }
    unsigned cols[] = {0, 2, 3};
    struct ioi_db_order newest = {0, 1};
    struct ioi_db_query q = {.table = 1, .cols = cols, .ncols = 3, .order = &newest, .norder = 1,
                             .shape = IOI_DB_BULK};
    uint32_t rows;
    if (ioi_db_select(&q, &rows))
        fail_db();
    struct ioi_db_query cnt = {.table = 1, .shape = IOI_DB_HOW_MANY};
    uint32_t n;
    if (ioi_db_select(&cnt, &n))
        fail_db();
    char buf[64];
    snprintf(buf, sizeof buf, "<p id=\"count\">%u notes</p><ul>", n);
    out(buf);
    for (size_t i = 0; i < ioi_cas_len(rows); i++) {
        uint32_t r = ioi_cas_get(rows, i);
        out("<li><a href=\"/notes/");
        out_value(ioi_cas_get(r, 0));
        out("\">");
        out_value(ioi_cas_get(r, 1));
        out("</a> <small>");
        ioi_web_append_ago(ioi_cas_get(r, 2));
        out("</small></li>");
    }
    out("</ul>");
}

static void page_note(uint32_t params)
{
    struct ioi_db_query q = {.table = 1, .shape = IOI_DB_PK, .pk = ioi_cas_get(params, 0)};
    uint32_t note;
    if (ioi_db_select(&q, &note))
        fail_db();
    if (ioi_cas_len(note) == 0) {
        ioi_web_error_page(404, "ICL404I NO SUCH NOTE\n        CORRECT SOURCE AND RESUBNIT");
        exit(0);
    }
    struct ioi_db_query uq = {.table = 0, .shape = IOI_DB_PK, .pk = ioi_cas_get(note, 0)};
    uint32_t user;
    if (ioi_db_select(&uq, &user))
        fail_db();
    size_t nl;
    const uint8_t *name = ioi_cas_data(ioi_cas_get(user, 0), &nl);
    ioi_web_begin(200, NULL);
    out("<article><header>");
    ioi_web_append_face(name, nl);
    out(" by ");
    ioi_web_append_escaped(name, nl);
    out("</header><div class=\"body\">");
    size_t bl;
    const uint8_t *body = ioi_cas_data(ioi_cas_get(note, 1), &bl);
    ioi_web_append_prose(body, bl);
    out("</div></article>");
}

static void do_login(void)
{
    unsigned id[] = {0};
    struct ioi_db_cond c[] = {{1, IOI_DB_EQ, field("name")}, {2, IOI_DB_EQ, field("password")}};
    struct ioi_db_query q = {.table = 0, .cols = id, .ncols = 1, .conds = c, .nconds = 2};
    uint32_t who;
    if (ioi_db_select(&q, &who))
        fail_db();
    if (ioi_val_kind(who) == IOI_LIST) {
        ioi_web_begin(401, "text/plain; charset=utf-8");
        out("ICL401I THAT IS NOT YOUR PASSWORD\n");
        return;
    }
    ioi_web_session_set(ioi_val_to_number(who, NULL));
    redirect("/");
}

static void do_create(void)
{
    uint32_t me = ioi_web_session_get();
    if (!me) {
        ioi_web_error_page(403, "ICL403I YOU ARE NOBODY\n        CORRECT SOURCE AND RESUBNIT");
        exit(0);
    }
    uint32_t row[] = {me, field("body"), ioi_web_time()};
    uint32_t id;
    if (ioi_db_insert(1, row, 3, &id))
        fail_db();
    char to[32];
    snprintf(to, sizeof to, "/notes/%u", id);
    redirect(to);
}

/* Owner-only update or delete, selected by HAVING #0 id HAVING #1 me. */
static void do_modify(uint32_t params, int del)
{
    uint32_t me = ioi_web_session_get();
    struct ioi_db_cond c[] = {{0, IOI_DB_EQ, ioi_cas_get(params, 0)}, {1, IOI_DB_EQ, me}};
    unsigned cbody[] = {2};
    struct ioi_db_query q = {.table = 1, .cols = cbody, .ncols = 1, .conds = c, .nconds = 2};
    uint32_t changed;
    int rc = del ? ioi_db_delete(&q, &changed) : ioi_db_update(&q, field("body"), &changed);
    if (rc)
        fail_db();
    if (!changed) {
        ioi_web_error_page(403, "ICL403I THAT NOTE IS NOT YOURS TO ABSTAIN FROM\n"
                                "        CORRECT SOURCE AND RESUBNIT");
        exit(0);
    }
    redirect("/");
}

static void do_number(void)
{
    const uint8_t *d;
    size_t n;
    int found = ioi_web_field("n", &d, &n);
    uint32_t v;
    int ok = ioi_web_parse_number(d, n, &v);
    ioi_web_begin(200, ioi_web_type_for("n.txt"));
    char buf[64];
    snprintf(buf, sizeof buf, "found=%d len=%zu ok=%d value=%u\n", found, n, ok, v);
    out(buf);
}

int main(void)
{
    ioi_web_init();
    if (!ioi_web_active()) {
        out("not a web request\n");
        ioi_web_finish();
        return 0;
    }
    /* routes in source order; origin 0 means "COME FROM (16000)" */
    static const struct { const char *pattern; int id; } routes[] = {
        {"/", 1},
        {"/notes/:id", 2},
        {"POST /login", 3},
        {"POST /logout", 4},
        {"POST /notes", 5},
        {"POST /notes/:id/edit", 6},
        {"POST /notes/:id/delete", 7},
        {"/number/:n", 8},
        {"/number", 8},
        {"/boom", 9},
    };
    uint32_t params = 0;
    int route = 0;
    for (size_t i = 0; i < sizeof routes / sizeof routes[0] && !route; i++)
        if (ioi_web_route(routes[i].pattern, &params))
            route = routes[i].id;
    if (!route) {
        ioi_web_error_page(404, "ICL404I I CAN'T FIND THAT ROUTE\n        CORRECT SOURCE AND RESUBNIT");
        return 0;
    }
    const char *m = ioi_web_method();
    if (strcmp(m, "GET") && strcmp(m, "HEAD") && !ioi_web_same_origin()) {
        ioi_web_error_page(403, "ICL403I YOU CAN'T GET THERE FROM HERE\n"
                                "        CORRECT SOURCE AND RESUBNIT");
        return 0;
    }
    switch (route) {
    case 1: page_index(); break;
    case 2: page_note(params); break;
    case 3: do_login(); break;
    case 4: ioi_web_session_set(0); redirect("/"); break;
    case 5: do_create(); break;
    case 6: do_modify(params, 0); break;
    case 7: do_modify(params, 1); break;
    case 8: do_number(); break;
    case 9:
        out("this partial output is discarded");
        ioi_web_error_page(500, "ICL774I RANDOM COMPILER BUG\n"
                                "        ON THE WAY TO STATEMENT 0009\n"
                                "        CORRECT SOURCE AND RESUBNIT");
        return 1;
    }
    ioi_web_finish();
    return 0;
}
