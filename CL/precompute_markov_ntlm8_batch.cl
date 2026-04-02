#include "markov8_functions.cl"

/* Batched precompute: processes ALL hashes in a single kernel dispatch.
 *
 * Called in position-based chunks to avoid GPU watchdog timeouts.
 * Each chunk handles a range of chain positions for all hashes.
 *
 * g_hashes:  all NTLM hashes concatenated (num_hashes * 16 bytes)
 * g_output:  flat array of num_hashes * full_positions entries
 *            layout: [hash0_pos0, hash0_pos1, ..., hash1_pos0, ...]
 *
 * Per-chunk dispatch:
 *   g_chunk_positions: number of positions in this chunk
 *   g_pos_start:       first position index for this chunk
 *   GWS = num_hashes * chunk_positions
 */
__kernel void precompute_markov_ntlm8_batch(
    __global unsigned char *g_hashes,
    __global unsigned int *g_num_hashes,
    __global unsigned int *g_chunk_positions,
    __global unsigned int *g_charset_len,
    __global unsigned long *g_chain_len,
    __global unsigned int *g_pos_start,
    __global unsigned int *g_total_positions,
    __global unsigned long *g_output,
    __constant unsigned char *g_sorted_pos0,
    __global const unsigned char *g_sorted_bigram) {

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

  if (target_chain_len < 1) {
    g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = 0;
    return;
  }

  __global unsigned char *hash = g_hashes + hash_idx * 16;
  unsigned char plaintext[8];
  unsigned int charset_len = *g_charset_len;
  unsigned long index = hash_char_to_index_markov8(hash, 0, target_chain_len - 1);

  for (unsigned long i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov8(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov8(hash_ntlm8(plaintext), 0, i);
  }

  g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = index;
}
