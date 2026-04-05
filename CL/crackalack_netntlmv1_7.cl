#include "netntlmv1_7_functions.cl"


__kernel void crackalack_netntlmv1_7(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *g_reduction_offset,
    __global unsigned int *unused_chain_len,
    __global unsigned long *g_indices,
    __global unsigned long *unused8,
    __global unsigned long *unused9,
    __global unsigned int *unused10,
    __global char *unused11,
    __global unsigned int *unused12,
    __global unsigned int *unused13) {
  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  for (unsigned int pos = 0; pos < 881688; pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7(plaintext), reduction_offset, pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
