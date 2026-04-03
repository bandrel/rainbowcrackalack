#include "markov10_functions.cl"


__kernel void crackalack_markov_ntlm10(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *g_charset_len,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *g_reduction_offset,
    __global unsigned int *g_chain_len,
    __global unsigned long *g_indices,
    __global unsigned int *g_pos_start,
    __global unsigned long *unused6,
    __global unsigned long *g_pspace_total,
    __constant unsigned char *g_sorted_pos0,
    __global const unsigned char *g_sorted_bigram,
    __global unsigned int *unused8) {
  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[10];
  unsigned int charset_len = *g_charset_len;
  unsigned int reduction_offset = *g_reduction_offset;
  unsigned long pspace_total = *g_pspace_total;

  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_markov10(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov10(hash_ntlm10(plaintext), reduction_offset, pspace_total, pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
