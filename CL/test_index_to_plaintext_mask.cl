#include "shared.h"
#include "rt_mask.cl"
#include "string.cl"

/*
 * GPU test kernel for index_to_plaintext_mask.
 *
 * Arg slots (MUST match host in test_index_to_plaintext_mask.c):
 *   0  g_mask_data         - flat per-position charset buffer
 *                            [pos * MAX_CHARSET_LEN + char_index],
 *                            size MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN bytes
 *   1  g_mask_lens         - per-position charset sizes, MAX_PLAINTEXT_LEN unsigned ints
 *   2  g_mask_len          - number of positions in the mask (scalar unsigned int)
 *   3  g_index             - the index to decode (scalar unsigned long)
 *   4  g_plaintext         - output plaintext buffer (write-only, MAX_PLAINTEXT_LEN bytes)
 *   5  g_plaintext_len_out - output decoded length (write-only, unsigned int)
 *   6  g_debug             - debug scratch buffer
 */
__kernel void test_index_to_plaintext_mask(
    __global const char        *g_mask_data,
    __global const unsigned int *g_mask_lens,
    __global unsigned int      *g_mask_len,
    __global unsigned long     *g_index,
    __global unsigned char     *g_plaintext,
    __global unsigned int      *g_plaintext_len_out,
    __global unsigned char     *g_debug)
{
    unsigned int mask_len  = *g_mask_len;
    unsigned long index    = *g_index;
    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned int plaintext_len = 0;

    /* Pass global buffers directly — index_to_plaintext_mask takes __global
     * pointers so no local copy is needed (unlike the charset copy in
     * test_index_to_plaintext_markov.cl which uses a local charset array). */
    index_to_plaintext_mask(index, g_mask_data, g_mask_lens, mask_len,
                             plaintext, &plaintext_len);

    *g_plaintext_len_out = plaintext_len;
    for (unsigned int i = 0; i < plaintext_len; i++)
        g_plaintext[i] = plaintext[i];
}
