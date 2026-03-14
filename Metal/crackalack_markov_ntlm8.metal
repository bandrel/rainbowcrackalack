#include <metal_stdlib>
using namespace metal;

#include "markov8_functions.metal"


/* TODO: specify array length in definition...somehow? */
kernel void crackalack_markov_ntlm8(
    device unsigned int *unused1 [[buffer(0)]],
    device char *unused2 [[buffer(1)]],
    device unsigned int *g_charset_len [[buffer(2)]],
    device unsigned int *unused3 [[buffer(3)]],
    device unsigned int *unused4 [[buffer(4)]],
    device unsigned int *unused5 [[buffer(5)]],
    device unsigned int *unused_chain_len [[buffer(6)]],
    device ulong *g_indices [[buffer(7)]],
    device unsigned int *unused6 [[buffer(8)]],
    device ulong *unused7 [[buffer(9)]],
    device unsigned int *unused8 [[buffer(10)]],
    constant unsigned char *g_sorted_pos0 [[buffer(11)]],
    constant unsigned char *g_sorted_bigram [[buffer(12)]],
    device unsigned int *unused9 [[buffer(13)]],
    uint gid [[thread_position_in_grid]]) {
  ulong index = g_indices[gid];
  unsigned char plaintext[8];
  unsigned int charset_len = *g_charset_len;


  for (unsigned int pos = 0; pos < 421999; pos++) {
    index_to_plaintext_markov8(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov8(hash_ntlm8(plaintext), pos);
  }

  g_indices[gid] = index;
  return;
}
