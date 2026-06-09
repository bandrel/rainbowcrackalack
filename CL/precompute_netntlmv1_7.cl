#include "netntlmv1_7_functions.cl"


__kernel void precompute_netntlmv1_7(
    __global unsigned int *unused1,
    __global unsigned char *g_hash,
    __global unsigned int *unused2,
    __global char *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *unused6,
    __global unsigned int *g_table_index,
    __global unsigned long *unused_chain_len,
    __global unsigned int *g_device_num,
    __global unsigned int *g_total_devices,
    __global unsigned int *g_exec_block_scaler,
    __global unsigned long *g_output,
    __global unsigned long *unused8,
    __global unsigned long *unused9,
    __global unsigned char *g_challenge) {

  /* Shared-memory S-box arrays -- one copy per workgroup. */
  __local uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __local uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(get_local_id(0), get_local_size(0),
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  long target_chain_len = (881689 - *g_device_num) - ((get_global_id(0) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[get_global_id(0)] = 0;
    return;
  }

  unsigned int reduction_offset = *g_table_index * 65536;
  unsigned char plaintext[8];
  unsigned long index = hash_char_to_index_netntlmv1_7(g_hash, reduction_offset, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < 881688; i++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, g_challenge, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, i);
  }

  g_output[get_global_id(0)] = index;
}
