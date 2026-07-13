#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "markov.h"
#include "mask_parse.h"
#include "markov_mask.h"
#include "test_markov_mask.h"

/* Build an in-memory markov model over charset "abc" with explicit freqs. */
static void make_model_abc(markov_model *m) {
    memset(m, 0, sizeof(*m));
    m->charset_len = 3; m->max_positions = 16;
    memcpy(m->charset, "abc", 3);
    m->pos0_freq   = calloc(3, sizeof(uint64_t));
    size_t bg = (size_t)m->max_positions * 3 * 3;
    m->bigram_freq = calloc(bg, sizeof(uint64_t));
    m->sorted_pos0 = calloc(3, sizeof(uint8_t));
    m->sorted_bigram = calloc(bg, sizeof(uint8_t));
    /* pos0: c(2) most frequent, then a(0), then b(1). */
    m->pos0_freq[0] = 5; m->pos0_freq[1] = 1; m->pos0_freq[2] = 9;
    /* bigram at pos0 (prev at index 2 = 'c'): next 'b'(1) most frequent. */
    /* offset = pos*(9) + prev*3 + cur ; pos here means the bigram table index. */
    size_t off = (size_t)0 * 9 + (size_t)2 * 3; /* pos table 0, prev 'c' */
    m->bigram_freq[off + 0] = 2; /* c->a */
    m->bigram_freq[off + 1] = 7; /* c->b */
    m->bigram_freq[off + 2] = 1; /* c->c */
    markov_build_sorted(m);
}

static int group_build_and_decode(void) {
    int ok = 1;
    markov_model m; make_model_abc(&m);
    Mask mask;
    /* mask ?1?1 with ?1='abc' (full charset) so sizes = {3,3}. */
    if (mask_parse("?1?1", &mask, "abc", NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MM-setup failed: mask_parse\n"); markov_free(&m); return 0;
    }
    markov_mask_tables t;
    if (markov_build_restricted(&m, &mask, &t) != 0) {
        fprintf(stderr, "MM-01 failed: build_restricted\n"); markov_free(&m); return 0;
    }
    /* MM-01: sizes + keyspace */
    if (t.mask_len != 2 || t.sizes[0] != 3 || t.sizes[1] != 3 ||
        markov_mask_keyspace(&t) != 9) {
        fprintf(stderr, "MM-01 failed: sizes/keyspace\n"); ok = 0;
    }
    /* MM-02: pos0 order = c,a,b (freqs 9,5,1) -> charset indices 2,0,1 */
    if (t.r_pos0[0] != 2 || t.r_pos0[1] != 0 || t.r_pos0[2] != 1) {
        fprintf(stderr, "MM-02 failed: r_pos0 order %u,%u,%u\n",
                t.r_pos0[0], t.r_pos0[1], t.r_pos0[2]); ok = 0;
    }
    /* MM-03: index 0 -> most probable plaintext.
     * pos0 -> 'c'(idx2); pos1 given prev 'c' -> 'b'(freq 7 highest). => "cb". */
    {
        unsigned char pt[MAX_PLAINTEXT_LEN]; unsigned int len = 0;
        index_to_plaintext_markov_mask_cpu(&t, 0, pt, &len);
        if (len != 2 || pt[0] != 'c' || pt[1] != 'b') {
            fprintf(stderr, "MM-03 failed: index0 -> '%.*s'\n", (int)len, pt); ok = 0;
        }
    }
    /* MM-04: mixed-radix — index 1 advances position 0 (least-significant).
     * index 1: pos0 rank1 -> 'a'(idx0); pos1 given prev 'a' uses default row
     * (all-zero freq -> stable order a,b,c) rank0 -> 'a'. => "aa". */
    {
        unsigned char pt[MAX_PLAINTEXT_LEN]; unsigned int len = 0;
        index_to_plaintext_markov_mask_cpu(&t, 1, pt, &len);
        if (len != 2 || pt[0] != 'a' || pt[1] != 'a') {
            fprintf(stderr, "MM-04 failed: index1 -> '%.*s'\n", (int)len, pt); ok = 0;
        }
    }
    markov_mask_tables_free(&t);
    markov_free(&m);
    return ok;
}

static int group_subset_violation(void) {
    int ok = 1;
    markov_model m; make_model_abc(&m);
    Mask mask;
    /* mask has 'z' which is not in charset "abc" -> build must fail. */
    if (mask_parse("?1", &mask, "az", NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MM-05 setup failed\n"); markov_free(&m); return 0;
    }
    markov_mask_tables t;
    if (markov_build_restricted(&m, &mask, &t) == 0) {
        fprintf(stderr, "MM-05 failed: subset violation accepted\n");
        markov_mask_tables_free(&t); ok = 0;
    }
    markov_free(&m);
    return ok;
}

int test_markov_mask(void) {
    int ok = 1;
    if (!group_build_and_decode()) ok = 0;
    if (!group_subset_violation()) ok = 0;
    return ok;
}
