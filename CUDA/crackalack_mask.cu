#include "rt.cu"
#include "rt_mask.cu"
#include "string.cu"


extern "C" __global__ void crackalack_mask(
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
    const char *g_mask_data,
    const unsigned int *g_mask_lens,
    unsigned int *g_mask_len)
{
  /* Mask generation uses fixed-length plaintexts (length == mask->length,
   * enforced by the host). g_charset, g_charset_len, g_plaintext_len_min,
   * and g_plaintext_space_up_to_index are accepted for ABI compatibility
   * with crackalack.cu and crackalack_markov.cu but are not used here.
   * g_plaintext_len_max is also unused: mask_len fully determines the
   * plaintext length. */
    unsigned int hash_type = *g_hash_type;
    unsigned int reduction_offset = *g_reduction_offset;
    unsigned int chain_len = *g_chain_len;
    unsigned long long index = g_indices[(blockIdx.x * blockDim.x + threadIdx.x)];
    unsigned int pos = *g_pos_start;
    unsigned int mask_len = *g_mask_len;

    unsigned long long plaintext_space_total = *g_plaintext_space_total;

    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned char hash[MAX_HASH_OUTPUT_LEN];
    unsigned int hash_len;
    unsigned int plaintext_len;

    /*
     * Generate a chain segment starting at pos and ending at chain_len - 1.
     * The chain may be split across multiple kernel invocations (passes); pos
     * tracks where this pass begins, matching the crackalack kernel's pattern.
     * Mask tables assume fixed-length plaintexts (length == mask_len).
     */
    for (; pos < chain_len - 1; pos++) {
        index_to_plaintext_mask(index, g_mask_data, g_mask_lens, mask_len,
                                plaintext, &plaintext_len);
        do_hash(hash_type, plaintext, mask_len, hash, &hash_len);
        index = hash_to_index(hash, hash_len, reduction_offset,
                              plaintext_space_total, pos);
    }

    g_indices[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
