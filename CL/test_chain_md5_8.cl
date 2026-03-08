#include "md5_8_functions.cl"

__kernel void test_chain_md5_8(__global unsigned int *g_chain_len, __global unsigned long *g_start, __global unsigned long *g_end) {

  unsigned long index = *g_start;
  unsigned char plaintext[8];


  for (unsigned int pos = 0; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_md5_8(index, charset, plaintext);
    index = hash_to_index_md5_8(hash_md5_8(plaintext), pos);
  }

  *g_end = index;
}
