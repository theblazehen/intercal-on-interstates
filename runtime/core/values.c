/* values.c — Interstates values: implicit conversions between INTERCAL
 * storage and cassettes (SPEC.md §2.2) and the template renderer's
 * helpers (SPEC.md §4). */
#include "ioi_core.h"
#include "ioi_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t ioi_num(uint32_t v)
{
    if (!ioi_is_cassette(v))
        return v;
    switch (ioi_val_kind(v)) {
    case IOI_NUMBER:
        return ioi_val_to_number(v, NULL);
    case IOI_BYTES: {
        int ok;
        uint32_t n = ioi_val_to_number(v, &ok);
        if (!ok)
            ioi_lose(410, ioi_lineno, NULL);
        return n;
    }
    default:
        ioi_lose(411, ioi_lineno, NULL);
    }
}

uint32_t ioi_val_sub(uint32_t v, uint32_t k)
{
    if (ioi_val_kind(v) != IOI_LIST)
        ioi_lose(411, ioi_lineno, NULL);
    if (k == 0 || k > ioi_cas_len(v))
        ioi_lose(241, ioi_lineno, NULL);
    return ioi_cas_get(v, k - 1);
}

static size_t nelem(const ioi_array *a)
{
    size_t n = a->rank ? 1 : 0;
    for (uint32_t i = 0; i < a->rank; i++)
        n *= a->dims[i];
    return n;
}

uint32_t ioi_tail_value(const ioi_array *a)
{
    size_t n = nelem(a);
    uint8_t *buf = malloc(n ? n : 1);
    if (!buf)
        ioi_lose(991, ioi_lineno, NULL);
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)((const uint16_t *)a->data)[i];
    uint32_t v = ioi_cas_bytes(buf, n);
    free(buf);
    return v;
}

uint32_t ioi_hyb_value(const ioi_array *a)
{
    uint32_t l = ioi_cas_list();
    size_t n = nelem(a);
    for (size_t i = 0; i < n; i++)
        ioi_cas_push(l, ((const uint32_t *)a->data)[i]);
    return l;
}

/* (Re)dimension a one-dimensional array to N elements, zero-filled. */
static void dim1(ioi_array *a, int wide, size_t n)
{
    void *data = calloc(n ? n : 1, wide ? 4 : 2);
    uint32_t *d = malloc(sizeof *d);
    if (!data || !d)
        ioi_lose(991, ioi_lineno, NULL);
    free(a->data);
    free(a->dims);
    a->rank = 1;
    a->dims = d;
    a->dims[0] = (uint32_t)n;
    a->data = data;
}

void ioi_tail_set_bytes(ioi_array *a, int ign, const void *data, size_t len)
{
    if (ign)
        return;
    if (len > 0xffffffffu)
        ioi_lose(241, ioi_lineno, NULL);
    dim1(a, 0, len);
    for (size_t i = 0; i < len; i++)
        ((uint16_t *)a->data)[i] = ((const uint8_t *)data)[i];
}

static void dim_number(ioi_array *a, int wide, int ign, uint32_t n)
{
    ioi_resize(a, wide, ign, 1, &n);
}

void ioi_tail_assign(ioi_array *a, int ign, uint32_t v)
{
    switch (ioi_val_kind(v)) {
    case IOI_NUMBER:
        dim_number(a, 0, ign, ioi_val_to_number(v, NULL));
        break;
    case IOI_BYTES: {
        size_t n;
        const uint8_t *d = ioi_cas_data(v, &n);
        ioi_tail_set_bytes(a, ign, d, n);
        break;
    }
    default:
        ioi_lose(411, ioi_lineno, NULL);
    }
}

void ioi_hyb_assign(ioi_array *a, int ign, uint32_t v)
{
    switch (ioi_val_kind(v)) {
    case IOI_NUMBER:
        dim_number(a, 1, ign, ioi_val_to_number(v, NULL));
        break;
    case IOI_LIST: {
        if (ign)
            return;
        size_t n = ioi_cas_len(v);
        dim1(a, 1, n);
        for (size_t i = 0; i < n; i++)
            ((uint32_t *)a->data)[i] = ioi_cas_get(v, i);
        break;
    }
    default:
        ioi_lose(414, ioi_lineno, NULL);
    }
}

