#include "markov8_functions.cl"


#define BIGRAM_LOCAL_SIZE 45125  /* 5 positions * 95 * 95 */

__kernel void crackalack_markov_ntlm8(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *g_charset_len,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *g_reduction_offset,
    __global unsigned int *g_chain_len,
    __global unsigned long *g_indices,
    __global unsigned int *g_pos_start,
    __global unsigned long *unused7,
    __global unsigned int *unused8,
    __constant unsigned char *g_sorted_pos0,
    __global const unsigned char *g_sorted_bigram,
    __global unsigned int *unused9) {
  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  /* Cooperatively load first 5 bigram position tables into local memory. */
  __local unsigned char l_bigram[BIGRAM_LOCAL_SIZE];
  unsigned int lid = get_local_id(0);
  unsigned int lsz = get_local_size(0);
  for (unsigned int i = lid; i < BIGRAM_LOCAL_SIZE; i += lsz)
    l_bigram[i] = g_sorted_bigram[i];
  barrier(CLK_LOCAL_MEM_FENCE);

  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_markov8_local(index, g_sorted_pos0, l_bigram, g_sorted_bigram, plaintext);
    index = hash_to_index_markov8(hash_ntlm8(plaintext), reduction_offset, pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
