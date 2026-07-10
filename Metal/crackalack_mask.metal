#include <metal_stdlib>
using namespace metal;

#include "rt.metal"
#include "rt_mask.metal"
#include "string.metal"


kernel void crackalack_mask(
    device unsigned int      *g_hash_type                    [[buffer(0)]],
    device char              *g_charset                      [[buffer(1)]],
    device unsigned int      *g_charset_len                  [[buffer(2)]],
    device unsigned int      *g_plaintext_len_min            [[buffer(3)]],
    device unsigned int      *g_plaintext_len_max            [[buffer(4)]],
    device unsigned int      *g_reduction_offset             [[buffer(5)]],
    device unsigned int      *g_chain_len                    [[buffer(6)]],
    device ulong             *g_indices                      [[buffer(7)]],
    device unsigned int      *g_pos_start                    [[buffer(8)]],
    device ulong             *g_plaintext_space_up_to_index  [[buffer(9)]],
    device ulong             *g_plaintext_space_total        [[buffer(10)]],
    device const char        *g_mask_data                    [[buffer(11)]],
    device const unsigned int *g_mask_lens                   [[buffer(12)]],
    device unsigned int      *g_mask_len                     [[buffer(13)]],
    uint gid [[thread_position_in_grid]])
{
  /* Mask generation uses fixed-length plaintexts (length == mask->length,
   * enforced by the host). g_charset, g_charset_len, g_plaintext_len_min,
   * and g_plaintext_space_up_to_index are accepted for ABI compatibility
   * with crackalack.metal and crackalack_markov.metal but are not used here.
   * g_plaintext_len_max is also unused: mask_len fully determines the
   * plaintext length. */
    unsigned int hash_type = *g_hash_type;
    unsigned int reduction_offset = *g_reduction_offset;
    unsigned int chain_len = *g_chain_len;
    ulong index = g_indices[gid];
    unsigned int pos = *g_pos_start;
    unsigned int mask_len = *g_mask_len;

    ulong plaintext_space_total = *g_plaintext_space_total;

    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned char hash[MAX_HASH_OUTPUT_LEN];
    unsigned int hash_len;
    unsigned int plaintext_len;

    /*
     * Generate a chain segment starting at pos and ending at chain_len - 1.
     * The chain may be split across multiple kernel invocations (passes); pos
     * tracks where this pass begins, matching the crackalack kernel's pattern.
     * Mask tables assume fixed-length plaintexts (length == mask_len).
     */
    for (; pos < chain_len - 1; pos++) {
        index_to_plaintext_mask(index, g_mask_data, g_mask_lens, mask_len,
                                plaintext, &plaintext_len);
        do_hash(hash_type, plaintext, mask_len, hash, &hash_len);
        index = hash_to_index(hash, hash_len, reduction_offset,
                              plaintext_space_total, pos);
    }

    g_indices[gid] = index;
}
