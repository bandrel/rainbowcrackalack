#include "ntlm10_functions.cl"


__kernel void precompute_ntlm10(
    __global unsigned int *unused1,
    __global unsigned char *g_hash,
    __global unsigned int *unused2,
    __global char *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *g_chain_len,
    __global unsigned long *unused7,
    __global unsigned long *unused8,
    __global unsigned int *g_device_num,
    __global unsigned int *g_total_devices,
    __global unsigned int *g_exec_block_scaler,
    __global unsigned long *g_output,
    __global unsigned long *unused9,
    __global unsigned long *unused10) {

  unsigned int chain_len = *g_chain_len;
  long target_chain_len = (chain_len - *g_device_num) - ((get_global_id(0) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[get_global_id(0)] = 0;
    return;
  }

  unsigned char plaintext[10];
  unsigned long index = hash_char_to_index_ntlm10(g_hash, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_ntlm10(index, plaintext);
    index = hash_to_index_ntlm10(hash_ntlm10(plaintext), i);
  }

  g_output[get_global_id(0)] = index;
}
