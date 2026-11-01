/* interstates.c — glue between generated -s programs and the web runtime:
 * error pages, response flushing, URL security, ActiveCassette checks. */
#include "ioi_core.h"
#include "ioi_web.h"

#include <stdlib.h>
#include <string.h>

static void error_hook(int code, const char *msg)
{
    int status = code == 404 ? 404 : code == 403 ? 403 : 500;
    ioi_web_error_page(status, msg);
    ioi_web_finish();
}

static void exit_hook(void) { ioi_web_finish(); }

void ioi_s_init(void)
{
    ioi_web_init();
    ioi_set_error_hook(error_hook);
    ioi_set_exit_hook(exit_hook);
}

_Noreturn void ioi_s_no_route(void) { ioi_lose(404, 16000, NULL); }
_Noreturn void ioi_s_forbidden(void) { ioi_lose(403, 16000, NULL); }

void ioi_s_check_origin(int origin_is_16000)
{
    const char *m = ioi_web_method();
    int safe = !strcmp(m, "GET") || !strcmp(m, "HEAD");
    if ((!origin_is_16000 || !safe) && !ioi_web_same_origin())
        ioi_s_forbidden();
}

void ioi_s_db_check(int status)
{
    if (status == IOI_DB_OK)
        return;
    if (status == IOI_DB_E_TABLE || status == IOI_DB_E_COLUMN)
        ioi_lose(421, ioi_lineno, ioi_db_errmsg());
    ioi_lose(420, ioi_lineno, ioi_db_errmsg());
}

void ioi_s_db_insert(unsigned table, ioi_array *a, uint32_t *rowid)
{
    size_t n = 0;
    if (a->rank > 1)
        ioi_lose(241, ioi_lineno, NULL);
    if (a->rank == 1)
        n = a->dims[0];
    uint32_t id = 0;
    ioi_s_db_check(ioi_db_insert(table, (const uint32_t *)a->data, n, &id));
    if (rowid)
        *rowid = id;
}

void ioi_s_session_set(uint32_t n)
{
    if (ioi_web_session_set(n) < 0 && ioi_web_active())
        ioi_lose(422, ioi_lineno, NULL);
}

uint32_t ioi_s_list1(uint32_t v)
{
    uint32_t l = ioi_cas_list();
    ioi_cas_push(l, v);
    return l;
}
