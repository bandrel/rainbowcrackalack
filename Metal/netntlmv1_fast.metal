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

/* ---- On-the-fly DES key schedule ----
 *
 * DES_PC2 emits one round's two subkey words directly from the rotated 28-bit
 * key halves c (left) and d (right).  The bit layout is identical to the two
 * "*SK++ =" assignments in des_ecb_setkey (netntlmv1.metal); deriving each
 * round's subkey on the fly means the full 32-word SK[] schedule is never
 * materialized, avoiding the per-thread spill a live SK[32] array causes. */
#define DES_PC2(c, d, sk0, sk1) \
    do { \
        (sk0) = (((c) <<  4) & 0x24000000) | (((c) << 28) & 0x10000000) \
              | (((c) << 14) & 0x08000000) | (((c) << 18) & 0x02080000) \
              | (((c) <<  6) & 0x01000000) | (((c) <<  9) & 0x00200000) \
              | (((c) >>  1) & 0x00100000) | (((c) << 10) & 0x00040000) \
              | (((c) <<  2) & 0x00020000) | (((c) >> 10) & 0x00010000) \
              | (((d) >> 13) & 0x00002000) | (((d) >>  4) & 0x00001000) \
              | (((d) <<  6) & 0x00000800) | (((d) >>  1) & 0x00000400) \
              | (((d) >> 14) & 0x00000200) | (((d)      ) & 0x00000100) \
              | (((d) >>  5) & 0x00000020) | (((d) >> 10) & 0x00000010) \
              | (((d) >>  3) & 0x00000008) | (((d) >> 18) & 0x00000004) \
              | (((d) >> 26) & 0x00000002) | (((d) >> 24) & 0x00000001); \
        (sk1) = (((c) << 15) & 0x20000000) | (((c) << 17) & 0x10000000) \
              | (((c) << 10) & 0x08000000) | (((c) << 22) & 0x04000000) \
              | (((c) >>  2) & 0x02000000) | (((c) <<  1) & 0x01000000) \
              | (((c) << 16) & 0x00200000) | (((c) << 11) & 0x00100000) \
              | (((c) <<  3) & 0x00080000) | (((c) >>  6) & 0x00040000) \
              | (((c) << 15) & 0x00020000) | (((c) >>  4) & 0x00010000) \
              | (((d) >>  2) & 0x00002000) | (((d) <<  8) & 0x00001000) \
              | (((d) >> 14) & 0x00000808) | (((d) >>  9) & 0x00000400) \
              | (((d)      ) & 0x00000200) | (((d) <<  7) & 0x00000100) \
              | (((d) >>  7) & 0x00000020) | (((d) >>  3) & 0x00000011) \
              | (((d) <<  2) & 0x00000004) | (((d) >> 21) & 0x00000002); \
    } while(0)

/* One DES Feistel round using explicit subkey words sk0/sk1 instead of
 * DES_ROUND_LOCAL's *SK_ptr++.  Otherwise identical to DES_ROUND_LOCAL. */
#define DES_ROUND_SK(X, Y, sk0, sk1, sb1, sb2, sb3, sb4, sb5, sb6, sb7, sb8) \
    do { \
        T = (sk0) ^ (X); \
        (Y) ^= (sb8)[ (T      ) & 0x3F ] ^ \
               (sb6)[ (T >>  8) & 0x3F ] ^ \
               (sb4)[ (T >> 16) & 0x3F ] ^ \
               (sb2)[ (T >> 24) & 0x3F ]; \
        T = (sk1) ^ (((X) << 28) | ((X) >> 4)); \
        (Y) ^= (sb7)[ (T      ) & 0x3F ] ^ \
               (sb5)[ (T >>  8) & 0x3F ] ^ \
               (sb3)[ (T >> 16) & 0x3F ] ^ \
               (sb1)[ (T >> 24) & 0x3F ]; \
    } while(0)

/* ---- Challenge initial-permutation precompute ----
 *
 * The DES initial permutation of the server challenge is identical for every
 * chain step and every thread, so chain loops compute it once and reuse it. */
inline void netntlmv1_challenge_to_ip(
    thread unsigned char *challenge, thread uint32_t *ip_x, thread uint32_t *ip_y)
{
  uint32_t X, Y, T;
  GET_UINT32_BE(X, challenge, 0);
  GET_UINT32_BE(Y, challenge, 4);
  DES_IP(X, Y);
  *ip_x = X;
  *ip_y = Y;
}

/* ---- Fast hash using threadgroup S-boxes, pre-permuted challenge ----
 * cx/cy are the challenge after DES_IP (see netntlmv1_challenge_to_ip).  The
 * DES key schedule is fused on the fly, so the 32-word SK[] is never built. */
