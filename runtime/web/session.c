/* session.c — the session number, kept in an HMAC-SHA256-signed cookie.
 *
 * Cookie value: "v1.<value>.<issued>.<hex hmac>" where the HMAC (keyed by
 * IOI_SECRET) covers "v1.<value>.<issued>". Valid for IOI_SESSION_MAX_AGE
 * seconds after issue. */
#include "ioi_web.h"
#include "web_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int loaded;
    uint32_t value;
    int changed;     /* set during this request: emit Set-Cookie */
    char *header;    /* Set-Cookie value to send */
} ss;

void ioi_web_session_reset(void)
{
    free(ss.header);
    memset(&ss, 0, sizeof ss);
}

static void mac_hex(const char *secret, const char *msg, char out[65])
{
    uint8_t mac[32];
    ioi_hmac_sha256(secret, strlen(secret), msg, strlen(msg), mac);
    ioi_hex(mac, 32, out);
}

char *ioi_session_encode(uint32_t value, uint32_t now, const char *secret)
{
    char msg[64], hex[65];
    snprintf(msg, sizeof msg, "v1.%u.%u", value, now);
    mac_hex(secret, msg, hex);
    size_t n = strlen(msg) + 1 + 64 + 1;
    char *out = ioi_xmalloc(n);
    snprintf(out, n, "%s.%s", msg, hex);
    return out;
}

static int parse_u32(const char *s, size_t n, uint32_t *out)
{
    uint64_t v = 0;
    if (n == 0 || n > 10)
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10 + (uint64_t)(s[i] - '0');
    }
    if (v > UINT32_MAX)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

int ioi_session_decode(const char *cookie, uint32_t now, const char *secret, uint32_t *value)
{
    if (!cookie || !secret || !*secret || strncmp(cookie, "v1.", 3))
        return 0;
    const char *last = strrchr(cookie, '.');
    if (!last || last - cookie > 40 || strlen(last + 1) != 64)
        return 0;
    char msg[64], hex[65];
    size_t mlen = (size_t)(last - cookie);
    memcpy(msg, cookie, mlen);
    msg[mlen] = 0;
    mac_hex(secret, msg, hex);
    if (!ioi_ct_equal(hex, last + 1, 64))
        return 0;
    const char *v = msg + 3, *dot = strchr(v, '.');
    uint32_t val, issued;
    if (!dot || !parse_u32(v, (size_t)(dot - v), &val) ||
        !parse_u32(dot + 1, strlen(dot + 1), &issued))
        return 0;
    if (issued > now + 300u || now - issued >= IOI_SESSION_MAX_AGE)
        return 0;
    *value = val;
    return 1;
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
    char *c = ioi_cookie_lookup(hdr, IOI_SESSION_COOKIE);
    uint32_t v;
    if (c && ioi_session_decode(c, ioi_web_time(), getenv("IOI_SECRET"), &v))
        ss.value = v;
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
    ss.value = value;
    ss.changed = 1;
    free(ss.header);
    const char *https = getenv("HTTPS");
    const char *secure = https && (!strcmp(https, "on") || !strcmp(https, "1")) ? "; Secure" : "";
    if (value == 0) {
        size_t n = 128;
        ss.header = ioi_xmalloc(n);
        snprintf(ss.header, n, IOI_SESSION_COOKIE "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0%s",
                 secure);
    } else {
        char *c = ioi_session_encode(value, ioi_web_time(), secret);
        size_t n = strlen(c) + 128;
        ss.header = ioi_xmalloc(n);
        snprintf(ss.header, n, IOI_SESSION_COOKIE "=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%u%s",
                 c, IOI_SESSION_MAX_AGE, secure);
        free(c);
    }
    return 0;
}

/* Set-Cookie header value to emit, or NULL. */
const char *ioi_session_header(void)
{
    return ss.changed ? ss.header : NULL;
}
