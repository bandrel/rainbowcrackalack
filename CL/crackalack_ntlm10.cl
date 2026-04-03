#include "ntlm10_functions.cl"


__kernel void crackalack_ntlm10(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *unused6,
    __global unsigned int *g_chain_len,
    __global unsigned long *g_indices,
    __global unsigned int *g_pos_start,
    __global unsigned long *unused9,
    __global unsigned int *unused10,
    __global char *unused11,
    __global unsigned int *unused12,
    __global unsigned int *unused13) {
  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[10];


  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_ntlm10(index, plaintext);
    index = hash_to_index_ntlm10(hash_ntlm10(plaintext), pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