inline void netntlmv1_hash_fast_ip(
    thread uint32_t SK[32],
    thread unsigned char *plaintext,
    thread unsigned char *output,
    uint32_t cx, uint32_t cy,
    threadgroup uint32_t *l_SB1, threadgroup uint32_t *l_SB2,
    threadgroup uint32_t *l_SB3, threadgroup uint32_t *l_SB4,
    threadgroup uint32_t *l_SB5, threadgroup uint32_t *l_SB6,
    threadgroup uint32_t *l_SB7, threadgroup uint32_t *l_SB8)
{
  (void)SK;  /* Schedule is fused below; SK[] is never materialized. */
  uint32_t X = cx, Y = cy, T;

  plaintext[7] = '\0';

  /* DES key expansion: 7-byte plaintext -> 8-byte key (from des_ecb_setkey_56). */
  uchar key[DES_KEY_SIZE];
  key[0] = (                          ((plaintext[0] >> 1) & 0x7f)  << 1);
  key[1] = (((plaintext[0] & 0x01) << 6 | ((plaintext[1] >> 2) & 0x3f)) << 1);
  key[2] = (((plaintext[1] & 0x03) << 5 | ((plaintext[2] >> 3) & 0x1f)) << 1);
  key[3] = (((plaintext[2] & 0x07) << 4 | ((plaintext[3] >> 4) & 0x0f)) << 1);
  key[4] = (((plaintext[3] & 0x0f) << 3 | ((plaintext[4] >> 5) & 0x07)) << 1);
  key[5] = (((plaintext[4] & 0x1f) << 2 | ((plaintext[5] >> 6) & 0x03)) << 1);
  key[6] = (((plaintext[5] & 0x3f) << 1 | ((plaintext[6] >> 7) & 0x01)) << 1);
  key[7] =  ((plaintext[6] & 0x7f) << 1);

  /* PC-1: derive the two 28-bit key halves c (left) and d (right). */
  uint32_t c, d;
  GET_UINT32_BE(c, key, 0);
  GET_UINT32_BE(d, key, 4);
  T = ((d >> 4) ^ c) & 0x0F0F0F0F; c ^= T; d ^= (T << 4);
  T = ((d)      ^ c) & 0x10101010; c ^= T; d ^= (T);
  c = (LHs[(c)       & 0xF] << 3) | (LHs[(c >>  8) & 0xF] << 2)
    | (LHs[(c >> 16) & 0xF] << 1) | (LHs[(c >> 24) & 0xF])
    | (LHs[(c >>  5) & 0xF] << 7) | (LHs[(c >> 13) & 0xF] << 6)
    | (LHs[(c >> 21) & 0xF] << 5) | (LHs[(c >> 29) & 0xF] << 4);
  d = (RHs[(d >>  1) & 0xF] << 3) | (RHs[(d >>  9) & 0xF] << 2)
    | (RHs[(d >> 17) & 0xF] << 1) | (RHs[(d >> 25) & 0xF])
    | (RHs[(d >>  4) & 0xF] << 7) | (RHs[(d >> 12) & 0xF] << 6)
    | (RHs[(d >> 20) & 0xF] << 5) | (RHs[(d >> 28) & 0xF] << 4);
  c &= 0x0FFFFFFF;
  d &= 0x0FFFFFFF;

  /* 16 fused rounds: rotate the key halves, derive this round's subkey pair,
   * and apply the Feistel round immediately.  Rotation schedule and the
   * (Y,X)/(X,Y) alternation match des_ecb_setkey + the original 8x2 loop. */
  for (int r = 0; r < 16; r++) {
    if (r < 2 || r == 8 || r == 15) {
      c = ((c << 1) | (c >> 27)) & 0x0FFFFFFF;
      d = ((d << 1) | (d >> 27)) & 0x0FFFFFFF;
    } else {
      c = ((c << 2) | (c >> 26)) & 0x0FFFFFFF;
      d = ((d << 2) | (d >> 26)) & 0x0FFFFFFF;
    }

    uint32_t sk0, sk1;
    DES_PC2(c, d, sk0, sk1);

    if ((r & 1) == 0)
      DES_ROUND_SK(Y, X, sk0, sk1, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);
    else
      DES_ROUND_SK(X, Y, sk0, sk1, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8);
  }

  DES_FP(Y, X);

  PUT_UINT32_BE(Y, output, 0);
  PUT_UINT32_BE(X, output, 4);
}

/* ---- Fast hash using threadgroup S-boxes ----
 * Thin wrapper that derives the IP-permuted challenge per call. */
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
  uint32_t cx, cy;
  netntlmv1_challenge_to_ip(challenge, &cx, &cy);
  netntlmv1_hash_fast_ip(SK, plaintext, output, cx, cy,
                         l_SB1, l_SB2, l_SB3, l_SB4,
                         l_SB5, l_SB6, l_SB7, l_SB8);
}
