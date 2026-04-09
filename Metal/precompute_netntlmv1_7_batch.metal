#include <metal_stdlib>
using namespace metal;

#include "netntlmv1_7_functions.metal"

/* Batched precompute for NetNTLMv1-7 tables.
 * See CL/precompute_netntlmv1_7_batch.cl for detailed documentation. */
kernel void precompute_netntlmv1_7_batch(
    device unsigned char *g_hashes [[buffer(0)]],
    device unsigned int *g_num_hashes [[buffer(1)]],
    device unsigned int *g_chunk_positions [[buffer(2)]],
    device unsigned int *g_reduction_offset [[buffer(3)]],
    device ulong *g_chain_len [[buffer(4)]],
    device unsigned int *g_pos_start [[buffer(5)]],
    device unsigned int *g_total_positions [[buffer(6)]],
    device ulong *g_output [[buffer(7)]],
    uint gid [[thread_position_in_grid]],
    uint lid [[thread_index_in_threadgroup]],
    uint lsz [[threads_per_threadgroup]]) {

  /* Threadgroup S-box arrays -- one copy per threadgroup. */
  threadgroup uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  threadgroup uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  for (uint _i = lid; _i < 64; _i += lsz) {
    l_SB1[_i] = SB1[_i]; l_SB2[_i] = SB2[_i];
    l_SB3[_i] = SB3[_i]; l_SB4[_i] = SB4[_i];
    l_SB5[_i] = SB5[_i]; l_SB6[_i] = SB6[_i];
    l_SB7[_i] = SB7[_i]; l_SB8[_i] = SB8[_i];
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);

  unsigned int chunk_positions = *g_chunk_positions;
  unsigned int hash_idx = gid / chunk_positions;
  unsigned int local_pos = gid % chunk_positions;

  if (hash_idx >= *g_num_hashes) {
    return;
  }

  unsigned int absolute_pos = *g_pos_start + local_pos;
  unsigned int total_positions = *g_total_positions;

  if (absolute_pos >= total_positions) {
    return;
  }

  ulong chain_len = *g_chain_len;
  long target_chain_len = (long)chain_len - (long)absolute_pos - 1;

  if (target_chain_len < 1) {
    g_output[(ulong)hash_idx * total_positions + absolute_pos] = 0;
    return;
  }

  unsigned int reduction_offset = *g_reduction_offset;
  device unsigned char *hash = g_hashes + hash_idx * 16;
  unsigned char plaintext[8];
  ulong index = hash_char_to_index_netntlmv1_7(hash, reduction_offset, target_chain_len - 1);

  for (ulong i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, i);
  }

  g_output[(ulong)hash_idx * total_positions + absolute_pos] = index;
}
