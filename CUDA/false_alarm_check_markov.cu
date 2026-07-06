#include "shared.h"
#include "rt.cu"
#include "rt_markov.cu"

extern "C" __global__ void false_alarm_check_markov(
    unsigned int *g_hash_type,
    char *g_charset,
    unsigned int *g_charset_len,
    unsigned int *g_plaintext_len_min,
    unsigned int *g_plaintext_len_max,
    unsigned int *g_reduction_offset,
    unsigned long long *g_plaintext_space_total,
    unsigned long long *g_plaintext_space_up_to_index,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_num_start_indices,
    unsigned long long *g_start_indices,
    unsigned int *g_start_index_positions,
    unsigned long long *g_hash_base_indices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_plaintext_indices,
    const unsigned char *g_sorted_pos0,
    const unsigned char *g_sorted_bigram,
    unsigned int *g_max_positions) {

  int index_pos = (*g_num_start_indices - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;
  if (index_pos < 0)
    return;

  char charset[MAX_CHARSET_LEN];
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned char hash[MAX_HASH_OUTPUT_LEN];
  unsigned int plaintext_len;
  unsigned int hash_len;

  unsigned int charset_len = *g_charset_len;
  g_memcpy((unsigned char *)charset, (unsigned char *)g_charset, charset_len);
  unsigned int hash_type = *g_hash_type;
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  unsigned int reduction_offset = *g_reduction_offset;
  unsigned int max_positions = *g_max_positions;
  unsigned long long plaintext_space_total = *g_plaintext_space_total;
  unsigned long long plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1];

  copy_plaintext_space_up_to_index(plaintext_space_up_to_index, g_plaintext_space_up_to_index, plaintext_len_max);

  unsigned long long index = g_start_indices[index_pos], previous_index = 0;
  unsigned long long hash_base_index = g_hash_base_indices[index_pos] % plaintext_space_total;
  unsigned int endpoint = g_start_index_positions[index_pos];


  for (unsigned int pos = 0; pos < endpoint + 1; pos++) {
    index_to_plaintext_markov(index, charset, charset_len, plaintext_len_max, max_positions, g_sorted_pos0, g_sorted_bigram, plaintext);
    plaintext_len = plaintext_len_max;
    do_hash(hash_type, plaintext, plaintext_len, hash, &hash_len);

    previous_index = index;
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, pos);

    if ((index == (hash_base_index + pos)) || (index == (hash_base_index + pos - plaintext_space_total))) {
      g_plaintext_indices[index_pos] = previous_index;
      return;
    }
  }
}
