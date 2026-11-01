/* db.c — ActiveCassette: numbered tables and columns over vendored SQLite.
 *
 * Every operand is bound as a parameter. The only text spliced into SQL is
 * table and column names read back from the schema itself (quoted as
 * identifiers). */
#include "ioi_web.h"
#include "web_internal.h"

#include "sqlite3.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct column {
    char *name;
    int password;
};

struct table {
    char *name;
    struct column *cols;
    size_t ncols;
};

static sqlite3 *db;
static int db_state; /* 0 unopened, 1 open, -1 failed */
static struct table *tables;
static size_t ntables;
static char errbuf[512];

const char *ioi_db_errmsg(void) { return errbuf; }

static int fail(int code, const char *fmt, const char *arg)
{
    snprintf(errbuf, sizeof errbuf, fmt, arg ? arg : "");
    return code;
}

static int sql_fail(void) { return fail(IOI_DB_E_SQL, "%s", sqlite3_errmsg(db)); }

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(ioi_xmalloc(n), s, n);
}

void ioi_db_close(void)
{
    if (db)
        sqlite3_close(db);
    db = NULL;
    for (size_t i = 0; i < ntables; i++) {
        for (size_t c = 0; c < tables[i].ncols; c++)
            free(tables[i].cols[c].name);
        free(tables[i].cols);
        free(tables[i].name);
    }
    free(tables);
    tables = NULL;
    ntables = 0;
    db_state = 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    struct ioi_buf b = {0};
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        ioi_buf_put(&b, chunk, n);
    fclose(f);
    if (!b.p)
        ioi_buf_put(&b, "", 0);
    return b.p;
}

static char *app_dir(void)
{
    const char *d = getenv("IOI_APP_DIR");
    if (d && *d)
        return xstrdup(d);
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return xstrdup(".");
    buf[n] = 0;
    char *slash = strrchr(buf, '/');
    if (slash == buf)
        slash[1] = 0;
    else if (slash)
        *slash = 0;
    return xstrdup(buf);
}

static void quote_ident(struct ioi_buf *b, const char *name)
{
    ioi_buf_putc(b, '"');
    for (const char *p = name; *p; p++) {
        if (*p == '"')
            ioi_buf_putc(b, '"');
        ioi_buf_putc(b, *p);
    }
    ioi_buf_putc(b, '"');
}

static int load_schema(void)
{
    sqlite3_stmt *st;
    const char *q = "SELECT name FROM sqlite_master WHERE type IN ('table','view') "
                    "AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' "
                    "AND name NOT IN (SELECT name FROM pragma_table_list WHERE type = 'shadow') "
                    "ORDER BY rowid";
    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) != SQLITE_OK)
        return sql_fail();
    size_t cap = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (ntables == cap) {
            cap = cap ? cap * 2 : 16;
            tables = ioi_xrealloc(tables, cap * sizeof *tables);
        }
        struct table *t = &tables[ntables++];
        memset(t, 0, sizeof *t);
        t->name = xstrdup((const char *)sqlite3_column_text(st, 0));
    }
    sqlite3_finalize(st);
    for (size_t i = 0; i < ntables; i++) {
        struct table *t = &tables[i];
        if (sqlite3_prepare_v2(db, "SELECT name, type FROM pragma_table_info(?) ORDER BY cid", -1,
                               &st, NULL) != SQLITE_OK)
            return sql_fail();
        sqlite3_bind_text(st, 1, t->name, -1, SQLITE_STATIC);
        size_t ccap = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (t->ncols == ccap) {
                ccap = ccap ? ccap * 2 : 8;
                t->cols = ioi_xrealloc(t->cols, ccap * sizeof *t->cols);
            }
            const char *type = (const char *)sqlite3_column_text(st, 1);
            t->cols[t->ncols].name = xstrdup((const char *)sqlite3_column_text(st, 0));
            t->cols[t->ncols].password = type && !strcasecmp(type, "PASSWORD");
            t->ncols++;
        }
        sqlite3_finalize(st);
    }
    return IOI_DB_OK;
}