void ioi_tail_elem_assign(uint16_t *p, int ign, uint32_t v)
{
    if (ioi_val_kind(v) != IOI_NUMBER)
        ioi_lose(412, ioi_lineno, NULL);
    ioi_set16(p, ign, ioi_val_to_number(v, NULL));
}

static void *grow(ioi_array *a, int wide, uint32_t k)
{
    size_t esz = wide ? 4 : 2;
    if (a->rank == 0)
        dim1(a, wide, 0);
    if (a->rank != 1 || k == 0)
        ioi_lose(241, ioi_lineno, NULL);
    if (k > a->dims[0]) {
        void *data = realloc(a->data, (size_t)k * esz);
        if (!data)
            ioi_lose(991, ioi_lineno, NULL);
        memset((char *)data + (size_t)a->dims[0] * esz, 0, (size_t)(k - a->dims[0]) * esz);
        a->data = data;
        a->dims[0] = k;
    }
    return (char *)a->data + (size_t)(k - 1) * esz;
}

uint16_t *ioi_aref16_grow(ioi_array *a, uint32_t k) { return grow(a, 0, k); }
uint32_t *ioi_aref32_grow(ioi_array *a, uint32_t k) { return grow(a, 1, k); }

/* ---- templates ------------------------------------------------------------ */

void ioi_tpl_text(const char *s, size_t n) { ioi_web_append_raw(s, n); }

void ioi_tpl_roman(uint32_t n)
{
    char buf[96];
    ioi_roman(n, buf);
    if (n == 0) {
        ioi_web_append_raw("_", 1);
        return;
    }
    char *nl = strchr(buf, '\n');
    const char *bar = buf, *let = nl + 1;
    size_t len = strlen(let);
    int inbar = 0;
    for (size_t i = 0; i < len; i++) {
        int b = bar[i] == '_';
        if (b && !inbar)
            ioi_web_append_raw("<span class=\"ick-bar\">", 22);
        if (!b && inbar)
            ioi_web_append_raw("</span>", 7);
        inbar = b;
        ioi_web_append_raw(let + i, 1);
    }
    if (inbar)
        ioi_web_append_raw("</span>", 7);
}

static void decimal(uint32_t n)
{
    char b[16];
    int l = snprintf(b, sizeof b, "%u", n);
    ioi_web_append_raw(b, (size_t)l);
}

void ioi_tpl_value(uint32_t v, int filter)
{
    switch (ioi_val_kind(v)) {
    case IOI_LIST:
        for (size_t i = 0; i < ioi_cas_len(v); i++)
            ioi_tpl_value(ioi_cas_get(v, i), filter);
        return;
    case IOI_BYTES: {
        size_t n;
        const uint8_t *d = ioi_cas_data(v, &n);
        int ok;
        uint32_t num;
        switch (filter) {
        case IOI_F_RAW:
            ioi_web_append_raw(d, n);
            return;
        case IOI_F_PROSE:
            ioi_web_append_prose(d, n);
            return;
        case IOI_F_AGO:
            ioi_web_append_ago(v);
            return;
        case IOI_F_FACE:
            ioi_web_append_face(d, n);
            return;
        case IOI_F_ARABIC:
        case IOI_F_ROMAN:
            num = ioi_val_to_number(v, &ok);
            if (!ok) {
                ioi_web_append_escaped(d, n); /* not a number: show it as text */
                return;
            }
            if (filter == IOI_F_ARABIC)
                decimal(num);
            else
                ioi_tpl_roman(num);
            return;
        default:
            ioi_web_append_escaped(d, n);
            return;
        }
    }
    default: {
        uint32_t num = ioi_val_to_number(v, NULL);
        switch (filter) {
        case IOI_F_ARABIC:
            decimal(num);
            return;
        case IOI_F_AGO:
            ioi_web_append_ago(v);
            return;
        case IOI_F_FACE: {
            char b[16];
            int l = snprintf(b, sizeof b, "%u", num);
            ioi_web_append_face(b, (size_t)l);
            return;
        }
        default:
            ioi_tpl_roman(num);
            return;
        }
    }
    }
}

