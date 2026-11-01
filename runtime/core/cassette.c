/* cassette.c — per-process cassette arena. See include/ioi_cassette.h. */
#include "ioi_cassette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cassette {
    enum ioi_kind kind;
    uint32_t number;   /* NUMBER */
    uint8_t *data;     /* BYTES: data/len; LIST: (uint32_t *)data, len items */
    size_t len, cap;
};

static struct cassette *arena;
static size_t arena_len, arena_cap;

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("ICL991I YOU HAVE TOO MUCH ROPE TO HANG YOURSELF\n", stderr);
        exit(1);
    }
    return q;
}

static uint32_t alloc_cassette(enum ioi_kind kind)
{
    if (arena_len == arena_cap) {
        arena_cap = arena_cap ? arena_cap * 2 : 256;
        arena = xrealloc(arena, arena_cap * sizeof *arena);
    }
    if (arena_len >= IOI_CAS_TAG - 1) {
        fputs("ICL991I YOU HAVE TOO MUCH ROPE TO HANG YOURSELF\n", stderr);
        exit(1);
    }
    struct cassette *c = &arena[arena_len];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    return IOI_CAS_TAG | (uint32_t)arena_len++;
}

static struct cassette *deref(uint32_t v)
{
    if (!ioi_is_cassette(v))
        return NULL;
    uint32_t i = v & ~IOI_CAS_TAG;
    return i < arena_len ? &arena[i] : NULL;
}

uint32_t ioi_cas_bytes(const void *data, size_t len)
{
    uint32_t h = alloc_cassette(IOI_BYTES);
    struct cassette *c = deref(h);
    c->data = xrealloc(NULL, len);
    if (len)
        memcpy(c->data, data, len);
    c->len = c->cap = len;
    return h;
}

uint32_t ioi_cas_cstr(const char *s) { return ioi_cas_bytes(s, strlen(s)); }

uint32_t ioi_cas_list(void) { return alloc_cassette(IOI_LIST); }

uint32_t ioi_val_number(uint32_t n)
{
    if (!ioi_is_cassette(n))
        return n;
    uint32_t h = alloc_cassette(IOI_NUMBER);
    deref(h)->number = n;
    return h;
}

void ioi_cas_push(uint32_t list, uint32_t item)
{
    struct cassette *c = deref(list);
    if (!c || c->kind != IOI_LIST)
        return;
    if (c->len == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->data = xrealloc(c->data, c->cap * sizeof(uint32_t));
    }
    ((uint32_t *)c->data)[c->len++] = item;
}

enum ioi_kind ioi_val_kind(uint32_t v)
{
    struct cassette *c = deref(v);
    return c ? c->kind : IOI_NUMBER;
}

uint32_t ioi_val_to_number(uint32_t v, int *ok)
{
    struct cassette *c = deref(v);
    if (ok)
        *ok = 1;
    if (!ioi_is_cassette(v))
        return v;
    if (c && c->kind == IOI_NUMBER)
        return c->number;
    if (c && c->kind == IOI_BYTES && c->len > 0 && c->len <= 10) {
        uint64_t n = 0;
        for (size_t i = 0; i < c->len; i++) {
            if (c->data[i] < '0' || c->data[i] > '9')
                goto bad;
            n = n * 10 + (uint64_t)(c->data[i] - '0');
        }
        if (n <= UINT32_MAX)
            return (uint32_t)n;
    }
bad:
    if (ok)
        *ok = 0;
    return 0;
}

const uint8_t *ioi_cas_data(uint32_t v, size_t *len)
{
    struct cassette *c = deref(v);
    if (!c || c->kind != IOI_BYTES) {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = c->len;
    return c->data;
}

size_t ioi_cas_len(uint32_t v)
{
    struct cassette *c = deref(v);
    return c && c->kind == IOI_LIST ? c->len : 0;
}

uint32_t ioi_cas_get(uint32_t list, size_t i)
{
    struct cassette *c = deref(list);
    if (!c || c->kind != IOI_LIST || i >= c->len)
        return 0;
    return ((uint32_t *)c->data)[i];
}

int ioi_val_truthy(uint32_t v)
{
    struct cassette *c = deref(v);
    if (!ioi_is_cassette(v))
        return v != 0;
    if (!c)
        return 0;
    switch (c->kind) {
    case IOI_NUMBER: return c->number != 0;
    case IOI_BYTES:
    case IOI_LIST: return c->len != 0;
    }
    return 0;
}

static size_t render_decimal(uint32_t n, char *buf)
{
    return (size_t)snprintf(buf, 11, "%u", n);
}

int ioi_val_equal(uint32_t a, uint32_t b)
{
    enum ioi_kind ka = ioi_val_kind(a), kb = ioi_val_kind(b);
    if (ka == IOI_LIST || kb == IOI_LIST) {
        if (ka != kb || ioi_cas_len(a) != ioi_cas_len(b))
            return 0;
        for (size_t i = 0; i < ioi_cas_len(a); i++)
            if (!ioi_val_equal(ioi_cas_get(a, i), ioi_cas_get(b, i)))
                return 0;
        return 1;
    }
    if (ka == IOI_NUMBER && kb == IOI_NUMBER)
        return ioi_val_to_number(a, NULL) == ioi_val_to_number(b, NULL);
    char ba[11], bb[11];
    size_t la, lb;
    const uint8_t *da, *db;
    if (ka == IOI_NUMBER) {
        la = render_decimal(ioi_val_to_number(a, NULL), ba);
        da = (const uint8_t *)ba;
    } else {
        da = ioi_cas_data(a, &la);
    }
    if (kb == IOI_NUMBER) {
        lb = render_decimal(ioi_val_to_number(b, NULL), bb);
        db = (const uint8_t *)bb;
    } else {
        db = ioi_cas_data(b, &lb);
    }
    return la == lb && (la == 0 || memcmp(da, db, la) == 0);
}