/* Replace every PASSWORD value that is not already a hash by its hash. */
static int hash_plaintext_passwords(void)
{
    for (size_t i = 0; i < ntables; i++) {
        struct table *t = &tables[i];
        for (size_t c = 0; c < t->ncols; c++) {
            if (!t->cols[c].password)
                continue;
            struct ioi_buf sel = {0}, upd = {0};
            ioi_buf_puts(&sel, "SELECT rowid, CAST(");
            quote_ident(&sel, t->cols[c].name);
            ioi_buf_puts(&sel, " AS TEXT) FROM ");
            quote_ident(&sel, t->name);
            ioi_buf_puts(&sel, " WHERE ");
            quote_ident(&sel, t->cols[c].name);
            ioi_buf_puts(&sel, " IS NOT NULL");
            ioi_buf_puts(&upd, "UPDATE ");
            quote_ident(&upd, t->name);
            ioi_buf_puts(&upd, " SET ");
            quote_ident(&upd, t->cols[c].name);
            ioi_buf_puts(&upd, " = ? WHERE rowid = ?");
            sqlite3_stmt *s1 = NULL, *s2 = NULL;
            int rc = IOI_DB_OK;
            sqlite3_stmt *probe;
            /* views carry no rows of their own */
            if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='view' AND name=?",
                                   -1, &probe, NULL) != SQLITE_OK) {
                rc = sql_fail();
                goto done;
            }
            sqlite3_bind_text(probe, 1, t->name, -1, SQLITE_STATIC);
            int is_view = sqlite3_step(probe) == SQLITE_ROW;
            sqlite3_finalize(probe);
            if (is_view)
                goto done;
            if (sqlite3_prepare_v2(db, sel.p, -1, &s1, NULL) != SQLITE_OK ||
                sqlite3_prepare_v2(db, upd.p, -1, &s2, NULL) != SQLITE_OK) {
                rc = sql_fail();
                goto done;
            }
            while (sqlite3_step(s1) == SQLITE_ROW) {
                const void *pw = sqlite3_column_text(s1, 1);
                int len = sqlite3_column_bytes(s1, 1);
                if (ioi_password_is_hash(pw, (size_t)len))
                    continue;
                char *h = ioi_password_hash(pw, (size_t)len);
                if (!h) {
                    rc = fail(IOI_DB_E_OPEN, "%s", "no randomness for password salt");
                    goto done;
                }
                sqlite3_bind_text(s2, 1, h, -1, free);
                sqlite3_bind_int64(s2, 2, sqlite3_column_int64(s1, 0));
                if (sqlite3_step(s2) != SQLITE_DONE) {
                    rc = sql_fail();
                    goto done;
                }
                sqlite3_reset(s2);
            }
        done:
            sqlite3_finalize(s1);
            sqlite3_finalize(s2);
            free(sel.p);
            free(upd.p);
            if (rc)
                return rc;
        }
    }
    return IOI_DB_OK;
}

static int run_file(const char *dir, const char *name, int required)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    char *sql = read_file(path);
    if (!sql)
        return required ? fail(IOI_DB_E_OPEN, "cannot read %s", path) : IOI_DB_OK;
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    free(sql);
    if (rc != SQLITE_OK) {
        snprintf(errbuf, sizeof errbuf, "%s: %s", name, err ? err : sqlite3_errmsg(db));
        sqlite3_free(err);
        return IOI_DB_E_OPEN;
    }
    return IOI_DB_OK;
}

