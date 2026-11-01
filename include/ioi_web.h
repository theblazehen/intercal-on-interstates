/* ioi_web.h — the web runtime of INTERCAL on Interstates.
 *
 * Owned by runtime/web/. Generated code (ick -s) calls only what is declared
 * here. Values are uint32_t per ioi_cassette.h. One process serves one CGI
 * request, so all state is process-global and nothing is ever freed.
 *
 * Call order in a generated program:
 *   ioi_web_init();                     at start, always
 *   if (ioi_web_active()) {             a CGI request is present
 *       for each route in source order: ioi_web_route(pattern, &params)
 *       (no match -> ioi_web_error_page(404, ...); exit)
 *       (origin != 16000 or method != GET/HEAD) && !ioi_web_same_origin()
 *           -> ioi_web_error_page(403, ...); exit
 *   }
 *   ... READ OUT WITH TEMPLATE -> ioi_web_begin(...) + ioi_web_append_*()
 *   ioi_web_finish();                   at GIVE UP / program end
 *
 * Outside a web request (run from a shell) every function still works:
 * fields are absent, the session is 0, and ioi_web_finish() writes only the
 * body to stdout.
 */
#ifndef IOI_WEB_H
#define IOI_WEB_H

#include <stddef.h>
#include <stdint.h>

#include "ioi_cassette.h"

/* ------------------------------------------------------------------ */
/* Request                                                             */
/* ------------------------------------------------------------------ */

/* Parse the CGI environment (REQUEST_METHOD, PATH_INFO, QUERY_STRING,
 * CONTENT_TYPE, CONTENT_LENGTH, HTTP_COOKIE, ...) and read the request body
 * from stdin (at most 1 MiB). Idempotent. Returns ioi_web_active(). */
int ioi_web_init(void);

/* 1 when the process is serving a CGI request (REQUEST_METHOD is set). */
int ioi_web_active(void);

/* Upper-case request method ("GET", "POST", "HEAD", ...); "" when inactive. */
const char *ioi_web_method(void);

/* Decoded request path (PATH_INFO; "/" when empty or inactive). */
const char *ioi_web_path(void);

/* 1 when the Origin header (or, absent that, the Referer header) names the
 * same host:port as HTTP_HOST. 0 when neither header is present, when
 * Origin is "null", or when the hosts differ. This is the CSRF check of
 * SPEC.md §2.4; the generated code decides when it is required. */
int ioi_web_same_origin(void);

/* Match the current request against PATTERN = "[METHOD ]/a/:b/ *" (written
 * here with a space before the final star; METHOD defaults to GET, and GET
 * also answers HEAD). "*" alone is the catch-all and answers every method;
 * "POST *" answers only POST.
 * Empty path segments are ignored, so "/a/" matches "/a". ":name" matches
 * one non-empty segment; a final "*" matches the remaining zero or more
 * segments joined by "/". On match returns 1 and stores in *params a new
 * LIST of BYTES cassettes, one per ":name" / "*" in pattern order (suitable
 * for ;4400). Returns 0 (and leaves *params untouched) otherwise. */
int ioi_web_route(const char *pattern, uint32_t *params);

/* The same matcher on explicit inputs (no request needed). PARAM_NAMES, if
 * not NULL, receives a LIST of BYTES with the ":name"s ("*" for a splat).
 * Returns 1 on match, -1 when PATTERN is malformed, 0 otherwise. */
int ioi_web_match(const char *pattern, const char *method, const char *path,
                  uint32_t *params, uint32_t *param_names);

/* Field lookup per SPEC.md §2.5: route parameter ":name" (from the last
 * successful ioi_web_route), then the urlencoded form body
 * (application/x-www-form-urlencoded only), then the query string. The first
 * occurrence of a repeated name wins. Returns 1 and points *data and *len
 * at the decoded bytes (valid for the process lifetime; not NUL-terminated),
 * or 0 with *data = "" and *len = 0 when absent. */
int ioi_web_field(const char *name, const uint8_t **data, size_t *len);

/* Parse a number written as decimal digits ("12") or as C-INTERCAL digit
 * words ("ONE TWO"; also OH for 0 and NINER for 9; case-insensitive,
 * separated by blanks). Surrounding blanks are ignored. Returns 1 and stores
 * *out on success; 0 (and *out = 0) when empty, malformed, or > 4294967295. */
int ioi_web_parse_number(const uint8_t *data, size_t len, uint32_t *out);

/* Session value (a number; 0 when absent, unsigned, expired or forged). */
uint32_t ioi_web_session_get(void);

/* Set the session value; 0 logs out (the cookie is deleted). The cookie is
 * HMAC-SHA256 signed with IOI_SECRET, HttpOnly, SameSite=Lax, Path=/, valid
 * 30 days. Takes effect for later ioi_web_session_get calls at once and is
 * sent by ioi_web_finish. Returns 0, or -1 when IOI_SECRET is unset or empty
 * (nothing is set). */
int ioi_web_session_set(uint32_t value);

/* Unix time in seconds (IOI_NOW overrides, for tests). */
uint32_t ioi_web_time(void);

/* ------------------------------------------------------------------ */
/* Response                                                            */
/* ------------------------------------------------------------------ */

/* First call fixes status and content type; later calls do nothing.
 * CONTENT_TYPE NULL means "text/html; charset=utf-8". */
void ioi_web_begin(int status, const char *content_type);

/* Content type for a template file name per SPEC.md §2.6: ".txt" ->
 * text/plain, ".xml" -> application/xml, ".json" -> application/json,
 * anything else text/html (all with charset=utf-8). */
const char *ioi_web_type_for(const char *template_name);

