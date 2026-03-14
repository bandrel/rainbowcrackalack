#include "string.cl"
#include "rt.cl"
#include "rt_markov.cl"

__kernel void precompute_markov(
    __global unsigned int *g_hash_type,
    __global unsigned char *g_hash,
    __global unsigned int *g_hash_len,
    __global char *g_charset,
    __global unsigned int *g_charset_len,
    __global unsigned int *g_plaintext_len_min,
    __global unsigned int *g_plaintext_len_max,
    __global unsigned int *g_table_index,
    __global unsigned long *g_chain_len,
    __global unsigned int *g_device_num,
    __global unsigned int *g_total_devices,
    __global unsigned int *g_exec_block_scaler,
    __global unsigned long *g_output,
    __global unsigned long *g_plaintext_space_up_to_index,
    __global unsigned long *g_plaintext_space_total,
    __constant unsigned char *g_sorted_pos0,
    __constant unsigned char *g_sorted_bigram,
    __global unsigned int *g_max_positions) {

  long target_chain_len = (*g_chain_len - *g_device_num) - ((get_global_id(0) + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[get_global_id(0)] = 0;
    return;
  }

  char charset[MAX_CHARSET_LEN];
  unsigned long plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1];
  unsigned char hash[MAX_HASH_OUTPUT_LEN];
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned int plaintext_len = 0;
  unsigned long index;

  unsigned int hash_type = *g_hash_type;
  unsigned int hash_len = *g_hash_len;
  unsigned int charset_len = *g_charset_len;
  g_memcpy((unsigned char *)charset, (unsigned char __global *)g_charset, charset_len);
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(*g_table_index);
  unsigned int chain_len = *g_chain_len;
  unsigned int max_positions = *g_max_positions;
  copy_plaintext_space_up_to_index(plaintext_space_up_to_index, g_plaintext_space_up_to_index, plaintext_len_max);
  unsigned long plaintext_space_total = *g_plaintext_space_total;


  g_memcpy(hash, g_hash, *g_hash_len);
  index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_markov(index, charset, charset_len, plaintext_len_max, max_positions, g_sorted_pos0, g_sorted_bigram, plaintext);
    plaintext_len = plaintext_len_max;
    do_hash(hash_type, plaintext, plaintext_len, hash, &hash_len);
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, i);
  }

  g_output[get_global_id(0)] = index;
}
