#include <metal_stdlib>
using namespace metal;

#include "rt_markov.metal"
#include "shared.h"
#include "string.metal"

/*
 * GPU test kernel for index_to_plaintext_markov (Metal backend).
 *
 * Buffer layout mirrors the OpenCL kernel:
 *   0  g_charset
 *   1  g_charset_len
 *   2  g_plaintext_len
 *   3  g_index
 *   4  g_plaintext        (output)
 *   5  g_plaintext_len_out (output)
 *   6  g_debug
 *   7  g_sorted_pos0      (constant)
 *   8  g_sorted_bigram    (constant)
 */
kernel void test_index_to_plaintext_markov(
    device char           *g_charset          [[buffer(0)]],
    device unsigned int   *g_charset_len       [[buffer(1)]],
    device unsigned int   *g_plaintext_len     [[buffer(2)]],
    device ulong          *g_index             [[buffer(3)]],
    device unsigned char  *g_plaintext         [[buffer(4)]],
    device unsigned int   *g_plaintext_len_out [[buffer(5)]],
    device unsigned char  *g_debug             [[buffer(6)]],
    constant unsigned char *g_sorted_pos0      [[buffer(7)]],
    constant unsigned char *g_sorted_bigram    [[buffer(8)]],
    uint gid [[thread_position_in_grid]])
{
    char         charset[MAX_CHARSET_LEN];
    unsigned int charset_len   = *g_charset_len;
    unsigned int plaintext_len = *g_plaintext_len;
    ulong        index         = *g_index;
    unsigned char plaintext[MAX_PLAINTEXT_LEN];

    g_memcpy((thread unsigned char *)charset, (device unsigned char *)g_charset, charset_len);

    index_to_plaintext_markov(index, charset, charset_len, plaintext_len,
                               g_sorted_pos0, g_sorted_bigram, plaintext);

    *g_plaintext_len_out = plaintext_len;
    for (unsigned int i = 0; i < plaintext_len; i++)
        g_plaintext[i] = plaintext[i];
}
