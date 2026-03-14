#include <metal_stdlib>
using namespace metal;

#include "string.metal"
#include "rt.metal"

kernel void precompute(
    device unsigned int *g_hash_type [[buffer(0)]],
    device unsigned char *g_hash [[buffer(1)]],
    device unsigned int *g_hash_len [[buffer(2)]],
    device char *g_charset [[buffer(3)]],
    device unsigned int *g_charset_len [[buffer(4)]],
    device unsigned int *g_plaintext_len_min [[buffer(5)]],
    device unsigned int *g_plaintext_len_max [[buffer(6)]],
    device unsigned int *g_table_index [[buffer(7)]],
    device ulong *g_chain_len [[buffer(8)]],
    device unsigned int *g_device_num [[buffer(9)]],
    device unsigned int *g_total_devices [[buffer(10)]],
    device unsigned int *g_exec_block_scaler [[buffer(11)]],
    device ulong *g_output [[buffer(12)]],
    device ulong *g_plaintext_space_up_to_index [[buffer(13)]],
    device ulong *g_plaintext_space_total [[buffer(14)]],
    uint gid [[thread_position_in_grid]]) {

  long target_chain_len = (*g_chain_len - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[gid] = 0;
    return;
  }

  char charset[MAX_CHARSET_LEN];
  ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1];
  unsigned char hash[MAX_HASH_OUTPUT_LEN];
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned int plaintext_len = 0;
  ulong index;

  unsigned int hash_type = *g_hash_type;
  unsigned int hash_len = *g_hash_len;
  unsigned int charset_len = *g_charset_len;
  g_memcpy((thread unsigned char *)charset, (device unsigned char *)g_charset, charset_len);
  unsigned int plaintext_len_min = *g_plaintext_len_min;
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(*g_table_index);
  unsigned int chain_len = *g_chain_len;
  copy_plaintext_space_up_to_index(plaintext_space_up_to_index, g_plaintext_space_up_to_index, plaintext_len_max);
  ulong plaintext_space_total = *g_plaintext_space_total;


  g_memcpy(hash, g_hash, *g_hash_len);
  index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, target_chain_len - 1);

  for(unsigned int i = target_chain_len; i < chain_len - 1; i++) { // was chain_len - 1
    index_to_plaintext(index, charset, charset_len, plaintext_len_min, plaintext_len_max, plaintext_space_up_to_index, plaintext, &plaintext_len);
    do_hash(hash_type, plaintext, plaintext_len, hash, &hash_len);
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, i);
  }

  g_output[gid] = index;
}