static int open_db(void)
{
    if (db_state == 1)
        return IOI_DB_OK;
    if (db_state == -1)
        return IOI_DB_E_OPEN;
    db_state = -1;
    const char *path = getenv("IOI_DATABASE");
    if (!path || !*path)
        path = "cassette.db";
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        fail(IOI_DB_E_OPEN, "%s", db ? sqlite3_errmsg(db) : "out of memory");
        sqlite3_close(db);
        db = NULL;
        return IOI_DB_E_OPEN;
    }
    static int registered;
    if (!registered) {
        registered = 1;
        atexit(ioi_db_close);
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);

    int rc = IOI_DB_OK;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK)
        return fail(IOI_DB_E_OPEN, "%s", sqlite3_errmsg(db));
    sqlite3_stmt *st;
    int empty = 0;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master", -1, &st, NULL) == SQLITE_OK) {
        empty = sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) == 0;
        sqlite3_finalize(st);
    }
    if (empty) {
        char *dir = app_dir();
        rc = run_file(dir, "schema.sql", 0);
        if (!rc)
            rc = run_file(dir, "seed.sql", 0);
        free(dir);
    }
    if (!rc)
        rc = load_schema();
    if (!rc && empty)
        rc = hash_plaintext_passwords();
    if (rc) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return IOI_DB_E_OPEN;
    }
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        fail(IOI_DB_E_OPEN, "%s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return IOI_DB_E_OPEN;
    }
    db_state = 1;
    errbuf[0] = 0;
    return IOI_DB_OK;
}

/* ---------------------------------------------------------------- */
/* Values                                                           */

enum bind_mode { BIND_STORE, BIND_COMPARE };

/* Decimal rendering of a scalar value into buf; returns bytes or NULL. */
static const char *scalar_text(uint32_t v, char numbuf[16], size_t *len)
{
    if (ioi_val_kind(v) == IOI_NUMBER) {
        *len = (size_t)snprintf(numbuf, 16, "%u", ioi_val_to_number(v, NULL));
        return numbuf;
    }
    if (ioi_val_kind(v) == IOI_BYTES) {
        const uint8_t *d = ioi_cas_data(v, len);
        return d ? (const char *)d : "";
    }
    return NULL;
}

static int canonical_decimal(const uint8_t *d, size_t n, sqlite3_int64 *out)
{
    if (n == 0 || n > 18 || (n > 1 && d[0] == '0'))
        return 0;
    sqlite3_int64 v = 0;
    for (size_t i = 0; i < n; i++) {
        if (d[i] < '0' || d[i] > '9')
            return 0;
        v = v * 10 + (d[i] - '0');
    }
    *out = v;
    return 1;
}

static int bind_value(sqlite3_stmt *st, int idx, uint32_t v, enum bind_mode mode)
{
    sqlite3_int64 iv;
    switch (ioi_val_kind(v)) {
    case IOI_NUMBER:
        sqlite3_bind_int64(st, idx, ioi_val_to_number(v, NULL));
        return IOI_DB_OK;
    case IOI_BYTES: {
        size_t n;
        const uint8_t *d = ioi_cas_data(v, &n);
        if (mode == BIND_COMPARE && canonical_decimal(d, n, &iv))
            sqlite3_bind_int64(st, idx, iv);
        else
            sqlite3_bind_text(st, idx, d ? (const char *)d : "", (int)n, SQLITE_TRANSIENT);
        return IOI_DB_OK;
    }
    default:
        return fail(IOI_DB_E_VALUE, "%s", "a LIST cannot be stored in or compared with one column");
    }
}

/* Bind a value destined for column c of t (hashing PASSWORD columns). */
static int bind_store(sqlite3_stmt *st, int idx, const struct column *c, uint32_t v)
{
    if (!c->password)
        return bind_value(st, idx, v, BIND_STORE);
    char nb[16];
    size_t n;
    const char *s = scalar_text(v, nb, &n);
    if (!s)
        return fail(IOI_DB_E_VALUE, "%s", "a LIST cannot be a password");
    char *h = ioi_password_hash(s, n);
    if (!h)
        return fail(IOI_DB_E_SQL, "%s", "no randomness for password salt");
    sqlite3_bind_text(st, idx, h, -1, free);
    return IOI_DB_OK;
}

