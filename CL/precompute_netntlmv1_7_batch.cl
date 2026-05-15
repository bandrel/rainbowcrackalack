#include "netntlmv1_7_functions.cl"

/* Batched precompute for NetNTLMv1-7 tables.
 * Processes ALL hashes in a single kernel dispatch, called in
 * position-based chunks to avoid GPU watchdog timeouts.
 *
 * The reduction function for NetNTLMv1-7 is `(hash + offset + pos) mod 2^56`,
 * which has no exploitable algebraic structure across positions — every output
 * position requires its own independent chain walk from a different reduced
 * starting point.  Total work is therefore O(chain_len^2) per hash; the host
 * keeps the GPU saturated by tuning chunk_size (positions per dispatch).
 *
 * g_hashes:  all hashes concatenated (num_hashes * 16 bytes)
 * g_output:  flat array of num_hashes * total_positions entries, pre-zeroed
 *            by the host so positions whose walk would be empty already read 0
 *            layout: [hash0_pos0, hash0_pos1, ..., hash1_pos0, ...]
 *
 * Per-chunk dispatch:
 *   g_chunk_positions: number of positions in this chunk
 *   g_pos_start:       first position index for this chunk
 *   GWS = num_hashes * chunk_positions
 */
__kernel void precompute_netntlmv1_7_batch(
    __global unsigned char *g_hashes,
    __global unsigned int *g_num_hashes,
    __global unsigned int *g_chunk_positions,
    __global unsigned int *g_reduction_offset,
    __global unsigned long *g_chain_len,
    __global unsigned int *g_pos_start,
    __global unsigned int *g_total_positions,
    __global unsigned long *g_output) {

  /* Shared-memory S-box arrays -- one copy per workgroup. */
  __local uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __local uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(get_local_id(0), get_local_size(0),
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  unsigned int gid = get_global_id(0);
  unsigned int chunk_positions = *g_chunk_positions;
  unsigned int hash_idx = gid / chunk_positions;
  unsigned int local_pos = gid % chunk_positions;

  if (hash_idx >= *g_num_hashes) {
    return;
  }

  unsigned int absolute_pos = *g_pos_start + local_pos;
  unsigned int total_positions = *g_total_positions;

  if (absolute_pos >= total_positions) {
    return;
  }

  unsigned long chain_len = *g_chain_len;
  long target_chain_len = (long)chain_len - (long)absolute_pos - 1;

  /* Output buffer is pre-zeroed by the host, so we can skip the write here. */
  if (target_chain_len < 1) {
    return;
  }

  unsigned int reduction_offset = *g_reduction_offset;
  __global unsigned char *hash = g_hashes + hash_idx * 16;
  unsigned char plaintext[8];
  unsigned long index = hash_char_to_index_netntlmv1_7(hash, reduction_offset, target_chain_len - 1);

  for (unsigned long i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), reduction_offset, i);
  }

  g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = index;
}
