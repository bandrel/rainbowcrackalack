#include "markov10_functions.cl"


__kernel void precompute_markov_ntlm10(
    __global unsigned int *unused1,
    __global unsigned char *g_hash,
    __global unsigned int *unused2,
    __global char *unused3,
    __global unsigned int *g_charset_len,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *unused6,
    __global unsigned long *g_chain_len,
    __global unsigned int *g_device_num,
    __global unsigned int *g_total_devices,
    __global unsigned int *g_exec_block_scaler,
    __global unsigned long *g_output,
    __global unsigned long *unused7,
    __global unsigned long *g_pspace_total,
    __constant unsigned char *g_sorted_pos0,
    __global const unsigned char *g_sorted_bigram,
    __global unsigned int *unused8) {

  unsigned long chain_len = *g_chain_len;
  long target_chain_len = (chain_len - *g_device_num) - ((get_global_id(0) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[get_global_id(0)] = 0;
    return;
  }

  unsigned char plaintext[10];
  unsigned int charset_len = *g_charset_len;
  unsigned long pspace_total = *g_pspace_total;
  unsigned long index = hash_char_to_index_markov10(g_hash, 0, pspace_total, target_chain_len - 1);

  for(unsigned long i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov10(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov10(hash_ntlm10(plaintext), 0, pspace_total, i);
  }

  g_output[get_global_id(0)] = index;
}
