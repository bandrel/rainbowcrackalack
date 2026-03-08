#ifndef MASK_PARSE_H
#define MASK_PARSE_H

#include <stdint.h>
#include <stddef.h>
#include "shared.h"

typedef struct {
    char         chars[MAX_CHARSET_LEN];
    unsigned int size;
} MaskPosition;

typedef struct {
    MaskPosition positions[MAX_PLAINTEXT_LEN];
    int          length;
} Mask;

/* Returns 1 if the string contains '?' (indicating a hashcat-style mask). */
int is_mask_string(const char *s);

/*
 * Parse a hashcat-style mask string into a Mask.
 * Supports ?l ?u ?d ?s ?a ?b and custom ?1-?4 (pass NULL if unused).
 * Literal characters are single-char positions.
 * Returns 0 on success, -1 on error (writes message to stderr).
 */
int mask_parse(const char *mask_str, Mask *out,
               const char *c1, const char *c2,
               const char *c3, const char *c4);

/* Product of per-position charset sizes. */
uint64_t mask_keyspace(const Mask *m);

/*
 * Encode a mask string for safe use in a filename by replacing '?' with '%'.
 * E.g. "?u?l?d?s" -> "%u%l%d%s".  dst must be at least dst_len bytes.
 */
void mask_encode_for_filename(const char *src, char *dst, size_t dst_len);

/*
 * Decode a filename-encoded mask back to standard '?' notation in-place.
 * E.g. "%u%l%d%s" -> "?u?l?d?s".  Only converts '%' followed by a valid
 * mask specifier character (l u d s a b 1 2 3 4).
 */
void mask_decode_from_filename(char *s);

/*
 * Fill flat GPU buffers from a parsed mask:
 *   mask_data[i * MAX_CHARSET_LEN .. +size-1] = chars for position i
 *   mask_lens[i] = size of charset at position i
 */
void mask_to_gpu_buffers(const Mask *m,
                         char         mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN],
                         unsigned int mask_lens[MAX_PLAINTEXT_LEN]);

#endif /* MASK_PARSE_H */
