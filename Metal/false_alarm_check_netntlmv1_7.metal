#include <metal_stdlib>
using namespace metal;

#include "netntlmv1_7_functions.metal"


kernel void false_alarm_check_netntlmv1_7(
    device unsigned int *unused1 [[buffer(0)]],
    device char *unused2 [[buffer(1)]],
    device unsigned int *unused3 [[buffer(2)]],
    device unsigned int *unused4 [[buffer(3)]],
    device unsigned int *unused5 [[buffer(4)]],
    device unsigned int *g_reduction_offset [[buffer(5)]],
    device ulong *unused6 [[buffer(6)]],
    device ulong *unused_pspace_table [[buffer(7)]],
    device unsigned int *g_device_num [[buffer(8)]],
    device unsigned int *g_total_devices [[buffer(9)]],
    device unsigned int *g_num_start_indices [[buffer(10)]],
    device ulong *g_start_indices [[buffer(11)]],
    device unsigned int *g_start_index_positions [[buffer(12)]],
    device ulong *g_hash_base_indices [[buffer(13)]],
    device unsigned int *g_exec_block_scaler [[buffer(14)]],
    device ulong *g_plaintext_indices [[buffer(15)]],
    device const unsigned char *g_challenge [[buffer(16)]],
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

  int index_pos = (*g_num_start_indices - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;
  if (index_pos < 0)
    return;

  unsigned int reduction_offset = *g_reduction_offset;
  unsigned char plaintext[8];
  ulong index = g_start_indices[index_pos], previous_index = 0;
  ulong hash_base_index = g_hash_base_indices[index_pos] & 0x00FFFFFFFFFFFFFFUL;
  unsigned int endpoint = g_start_index_positions[index_pos];

  unsigned char challenge_local[8];
  for (int _c = 0; _c < 8; _c++) challenge_local[_c] = g_challenge[_c];

  /* Challenge initial permutation is loop-invariant: compute once. */
  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(challenge_local, &cx, &cy);

  for (unsigned int pos = 0; pos < endpoint + 1; pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);

    previous_index = index;
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast_ip(plaintext, cx, cy, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, pos);

    if ((index == (hash_base_index + pos)) || (index == (hash_base_index + pos - 72057594037927936UL))) {
      g_plaintext_indices[index_pos] = previous_index;
      return;
    }
  }
}
