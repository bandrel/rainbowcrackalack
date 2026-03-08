#include "rt.cl"
#include "rt_markov.cl"
#include "string.cl"


__kernel void crackalack_markov(
    __global unsigned int *g_hash_type,
    __global char *g_charset,
    __global unsigned int *g_charset_len,
    __global unsigned int *g_plaintext_len_min,
    __global unsigned int *g_plaintext_len_max,
    __global unsigned int *g_reduction_offset,
    __global unsigned int *g_chain_len,
    __global unsigned long *g_indices,
    __global unsigned int *g_pos_start,
    __global unsigned long *g_plaintext_space_up_to_index,
    __global unsigned long *g_plaintext_space_total,
    __global unsigned int *g_is_mask,
    __global char *g_mask_charset_data,
    __global unsigned int *g_mask_charset_lens,
    __constant unsigned char *g_sorted_pos0,
    __constant unsigned char *g_sorted_bigram)
{
  /* Markov generation uses fixed-length plaintexts (plaintext_len_min == plaintext_len_max,
   * enforced by the host). g_plaintext_len_min, g_plaintext_space_up_to_index, g_is_mask,
   * g_mask_charset_data, and g_mask_charset_lens are accepted for ABI compatibility with
   * crackalack.cl but are not used here. */
    unsigned int hash_type = *g_hash_type;
    char charset[MAX_CHARSET_LEN];
    unsigned int plaintext_len_max = *g_plaintext_len_max;
    unsigned int reduction_offset = *g_reduction_offset;
    unsigned int chain_len = *g_chain_len;
    unsigned long index = g_indices[get_global_id(0)];
    unsigned int pos = *g_pos_start;

    unsigned int charset_len = *g_charset_len;
    g_memcpy((unsigned char *)charset, (unsigned char __global *)g_charset, charset_len);

    unsigned long plaintext_space_total = *g_plaintext_space_total;

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
                                  plaintext_len_max,
                                  g_sorted_pos0, g_sorted_bigram,
                                  plaintext);
        do_hash(hash_type, plaintext, plaintext_len_max, hash, &hash_len);
        index = hash_to_index(hash, hash_len, reduction_offset,
                              plaintext_space_total, pos);
    }

    g_indices[get_global_id(0)] = index;
}
