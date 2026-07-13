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

/* Core parser taking PRE-EXPANDED custom charsets (or NULL for unused). */
int mask_parse_ex(const char *mask_str, Mask *out,
                  const MaskPosition *c1, const MaskPosition *c2,
                  const MaskPosition *c3, const MaskPosition *c4);

/* Expand a custom-charset definition string (literals, ?l ?u ?d ?s ?a ?b ?h ?H,
 * \xNN, ?? -> '?', \\ -> '\') into raw bytes.  Rejects ?1-?4 and unknown ?x.
 * Output may contain NUL bytes, so length is returned explicitly.
 * Returns 0 on success, -1 on error. */
int expand_charset_def(const char *def, char out[MAX_CHARSET_LEN],
                       unsigned int *out_len);

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
