#include "netntlmv1_7_functions.cu"


extern "C" __global__ void crackalack_netntlmv1_7(
    unsigned int *unused1,
    char *unused2,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *g_reduction_offset,
    unsigned int *g_chain_len,
    unsigned long long *g_indices,
    unsigned int *g_pos_start,
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

  /* Challenge initial permutation is loop-invariant: compute once. */
  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(g_challenge, &cx, &cy);

  /* Honor the host's calibrated pos_start/chain_len (like crackalack_ntlm8),
   * so multi-pass generation is correct and the gen probe measures real
   * throughput.  Previously this loop was hardcoded 0..881688, which made the
   * calibration mis-measure (it walked the full chain regardless of the probe's
   * chain_len) and broke multi-pass (each pass re-walked the whole chain). */
  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast_ip(plaintext, cx, cy, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, pos);
  }

  g_indices[blockIdx.x * blockDim.x + threadIdx.x] = index;
  return;
}
