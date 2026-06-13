#include <metal_stdlib>
using namespace metal;

#include "netntlmv1_7_functions.metal"


kernel void precompute_netntlmv1_7(
    device unsigned int *unused1 [[buffer(0)]],
    device unsigned char *g_hash [[buffer(1)]],
    device unsigned int *unused2 [[buffer(2)]],
    device char *unused3 [[buffer(3)]],
    device unsigned int *unused4 [[buffer(4)]],
    device unsigned int *unused5 [[buffer(5)]],
    device unsigned int *unused6 [[buffer(6)]],
    device unsigned int *g_table_index [[buffer(7)]],
    device ulong *g_chain_len [[buffer(8)]],
    device unsigned int *g_device_num [[buffer(9)]],
    device unsigned int *g_total_devices [[buffer(10)]],
    device unsigned int *g_exec_block_scaler [[buffer(11)]],
    device ulong *g_output [[buffer(12)]],
    device ulong *unused8 [[buffer(13)]],
    device ulong *unused9 [[buffer(14)]],
    device const unsigned char *g_challenge [[buffer(15)]],
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

  /* Honor the host's chain_len (arg 8) instead of a hardcoded constant, so the
   * per-hash fallback path is correct for tables of any chain length.  Mirrors
   * the fix applied to crackalack_netntlmv1_7 (f11bf2f) and the batch kernel. */
  long chain_len = (long)(*g_chain_len);
  long target_chain_len = (chain_len - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[gid] = 0;
    return;
  }

  unsigned int reduction_offset = *g_table_index * 65536;
  unsigned char plaintext[8];
  ulong index = hash_char_to_index_netntlmv1_7(g_hash, reduction_offset, target_chain_len - 1);

  unsigned char challenge_local[8];
  for (int _c = 0; _c < 8; _c++) challenge_local[_c] = g_challenge[_c];

  /* Challenge initial permutation is loop-invariant: compute once. */
  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(challenge_local, &cx, &cy);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast_ip(plaintext, cx, cy, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, i);
  }

  g_output[gid] = index;
}
