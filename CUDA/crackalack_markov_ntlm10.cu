#include "markov10_functions.cu"


extern "C" __global__ void crackalack_markov_ntlm10(
    unsigned int *unused1,
    char *unused2,
    unsigned int *g_charset_len,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned int *g_reduction_offset,
    unsigned int *g_chain_len,
    unsigned long long *g_indices,
    unsigned int *g_pos_start,
    unsigned long long *unused6,
    unsigned long long *g_pspace_total,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *unused8) {
  unsigned long long index = g_indices[blockIdx.x * blockDim.x + threadIdx.x];
  unsigned char plaintext[10];
  unsigned int charset_len = *g_charset_len;
  unsigned int reduction_offset = *g_reduction_offset;
  unsigned long long pspace_total = *g_pspace_total;

  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_markov10(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov10(hash_ntlm10(plaintext), reduction_offset, pspace_total, pos);
  }

  g_indices[blockIdx.x * blockDim.x + threadIdx.x] = index;
  return;
}
