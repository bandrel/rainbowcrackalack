#include "markov9_functions.cu"


/* TODO: specify array length in definition...somehow? */
extern "C" __global__ void crackalack_markov_ntlm9(
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
    unsigned int *unused7,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *unused8) {
  unsigned long long index = g_indices[blockIdx.x * blockDim.x + threadIdx.x];
  unsigned char plaintext[9];
  unsigned int charset_len = *g_charset_len;
  unsigned int reduction_offset = *g_reduction_offset;


  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_markov9(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov9(hash_ntlm9(plaintext), reduction_offset, pos);
  }

  g_indices[blockIdx.x * blockDim.x + threadIdx.x] = index;
  return;
}
