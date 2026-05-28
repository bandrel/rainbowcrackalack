#include "rt.cu"
#include "rt_markov.cu"
#include "string.cu"


extern "C" __global__ void crackalack_markov(
    unsigned int *g_hash_type,
    char *g_charset,
    unsigned int *g_charset_len,
    unsigned int *g_plaintext_len_min,
    unsigned int *g_plaintext_len_max,
    unsigned int *g_reduction_offset,
    unsigned int *g_chain_len,
    unsigned long long *g_indices,
    unsigned int *g_pos_start,
    unsigned long long *g_plaintext_space_up_to_index,
    unsigned long long *g_plaintext_space_total,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *g_max_positions)
{
  /* Markov generation uses fixed-length plaintexts (plaintext_len_min == plaintext_len_max,
   * enforced by the host). g_plaintext_len_min and g_plaintext_space_up_to_index are
   * accepted for ABI compatibility with crackalack.cu but are not used here. */
    unsigned int hash_type = *g_hash_type;
    char charset[MAX_CHARSET_LEN];
    unsigned int plaintext_len_max = *g_plaintext_len_max;
    unsigned int reduction_offset = *g_reduction_offset;
    unsigned int chain_len = *g_chain_len;
    unsigned long long index = g_indices[(blockIdx.x * blockDim.x + threadIdx.x)];
    unsigned int pos = *g_pos_start;
    unsigned int max_positions = *g_max_positions;

    unsigned int charset_len = *g_charset_len;
    g_memcpy((unsigned char *)charset, (unsigned char *)g_charset, charset_len);

    unsigned long long plaintext_space_total = *g_plaintext_space_total;

    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned char hash[MAX_HASH_OUTPUT_LEN];
    unsigned int hash_len;

    /*
     * Generate a chain segment starting at pos and ending at chain_len - 1.
     * The chain may be split across multiple kernel invocations (passes); pos
     * tracks where this pass begins, matching the crackalack kernel's pattern.
     * Markov tables assume fixed-length plaintexts, so plaintext_len_max is
     * used directly.
     */
    for (; pos < chain_len - 1; pos++) {
        index_to_plaintext_markov(index, charset, charset_len,
                                  plaintext_len_max, max_positions,
                                  g_sorted_pos0, g_sorted_bigram,
                                  plaintext);
        do_hash(hash_type, plaintext, plaintext_len_max, hash, &hash_len);
        index = hash_to_index(hash, hash_len, reduction_offset,
                              plaintext_space_total, pos);
    }

    g_indices[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
