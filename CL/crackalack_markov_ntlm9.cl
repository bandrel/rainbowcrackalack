#include "markov9_functions.cl"


/* TODO: specify array length in definition...somehow? */
__kernel void crackalack_markov_ntlm9(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *g_charset_len,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *g_chain_len,
    __global unsigned long *g_indices,
    __global unsigned int *g_pos_start,
    __global unsigned long *unused6,
    __global unsigned int *unused7,
    __constant unsigned char *g_sorted_pos0,
    __constant unsigned char *g_sorted_bigram,
    __global unsigned int *unused8) {
  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[9];
  unsigned int charset_len = *g_charset_len;


  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_markov9(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov9(hash_ntlm9(plaintext), pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
