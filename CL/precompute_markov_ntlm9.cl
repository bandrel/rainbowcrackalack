#include "markov9_functions.cl"


__kernel void precompute_markov_ntlm9(
    __global unsigned int *unused1,
    __global unsigned char *g_hash,
    __global unsigned int *unused2,
    __global char *unused3,
    __global unsigned int *g_charset_len,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned long *unused6,
    __global unsigned int *g_device_num,
    __global unsigned int *g_total_devices,
    __global unsigned int *g_exec_block_scaler,
    __global unsigned long *g_output,
    __global unsigned long *unused7,
    __global unsigned long *unused8,
    __constant unsigned char *g_sorted_pos0,
    __global const unsigned char *g_sorted_bigram,
    __global unsigned int *unused9) {

  long target_chain_len = (803000 - *g_device_num) - ((get_global_id(0) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[get_global_id(0)] = 0;
    return;
  }

  unsigned char plaintext[9];
  unsigned int charset_len = *g_charset_len;
  unsigned long index = hash_char_to_index_markov9(g_hash, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < 802999; i++) {
    index_to_plaintext_markov9(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov9(hash_ntlm9(plaintext), i);
  }

  g_output[get_global_id(0)] = index;
}
