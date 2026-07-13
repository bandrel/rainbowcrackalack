/*
 * Markov-biased index_to_plaintext restricted to a mask (per-position charset).
 *
 * Like index_to_plaintext_markov (rt_markov.cu) but each position i has its own
 * radix sizes[i] (the number of allowed characters at that mask position) and
 * consults restricted, per-position sorted tables:
 *
 *   r_pos0[k]  - charset index of the k-th most probable first character among
 *                the sizes[0] characters allowed at position 0.
 *
 *   r_bigram[(i * charset_len + prev) * max_sz + k]
 *              - charset index of the k-th most probable character allowed at
 *                position i, following the character at charset[prev].
 *                Row stride is max_sz = max(sizes); only the first sizes[i]
 *                entries of each row are valid.
 *
 * Only fixed-length plaintexts are supported (plaintext_len == mask_len).
 */
__device__ inline void index_to_plaintext_markov_mask(
    unsigned long long index,
    const char *charset,
    unsigned int charset_len,
    unsigned int mask_len,
    unsigned int max_sz,
    const unsigned int *sizes,
    const unsigned char *r_pos0,
    const unsigned char *r_bigram,
    unsigned char *plaintext)
{
    unsigned int ci = r_pos0[index % sizes[0]];
    plaintext[0] = charset[ci];
    index /= sizes[0];
    unsigned int prev = ci;
    for (unsigned int i = 1; i < mask_len; i++) {
        unsigned long long off =
            ((unsigned long long)i * charset_len + prev) * max_sz;
        ci = r_bigram[off + (index % sizes[i])];
        plaintext[i] = charset[ci];
        index /= sizes[i];
        prev = ci;
    }
}
