#include <metal_stdlib>
using namespace metal;

#include "rt_mask.metal"
#include "shared.h"
#include "string.metal"

/*
 * GPU test kernel for index_to_plaintext_mask (Metal backend).
 *
 * Buffer layout mirrors the CUDA/OpenCL kernel:
 *   0  g_mask_data       - flat per-position charset buffer
 *                          [pos * MAX_CHARSET_LEN + char_index]
 *   1  g_mask_lens       - per-position charset sizes
 *   2  g_mask_len        - number of positions in the mask (scalar)
 *   3  g_index           - the index to decode (scalar ulong)
 *   4  g_plaintext       - output plaintext buffer (write-only)
 *   5  g_plaintext_len_out - output decoded length (write-only)
 *   6  g_debug           - debug scratch buffer
 */
kernel void test_index_to_plaintext_mask(
    device const char        *g_mask_data         [[buffer(0)]],
    device const unsigned int *g_mask_lens         [[buffer(1)]],
    device unsigned int      *g_mask_len           [[buffer(2)]],
    device ulong             *g_index              [[buffer(3)]],
    device unsigned char     *g_plaintext          [[buffer(4)]],
    device unsigned int      *g_plaintext_len_out  [[buffer(5)]],
    device unsigned char     *g_debug              [[buffer(6)]],
    uint gid [[thread_position_in_grid]])
{
    unsigned int mask_len = *g_mask_len;
    ulong        index    = *g_index;
    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned int  plaintext_len = 0;

    index_to_plaintext_mask(index, g_mask_data, g_mask_lens, mask_len,
                             plaintext, &plaintext_len);

    *g_plaintext_len_out = plaintext_len;
    for (unsigned int i = 0; i < plaintext_len; i++)
        g_plaintext[i] = plaintext[i];
}
