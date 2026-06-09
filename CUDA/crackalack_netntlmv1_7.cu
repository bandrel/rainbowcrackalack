#include "netntlmv1_7_functions.cu"


extern "C" __global__ void crackalack_netntlmv1_7(
    unsigned int *unused1,
    char *unused2,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *g_reduction_offset,
    unsigned int *unused_chain_len,
    unsigned long long *g_indices,
    unsigned long long *unused8,
    unsigned long long *unused9,
    unsigned int *unused10,
    char *unused11,
    unsigned int *unused12,
    unsigned int *unused13,
    unsigned char *g_challenge) {

  /* Shared-memory S-box arrays -- one copy per workgroup. */
  __shared__ uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __shared__ uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(threadIdx.x, blockDim.x,
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  unsigned long long index = g_indices[blockIdx.x * blockDim.x + threadIdx.x];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  for (unsigned int pos = 0; pos < 881688; pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, g_challenge, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, pos);
  }

  g_indices[blockIdx.x * blockDim.x + threadIdx.x] = index;
  return;
}
