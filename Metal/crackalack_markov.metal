#include <metal_stdlib>
using namespace metal;

#include "rt.metal"
#include "rt_markov.metal"
#include "string.metal"


kernel void crackalack_markov(
    device unsigned int *g_hash_type [[buffer(0)]],
    device char *g_charset [[buffer(1)]],
    device unsigned int *g_plaintext_len_min [[buffer(2)]],
    device unsigned int *g_plaintext_len_max [[buffer(3)]],
    device unsigned int *g_reduction_offset [[buffer(4)]],
    device unsigned int *g_chain_len [[buffer(5)]],
    device ulong *g_indices [[buffer(6)]],
    device unsigned int *g_pos_start [[buffer(7)]],
    device ulong *g_plaintext_space_up_to_index [[buffer(8)]],
    device ulong *g_plaintext_space_total [[buffer(9)]],
    device unsigned int *g_is_mask [[buffer(10)]],
    device char *g_mask_charset_data [[buffer(11)]],
    device unsigned int *g_mask_charset_lens [[buffer(12)]],
    constant unsigned char *g_sorted_pos0 [[buffer(13)]],
    constant unsigned char *g_sorted_bigram [[buffer(14)]],
    uint gid [[thread_position_in_grid]])
{
    unsigned int hash_type = *g_hash_type;
    char charset[MAX_CHARSET_LEN];
    unsigned int plaintext_len_max = *g_plaintext_len_max;
    unsigned int reduction_offset = *g_reduction_offset;
    unsigned int chain_len = *g_chain_len;
    ulong index = g_indices[gid];
    unsigned int pos = *g_pos_start;

    unsigned int charset_len = g_strncpy(charset, g_charset, sizeof(charset));

    ulong plaintext_space_total = *g_plaintext_space_total;

    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned char hash[MAX_HASH_OUTPUT_LEN];
    unsigned int hash_len;

    /*
     * Generate a chain segment starting at pos and ending at chain_len - 1.
     * The chain may be split across multiple kernel invocations (passes); pos
     * tracks where this pass begins, matching the crackalack kernel's pattern.
     * Markov tables assume fixed-length plaintexts, so plaintext_len_max is
     * used directly.
     */
    for (; pos < chain_len - 1; pos++) {
        index_to_plaintext_markov(index, charset, charset_len,
                                  plaintext_len_max,
                                  g_sorted_pos0, g_sorted_bigram,
                                  plaintext);
        do_hash(hash_type, plaintext, plaintext_len_max, hash, &hash_len);
        index = hash_to_index(hash, hash_len, reduction_offset,
                              plaintext_space_total, pos);
    }

    g_indices[gid] = index;
}
