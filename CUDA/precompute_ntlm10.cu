#include "ntlm10_functions.cu"


extern "C" __global__ void precompute_ntlm10(
    unsigned int *unused1,
    unsigned char *g_hash,
    unsigned int *unused2,
    char *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *unused6,
    unsigned int *g_table_index,
    unsigned long long *g_chain_len,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_output,
    unsigned long long *unused9,
    unsigned long long *unused10) {

  /* Honor the host's chain_len (arg 8) instead of arg 6, which the host
   * actually binds to plaintext_len_max. */
  unsigned long long chain_len = *g_chain_len;
  long long target_chain_len = (chain_len - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = 0;
    return;
  }

  unsigned char plaintext[10];
  unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(*g_table_index);
  unsigned long long index = hash_char_to_index_ntlm10(g_hash, reduction_offset, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_ntlm10(index, plaintext);
    index = hash_to_index_ntlm10(hash_ntlm10(plaintext), reduction_offset, i);
  }

  g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
