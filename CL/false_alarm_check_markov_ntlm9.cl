#include "markov9_functions.cl"


__kernel void false_alarm_check_markov_ntlm9(
    __global unsigned int *unused1,
    __global char *unused2,
    __global unsigned int *g_charset_len,
    __global unsigned int *unused3,
    __global unsigned int *unused4,
    __global unsigned long *unused5,
    __global unsigned long *unused6,
    __global unsigned int *g_device_num,
    __global unsigned int *g_total_devices,
    __global unsigned int *g_num_start_indices,
    __global unsigned long *g_start_indices,
    __global unsigned int *g_start_index_positions,
    __global unsigned long *g_hash_base_indices,
    __global unsigned int *g_exec_block_scaler,
    __global unsigned long *g_plaintext_indices,
    __constant unsigned char *g_sorted_pos0,
    __constant unsigned char *g_sorted_bigram,
    __global unsigned int *unused7) {

  int index_pos = (*g_num_start_indices - *g_device_num) - ((get_global_id(0) + *g_exec_block_scaler) * *g_total_devices) - 1;
  if (index_pos < 0)
    return;

  unsigned char plaintext[9];
  unsigned int charset_len = *g_charset_len;
  unsigned long index = g_start_indices[index_pos], previous_index = 0;
  unsigned long hash_base_index = g_hash_base_indices[index_pos] % PLAINTEXT_SPACE_TOTAL;
  unsigned int endpoint = g_start_index_positions[index_pos];

  for (unsigned int pos = 0; pos < endpoint + 1; pos++) {
    index_to_plaintext_markov9(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);

    previous_index = index;
    index = hash_to_index_markov9(hash_ntlm9(plaintext), pos);

    if ((index == (hash_base_index + pos)) || (index == (hash_base_index + pos - PLAINTEXT_SPACE_TOTAL))) {
      g_plaintext_indices[index_pos] = previous_index;
      return;
    }
  }
}
