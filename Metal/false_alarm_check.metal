#include <metal_stdlib>
using namespace metal;

#include "shared.h"
#include "rt.metal"

kernel void false_alarm_check(
    device unsigned int *g_hash_type [[buffer(0)]],
    device char *g_charset [[buffer(1)]],
    device unsigned int *g_charset_len [[buffer(2)]],
    device unsigned int *g_plaintext_len_min [[buffer(3)]],
    device unsigned int *g_plaintext_len_max [[buffer(4)]],
    device unsigned int *g_reduction_offset [[buffer(5)]],
    device ulong *g_plaintext_space_total [[buffer(6)]],
    device ulong *g_plaintext_space_up_to_index [[buffer(7)]],
    device unsigned int *g_device_num [[buffer(8)]],
    device unsigned int *g_total_devices [[buffer(9)]],
    device unsigned int *g_num_start_indices [[buffer(10)]],
    device ulong *g_start_indices [[buffer(11)]],
    device unsigned int *g_start_index_positions [[buffer(12)]],
    device ulong *g_hash_base_indices [[buffer(13)]],
    device unsigned int *g_exec_block_scaler [[buffer(14)]],
    device ulong *g_plaintext_indices [[buffer(15)]],
    uint gid [[thread_position_in_grid]]) {

  int index_pos = (*g_num_start_indices - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;
  if (index_pos < 0)
    return;

  char charset[MAX_CHARSET_LEN];
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned char hash[MAX_HASH_OUTPUT_LEN];
  unsigned int plaintext_len;
  unsigned int hash_len;

  unsigned int charset_len = *g_charset_len;
  g_memcpy((thread unsigned char *)charset, (device unsigned char *)g_charset, charset_len);
  unsigned int hash_type = *g_hash_type;
  unsigned int plaintext_len_min = *g_plaintext_len_min;
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  unsigned int reduction_offset = *g_reduction_offset;
  ulong plaintext_space_total = *g_plaintext_space_total;
  ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN];

  copy_plaintext_space_up_to_index(plaintext_space_up_to_index, g_plaintext_space_up_to_index);

  ulong index = g_start_indices[index_pos], previous_index = 0;
  ulong hash_base_index = g_hash_base_indices[index_pos] % plaintext_space_total;
  unsigned int endpoint = g_start_index_positions[index_pos];


  for (unsigned int pos = 0; pos < endpoint + 1; pos++) {
    index_to_plaintext(index, charset, charset_len, plaintext_len_min, plaintext_len_max, plaintext_space_up_to_index, plaintext, &plaintext_len);
    do_hash(hash_type, plaintext, plaintext_len, hash, &hash_len);

    //printf("hash_type: %d, plaintext: %x, plaintext_len: %x, hash: %x\n", hash_type, plaintext, plaintext_len, hash);
    //printf("previous_index: %d, index: %d, hash_base_index: %d, pos: %d, plaintext_space_total: %d, index_pos: %d\n", previous_index, index, hash_base_index, pos, plaintext_space_total, index_pos);

    previous_index = index;
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, pos);

    if ((index == (hash_base_index + pos)) || (index == (hash_base_index + pos - plaintext_space_total))) {
      g_plaintext_indices[index_pos] = previous_index;
      return;
    }
  }
}
