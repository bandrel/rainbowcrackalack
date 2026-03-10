#include "rt_markov.cl"
#include "string.cl"

/*
 * GPU test kernel for index_to_plaintext_markov.
 *
 * Args:
 *   0  g_charset          - charset bytes
 *   1  g_charset_len      - number of charset chars
 *   2  g_plaintext_len    - fixed plaintext length
 *   3  g_index            - the index to decode
 *   4  g_plaintext        - output plaintext (write-only, MAX_PLAINTEXT_LEN bytes)
 *   5  g_plaintext_len_out - output decoded length
 *   6  g_debug            - debug scratch buffer
 *   7  g_sorted_pos0      - charset_len-entry sorted position-0 table
 *   8  g_sorted_bigram    - charset_len^2-entry sorted bigram table
 */
__kernel void test_index_to_plaintext_markov(
    __global char          *g_charset,
    __global unsigned int  *g_charset_len,
    __global unsigned int  *g_plaintext_len,
    __global unsigned long *g_index,
    __global unsigned char *g_plaintext,
    __global unsigned int  *g_plaintext_len_out,
    __global unsigned char *g_debug,
    __constant unsigned char *g_sorted_pos0,
    __constant unsigned char *g_sorted_bigram)
{
    char         charset[MAX_CHARSET_LEN];
    unsigned int charset_len   = *g_charset_len;
    unsigned int plaintext_len = *g_plaintext_len;
    unsigned long index        = *g_index;
    unsigned char plaintext[MAX_PLAINTEXT_LEN];

    g_memcpy((unsigned char *)charset, (unsigned char __global *)g_charset, charset_len);

    index_to_plaintext_markov(index, charset, charset_len, plaintext_len,
                               g_sorted_pos0, g_sorted_bigram, plaintext);

    *g_plaintext_len_out = plaintext_len;
    for (unsigned int i = 0; i < plaintext_len; i++)
        g_plaintext[i] = plaintext[i];
}
