#include <metal_stdlib>
using namespace metal;

#include "markov8_functions.metal"


kernel void precompute_markov_ntlm8(
    device unsigned int *unused1 [[buffer(0)]],
    device unsigned char *g_hash [[buffer(1)]],
    device unsigned int *unused2 [[buffer(2)]],
    device char *unused3 [[buffer(3)]],
    device unsigned int *g_charset_len [[buffer(4)]],
    device unsigned int *unused4 [[buffer(5)]],
    device unsigned int *unused5 [[buffer(6)]],
    device unsigned int *unused6 [[buffer(7)]],
    device ulong *g_chain_len [[buffer(8)]],
    device unsigned int *g_device_num [[buffer(9)]],
    device unsigned int *g_total_devices [[buffer(10)]],
    device unsigned int *g_exec_block_scaler [[buffer(11)]],
    device ulong *g_output [[buffer(12)]],
    device ulong *unused7 [[buffer(13)]],
    device ulong *unused8 [[buffer(14)]],
    constant unsigned char *g_sorted_pos0 [[buffer(15)]],
    constant unsigned char *g_sorted_bigram [[buffer(16)]],
    device unsigned int *unused9 [[buffer(17)]],
    uint gid [[thread_position_in_grid]]) {

  ulong chain_len = *g_chain_len;
  long target_chain_len = (chain_len - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[gid] = 0;
    return;
  }

  unsigned char plaintext[8];
  unsigned int charset_len = *g_charset_len;
  ulong index = hash_char_to_index_markov8(g_hash, 0, target_chain_len - 1);

  for(ulong i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov8(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov8(hash_ntlm8(plaintext), 0, i);
  }

  g_output[gid] = index;
}
