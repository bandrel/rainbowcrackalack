#include "netntlmv1_7_functions.cl"


__kernel void crackalack_netntlmv1_7(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned int *unused5,
    __global unsigned int *g_reduction_offset,
    __global unsigned int *g_chain_len,
    __global unsigned long *g_indices,
    __global unsigned int *g_pos_start,
    __global unsigned long *unused9,
    __global unsigned int *unused10,
    __global char *unused11,
    __global unsigned int *unused12,
    __global unsigned int *unused13,
    __global unsigned char *g_challenge) {

  /* Shared-memory S-box arrays -- one copy per workgroup. */
  __local uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __local uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(get_local_id(0), get_local_size(0),
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  unsigned long index = g_indices[get_global_id(0)];
  unsigned char plaintext[8];
  unsigned int reduction_offset = *g_reduction_offset;

  unsigned char challenge_local[8];
  for (int _c = 0; _c < 8; _c++) challenge_local[_c] = g_challenge[_c];

  /* Honor the host's calibrated pos_start/chain_len (like crackalack_ntlm8),
   * so multi-pass generation is correct and the gen probe measures real
   * throughput.  Previously this loop was hardcoded 0..881688, which made the
   * calibration mis-measure (it walked the full chain regardless of the probe's
   * chain_len) and broke multi-pass (each pass re-walked the whole chain). */
  for (unsigned int pos = *g_pos_start; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, challenge_local, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, pos);
  }

  g_indices[get_global_id(0)] = index;
  return;
}
