#include "md5_9_functions.cu"


extern "C" __global__ void precompute_md5_9(
    unsigned int *unused1,
    unsigned char *g_hash,
    unsigned int *unused2,
    char *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *unused6,
    unsigned long long *unused7,
    unsigned long long *unused_chain_len,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_output,
    unsigned long long *unused8,
    unsigned long long *unused9) {

  long long target_chain_len = (803000 - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = 0;
    return;
  }

  unsigned char plaintext[9];
  unsigned long long index = hash_char_to_index_md5_9(g_hash, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < 802999; i++) {
    index_to_plaintext_md5_9(index, charset, plaintext);
    index = hash_to_index_md5_9(hash_md5_9(plaintext), i);
  }

  g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
