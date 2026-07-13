#include "rt.cl"
#include "rt_mask.cl"
#include "string.cl"


__kernel void crackalack_mask(
    __global unsigned int      *g_hash_type,
    __global char              *g_charset,
    __global unsigned int      *g_charset_len,
    __global unsigned int      *g_plaintext_len_min,
    __global unsigned int      *g_plaintext_len_max,
    __global unsigned int      *g_reduction_offset,
    __global unsigned int      *g_chain_len,
    __global unsigned long     *g_indices,
    __global unsigned int      *g_pos_start,
    __global unsigned long     *g_plaintext_space_up_to_index,
    __global unsigned long     *g_plaintext_space_total,
    __global const char        *g_mask_data,
    __global const unsigned int *g_mask_lens,
    __global unsigned int      *g_mask_len)
{
  /* Mask generation uses fixed-length plaintexts (length == mask->length,
   * enforced by the host). g_charset, g_charset_len, g_plaintext_len_min,
   * and g_plaintext_space_up_to_index are accepted for ABI compatibility
   * with crackalack.cl and crackalack_markov.cl but are not used here.
   * g_plaintext_len_max is also unused: mask_len fully determines the
   * plaintext length. */
    unsigned int hash_type = *g_hash_type;
    unsigned int reduction_offset = *g_reduction_offset;
    unsigned int chain_len = *g_chain_len;
    unsigned long index = g_indices[get_global_id(0)];
    unsigned int pos = *g_pos_start;
    unsigned int mask_len = *g_mask_len;

    unsigned long plaintext_space_total = *g_plaintext_space_total;

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

    g_indices[get_global_id(0)] = index;
}
