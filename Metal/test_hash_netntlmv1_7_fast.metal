#include <metal_stdlib>
using namespace metal;

#include "netntlmv1_7_functions.metal"

/* Unit-test kernel: exercises the optimized NetNTLMv1-7 fast path
 * (netntlmv1_challenge_to_ip + hash_netntlmv1_7_fast_ip) for a single
 * (plaintext, challenge) per dispatch, so the host can compare its 8-byte
 * output against the CPU reference.  See CUDA/test_hash_netntlmv1_7_fast.cu. */
kernel void test_hash_netntlmv1_7_fast(
    device unsigned char *g_plaintext [[buffer(0)]],
    device const unsigned char *g_challenge [[buffer(1)]],
    device unsigned char *g_output [[buffer(2)]],
    uint gid [[thread_position_in_grid]],
    uint lid [[thread_index_in_threadgroup]],
    uint lsz [[threads_per_threadgroup]]) {

  threadgroup uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  threadgroup uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  for (uint _i = lid; _i < 64; _i += lsz) {
    l_SB1[_i] = SB1[_i]; l_SB2[_i] = SB2[_i];
    l_SB3[_i] = SB3[_i]; l_SB4[_i] = SB4[_i];
    l_SB5[_i] = SB5[_i]; l_SB6[_i] = SB6[_i];
    l_SB7[_i] = SB7[_i]; l_SB8[_i] = SB8[_i];
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);

  unsigned char plaintext[8];
  for (int i = 0; i < 7; i++) plaintext[i] = g_plaintext[i];
  unsigned char challenge[8];
  for (int i = 0; i < 8; i++) challenge[i] = g_challenge[i];

  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(challenge, &cx, &cy);
  ulong h = hash_netntlmv1_7_fast_ip(plaintext, cx, cy,
      l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);

  /* hash_netntlmv1_7_fast_ip packs output[0..7] little-endian into h. */
  for (int i = 0; i < 8; i++)
    g_output[i] = (unsigned char)((h >> (8 * i)) & 0xFF);
}
