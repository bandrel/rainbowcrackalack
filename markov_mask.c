#include "markov_mask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stable sort of `n` charset indices in `idx[]` by freq[idx] desc,
 * tie-break by ascending index. n is tiny (<=256) so insertion sort is fine. */
static void sort_indices_desc(uint8_t *idx, unsigned int n, const uint64_t *freq) {
    for (unsigned int i = 1; i < n; i++) {
        uint8_t cur = idx[i];
        int j = (int)i - 1;
        while (j >= 0 &&
               (freq[idx[j]] < freq[cur] ||
                (freq[idx[j]] == freq[cur] && idx[j] > cur))) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = cur;
    }
}

int markov_build_restricted(const markov_model *mk, const Mask *mask,
                            markov_mask_tables *out) {
    if (!mk || !mask || !out) return -1;
    if (mask->length <= 0 || mask->length > MAX_PLAINTEXT_LEN) return -1;

    memset(out, 0, sizeof(*out));
    out->charset_len = mk->charset_len;
    memcpy(out->charset, mk->charset, 256);
    out->mask_len = (unsigned int)mask->length;

    /* charset byte -> index map (-1 if absent). */
    int rev[256];
    for (int i = 0; i < 256; i++) rev[i] = -1;
    for (unsigned int i = 0; i < mk->charset_len; i++)
        rev[(unsigned char)mk->charset[i]] = (int)i;

    /* Per-position allowed charset indices (validated subset) + sizes. */
    uint8_t allowed[MAX_PLAINTEXT_LEN][256];
    for (unsigned int i = 0; i < out->mask_len; i++) {
        unsigned int sz = mask->positions[i].size;
        if (sz == 0) return -1;
        out->sizes[i] = sz;
        if (sz > out->max_sz) out->max_sz = sz;
        for (unsigned int k = 0; k < sz; k++) {
            unsigned char c = (unsigned char)mask->positions[i].chars[k];
            if (rev[c] < 0) {
                fprintf(stderr, "markov_build_restricted: mask position %u char "
                        "0x%02x is not in the markov charset\n", i, c);
                return -1;
            }
            allowed[i][k] = (uint8_t)rev[c];
        }
    }

    unsigned int n = mk->charset_len;

    /* r_pos0: allowed[0] sorted by pos0_freq desc. */
    for (unsigned int k = 0; k < out->sizes[0]; k++)
        out->r_pos0[k] = allowed[0][k];
    sort_indices_desc(out->r_pos0, out->sizes[0], mk->pos0_freq);

    /* r_bigram[(i*n + prev)*max_sz + k]: allowed[i] sorted by
     * bigram_freq[bgpos][prev][cur] desc, per position i>=1 and every prev. */
    size_t rb = (size_t)out->mask_len * n * out->max_sz;
    out->r_bigram = calloc(rb ? rb : 1, sizeof(uint8_t));
    if (!out->r_bigram) return -1;

    for (unsigned int i = 1; i < out->mask_len; i++) {
        unsigned int bgpos = ((i - 1) < mk->max_positions) ? (i - 1)
                                                           : (mk->max_positions - 1);
        for (unsigned int prev = 0; prev < n; prev++) {
            uint8_t row[256];
            for (unsigned int k = 0; k < out->sizes[i]; k++)
                row[k] = allowed[i][k];
            const uint64_t *freq = mk->bigram_freq +
                (size_t)bgpos * n * n + (size_t)prev * n;
            sort_indices_desc(row, out->sizes[i], freq);
            uint8_t *dst = out->r_bigram + ((size_t)i * n + prev) * out->max_sz;
            for (unsigned int k = 0; k < out->sizes[i]; k++)
                dst[k] = row[k];
        }
    }
    return 0;
}

void markov_mask_tables_free(markov_mask_tables *t) {
    if (t && t->r_bigram) { free(t->r_bigram); t->r_bigram = NULL; }
}

uint64_t markov_mask_keyspace(const markov_mask_tables *t) {
    uint64_t product = 1;
    for (unsigned int i = 0; i < t->mask_len; i++) {
        if (t->sizes[i] != 0 && product > UINT64_MAX / t->sizes[i]) {
            fprintf(stderr, "markov_mask_keyspace: overflow at position %u\n", i);
            return 0;
        }
        product *= t->sizes[i];
    }
    return product;
}

void index_to_plaintext_markov_mask_cpu(const markov_mask_tables *t,
                                        uint64_t index,
                                        unsigned char *plaintext,
                                        unsigned int *plaintext_len) {
    unsigned int n = t->charset_len;
    unsigned int ci = t->r_pos0[index % t->sizes[0]];
    plaintext[0] = (unsigned char)t->charset[ci];
    index /= t->sizes[0];
    unsigned int prev = ci;
    for (unsigned int i = 1; i < t->mask_len; i++) {
        const uint8_t *row = t->r_bigram + ((size_t)i * n + prev) * t->max_sz;
        ci = row[index % t->sizes[i]];
        plaintext[i] = (unsigned char)t->charset[ci];
        index /= t->sizes[i];
        prev = ci;
    }
    *plaintext_len = t->mask_len;
}

uint64_t fill_plaintext_space_markov_mask(const markov_mask_tables *t,
                                          uint64_t markov_keyspace,
                                          uint64_t *plaintext_space_up_to_index) {
    uint64_t ks = markov_mask_keyspace(t);
    uint64_t total = (markov_keyspace > 0 && markov_keyspace < ks) ? markov_keyspace : ks;
    unsigned int i;
    for (i = 0; i < t->mask_len; i++) plaintext_space_up_to_index[i] = 0;
    plaintext_space_up_to_index[t->mask_len] = total;
    return total;
}

void markov_mask_tables_to_gpu_buffers(const markov_mask_tables *t,
                                       uint8_t r_pos0_out[256],
                                       uint8_t *r_bigram_out) {
    memcpy(r_pos0_out, t->r_pos0, 256);
    memcpy(r_bigram_out, t->r_bigram,
           (size_t)t->mask_len * t->charset_len * t->max_sz);
}
