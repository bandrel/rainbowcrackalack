#include "ntlm10_functions.cu"


extern "C" __global__ void false_alarm_check_ntlm10(
    unsigned int *unused1,
    char *unused2,
    unsigned int *unused3,
    unsigned int *unused4,
    unsigned int *unused5,
    unsigned long long *unused6,
    unsigned long long *unused7,
    unsigned long long *unused_pspace_table,
    unsigned int *g_device_num,
    unsigned int *g_total_devices,
    unsigned int *g_num_start_indices,
    unsigned long long *g_start_indices,
    unsigned int *g_start_index_positions,
    unsigned long long *g_hash_base_indices,
    unsigned int *g_exec_block_scaler,
    unsigned long long *g_plaintext_indices) {

  int index_pos = (*g_num_start_indices - *g_device_num) - (((blockIdx.x * blockDim.x + threadIdx.x) + *g_exec_block_scaler) * *g_total_devices) - 1;
  if (index_pos < 0)
    return;

  unsigned char plaintext[10];
  unsigned long long index = g_start_indices[index_pos], previous_index = 0;
  /* 95^10 > 2^64: hash_base_index is already in range, no modulo needed. */
  unsigned long long hash_base_index = g_hash_base_indices[index_pos];
  unsigned int endpoint = g_start_index_positions[index_pos];

  for (unsigned int pos = 0; pos < endpoint + 1; pos++) {
    index_to_plaintext_ntlm10(index, plaintext);

    previous_index = index;
    index = hash_to_index_ntlm10(hash_ntlm10(plaintext), pos);

    if (index == (hash_base_index + pos)) {
      g_plaintext_indices[index_pos] = previous_index;
      return;
    }
  }
}
