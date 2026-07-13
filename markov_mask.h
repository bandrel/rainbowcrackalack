/* markov_mask.h */
#ifndef MARKOV_MASK_H
#define MARKOV_MASK_H
#include <stdint.h>
#include "shared.h"        /* MAX_PLAINTEXT_LEN, MAX_CHARSET_LEN */
#include "markov.h"
#include "mask_parse.h"

typedef struct {
    unsigned int charset_len;                  /* from the markov model */
    char         charset[256];                 /* markov charset bytes (copied) */
    unsigned int mask_len;                     /* number of positions */
    unsigned int sizes[MAX_PLAINTEXT_LEN];     /* sz[i] = mask[i] size (per-pos radix) */
    unsigned int max_sz;                       /* max(sizes) = r_bigram row stride */
    uint8_t      r_pos0[256];                  /* first sizes[0] = charset indices, pos0-sorted */
    uint8_t     *r_bigram;                     /* [mask_len * charset_len * max_sz] indices */
} markov_mask_tables;

/* Build restricted tables. Every mask-position char MUST be in mk->charset,
 * else -1 with a message. Returns 0 on success; caller frees via
 * markov_mask_tables_free(). */
int markov_build_restricted(const markov_model *mk, const Mask *mask,
                            markov_mask_tables *out);
void markov_mask_tables_free(markov_mask_tables *t);

/* Product of sizes[0..mask_len-1] with uint64 overflow guard (0 on overflow). */
uint64_t markov_mask_keyspace(const markov_mask_tables *t);

/* Mixed-radix Markov-conditional decode restricted to the mask. */
void index_to_plaintext_markov_mask_cpu(const markov_mask_tables *t,
                                        uint64_t index,
                                        unsigned char *plaintext,
                                        unsigned int *plaintext_len);

/* Fixed-length plaintext-space fill (tiers < mask_len = 0; tier mask_len =
 * keyspace, or the truncated markov_keyspace when smaller and non-zero).
 * Returns the effective total. */
uint64_t fill_plaintext_space_markov_mask(const markov_mask_tables *t,
                                          uint64_t markov_keyspace,
                                          uint64_t *plaintext_space_up_to_index);

/* Flatten to GPU buffers (fixed-size, zero-padded). */
void markov_mask_tables_to_gpu_buffers(const markov_mask_tables *t,
                                       uint8_t r_pos0_out[256],
                                       uint8_t *r_bigram_out /* mask_len*charset_len*max_sz */);
#endif
