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

  /* Shared-memory S-box arrays -- one copy per workgroup. */
  __local uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __local uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(get_local_id(0), get_local_size(0),
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  for (unsigned int pos = 0; pos < 881688; pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
