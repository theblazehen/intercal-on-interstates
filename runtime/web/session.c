/* session.c — the session number, kept server-side.
 *
 * The cookie carries "<id>.<mac>": a random 128-bit id (32 hex digits) and
 * HMAC-SHA256(IOI_SECRET, id). The number lives in the framework table
 * ioi_sessions under SHA-256(id), with an expiry 30 days after it was set.
 * Setting a new value issues a new id and deletes the old row; setting 0
 * deletes the row (a real logout), so a copied cookie dies with it. A new or
 * reset database has no rows, so old cookies mean nothing there. */
#include "ioi_web.h"
#include "web_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int loaded;
    uint32_t value;
    char id[33];     /* id of the session this request holds, "" for none */
    int changed;     /* set during this request: emit Set-Cookie */
    char *header;    /* Set-Cookie value to send */
} ss;

void ioi_web_session_reset(void)
{
    free(ss.header);
    memset(&ss, 0, sizeof ss);
}

int ioi_session_secure(void)
{
    const char *s = getenv("IOI_SECURE_COOKIES");
    const char *https = getenv("HTTPS");
    return (s && !strcmp(s, "1")) || (https && (!strcmp(https, "on") || !strcmp(https, "1")));
}

static void mac_hex(const char *secret, const char *id, char out[65])
{
    uint8_t mac[32];
    ioi_hmac_sha256(secret, strlen(secret), id, strlen(id), mac);
    ioi_hex(mac, 32, out);
}

char *ioi_session_sign(const char *id, const char *secret)
{
    char hex[65];
    mac_hex(secret, id, hex);
    size_t n = strlen(id) + 1 + 64 + 1;
    char *out = ioi_xmalloc(n);
    snprintf(out, n, "%s.%s", id, hex);
    return out;
}

static int is_lower_hex(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return 1;
}

int ioi_session_check(const char *cookie, const char *secret, char id[33])
{
    if (!cookie || !secret || !*secret || strlen(cookie) != 32 + 1 + 64 || cookie[32] != '.' ||
        !is_lower_hex(cookie, 32) || !is_lower_hex(cookie + 33, 64))
        return 0;
    memcpy(id, cookie, 32);
    id[32] = 0;
    char hex[65];
    mac_hex(secret, id, hex);
    return ioi_ct_equal(hex, cookie + 33, 64);
}

/* Storage key: hex SHA-256 of the id, so a copy of the database holds no
 * usable cookies. */
static void storage_key(const char *id, char key[65])
{
    uint8_t h[32];
    ioi_sha256(id, strlen(id), h);
    ioi_hex(h, 32, key);
}

static const char *cookie_name(void)
{
    return ioi_session_secure() ? IOI_SESSION_COOKIE_SECURE : IOI_SESSION_COOKIE;
}

static void load(void)
{
    if (ss.loaded)
        return;
    ss.loaded = 1;
    ioi_web_init();
    const char *hdr = getenv("HTTP_COOKIE");
    if (!hdr)
        return;
    char *c = ioi_cookie_lookup(hdr, cookie_name());
    char id[33], key[65];
    uint32_t v;
    if (c && ioi_session_check(c, getenv("IOI_SECRET"), id)) {
        storage_key(id, key);
        if (ioi_db_session_get(key, ioi_web_time(), &v) == IOI_DB_OK && v) {
            ss.value = v;
            memcpy(ss.id, id, sizeof id);
        }
    }
    free(c);
}

uint32_t ioi_web_session_get(void)
{
    load();
    return ss.value;
}

int ioi_web_session_set(uint32_t value)
{
    const char *secret = getenv("IOI_SECRET");
    if (!secret || !*secret)
        return -1;
    load();
    char key[65];
    uint32_t now = ioi_web_time();
    if (ss.id[0]) {
        storage_key(ss.id, key);
        if (ioi_db_session_delete(key))
            return -1;
        ss.id[0] = 0;
        ss.value = 0;
    }
    const char *secure = ioi_session_secure() ? "; Secure" : "";
    char *cookie = NULL;
    if (value) {
        uint8_t raw[16];
        char id[33];
        if (ioi_random_bytes(raw, sizeof raw))
            return -1;
        ioi_hex(raw, sizeof raw, id);
        storage_key(id, key);
        if (ioi_db_session_put(key, value, now, now + IOI_SESSION_MAX_AGE))
            return -1;
        memcpy(ss.id, id, sizeof id);
        cookie = ioi_session_sign(id, secret);
    }
    ss.value = value;
    ss.changed = 1;
    free(ss.header);
    size_t n = (cookie ? strlen(cookie) : 0) + 160;
    ss.header = ioi_xmalloc(n);
    if (cookie)
        snprintf(ss.header, n, "%s=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%u%s", cookie_name(),
                 cookie, IOI_SESSION_MAX_AGE, secure);
    else
        snprintf(ss.header, n, "%s=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0%s", cookie_name(),
                 secure);
    free(cookie);
    return 0;
}

/* Set-Cookie header value to emit, or NULL. */
const char *ioi_session_header(void)
{
    return ss.changed ? ss.header : NULL;
}