static uint32_t column_value(sqlite3_stmt *st, int i)
{
    switch (sqlite3_column_type(st, i)) {
    case SQLITE_INTEGER: {
        sqlite3_int64 v = sqlite3_column_int64(st, i);
        if (v >= 0 && v <= (sqlite3_int64)UINT32_MAX)
            return ioi_val_number((uint32_t)v);
        break;
    }
    case SQLITE_NULL:
        return ioi_cas_bytes("", 0);
    case SQLITE_BLOB:
        return ioi_cas_bytes(sqlite3_column_blob(st, i), (size_t)sqlite3_column_bytes(st, i));
    default:
        break;
    }
    const unsigned char *t = sqlite3_column_text(st, i);
    return ioi_cas_bytes(t ? (const char *)t : "", (size_t)sqlite3_column_bytes(st, i));
}

/* ---------------------------------------------------------------- */
/* Query building                                                   */

static int check_query(const struct ioi_db_query *q, struct table **tp)
{
    int rc = open_db();
    if (rc)
        return rc;
    if (q->table >= ntables) {
        char n[16];
        snprintf(n, sizeof n, "%u", q->table);
        return fail(IOI_DB_E_TABLE, "there is no table #%s", n);
    }
    struct table *t = &tables[q->table];
    char n[16];
    for (size_t i = 0; i < q->ncols; i++)
        if (q->cols[i] >= t->ncols)
            return snprintf(n, sizeof n, "%u", q->cols[i]),
                   fail(IOI_DB_E_COLUMN, "there is no column #%s", n);
    for (size_t i = 0; i < q->nconds; i++) {
        if (q->conds[i].col >= t->ncols)
            return snprintf(n, sizeof n, "%u", q->conds[i].col),
                   fail(IOI_DB_E_COLUMN, "there is no column #%s", n);
        if (t->cols[q->conds[i].col].password && q->conds[i].op != IOI_DB_EQ)
            return fail(IOI_DB_E_VALUE, "%s", "a PASSWORD can only be HAVING-checked for equality");
        if (ioi_val_kind(q->conds[i].value) == IOI_LIST)
            return fail(IOI_DB_E_VALUE, "%s", "HAVING cannot compare a column with a LIST");
    }
    for (size_t i = 0; i < q->norder; i++)
        if (q->order[i].col >= t->ncols)
            return snprintf(n, sizeof n, "%u", q->order[i].col),
                   fail(IOI_DB_E_COLUMN, "there is no column #%s", n);
    if (t->ncols == 0)
        return fail(IOI_DB_E_TABLE, "table %s has no columns", t->name);
    *tp = t;
    return IOI_DB_OK;
}

static int has_pw_cond(const struct ioi_db_query *q, const struct table *t)
{
    for (size_t i = 0; i < q->nconds; i++)
        if (t->cols[q->conds[i].col].password)
            return 1;
    return 0;
}

/* Append " WHERE ..." for the non-password conditions; parameters are
 * numbered from *param upward and bound later by bind_where. */
static void build_where(struct ioi_buf *b, const struct ioi_db_query *q, const struct table *t)
{
    int first = 1;
    for (size_t i = 0; i < q->nconds; i++) {
        const struct ioi_db_cond *c = &q->conds[i];
        if (t->cols[c->col].password)
            continue;
        ioi_buf_puts(b, first ? " WHERE " : " AND ");
        first = 0;
        const char *name = t->cols[c->col].name;
        size_t n = 1;
        int empty = ioi_val_kind(c->value) == IOI_BYTES && (ioi_cas_data(c->value, &n), n == 0);
        switch (c->op) {
        case IOI_DB_EQ:
            if (empty) {
                ioi_buf_putc(b, '(');
                quote_ident(b, name);
                ioi_buf_puts(b, " = ? OR ");
                quote_ident(b, name);
                ioi_buf_puts(b, " IS NULL)");
            } else {
                quote_ident(b, name);
                ioi_buf_puts(b, " = ?");
            }
            break;
        case IOI_DB_SOMEWHERE:
            quote_ident(b, name);
            ioi_buf_puts(b, " LIKE ? ESCAPE '\\'");
            break;
        case IOI_DB_BEYOND:
            quote_ident(b, name);
            ioi_buf_puts(b, " > ?");
            break;
        }
    }
}

