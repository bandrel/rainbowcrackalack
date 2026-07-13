#include "string.cu"
#include "rt.cu"
#include "rt_markov_mask.cu"

extern "C" __global__ void precompute_markov_mask(
    unsigned int *g_hash_type,
    unsigned char *g_hash,
    unsigned int *g_hash_len,
    char *g_charset,
    unsigned int *g_charset_len,
    unsigned int *g_plaintext_len_min,
    unsigned int *g_plaintext_len_max,
    unsigned int *g_table_index,
    unsigned long long *g_chain_len,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_output,
    unsigned long long *g_plaintext_space_up_to_index,
    unsigned long long *g_plaintext_space_total,
    const unsigned char *g_r_pos0,       /* ARG 15 */
    const unsigned char *g_r_bigram,     /* ARG 16 */
    const unsigned int  *g_sizes,        /* ARG 17 */
    unsigned int *g_mask_len,            /* ARG 18 */
    unsigned int *g_max_sz) {            /* ARG 19 */

  long long target_chain_len = (*g_chain_len - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = 0;
    return;
  }

  char charset[MAX_CHARSET_LEN];
  unsigned long long plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1];
  unsigned char hash[MAX_HASH_OUTPUT_LEN];
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned int plaintext_len = 0;
  unsigned long long index;

  unsigned int hash_type = *g_hash_type;
  unsigned int hash_len = *g_hash_len;
  unsigned int charset_len = *g_charset_len;
  g_memcpy((unsigned char *)charset, (unsigned char *)g_charset, charset_len);
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(*g_table_index);
  unsigned int chain_len = *g_chain_len;
  unsigned int mask_len = *g_mask_len;
  unsigned int max_sz = *g_max_sz;
  copy_plaintext_space_up_to_index(plaintext_space_up_to_index, g_plaintext_space_up_to_index, plaintext_len_max);
  unsigned long long plaintext_space_total = *g_plaintext_space_total;


  g_memcpy(hash, g_hash, *g_hash_len);
  index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov_mask(index, charset, charset_len, mask_len, max_sz, g_sizes, g_r_pos0, g_r_bigram, plaintext);
    plaintext_len = plaintext_len_max;
    do_hash(hash_type, plaintext, plaintext_len, hash, &hash_len);
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, i);
  }

  g_output[(blockIdx.x * blockDim.x + threadIdx.x)] = index;
}
