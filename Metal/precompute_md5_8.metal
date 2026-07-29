#include <metal_stdlib>
using namespace metal;

#include "md5_8_functions.metal"


kernel void precompute_md5_8(
    device unsigned int *unused1 [[buffer(0)]],
    device unsigned char *g_hash [[buffer(1)]],
    device unsigned int *unused2 [[buffer(2)]],
    device char *unused3 [[buffer(3)]],
    device unsigned int *unused4 [[buffer(4)]],
    device unsigned int *unused5 [[buffer(5)]],
    device unsigned int *unused6 [[buffer(6)]],
    device unsigned int *g_table_index [[buffer(7)]],
    device ulong *g_chain_len [[buffer(8)]],
    device unsigned int *g_device_num [[buffer(9)]],
    device unsigned int *g_total_devices [[buffer(10)]],
    device unsigned int *g_exec_block_scaler [[buffer(11)]],
    device ulong *g_output [[buffer(12)]],
    device ulong *unused8 [[buffer(13)]],
    device ulong *unused9 [[buffer(14)]],
    uint gid [[thread_position_in_grid]]) {

  /* Honor the host's chain_len (arg 8) instead of a hardcoded constant, so
   * lookups against tables of any chain length crack correctly. */
  long chain_len = (long)(*g_chain_len);
  long target_chain_len = (chain_len - *g_device_num) - ((gid + *g_exec_block_scaler) * *g_total_devices) - 1;

  if (target_chain_len < 1) {
    g_output[gid] = 0;
    return;
  }

  unsigned char plaintext[8];
  unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(*g_table_index);
  ulong index = hash_char_to_index_md5_8(g_hash, reduction_offset, target_chain_len - 1);

  for (unsigned int i = target_chain_len; i < chain_len - 1; i++) {
    index_to_plaintext_md5_8(index, plaintext);
    index = hash_to_index_md5_8(hash_md5_8(plaintext), reduction_offset, i);
  }

  g_output[gid] = index;
}
