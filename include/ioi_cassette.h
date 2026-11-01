/* ioi_cassette.h — the shared value contract of INTERCAL on Interstates.
 *
 * A "value" is a uint32_t. Values below IOI_CAS_TAG are plain numbers.
 * Values with the tag bit set are cassette handles: references to a byte
 * string (BYTES), a list of values (LIST), or a boxed number >= 2^31 (NUMBER).
 * Cassettes live in a per-process arena and are never freed: one process
 * serves one request.
 *
 * Hybrid (;) array elements and template/DB values carry these uint32s.
 * Tail (,) arrays never hold cassettes; a string in a tail array is one
 * byte per element.
 */
#ifndef IOI_CASSETTE_H
#define IOI_CASSETTE_H

#include <stddef.h>
#include <stdint.h>

#define IOI_CAS_TAG 0x80000000u

enum ioi_kind { IOI_NUMBER = 1, IOI_BYTES = 2, IOI_LIST = 3 };

static inline int ioi_is_cassette(uint32_t v) { return (v & IOI_CAS_TAG) != 0; }

/* Constructors. */
uint32_t ioi_cas_bytes(const void *data, size_t len);
uint32_t ioi_cas_cstr(const char *s);
uint32_t ioi_cas_list(void);
/* Plain value for n < 2^31, NUMBER cassette otherwise. */
uint32_t ioi_val_number(uint32_t n);
/* Append any value (plain number or cassette) to a LIST cassette. */
void ioi_cas_push(uint32_t list, uint32_t item);

/* Accessors. Every value has a kind; plain numbers are IOI_NUMBER. */
enum ioi_kind ioi_val_kind(uint32_t v);
/* Numeric value of a NUMBER value; BYTES holding decimal digits are parsed
 * (sets *ok = 0 when not a number; ok may be NULL). LIST yields 0, !ok. */
uint32_t ioi_val_to_number(uint32_t v, int *ok);
/* BYTES contents (NULL/0 for other kinds). Not NUL-terminated. */
const uint8_t *ioi_cas_data(uint32_t v, size_t *len);
/* LIST length / element (0 when out of range or not a list). Index is 0-based. */
size_t ioi_cas_len(uint32_t v);
uint32_t ioi_cas_get(uint32_t list, size_t i);

/* Truthiness used by templates: number != 0, bytes non-empty, list non-empty. */
int ioi_val_truthy(uint32_t v);
/* Equality used by templates' IS: numbers compare numerically; BYTES vs
 * NUMBER compares the decimal rendering; BYTES vs BYTES bytewise; LISTs
 * elementwise. */
int ioi_val_equal(uint32_t a, uint32_t b);

#endif
