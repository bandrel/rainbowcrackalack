#include <metal_stdlib>
using namespace metal;

#include "netntlmv1_7_functions.metal"


kernel void crackalack_netntlmv1_7(
    device unsigned int *unused1 [[buffer(0)]],
    device char *unused2 [[buffer(1)]],
    device unsigned int *unused3 [[buffer(2)]],
    device unsigned int *unused4 [[buffer(3)]],
    device unsigned int *unused5 [[buffer(4)]],
    device unsigned int *g_reduction_offset [[buffer(5)]],
    device unsigned int *unused_chain_len [[buffer(6)]],
    device ulong *g_indices [[buffer(7)]],
    device ulong *unused8 [[buffer(8)]],
    device ulong *unused9 [[buffer(9)]],
    device unsigned int *unused10 [[buffer(10)]],
    device char *unused11 [[buffer(11)]],
    device unsigned int *unused12 [[buffer(12)]],
    device unsigned int *unused13 [[buffer(13)]],
    device const unsigned char *g_challenge [[buffer(14)]],
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

  ulong index = g_indices[gid];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  unsigned char challenge_local[8];
  for (int _c = 0; _c < 8; _c++) challenge_local[_c] = g_challenge[_c];

  for (unsigned int pos = 0; pos < 881688; pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, challenge_local, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, pos);
  }

  g_indices[gid] = index;
  return;
}
