#include <metal_stdlib>
using namespace metal;

#include "ntlm8_functions.metal"


/* TODO: specify array length in definition...somehow? */
kernel void crackalack_ntlm8(
    device unsigned int *unused1 [[buffer(0)]],
    device char *unused2 [[buffer(1)]],
    device unsigned int *unused3 [[buffer(2)]],
    device unsigned int *unused4 [[buffer(3)]],
    device unsigned int *unused5 [[buffer(4)]],
    device unsigned int *unused6 [[buffer(5)]],
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


  for (unsigned int pos = 0; pos < 421999; pos++) {
    index_to_plaintext_ntlm8(index, charset, plaintext);
    index = hash_to_index_ntlm8(hash_ntlm8(plaintext), pos);
  }

  g_indices[gid] = index;
  return;
}
