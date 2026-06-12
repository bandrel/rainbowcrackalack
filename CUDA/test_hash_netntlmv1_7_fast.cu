#include "netntlmv1_7_functions.cu"

/* Unit-test kernel: exercises the optimized NetNTLMv1-7 fast path
 * (netntlmv1_challenge_to_ip + hash_netntlmv1_7_fast_ip -> the IP-hoisted,
 * on-the-fly-key-schedule DES) for a single (plaintext, challenge) per
 * dispatch, so the host can compare its 8-byte output against the CPU
 * reference.  One work-item; the lone thread cooperatively loads the S-boxes. */
extern "C" __global__ void test_hash_netntlmv1_7_fast(
    unsigned char *g_plaintext,
    unsigned char *g_challenge,
    unsigned char *g_output) {

  __shared__ uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __shared__ uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];

  LOAD_LOCAL_SBOXES(threadIdx.x, blockDim.x,
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  unsigned char plaintext[8];
  for (int i = 0; i < 7; i++) plaintext[i] = g_plaintext[i];
  unsigned char challenge[8];
  for (int i = 0; i < 8; i++) challenge[i] = g_challenge[i];

  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(challenge, &cx, &cy);
  unsigned long long h = hash_netntlmv1_7_fast_ip(plaintext, cx, cy,
      l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);

  /* hash_netntlmv1_7_fast_ip packs output[0..7] little-endian into h. */
  for (int i = 0; i < 8; i++)
    g_output[i] = (unsigned char)((h >> (8 * i)) & 0xFF);
}
