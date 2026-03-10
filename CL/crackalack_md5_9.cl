#include "md5_9_functions.cl"


/* TODO: specify array length in definition...somehow? */
__kernel void crackalack_md5_9(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *unused6,
    __global unsigned int *unused_chain_len,
    __global unsigned long *g_indices,
    __global unsigned long *unused8,
    __global unsigned long *unused9,
    __global unsigned int *unused10,
    __global char *unused11,
    __global unsigned int *unused12,
    __global unsigned int *unused13) {
  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[9];


  for (unsigned int pos = 0; pos < 802999; pos++) {
    index_to_plaintext_md5_9(index, charset, plaintext);
    index = hash_to_index_md5_9(hash_md5_9(plaintext), pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
