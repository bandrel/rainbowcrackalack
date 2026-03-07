#include <metal_stdlib>
using namespace metal;

#include "rt.metal"
#include "shared.h"
#include "string.metal"

kernel void test_index_to_plaintext(
    device char *g_charset [[buffer(0)]],
    device unsigned int *g_charset_len [[buffer(1)]],
    device unsigned int *g_plaintext_len_min [[buffer(2)]],
    device unsigned int *g_plaintext_len_max [[buffer(3)]],
    device ulong *g_index [[buffer(4)]],
    device unsigned char *g_plaintext [[buffer(5)]],
    device unsigned int *g_plaintext_len [[buffer(6)]],
    device unsigned char *g_debug [[buffer(7)]],
    device unsigned int *g_is_mask [[buffer(8)]],
    device char *g_mask_charset_data [[buffer(9)]],
    device unsigned int *g_mask_charset_lens [[buffer(10)]],
    uint gid [[thread_position_in_grid]]) {

  ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN];

  char charset[MAX_CHARSET_LEN];
  unsigned int plaintext_len_min = *g_plaintext_len_min;
  unsigned int plaintext_len_max = *g_plaintext_len_max;
  ulong index = *g_index;
  unsigned char plaintext[MAX_PLAINTEXT_LEN];
  unsigned int plaintext_len = *g_plaintext_len;
  unsigned int is_mask = *g_is_mask;

  unsigned int charset_len = g_strncpy(charset, g_charset, sizeof(charset));

  fill_plaintext_space_table(charset_len, plaintext_len_min, plaintext_len_max, plaintext_space_up_to_index);

  index_to_plaintext(index, charset, charset_len, is_mask, g_mask_charset_data, g_mask_charset_lens, plaintext_len_min, plaintext_len_max, plaintext_space_up_to_index, plaintext, &plaintext_len);

  *g_plaintext_len = plaintext_len;
  for (int i = 0; i < plaintext_len; i++)
    g_plaintext[i] = plaintext[i];

  return;
}
