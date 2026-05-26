#include "md5_9_functions.cu"


extern "C" __global__ void crackalack_md5_9(
    unsigned int *unused1,
    char *unused2,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *unused6,
    unsigned int *g_chain_len,
    unsigned long long *g_indices,
    unsigned int *g_pos_start,
    unsigned long long *unused9,
    unsigned int *unused10,
    char *unused11,
    unsigned int *unused12,
    unsigned int *unused13) {
  unsigned long long index = g_indices[blockIdx.x * blockDim.x + threadIdx.x];
  unsigned char plaintext[9];


  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_md5_9(index, charset, plaintext);
    index = hash_to_index_md5_9(hash_md5_9(plaintext), pos);
  }

  g_indices[blockIdx.x * blockDim.x + threadIdx.x] = index;
  return;
}
