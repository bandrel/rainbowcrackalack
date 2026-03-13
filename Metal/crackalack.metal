#include <metal_stdlib>
using namespace metal;

#include "rt.metal"
#include "string.metal"


kernel void crackalack(
    device unsigned int *g_hash_type [[buffer(0)]],
    device char *g_charset [[buffer(1)]],
    device unsigned int *g_charset_len [[buffer(2)]],
    device unsigned int *g_plaintext_len_min [[buffer(3)]],
    device unsigned int *g_plaintext_len_max [[buffer(4)]],
    device unsigned int *g_reduction_offset [[buffer(5)]],
    device unsigned int *g_chain_len [[buffer(6)]],
    device ulong *g_indices [[buffer(7)]],
    device unsigned int *g_pos_start [[buffer(8)]],
    device ulong *g_plaintext_space_up_to_index [[buffer(9)]],
    device ulong *g_plaintext_space_total [[buffer(10)]],
    uint gid [[thread_position_in_grid]]) {

  unsigned int hash_type = *g_hash_type;
  char charset[MAX_CHARSET_LEN];
  unsigned int plaintext_len_min = *g_plaintext_len_min;
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  unsigned int reduction_offset = *g_reduction_offset;
  unsigned int chain_len = *g_chain_len;
  ulong start_index = g_indices[gid];
  unsigned int pos = *g_pos_start;

  unsigned int charset_len = *g_charset_len;
  g_memcpy((thread unsigned char *)charset, (device unsigned char *)g_charset, charset_len);

  ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN];
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned int plaintext_len = 0;
  unsigned char hash[MAX_HASH_OUTPUT_LEN];
  unsigned int hash_len;

  copy_plaintext_space_up_to_index(plaintext_space_up_to_index, g_plaintext_space_up_to_index);
  ulong plaintext_space_total = *g_plaintext_space_total;

  // Generate a chain, and store it in the local buffer.
  g_indices[gid] = generate_rainbow_chain(
        hash_type,
        charset,
        charset_len,
        plaintext_len_min,
        plaintext_len_max,
        reduction_offset,
        chain_len,
        start_index++,
	pos,
        plaintext_space_up_to_index,
        plaintext_space_total,
        plaintext,
        &plaintext_len,
        hash,
        &hash_len);
  return;
}
