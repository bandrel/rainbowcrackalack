#include <metal_stdlib>
using namespace metal;

#include "md5_9_functions.metal"

kernel void test_chain_md5_9(
    device unsigned int *g_chain_len [[buffer(0)]],
    device ulong *g_start [[buffer(1)]],
    device ulong *g_end [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {

  ulong index = *g_start;
  unsigned char plaintext[9];

  for (unsigned int pos = 0; pos < (*g_chain_len - 1); pos++) {
    index_to_plaintext_md5_9(index, plaintext);
    index = hash_to_index_md5_9(hash_md5_9(plaintext), pos);
  }

  *g_end = index;
}