void ioi_tpl_number(uint32_t n, int filter) { ioi_tpl_value(ioi_val_number(n), filter); }

/* Put an extracted element into variable number NUM (1..144). */
static void place(const struct ioi_tpl_vars *tv, uint32_t v, int num)
{
    if (num < 1 || num > 144)
        return;
    switch (ioi_val_kind(v)) {
    case IOI_LIST:
        if (tv->hyb[num])
            ioi_hyb_assign(tv->hyb[num], 0, v);
        break;
    case IOI_BYTES:
        if (tv->tail[num]) {
            size_t n;
            const uint8_t *d = ioi_cas_data(v, &n);
            ioi_tail_set_bytes(tv->tail[num], 0, d, n);
        }
        break;
    default:
        if (tv->tail[num]) {
            char b[16];
            int l = snprintf(b, sizeof b, "%u", ioi_val_to_number(v, NULL));
            ioi_tail_set_bytes(tv->tail[num], 0, b, (size_t)l);
        }
        break;
    }
}

void ioi_tpl_extract(const struct ioi_tpl_vars *tv, ioi_array *params, uint32_t n)
{
    for (uint32_t k = 1; k <= n; k++) {
        if (!params)
            ioi_lose(241, ioi_lineno, NULL);
        uint32_t v = *ioi_aref32(params, 1, &k);
        place(tv, v, 145 - (int)k);
        if (k == 144)
            break;
    }
}

struct ioi_tpl_save {
    ioi_array tail[145], hyb[145];
    uint16_t dot144;
};

static ioi_array copy_array(const ioi_array *a, size_t esz)
{
    ioi_array c = {0, NULL, NULL};
    size_t n = nelem(a);
    c.rank = a->rank;
    if (a->rank) {
        c.dims = malloc(a->rank * sizeof *c.dims);
        c.data = malloc(n ? n * esz : 1);
        if (!c.dims || !c.data)
            ioi_lose(991, ioi_lineno, NULL);
        memcpy(c.dims, a->dims, a->rank * sizeof *c.dims);
        if (n)
            memcpy(c.data, a->data, n * esz);
    }
    return c;
}

struct ioi_tpl_save *ioi_tpl_save(const struct ioi_tpl_vars *tv)
{
    struct ioi_tpl_save *s = calloc(1, sizeof *s);
    if (!s)
        ioi_lose(991, ioi_lineno, NULL);
    for (int k = 1; k <= 144; k++) {
        if (tv->tail[k])
            s->tail[k] = copy_array(tv->tail[k], 2);
        if (tv->hyb[k])
            s->hyb[k] = copy_array(tv->hyb[k], 4);
    }
    if (tv->dot144)
        s->dot144 = *tv->dot144;
    return s;
}

void ioi_tpl_restore(const struct ioi_tpl_vars *tv, struct ioi_tpl_save *s)
{
    for (int k = 1; k <= 144; k++) {
        if (tv->tail[k]) {
            free(tv->tail[k]->dims);
            free(tv->tail[k]->data);
            *tv->tail[k] = s->tail[k];
        }
        if (tv->hyb[k]) {
            free(tv->hyb[k]->dims);
            free(tv->hyb[k]->data);
            *tv->hyb[k] = s->hyb[k];
        }
    }
    if (tv->dot144)
        *tv->dot144 = s->dot144;
    free(s);
}

uint32_t ioi_tpl_loop_list(uint32_t v)
{
    if (ioi_val_kind(v) != IOI_LIST)
        ioi_lose(411, ioi_lineno, NULL);
    return v;
}

void ioi_tpl_item(const struct ioi_tpl_vars *tv, uint32_t elem, uint32_t index)
{
    if (tv->dot144)
        *tv->dot144 = (uint16_t)(index > 65535 ? 65535 : index);
    if (ioi_val_kind(elem) == IOI_LIST) {
        size_t n = ioi_cas_len(elem);
        for (size_t j = 1; j <= n && j <= 144; j++)
            place(tv, ioi_cas_get(elem, j - 1), 145 - (int)j);
    } else {
        place(tv, elem, 144);
    }
}
