#include <metal_stdlib>
using namespace metal;

#include "ntlm9_functions.metal"


/* TODO: specify array length in definition...somehow? */
kernel void crackalack_ntlm9(
    device unsigned int *unused1 [[buffer(0)]],
    device char *unused2 [[buffer(1)]],
    device unsigned int *unused3 [[buffer(2)]],
    device unsigned int *unused4 [[buffer(3)]],
    device unsigned int *unused5 [[buffer(4)]],
    device unsigned int *g_chain_len [[buffer(5)]],
    device ulong *g_indices [[buffer(6)]],
    device unsigned int *g_pos_start [[buffer(7)]],
    device ulong *unused8 [[buffer(8)]],
    device ulong *unused9 [[buffer(9)]],
    uint gid [[thread_position_in_grid]]) {
  ulong index = g_indices[gid];
  unsigned char plaintext[9];


  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_ntlm9(index, plaintext);
    index = hash_to_index_ntlm9(hash_ntlm9(plaintext), pos);
  }

  g_indices[gid] = index;
  return;
}