static int bind_where(sqlite3_stmt *st, int *param, const struct ioi_db_query *q,
                      const struct table *t)
{
    for (size_t i = 0; i < q->nconds; i++) {
        const struct ioi_db_cond *c = &q->conds[i];
        if (t->cols[c->col].password)
            continue;
        int rc;
        if (c->op == IOI_DB_SOMEWHERE) {
            char nb[16];
            size_t n;
            const char *s = scalar_text(c->value, nb, &n);
            struct ioi_buf pat = {0};
            ioi_buf_putc(&pat, '%');
            for (size_t k = 0; k < n; k++) {
                if (s[k] == '%' || s[k] == '_' || s[k] == '\\')
                    ioi_buf_putc(&pat, '\\');
                ioi_buf_putc(&pat, s[k]);
            }
            ioi_buf_putc(&pat, '%');
            sqlite3_bind_text(st, (*param)++, pat.p, (int)pat.len, free);
            continue;
        }
        if ((rc = bind_value(st, (*param)++, c->value, BIND_COMPARE)))
            return rc;
    }
    return IOI_DB_OK;
}

static void build_order(struct ioi_buf *b, const struct ioi_db_query *q, const struct table *t)
{
    for (size_t i = 0; i < q->norder; i++) {
        ioi_buf_puts(b, i ? ", " : " ORDER BY ");
        quote_ident(b, t->cols[q->order[i].col].name);
        ioi_buf_puts(b, q->order[i].backwards ? " DESC" : " ASC");
    }
}

/* LIMIT for SQL-side paging; `cap` limits further (ROW shape: 1). */
static void build_limit(struct ioi_buf *b, const struct ioi_db_query *q, int cap_one)
{
    long long lim = -1;
    if (q->has_take)
        lim = q->take;
    if (cap_one && (lim < 0 || lim > 1))
        lim = 1;
    if (lim >= 0 || q->skip)
        ioi_buf_printf(b, " LIMIT %lld OFFSET %u", lim, q->skip);
}

/* Do the password conditions hold for this row? Password columns sit at
 * result index `base` onwards, in condition order. */
static int pw_match(sqlite3_stmt *st, int base, const struct ioi_db_query *q, const struct table *t)
{
    int k = base;
    for (size_t i = 0; i < q->nconds; i++) {
        const struct ioi_db_cond *c = &q->conds[i];
        if (!t->cols[c->col].password)
            continue;
        char nb[16];
        size_t n;
        const char *pw = scalar_text(c->value, nb, &n);
        const void *stored = sqlite3_column_text(st, k);
        int sl = sqlite3_column_bytes(st, k);
        k++;
        if (!pw || !stored || !ioi_password_verify(pw, n, stored, (size_t)sl))
            return 0;
    }
    return 1;
}

static void append_pw_cols(struct ioi_buf *b, const struct ioi_db_query *q, const struct table *t,
                           int leading_comma)
{
    for (size_t i = 0; i < q->nconds; i++) {
        if (!t->cols[q->conds[i].col].password)
            continue;
        if (leading_comma)
            ioi_buf_puts(b, ", ");
        leading_comma = 1;
        ioi_buf_puts(b, "CAST(");
        quote_ident(b, t->cols[q->conds[i].col].name);
        ioi_buf_puts(b, " AS TEXT)");
    }
}

static uint32_t shape_row(sqlite3_stmt *st, size_t ncols)
{
    if (ncols == 1)
        return column_value(st, 0);
    uint32_t row = ioi_cas_list();
    for (size_t i = 0; i < ncols; i++)
        ioi_cas_push(row, column_value(st, (int)i));
    return row;
}

