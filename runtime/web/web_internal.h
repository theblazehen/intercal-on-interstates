/* web_internal.h — helpers shared inside runtime/web/ and by tests/web.
 * Not part of the generated-code contract (that is include/ioi_web.h). */
#ifndef IOI_WEB_INTERNAL_H
#define IOI_WEB_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* ---- growable byte buffer ---- */
struct ioi_buf {
    char *p;
    size_t len, cap;
};
void ioi_buf_put(struct ioi_buf *b, const void *data, size_t len);
void ioi_buf_puts(struct ioi_buf *b, const char *s);
void ioi_buf_putc(struct ioi_buf *b, char c);
void ioi_buf_printf(struct ioi_buf *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
/* HTML-escape & < > " ' ; drop NUL. */
void ioi_buf_escape(struct ioi_buf *b, const void *data, size_t len);
void *ioi_xmalloc(size_t n);
void *ioi_xrealloc(void *p, size_t n);

/* ---- SHA-256 / HMAC / PBKDF2 ---- */
struct ioi_sha256 {
    uint32_t h[8];
    uint64_t total;
    uint8_t block[64];
    size_t fill;
};
void ioi_sha256_init(struct ioi_sha256 *s);
void ioi_sha256_update(struct ioi_sha256 *s, const void *data, size_t len);
void ioi_sha256_final(struct ioi_sha256 *s, uint8_t out[32]);
void ioi_sha256(const void *data, size_t len, uint8_t out[32]);
void ioi_hmac_sha256(const void *key, size_t keylen, const void *msg, size_t msglen,
                     uint8_t out[32]);
void ioi_pbkdf2_sha256(const void *pw, size_t pwlen, const void *salt, size_t saltlen,
                       uint32_t iterations, uint8_t *out, size_t outlen);
/* Constant-time comparison: 1 when equal. */
int ioi_ct_equal(const void *a, const void *b, size_t len);
/* Fill with bytes from /dev/urandom; 0 on success. */
int ioi_random_bytes(void *out, size_t len);
void ioi_hex(const uint8_t *in, size_t len, char *out /* 2*len+1 */);

/* Password hashing: "pbkdf2-sha256$ITER$SALTHEX$HASHHEX". */
#define IOI_PBKDF2_PREFIX "pbkdf2-sha256$"
#define IOI_PBKDF2_ITERATIONS 20000u
/* Returns a malloc'd NUL-terminated string, or NULL when no randomness. */
char *ioi_password_hash(const void *pw, size_t len);
/* 1 when PW matches STORED (a well-formed hash string). */
int ioi_password_verify(const void *pw, size_t len, const void *stored, size_t storedlen);
int ioi_password_is_hash(const void *s, size_t len);

/* ---- request helpers ---- */
/* Decode application/x-www-form-urlencoded bytes ('+' -> space, %XX).
 * Invalid escapes are kept literally. Returns new length (in place). */
size_t ioi_urldecode(char *s, size_t len);
/* Find NAME in an urlencoded string; returns 1 and a malloc'd decoded copy. */
int ioi_form_lookup(const char *qs, size_t qslen, const char *name,
                    char **value, size_t *vlen);
/* Find cookie NAME in a Cookie header; returns malloc'd value or NULL. */
char *ioi_cookie_lookup(const char *header, const char *name);
/* Same-origin rule on explicit headers. */
int ioi_same_origin_check(const char *host, const char *origin, const char *referer);
/* Reset all request/response/session state (tests run several requests in
 * one process). Does not touch the database. */
void ioi_web_reset(void);

/* ---- sessions ---- */
/* Cookie value for VALUE issued at NOW, signed with SECRET. Malloc'd. */
char *ioi_session_encode(uint32_t value, uint32_t now, const char *secret);
/* 1 and *value when COOKIE is valid for SECRET at NOW. */
int ioi_session_decode(const char *cookie, uint32_t now, const char *secret, uint32_t *value);
#define IOI_SESSION_COOKIE "ioi_session"
#define IOI_SESSION_MAX_AGE 2592000u

/* ---- rendering ---- */
void ioi_prose_render(struct ioi_buf *out, const char *src, size_t len);
void ioi_web_response_reset(void);
void ioi_web_session_reset(void);
/* Set-Cookie value pending for this response, or NULL. */
const char *ioi_session_header(void);
/* Relative-time phrase for WHEN seen at NOW into out. */
void ioi_ago_render(struct ioi_buf *out, int64_t when, int64_t now);
/* Parse decimal seconds or "YYYY-MM-DD HH:MM:SS" (UTC); 1 on success. */
int ioi_parse_when(const char *s, size_t len, int64_t *when);
void ioi_face_render(struct ioi_buf *out, const char *name, size_t len);

/* ---- database ---- */
/* Close the database (also registered with atexit). */
void ioi_db_close(void);

#endif