/* Override status / content type unconditionally. */
void ioi_web_set_status(int status);
void ioi_web_set_content_type(const char *content_type);

/* Append to the buffered response body. */
void ioi_web_append_raw(const void *data, size_t len);
/* HTML-escaped: & < > " ' (NUL bytes dropped). */
void ioi_web_append_escaped(const void *data, size_t len);
/* Markdown subset -> sanitised HTML (see runtime/web/README.md). */
void ioi_web_append_prose(const void *data, size_t len);
/* Relative time. VALUE is a number of Unix seconds, or BYTES holding
 * decimal seconds or "YYYY-MM-DD HH:MM:SS" (UTC). Emits plain text:
 * "just now", "5 minutes ago", "3 days ago", "in 2 hours"; unparseable
 * input is appended escaped as-is; a LIST renders each element. */
void ioi_web_append_ago(uint32_t value);
/* Deterministic 5x5 identicon for NAME (case-insensitive) as inline
 * <svg class="ick-face" role="img"> with a <title>. */
void ioi_web_append_face(const void *name, size_t len);

/* Current body length; pass to ioi_web_redirect_since. */
size_t ioi_web_mark(void);

/* Take the body bytes appended since MARK, remove them from the body, and
 * answer 303 See Other with them as Location: surrounding whitespace is
 * trimmed, the entities ioi_web_append_escaped produces are decoded, CR/LF
 * are dropped and other bytes <= 0x20 or >= 0x7f are percent-encoded. The
 * redirect fixes the status even after ioi_web_begin. */
void ioi_web_redirect_since(size_t mark);

/* Write the CGI response (Status, headers, Set-Cookie, blank line, body;
 * no body for HEAD) to stdout. Only the first call writes. When inactive,
 * writes the bare body. */
void ioi_web_finish(void);

/* Discard the buffered body and write an INTERCAL-voiced error page with
 * STATUS (404, 403, 500, ...) and MESSAGE (the full ICLnnnI text, possibly
 * several lines) in a <pre>, then mark the response finished. When inactive
 * this does nothing (the caller prints the message to stderr as usual). */
void ioi_web_error_page(int status, const char *message);

/* ------------------------------------------------------------------ */
/* ActiveCassette (SQLite)                                             */
/* ------------------------------------------------------------------ */
/* Tables (and views) are numbered from 0 in creation order; columns from
 * 0 in declaration order (PRAGMA table_info). The database is opened
 * lazily on first use: IOI_DATABASE (default ./cassette.db); an empty
 * database gets IOI_APP_DIR/schema.sql then seed.sql (IOI_APP_DIR defaults
 * to the executable's directory) in one transaction. */

enum ioi_db_op {
    IOI_DB_EQ = 0,     /* HAVING #c expr  (PASSWORD columns: verify) */
    IOI_DB_SOMEWHERE,  /* HAVING #c SOMEWHERE expr: case-insensitive substring */
    IOI_DB_BEYOND      /* HAVING #c BEYOND expr: greater than */
};

enum ioi_db_shape {
    IOI_DB_ROW = 0,    /* first matching row: 1 column -> value, n -> LIST;
                          no row -> empty LIST */
    IOI_DB_BULK,       /* LIST of rows, each shaped as ROW */
    IOI_DB_HOW_MANY,   /* number of matching rows (columns ignored) */
    IOI_DB_PK          /* no clauses: row whose column 0 == pk, as LIST of
                          columns 1..N; missing -> empty LIST */
};

struct ioi_db_cond {
    unsigned col;
    enum ioi_db_op op;
    uint32_t value;    /* number or BYTES; LIST is an error */
};

struct ioi_db_order {
    unsigned col;
    int backwards;     /* nonzero: descending */
};

struct ioi_db_query {
    unsigned table;
    const unsigned *cols;              /* selected columns (SEL) */
    size_t ncols;
    const struct ioi_db_cond *conds;   /* HAVING clauses, ANDed */
    size_t nconds;
    const struct ioi_db_order *order;  /* BY clauses, in order */
    size_t norder;
    uint32_t skip;                     /* SKIP (0 = none) */
    uint32_t take;                     /* TAKE, used when has_take */
    int has_take;
    enum ioi_db_shape shape;
    uint32_t pk;                       /* IOI_DB_PK only */
};

/* Status codes returned by the ioi_db_* functions. */
enum ioi_db_status {
    IOI_DB_OK = 0,
    IOI_DB_E_OPEN,     /* cannot open/initialise the database */
    IOI_DB_E_TABLE,    /* no such table number */
    IOI_DB_E_COLUMN,   /* no such column number / too many values */
    IOI_DB_E_VALUE,    /* LIST where a scalar is needed, wrong arity, ... */
    IOI_DB_E_SQL       /* SQLite refused (constraint, view write, busy) */
};

/* Read. *out receives the shaped value. */
int ioi_db_select(const struct ioi_db_query *q, uint32_t *out);

/* Insert VALS[k-1] into column k (k = 1..n; column 0 is assigned by
 * SQLite). *rowid (may be NULL) receives the new row id (for :4400). */
int ioi_db_insert(unsigned table, const uint32_t *vals, size_t n, uint32_t *rowid);

/* Update the selected columns of every matching row (SKIP/TAKE/BY apply).
 * One column: VALUE is the value; several: VALUE must be a LIST with one
 * element per column. *changed (may be NULL) receives the row count. */
int ioi_db_update(const struct ioi_db_query *q, uint32_t value, uint32_t *changed);

/* Delete matching rows (columns and shape ignored). */
int ioi_db_delete(const struct ioi_db_query *q, uint32_t *changed);

/* Human-readable detail of the last failure ("" when none). */
const char *ioi_db_errmsg(void);

#endif
