/*
 * Mask-based index_to_plaintext for Metal.
 *
 * Maps a flat index to a plaintext determined by a hashcat-style mask.
 * Each mask position has its own charset; the decode is mixed-radix
 * with position 0 as the MOST-significant digit:
 *
 *   iterate i from (mask_len-1) down to 0:
 *     plaintext[i] = mask_data[i * MAX_CHARSET_LEN + (x % mask_lens[i])]
 *     x /= mask_lens[i]
 *
 * So index 0 maps to the first character in every position's charset,
 * and index (keyspace-1) maps to the last character in every position.
 *
 * mask_data  - flat per-position charset buffer:
 *              mask_data[pos * MAX_CHARSET_LEN + char_index]
 *              (exactly what mask_to_gpu_buffers() produces)
 * mask_lens  - number of valid chars at each position
 * mask_len   - number of positions in the mask
 *
 * NOTE: In Metal, `unsigned long` is 32-bit. Use `ulong` for 64-bit indices.
 */

#include "shared.h"
inline void index_to_plaintext_mask(
    ulong                    index,
    device const char       *mask_data,
    device const unsigned int *mask_lens,
    unsigned int              mask_len,
    thread unsigned char     *plaintext,
    thread unsigned int      *plaintext_len)
{
    *plaintext_len = mask_len;
    ulong x = index;

    for (int i = (int)mask_len - 1; i >= 0; i--) {
        unsigned int sz = mask_lens[i];
        plaintext[i] = (unsigned char)mask_data[i * MAX_CHARSET_LEN + (x % sz)];
        x /= sz;
    }
}
