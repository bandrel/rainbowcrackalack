#include "markov10_functions.cu"


extern "C" __global__ void false_alarm_check_markov_ntlm10(
    unsigned int *unused1,
    char *unused2,
    unsigned int *g_charset_len,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned long long *unused5,
    unsigned long long *unused6,
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
    unsigned long long *g_pspace_total) {

  int index_pos = (*g_num_start_indices - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;
  if (index_pos < 0)
    return;

  unsigned char plaintext[10];
  unsigned int charset_len = *g_charset_len;
  unsigned long long pspace_total = *g_pspace_total;
  unsigned long long index = g_start_indices[index_pos], previous_index = 0;
  unsigned long long hash_base_index = g_hash_base_indices[index_pos] % pspace_total;
  unsigned int endpoint = g_start_index_positions[index_pos];

  for (unsigned int pos = 0; pos < endpoint + 1; pos++) {
    index_to_plaintext_markov10(index, charset, charset_len, g_sorted_pos0, g_sorted_bigram, plaintext);

    previous_index = index;
    index = hash_to_index_markov10(hash_ntlm10(plaintext), 0, pspace_total, pos);

    if ((index == (hash_base_index + pos)) || (index == (hash_base_index + pos - pspace_total))) {
      g_plaintext_indices[index_pos] = previous_index;
      return;
    }
  }
}
