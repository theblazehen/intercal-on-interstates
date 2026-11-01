/* crypto.c — SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104), PBKDF2-HMAC-SHA256
 * (RFC 8018) and the password hash format used by PASSWORD columns. */
#include "web_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* One SHA-256 block given as 16 big-endian words. */
static void compress_words(uint32_t h[8], const uint32_t block[16])
{
    uint32_t w[64];
    memcpy(w, block, 16 * sizeof *w);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void compress(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    compress_words(h, w);
}

void ioi_sha256_init(struct ioi_sha256 *s)
{
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, iv, sizeof iv);
    s->total = 0;
    s->fill = 0;
}

void ioi_sha256_update(struct ioi_sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->total += len;
    if (s->fill) {
        size_t n = 64 - s->fill < len ? 64 - s->fill : len;
        memcpy(s->block + s->fill, p, n);
        s->fill += n;
        p += n;
        len -= n;
        if (s->fill < 64)
            return;
        compress(s->h, s->block);
        s->fill = 0;
    }
    while (len >= 64) {
        compress(s->h, p);
        p += 64;
        len -= 64;
    }
    memcpy(s->block, p, len);
    s->fill = len;
}

void ioi_sha256_final(struct ioi_sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->total * 8;
    s->block[s->fill++] = 0x80;
    if (s->fill > 56) {
        memset(s->block + s->fill, 0, 64 - s->fill);
        compress(s->h, s->block);
        s->fill = 0;
    }
    memset(s->block + s->fill, 0, 56 - s->fill);
    for (int i = 0; i < 8; i++)
        s->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    compress(s->h, s->block);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void ioi_sha256(const void *data, size_t len, uint8_t out[32])
{
    struct ioi_sha256 s;
    ioi_sha256_init(&s);
    ioi_sha256_update(&s, data, len);
    ioi_sha256_final(&s, out);
}

/* HMAC with precomputed inner/outer states, so PBKDF2 hashes the padded
 * key only once. */
struct hmac {
    struct ioi_sha256 inner, outer;
};

static void hmac_init(struct hmac *m, const void *key, size_t keylen)
{
    uint8_t k[64] = {0}, pad[64];
    if (keylen > 64)
        ioi_sha256(key, keylen, k);
    else if (keylen)
        memcpy(k, key, keylen);
    for (int i = 0; i < 64; i++)
        pad[i] = k[i] ^ 0x36;
    ioi_sha256_init(&m->inner);
    ioi_sha256_update(&m->inner, pad, 64);
    for (int i = 0; i < 64; i++)
        pad[i] = k[i] ^ 0x5c;
    ioi_sha256_init(&m->outer);
    ioi_sha256_update(&m->outer, pad, 64);
}

static void hmac_run(const struct hmac *m, const void *msg, size_t len, const void *msg2,
                     size_t len2, uint8_t out[32])
{
    struct ioi_sha256 s = m->inner;
    uint8_t ih[32];
    ioi_sha256_update(&s, msg, len);
    if (len2)
        ioi_sha256_update(&s, msg2, len2);
    ioi_sha256_final(&s, ih);
    s = m->outer;
    ioi_sha256_update(&s, ih, 32);
    ioi_sha256_final(&s, out);
}

void ioi_hmac_sha256(const void *key, size_t keylen, const void *msg, size_t msglen,
                     uint8_t out[32])
{
    struct hmac m;
    hmac_init(&m, key, keylen);
    hmac_run(&m, msg, msglen, NULL, 0, out);
}

void ioi_pbkdf2_sha256(const void *pw, size_t pwlen, const void *salt, size_t saltlen,
                       uint32_t iterations, uint8_t *out, size_t outlen)
{
    struct hmac m;
    hmac_init(&m, pw, pwlen);
    /* After the first round every HMAC input and inner digest is exactly 32
     * bytes, so each round is two compressions on pre-padded blocks: the
     * 64-byte key pad plus 32 bytes is 768 bits. */
    uint32_t inner[16] = {0}, outer[16] = {0};
    inner[8] = outer[8] = 0x80000000u;
    inner[15] = outer[15] = 768;
    for (uint32_t block = 1; outlen; block++) {
        uint8_t be[4] = {(uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8),
                         (uint8_t)block};
        uint8_t u[32];
        uint32_t t[8];
        hmac_run(&m, salt, saltlen, be, 4, u);
        for (int j = 0; j < 8; j++)
            t[j] = inner[j] = (uint32_t)u[4 * j] << 24 | (uint32_t)u[4 * j + 1] << 16 |
                              (uint32_t)u[4 * j + 2] << 8 | u[4 * j + 3];
        for (uint32_t i = 1; i < iterations; i++) {
            uint32_t h[8];
            memcpy(h, m.inner.h, sizeof h);
            compress_words(h, inner);
            memcpy(outer, h, sizeof h);
            memcpy(h, m.outer.h, sizeof h);
            compress_words(h, outer);
            for (int j = 0; j < 8; j++) {
                inner[j] = h[j];
                t[j] ^= h[j];
            }
        }
        for (int j = 0; j < 8; j++) {
            u[4 * j] = (uint8_t)(t[j] >> 24);
            u[4 * j + 1] = (uint8_t)(t[j] >> 16);
            u[4 * j + 2] = (uint8_t)(t[j] >> 8);
            u[4 * j + 3] = (uint8_t)t[j];
        }
        size_t n = outlen < 32 ? outlen : 32;
        memcpy(out, u, n);
        out += n;
        outlen -= n;
    }
}

int ioi_ct_equal(const void *a, const void *b, size_t len)
{
    const volatile uint8_t *x = a, *y = b;
    uint8_t d = 0;
    for (size_t i = 0; i < len; i++)
        d |= x[i] ^ y[i];
    return d == 0;
}

int ioi_random_bytes(void *out, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    uint8_t *p = out;
    while (len) {
        ssize_t n = read(fd, p, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    close(fd);
    return 0;
}

void ioi_hex(const uint8_t *in, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * len] = 0;
}

static int unhex(const char *s, size_t len, uint8_t *out)
{
    if (len % 2)
        return -1;
    for (size_t i = 0; i < len; i++) {
        int c = (unsigned char)s[i], v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return -1;
        if (i % 2)
            out[i / 2] = (uint8_t)(out[i / 2] << 4 | v);
        else
            out[i / 2] = (uint8_t)v;
    }
    return 0;
}

#define SALT_LEN 16
#define HASH_LEN 32

char *ioi_password_hash(const void *pw, size_t len)
{
    uint8_t salt[SALT_LEN], dk[HASH_LEN];
    char salthex[2 * SALT_LEN + 1], dkhex[2 * HASH_LEN + 1];
    if (ioi_random_bytes(salt, sizeof salt))
        return NULL;
    ioi_pbkdf2_sha256(pw, len, salt, sizeof salt, IOI_PBKDF2_ITERATIONS, dk, sizeof dk);
    ioi_hex(salt, sizeof salt, salthex);
    ioi_hex(dk, sizeof dk, dkhex);
    size_t n = sizeof IOI_PBKDF2_PREFIX + 12 + sizeof salthex + sizeof dkhex;
    char *out = ioi_xmalloc(n);
    snprintf(out, n, IOI_PBKDF2_PREFIX "%u$%s$%s", IOI_PBKDF2_ITERATIONS, salthex, dkhex);
    return out;
}

/* Split a stored hash into iterations, salt and digest. 0 on success. */
static int parse_hash(const char *s, size_t len, uint32_t *iter, uint8_t *salt, size_t *saltlen,
                      uint8_t *dk, size_t *dklen)
{
    size_t plen = sizeof IOI_PBKDF2_PREFIX - 1;
    if (len <= plen || memcmp(s, IOI_PBKDF2_PREFIX, plen))
        return -1;
    const char *p = s + plen, *end = s + len;
    uint64_t it = 0;
    if (p == end || *p == '$')
        return -1;
    while (p < end && *p != '$') {
        if (*p < '0' || *p > '9' || (it = it * 10 + (uint64_t)(*p - '0')) > 100000000)
            return -1;
        p++;
    }
    if (p == end || it == 0)
        return -1;
    const char *salt_s = ++p;
    while (p < end && *p != '$')
        p++;
    if (p == end)
        return -1;
    size_t sl = (size_t)(p - salt_s), hl = (size_t)(end - p - 1);
    if (sl == 0 || sl > 128 || hl == 0 || hl > 128)
        return -1;
    if (unhex(salt_s, sl, salt) || unhex(p + 1, hl, dk))
        return -1;
    *iter = (uint32_t)it;
    *saltlen = sl / 2;
    *dklen = hl / 2;
    return 0;
}

int ioi_password_is_hash(const void *s, size_t len)
{
    uint8_t salt[64], dk[64];
    size_t sl, dl;
    uint32_t it;
    return parse_hash(s, len, &it, salt, &sl, dk, &dl) == 0;
}

int ioi_password_verify(const void *pw, size_t len, const void *stored, size_t storedlen)
{
    uint8_t salt[64], dk[64], got[64];
    size_t sl, dl;
    uint32_t it;
    if (parse_hash(stored, storedlen, &it, salt, &sl, dk, &dl))
        return 0;
    ioi_pbkdf2_sha256(pw, len, salt, sl, it, got, dl);
    return ioi_ct_equal(got, dk, dl);
}
