#include "shared.h"
#include "rt_mask.cu"
#include "string.cu"

/*
 * GPU test kernel for index_to_plaintext_mask.
 *
 * Arg slots (MUST match host in test_index_to_plaintext_mask.c):
 *   0  g_mask_data       - flat per-position charset buffer
 *                          [pos * MAX_CHARSET_LEN + char_index], size MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN bytes
 *   1  g_mask_lens       - per-position charset sizes, MAX_PLAINTEXT_LEN unsigned ints
 *   2  g_mask_len        - number of positions in the mask (scalar unsigned int)
 *   3  g_index           - the index to decode (scalar unsigned long long)
 *   4  g_plaintext       - output plaintext buffer (write-only, MAX_PLAINTEXT_LEN bytes)
 *   5  g_plaintext_len_out - output decoded length (write-only, unsigned int)
 *   6  g_debug           - debug scratch buffer
 */
extern "C" __global__ void test_index_to_plaintext_mask(
    const char         *g_mask_data,
    const unsigned int *g_mask_lens,
    unsigned int       *g_mask_len,
    unsigned long long *g_index,
    unsigned char      *g_plaintext,
    unsigned int       *g_plaintext_len_out,
    unsigned char      *g_debug)
{
    char         mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    unsigned int mask_len = *g_mask_len;
    unsigned long long index = *g_index;
    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned int plaintext_len = 0;

    g_memcpy((unsigned char *)mask_data,
             (unsigned char *)g_mask_data,
             mask_len * MAX_CHARSET_LEN);

    for (unsigned int i = 0; i < mask_len; i++)
        mask_lens[i] = g_mask_lens[i];

    index_to_plaintext_mask(index, mask_data, mask_lens, mask_len,
                             plaintext, &plaintext_len);

    *g_plaintext_len_out = plaintext_len;
    for (unsigned int i = 0; i < plaintext_len; i++)
        g_plaintext[i] = plaintext[i];
}
