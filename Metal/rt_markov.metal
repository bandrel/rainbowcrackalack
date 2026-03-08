/*
 * Markov-biased index_to_plaintext for Metal.
 *
 * Maps index 0 to the most probable plaintext by consulting two sorted
 * lookup tables built from bigram statistics:
 *
 *   sorted_pos0[i]                          - charset index of the i-th most
 *                                             probable first character
 *   sorted_bigram[prev_idx * charset_len + i] - charset index of the i-th most
 *                                             probable character following the
 *                                             character at charset[prev_idx]
 *
 * Only fixed-length plaintexts are supported (plaintext_len_min == plaintext_len_max).
 */
inline void index_to_plaintext_markov(
    ulong index,
    thread char *charset,
    unsigned int charset_len,
    unsigned int plaintext_len,
    constant unsigned char *sorted_pos0,
    constant unsigned char *sorted_bigram,
    thread unsigned char *plaintext)
{
    /* Position 0: rank within the most-probable first characters. */
    unsigned int charset_idx = sorted_pos0[index % charset_len];
    plaintext[0] = charset[charset_idx];
    index /= charset_len;

    for (unsigned int i = 1; i < plaintext_len; i++) {
        /* Determine the charset index of the previous character. */
        unsigned int prev_charset_idx = 0;
        for (unsigned int j = 0; j < charset_len; j++) {
            if (charset[j] == plaintext[i - 1]) {
                prev_charset_idx = j;
                break;
            }
        }

        charset_idx = sorted_bigram[prev_charset_idx * charset_len + (index % charset_len)];
        plaintext[i] = charset[charset_idx];
        index /= charset_len;
    }
}
