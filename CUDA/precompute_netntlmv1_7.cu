#include "netntlmv1_7_functions.cu"


extern "C" __global__ void precompute_netntlmv1_7(
    unsigned int *unused1,
    unsigned char *g_hash,
    unsigned int *unused2,
    char *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned int *unused6,
    unsigned int *g_table_index,
    unsigned long long *g_chain_len,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_output,
    unsigned long long *unused8,
    unsigned long long *unused9,
    unsigned char *g_challenge) {

  /* Shared-memory S-box arrays -- one copy per workgroup. */
  __shared__ uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __shared__ uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(threadIdx.x, blockDim.x,
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  /* Honor the host's chain_len (arg 8) instead of a hardcoded constant, so the
   * per-hash fallback path is correct for tables of any chain length.  Mirrors
   * the fix applied to crackalack_netntlmv1_7 (f11bf2f) and the batch kernel. */
  long long chain_len = (long long)(*g_chain_len);
  long long target_chain_len = (chain_len - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = 0;
    return;
  }

  unsigned int reduction_offset = *g_table_index * 65536;
  unsigned char plaintext[8];
  unsigned long long index = hash_char_to_index_netntlmv1_7(g_hash, reduction_offset, target_chain_len - 1);

  /* Challenge initial permutation is loop-invariant: compute once. */
  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(g_challenge, &cx, &cy);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast_ip(plaintext, cx, cy, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, i);
  }

  g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
