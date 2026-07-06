#include "markov8_functions.cu"


#define BIGRAM_LOCAL_SIZE 45125  /* 5 positions * 95 * 95 */

extern "C" __global__ void crackalack_markov_ntlm8(
    unsigned int *unused1,
    char *unused2,
    unsigned int *g_charset_len,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned int *g_reduction_offset,
    unsigned int *g_chain_len,
    unsigned long long *g_indices,
    unsigned int *g_pos_start,
    unsigned long long *unused7,
    unsigned int *unused8,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *unused9) {
  unsigned long long index = g_indices[blockIdx.x * blockDim.x + threadIdx.x];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  /* Cooperatively load first 5 bigram position tables into shared memory. */
  __shared__ unsigned char l_bigram[BIGRAM_LOCAL_SIZE];
  unsigned int lid = threadIdx.x;
  unsigned int lsz = blockDim.x;
  for (unsigned int i = lid; i < BIGRAM_LOCAL_SIZE; i += lsz)
    l_bigram[i] = g_sorted_bigram[i];
  __syncthreads();

  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_markov8_local(index, g_sorted_pos0, l_bigram, g_sorted_bigram, plaintext);
    index = hash_to_index_markov8(hash_ntlm8(plaintext), reduction_offset, pos);
  }

  g_indices[blockIdx.x * blockDim.x + threadIdx.x] = index;
  return;
}
