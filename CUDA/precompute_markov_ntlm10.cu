#include "markov10_functions.cu"


extern "C" __global__ void precompute_markov_ntlm10(
    unsigned int *unused1,
    unsigned char *g_hash,
    unsigned int *unused2,
    char *unused3,
    unsigned int *g_charset_len,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *unused6,
    unsigned long long *g_chain_len,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_output,
    unsigned long long *unused7,
    unsigned long long *g_pspace_total,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *unused8) {

  unsigned long long chain_len = *g_chain_len;
  long long target_chain_len = (chain_len - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = 0;
    return;
  }

  unsigned char plaintext[10];
  unsigned int charset_len = *g_charset_len;
  unsigned long long pspace_total = *g_pspace_total;
  unsigned long long index = hash_char_to_index_markov10(g_hash, 0, pspace_total, target_chain_len - 1);

  for(unsigned long long i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov10(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov10(hash_ntlm10(plaintext), 0, pspace_total, i);
  }

  g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
