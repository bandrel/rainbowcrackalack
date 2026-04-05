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
    uint gid [[thread_position_in_grid]]) {
  ulong index = g_indices[gid];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  for (unsigned int pos = 0; pos < 881688; pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7(plaintext), reduction_offset, pos);
  }

  g_indices[gid] = index;
  return;
}
