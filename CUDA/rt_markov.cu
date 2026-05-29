/*
 * Markov-biased index_to_plaintext for CUDA.
 *
 * Maps index 0 to the most probable plaintext by consulting two sorted
 * lookup tables built from bigram statistics:
 *
 *   sorted_pos0[i]  - charset index of the i-th most probable first character
 *
 *   sorted_bigram[pos * charset_len^2 + prev_idx * charset_len + i]
 *                   - charset index of the i-th most probable character
 *                     at position (pos+1) following the character at charset[prev_idx]
 *
 * Position-aware: uses different bigram tables for each position.
 * For positions >= max_positions, uses the last table.
 *
 * Only fixed-length plaintexts are supported (plaintext_len_min == plaintext_len_max).
 */
__device__ inline void index_to_plaintext_markov(
    unsigned long long index,
    const char *charset,
    unsigned int charset_len,
    unsigned int plaintext_len,
    unsigned int max_positions,
    const unsigned char *sorted_pos0,
    const unsigned char *sorted_bigram,
    unsigned char *plaintext)
{
    /* Position 0: rank within the most-probable first characters. */
    unsigned int charset_idx = sorted_pos0[index % charset_len];
    plaintext[0] = charset[charset_idx];
    index /= charset_len;
    unsigned int prev_charset_idx = charset_idx;

    unsigned long long cs2 = (unsigned long long)charset_len * charset_len;

    for (unsigned int i = 1; i < plaintext_len; i++) {
        /* Select position-specific bigram table.
         * Position i-1 is where prev_char is (0-indexed).
         * Use last table for positions >= max_positions. */
        unsigned int pos = ((i - 1) < max_positions) ? (i - 1) : (max_positions - 1);
        unsigned long long offset = (unsigned long long)pos * cs2 + (unsigned long long)prev_charset_idx * charset_len;

        charset_idx = sorted_bigram[offset + (index % charset_len)];
        plaintext[i] = charset[charset_idx];
        index /= charset_len;
        prev_charset_idx = charset_idx;
    }
}
