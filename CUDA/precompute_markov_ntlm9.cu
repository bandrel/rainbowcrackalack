#include "markov9_functions.cu"


extern "C" __global__ void precompute_markov_ntlm9(
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
    unsigned long long *unused8,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *unused9) {

  unsigned long long chain_len = *g_chain_len;
  long long target_chain_len = (chain_len - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = 0;
    return;
  }

  unsigned char plaintext[9];
  unsigned int charset_len = *g_charset_len;
  unsigned long long index = hash_char_to_index_markov9(g_hash, 0, target_chain_len - 1);

  for(unsigned long long i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov9(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov9(hash_ntlm9(plaintext), 0, i);
  }

  g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
