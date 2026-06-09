/* netntlmv1_fast.metal -- Threadgroup (shared) memory S-box DES for optimized kernels.
 *
 * Metal port of netntlmv1_fast.cl.  Uses threadgroup memory instead of
 * __local, and threadgroup_barrier instead of barrier(CLK_LOCAL_MEM_FENCE).
 */

#include "netntlmv1.metal"

/* ---- DES_ROUND using threadgroup S-box pointers ---- */

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

/* ---- Fast hash using threadgroup S-boxes ---- */

inline void netntlmv1_hash_fast(
    thread uint32_t SK[32],
    thread unsigned char *plaintext,
    thread unsigned char *output,
    thread unsigned char *challenge,
    threadgroup uint32_t *l_SB1, threadgroup uint32_t *l_SB2,
    threadgroup uint32_t *l_SB3, threadgroup uint32_t *l_SB4,
    threadgroup uint32_t *l_SB5, threadgroup uint32_t *l_SB6,
    threadgroup uint32_t *l_SB7, threadgroup uint32_t *l_SB8)
{
  uint32_t X, Y, T;

  plaintext[7] = '\0';

  des_ecb_setkey_56(SK, plaintext);

  /* DES state after initial permutation, derived from runtime challenge */
  GET_UINT32_BE(X, challenge, 0);
  GET_UINT32_BE(Y, challenge, 4);
  DES_IP(X, Y);

  thread uint32_t *SK_ptr = SK;

  for (int _round = 0; _round < 8; _round++) {
    DES_ROUND_LOCAL(Y, X, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);
    DES_ROUND_LOCAL(X, Y, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);
  }

  DES_FP(Y, X);

  PUT_UINT32_BE(Y, output, 0);
  PUT_UINT32_BE(X, output, 4);
}