int ioi_db_select(const struct ioi_db_query *q, uint32_t *out)
{
    struct table *t;
    int rc = check_query(q, &t);
    if (rc)
        return rc;
    struct ioi_buf b = {0};
    sqlite3_stmt *st = NULL;
    int param = 1;

    if (q->shape == IOI_DB_PK) {
        ioi_buf_puts(&b, "SELECT ");
        if (t->ncols == 1)
            ioi_buf_puts(&b, "1");
        for (size_t c = 1; c < t->ncols; c++) {
            if (c > 1)
                ioi_buf_puts(&b, ", ");
            quote_ident(&b, t->cols[c].name);
        }
        ioi_buf_puts(&b, " FROM ");
        quote_ident(&b, t->name);
        ioi_buf_puts(&b, " WHERE ");
        quote_ident(&b, t->cols[0].name);
        ioi_buf_puts(&b, " = ? LIMIT 1");
        if (sqlite3_prepare_v2(db, b.p, -1, &st, NULL) != SQLITE_OK) {
            rc = sql_fail();
            goto done;
        }
        if ((rc = bind_value(st, 1, q->pk, BIND_COMPARE)))
            goto done;
        uint32_t row = ioi_cas_list();
        int s = sqlite3_step(st);
        if (s == SQLITE_ROW) {
            for (size_t c = 1; c < t->ncols; c++)
                ioi_cas_push(row, column_value(st, (int)c - 1));
        } else if (s != SQLITE_DONE) {
            rc = sql_fail();
            goto done;
        }
        *out = row;
        goto done;
    }

    int pw = has_pw_cond(q, t);
    if (q->shape != IOI_DB_HOW_MANY && q->ncols == 0) {
        rc = fail(IOI_DB_E_COLUMN, "%s", "no columns selected");
        goto done;
    }
    size_t ncols = q->shape == IOI_DB_HOW_MANY ? 0 : q->ncols;
    if (q->shape == IOI_DB_HOW_MANY && !pw)
        ioi_buf_puts(&b, "SELECT count(*) FROM (");
    ioi_buf_puts(&b, "SELECT ");
    if (ncols == 0)
        ioi_buf_puts(&b, "1");
    for (size_t i = 0; i < ncols; i++) {
        if (i)
            ioi_buf_puts(&b, ", ");
        quote_ident(&b, t->cols[q->cols[i]].name);
    }
    append_pw_cols(&b, q, t, 1);
    ioi_buf_puts(&b, " FROM ");
    quote_ident(&b, t->name);
    build_where(&b, q, t);
    build_order(&b, q, t);
    if (!pw)
        build_limit(&b, q, q->shape == IOI_DB_ROW);
    if (q->shape == IOI_DB_HOW_MANY && !pw)
        ioi_buf_puts(&b, ")");
    if (sqlite3_prepare_v2(db, b.p, -1, &st, NULL) != SQLITE_OK) {
        rc = sql_fail();
        goto done;
    }
    if ((rc = bind_where(st, &param, q, t)))
        goto done;

    int pwbase = ncols == 0 ? 1 : (int)ncols;
    uint32_t bulk = ioi_cas_list(), result = 0;
    int have_row = 0;
    uint64_t matched = 0, skipped = 0;
    for (;;) {
        int s = sqlite3_step(st);
        if (s == SQLITE_DONE)
            break;
        if (s != SQLITE_ROW) {
            rc = sql_fail();
            goto done;
        }
        if (q->shape == IOI_DB_HOW_MANY && !pw) {
            matched = (uint64_t)sqlite3_column_int64(st, 0);
            break;
        }
        if (pw) {
            if (!pw_match(st, pwbase, q, t))
                continue;
            if (skipped < q->skip) {
                skipped++;
                continue;
            }
            if (q->has_take && matched >= q->take)
                break;
        }
        matched++;
        if (q->shape == IOI_DB_HOW_MANY)
            continue;
        if (q->shape == IOI_DB_ROW) {
            result = shape_row(st, ncols);
            have_row = 1;
            break;
        }
        ioi_cas_push(bulk, shape_row(st, ncols));
    }
    if (q->shape == IOI_DB_HOW_MANY)
        *out = ioi_val_number(matched > UINT32_MAX ? UINT32_MAX : (uint32_t)matched);
    else if (q->shape == IOI_DB_ROW)
        *out = have_row ? result : ioi_cas_list();
    else
        *out = bulk;
done:
    sqlite3_finalize(st);
    free(b.p);
    return rc;
}

