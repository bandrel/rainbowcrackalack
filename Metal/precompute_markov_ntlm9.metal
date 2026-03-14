#include <metal_stdlib>
using namespace metal;

#include "markov9_functions.metal"


kernel void precompute_markov_ntlm9(
    device unsigned int *unused1 [[buffer(0)]],
    device unsigned char *g_hash [[buffer(1)]],
    device unsigned int *unused2 [[buffer(2)]],
    device char *unused3 [[buffer(3)]],
    device unsigned int *g_charset_len [[buffer(4)]],
    device unsigned int *unused4 [[buffer(5)]],
    device unsigned int *unused5 [[buffer(6)]],
    device ulong *unused6 [[buffer(7)]],
    device unsigned int *g_device_num [[buffer(8)]],
    device unsigned int *g_total_devices [[buffer(9)]],
    device unsigned int *g_exec_block_scaler [[buffer(10)]],
    device ulong *g_output [[buffer(11)]],
    device ulong *unused7 [[buffer(12)]],
    device ulong *unused8 [[buffer(13)]],
    constant unsigned char *g_sorted_pos0 [[buffer(14)]],
    constant unsigned char *g_sorted_bigram [[buffer(15)]],
    device unsigned int *unused9 [[buffer(16)]],
    uint gid [[thread_position_in_grid]]) {

  long target_chain_len = (803000 - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[gid] = 0;
    return;
  }

  unsigned char plaintext[9];
  unsigned int charset_len = *g_charset_len;
  ulong index = hash_char_to_index_markov9(g_hash, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < 802999; i++) {
    index_to_plaintext_markov9(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov9(hash_ntlm9(plaintext), i);
  }

  g_output[gid] = index;
}
