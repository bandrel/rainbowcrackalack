/* netntlmv1_fast.cu -- Shared-memory (shared) S-box DES for optimized kernels.
 *
 * The __constant S-box arrays in netntlmv1.cu suffer from serialized access
 * on NVIDIA when warp threads hit different addresses.  This file provides
 * netntlmv1_hash_fast() which reads from __shared__ copies instead, giving
 * full-bandwidth parallel access across the workgroup.
 *
 * Usage: each kernel that #includes this file must:
 *   1. Declare 8 x __shared__ uint32_t[64] arrays
 *   2. Cooperatively load them from the __constant originals
 *   3. __syncthreads()
 *   4. Pass all 8 pointers through to hash_netntlmv1_7_fast()
 */

#include "netntlmv1.cu"

/* ---- DES_ROUND using __shared__ S-box pointers ---- */

#define DES_ROUND_LOCAL(X, Y, sb1, sb2, sb3, sb4, sb5, sb6, sb7, sb8) \
    do { \
        T = *SK_ptr++ ^ (X); \
        (Y) ^= (sb8)[ (T      ) & 0x3F ] ^ \
               (sb6)[ (T >>  8) & 0x3F ] ^ \
               (sb4)[ (T >> 16) & 0x3F ] ^ \
               (sb2)[ (T >> 24) & 0x3F ]; \
        T = *SK_ptr++ ^ (((X) << 28) | ((X) >> 4)); \
        (Y) ^= (sb7)[ (T      ) & 0x3F ] ^ \
               (sb5)[ (T >>  8) & 0x3F ] ^ \
               (sb3)[ (T >> 16) & 0x3F ] ^ \
               (sb1)[ (T >> 24) & 0x3F ]; \
    } while(0)

/* ---- Fast hash using __shared__ S-boxes ---- */

__device__ inline void netntlmv1_hash_fast(
    uint32_t SK[32],
    unsigned char *plaintext,
    unsigned char *output,
    __shared__ uint32_t *l_SB1, __shared__ uint32_t *l_SB2,
    __shared__ uint32_t *l_SB3, __shared__ uint32_t *l_SB4,
    __shared__ uint32_t *l_SB5, __shared__ uint32_t *l_SB6,
    __shared__ uint32_t *l_SB7, __shared__ uint32_t *l_SB8)
{
  uint32_t X, Y, T;

  plaintext[7] = '\0';

  des_ecb_setkey_56(SK, plaintext);

  /* State after initial permutation of "1122334455667788" */
  X = 0xf0aaf0aa;
  Y = 0x00cd00cd;

  uint32_t *SK_ptr = SK;

  #pragma unroll
  for (int _round = 0; _round < 8; _round++) {
    DES_ROUND_LOCAL(Y, X, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);
    DES_ROUND_LOCAL(X, Y, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);
  }

  DES_FP(Y, X);

  PUT_UINT32_BE(Y, output, 0);
  PUT_UINT32_BE(X, output, 4);
}

/* ---- Cooperative S-box loading macro ---- */

#define LOAD_LOCAL_SBOXES(lid, lsz, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8) \
    do { \
        for (uint _i = (lid); _i < 64; _i += (lsz)) { \
            (l_SB1)[_i] = SB1[_i]; (l_SB2)[_i] = SB2[_i]; \
            (l_SB3)[_i] = SB3[_i]; (l_SB4)[_i] = SB4[_i]; \
            (l_SB5)[_i] = SB5[_i]; (l_SB6)[_i] = SB6[_i]; \
            (l_SB7)[_i] = SB7[_i]; (l_SB8)[_i] = SB8[_i]; \
        } \
        __syncthreads(); \
    } while(0)