int ioi_db_insert(unsigned table, const uint32_t *vals, size_t n, uint32_t *rowid)
{
    int rc = open_db();
    if (rc)
        return rc;
    if (table >= ntables) {
        char s[16];
        snprintf(s, sizeof s, "%u", table);
        return fail(IOI_DB_E_TABLE, "there is no table #%s", s);
    }
    struct table *t = &tables[table];
    if (n + 1 > t->ncols)
        return fail(IOI_DB_E_COLUMN, "too many values for table %s", t->name);
    struct ioi_buf b = {0};
    ioi_buf_puts(&b, "INSERT INTO ");
    quote_ident(&b, t->name);
    if (n == 0) {
        ioi_buf_puts(&b, " DEFAULT VALUES");
    } else {
        ioi_buf_puts(&b, " (");
        for (size_t i = 0; i < n; i++) {
            if (i)
                ioi_buf_puts(&b, ", ");
            quote_ident(&b, t->cols[i + 1].name);
        }
        ioi_buf_puts(&b, ") VALUES (");
        for (size_t i = 0; i < n; i++)
            ioi_buf_puts(&b, i ? ", ?" : "?");
        ioi_buf_puts(&b, ")");
    }
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, b.p, -1, &st, NULL) != SQLITE_OK) {
        rc = sql_fail();
        goto done;
    }
    for (size_t i = 0; i < n; i++)
        if ((rc = bind_store(st, (int)i + 1, &t->cols[i + 1], vals[i])))
            goto done;
    if (sqlite3_step(st) != SQLITE_DONE) {
        rc = sql_fail();
        goto done;
    }
    if (rowid) {
        sqlite3_int64 id = sqlite3_last_insert_rowid(db);
        *rowid = ioi_val_number(id < 0 ? 0 : id > (sqlite3_int64)UINT32_MAX ? UINT32_MAX : (uint32_t)id);
    }
done:
    sqlite3_finalize(st);
    free(b.p);
    return rc;
}

