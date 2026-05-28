#include "markov8_functions.cu"

/* Batched precompute for Markov-NTLM8 tables.  Processes ALL hashes
 * in a single kernel dispatch, called in position-based chunks to
 * avoid GPU watchdog timeouts. */
extern "C" __global__ void precompute_markov_ntlm8_batch(
    unsigned char *g_hashes,
    unsigned int *g_num_hashes,
    unsigned int *g_chunk_positions,
    unsigned int *g_charset_len,
    unsigned long long *g_chain_len,
    unsigned int *g_pos_start,
    unsigned int *g_total_positions,
    unsigned long long *g_output,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram) {

  unsigned int gid = (blockIdx.x * blockDim.x + threadIdx.x);
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

  unsigned long long chain_len = *g_chain_len;
  long long target_chain_len = (long long)chain_len - (long long)absolute_pos - 1;

  if (target_chain_len < 1) {
    g_output[(unsigned long long)hash_idx * total_positions + absolute_pos] = 0;
    return;
  }

  unsigned char *hash = g_hashes + hash_idx * 16;
  unsigned char plaintext[8];
  unsigned int charset_len = *g_charset_len;
  unsigned long long index = hash_char_to_index_markov8(hash, 0, target_chain_len - 1);

  for (unsigned long long i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov8(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);
    index = hash_to_index_markov8(hash_ntlm8(plaintext), 0, i);
  }

  g_output[(unsigned long long)hash_idx * total_positions + absolute_pos] = index;
}