/* Shared by update and delete. `set` is NULL for delete. */
static int modify(const struct ioi_db_query *q, const uint32_t *set, size_t nset, uint32_t *changed)
{
    struct table *t;
    int rc = check_query(q, &t);
    if (rc)
        return rc;
    struct ioi_buf b = {0};
    sqlite3_stmt *st = NULL, *pick = NULL;
    int pw = has_pw_cond(q, t);
    int paged = q->norder || q->skip || q->has_take;
    uint64_t total = 0;

    /* head: "UPDATE t SET a = ?, b = ?" or "DELETE FROM t" */
    if (set) {
        ioi_buf_puts(&b, "UPDATE ");
        quote_ident(&b, t->name);
        ioi_buf_puts(&b, " SET ");
        for (size_t i = 0; i < nset; i++) {
            if (i)
                ioi_buf_puts(&b, ", ");
            quote_ident(&b, t->cols[q->cols[i]].name);
            ioi_buf_puts(&b, " = ?");
        }
    } else {
        ioi_buf_puts(&b, "DELETE FROM ");
        quote_ident(&b, t->name);
    }

    if (!pw) {
        if (paged) {
            ioi_buf_puts(&b, " WHERE rowid IN (SELECT rowid FROM ");
            quote_ident(&b, t->name);
            build_where(&b, q, t);
            build_order(&b, q, t);
            build_limit(&b, q, 0);
            ioi_buf_puts(&b, ")");
        } else {
            build_where(&b, q, t);
        }
        if (sqlite3_prepare_v2(db, b.p, -1, &st, NULL) != SQLITE_OK) {
            rc = sql_fail();
            goto done;
        }
        int param = 1;
        for (size_t i = 0; i < nset; i++)
            if ((rc = bind_store(st, param++, &t->cols[q->cols[i]], set[i])))
                goto done;
        if ((rc = bind_where(st, &param, q, t)))
            goto done;
        if (sqlite3_step(st) != SQLITE_DONE) {
            rc = sql_fail();
            goto done;
        }
        total = (uint64_t)sqlite3_changes(db);
        goto done;
    }

    /* Password conditions are checked in C: pick candidate rowids first. */
    ioi_buf_puts(&b, " WHERE rowid = ?");
    struct ioi_buf p = {0};
    ioi_buf_puts(&p, "SELECT rowid");
    append_pw_cols(&p, q, t, 1);
    ioi_buf_puts(&p, " FROM ");
    quote_ident(&p, t->name);
    build_where(&p, q, t);
    build_order(&p, q, t);
    int prc = sqlite3_prepare_v2(db, p.p, -1, &pick, NULL);
    free(p.p);
    if (prc != SQLITE_OK || sqlite3_prepare_v2(db, b.p, -1, &st, NULL) != SQLITE_OK) {
        rc = sql_fail();
        goto done;
    }
    int param = 1;
    if ((rc = bind_where(pick, &param, q, t)))
        goto done;
    sqlite3_int64 *ids = NULL;
    size_t nids = 0, cap = 0;
    uint64_t skipped = 0;
    for (;;) {
        int s = sqlite3_step(pick);
        if (s == SQLITE_DONE)
            break;
        if (s != SQLITE_ROW) {
            rc = sql_fail();
            free(ids);
            goto done;
        }
        if (!pw_match(pick, 1, q, t))
            continue;
        if (skipped < q->skip) {
            skipped++;
            continue;
        }
        if (q->has_take && nids >= q->take)
            break;
        if (nids == cap) {
            cap = cap ? cap * 2 : 8;
            ids = ioi_xrealloc(ids, cap * sizeof *ids);
        }
        ids[nids++] = sqlite3_column_int64(pick, 0);
    }
    sqlite3_finalize(pick);
    pick = NULL;
    sqlite3_exec(db, "SAVEPOINT ioi_modify", NULL, NULL, NULL);
    for (size_t i = 0; i < nids && !rc; i++) {
        param = 1;
        for (size_t c = 0; c < nset && !rc; c++)
            rc = bind_store(st, param++, &t->cols[q->cols[c]], set[c]);
        if (rc)
            break;
        sqlite3_bind_int64(st, param, ids[i]);
        if (sqlite3_step(st) != SQLITE_DONE)
            rc = sql_fail();
        else
            total += (uint64_t)sqlite3_changes(db);
        sqlite3_reset(st);
    }
    free(ids);
    if (rc)
        sqlite3_exec(db, "ROLLBACK TO ioi_modify", NULL, NULL, NULL);
    sqlite3_exec(db, "RELEASE ioi_modify", NULL, NULL, NULL);
done:
    sqlite3_finalize(pick);
    sqlite3_finalize(st);
    free(b.p);
    if (!rc && changed)
        *changed = ioi_val_number(total > UINT32_MAX ? UINT32_MAX : (uint32_t)total);
    return rc;
}

int ioi_db_update(const struct ioi_db_query *q, uint32_t value, uint32_t *changed)
{
    if (q->ncols == 0)
        return fail(IOI_DB_E_COLUMN, "%s", "no columns selected");
    uint32_t one[1] = {value};
    const uint32_t *set = one;
    uint32_t *many = NULL;
    if (q->ncols > 1 || ioi_val_kind(value) == IOI_LIST) {
        size_t n = ioi_cas_len(value);
        if (ioi_val_kind(value) != IOI_LIST || n != q->ncols)
            return fail(IOI_DB_E_VALUE, "%s", "the number of values does not match the columns");
        many = ioi_xmalloc(n * sizeof *many);
        for (size_t i = 0; i < n; i++)
            many[i] = ioi_cas_get(value, i);
        set = many;
    }
    int rc = modify(q, set, q->ncols, changed);
    free(many);
    return rc;
}

int ioi_db_delete(const struct ioi_db_query *q, uint32_t *changed)
{
    struct ioi_db_query d = *q;
    d.ncols = 0;
    return modify(&d, NULL, 0, changed);
}
