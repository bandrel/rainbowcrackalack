/*
 * MD5 implementation for OpenCL kernels.
 * Based on RFC 1321: https://www.ietf.org/rfc/rfc1321.txt
 * Uses MD5_ prefixed macros to avoid conflicts with ntlm.cl MD4 macros.
 */

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define MD5_STEP(f, a, b, c, d, x, t, s) \
  (a) += f((b), (c), (d)) + (x) + (uint)(t); \
  (a) = rotate((uint)(a), (uint)(s));         \
  (a) += (b);

inline void md5_compress(__private uint *state, __private uint *W) {
  uint a = state[0], b = state[1], c = state[2], d = state[3];

  /* Round 1 - F function, sequential word access */
  MD5_STEP(MD5_F, a,b,c,d, W[ 0], 0xd76aa478,  7)
  MD5_STEP(MD5_F, d,a,b,c, W[ 1], 0xe8c7b756, 12)
  MD5_STEP(MD5_F, c,d,a,b, W[ 2], 0x242070db, 17)
  MD5_STEP(MD5_F, b,c,d,a, W[ 3], 0xc1bdceee, 22)
  MD5_STEP(MD5_F, a,b,c,d, W[ 4], 0xf57c0faf,  7)
  MD5_STEP(MD5_F, d,a,b,c, W[ 5], 0x4787c62a, 12)
  MD5_STEP(MD5_F, c,d,a,b, W[ 6], 0xa8304613, 17)
  MD5_STEP(MD5_F, b,c,d,a, W[ 7], 0xfd469501, 22)
  MD5_STEP(MD5_F, a,b,c,d, W[ 8], 0x698098d8,  7)
  MD5_STEP(MD5_F, d,a,b,c, W[ 9], 0x8b44f7af, 12)
  MD5_STEP(MD5_F, c,d,a,b, W[10], 0xffff5bb1, 17)
  MD5_STEP(MD5_F, b,c,d,a, W[11], 0x895cd7be, 22)
  MD5_STEP(MD5_F, a,b,c,d, W[12], 0x6b901122,  7)
  MD5_STEP(MD5_F, d,a,b,c, W[13], 0xfd987193, 12)
  MD5_STEP(MD5_F, c,d,a,b, W[14], 0xa679438e, 17)
  MD5_STEP(MD5_F, b,c,d,a, W[15], 0x49b40821, 22)

  /* Round 2 - G function, word access pattern: (1+5i) mod 16 */
  MD5_STEP(MD5_G, a,b,c,d, W[ 1], 0xf61e2562,  5)
  MD5_STEP(MD5_G, d,a,b,c, W[ 6], 0xc040b340,  9)
  MD5_STEP(MD5_G, c,d,a,b, W[11], 0x265e5a51, 14)
  MD5_STEP(MD5_G, b,c,d,a, W[ 0], 0xe9b6c7aa, 20)
  MD5_STEP(MD5_G, a,b,c,d, W[ 5], 0xd62f105d,  5)
  MD5_STEP(MD5_G, d,a,b,c, W[10], 0x02441453,  9)
  MD5_STEP(MD5_G, c,d,a,b, W[15], 0xd8a1e681, 14)
  MD5_STEP(MD5_G, b,c,d,a, W[ 4], 0xe7d3fbc8, 20)
  MD5_STEP(MD5_G, a,b,c,d, W[ 9], 0x21e1cde6,  5)
  MD5_STEP(MD5_G, d,a,b,c, W[14], 0xc33707d6,  9)
  MD5_STEP(MD5_G, c,d,a,b, W[ 3], 0xf4d50d87, 14)
  MD5_STEP(MD5_G, b,c,d,a, W[ 8], 0x455a14ed, 20)
  MD5_STEP(MD5_G, a,b,c,d, W[13], 0xa9e3e905,  5)
  MD5_STEP(MD5_G, d,a,b,c, W[ 2], 0xfcefa3f8,  9)
  MD5_STEP(MD5_G, c,d,a,b, W[ 7], 0x676f02d9, 14)
  MD5_STEP(MD5_G, b,c,d,a, W[12], 0x8d2a4c8a, 20)

  /* Round 3 - H function, word access pattern: (5+3i) mod 16 */
  MD5_STEP(MD5_H, a,b,c,d, W[ 5], 0xfffa3942,  4)
  MD5_STEP(MD5_H, d,a,b,c, W[ 8], 0x8771f681, 11)
  MD5_STEP(MD5_H, c,d,a,b, W[11], 0x6d9d6122, 16)
  MD5_STEP(MD5_H, b,c,d,a, W[14], 0xfde5380c, 23)
  MD5_STEP(MD5_H, a,b,c,d, W[ 1], 0xa4beea44,  4)
  MD5_STEP(MD5_H, d,a,b,c, W[ 4], 0x4bdecfa9, 11)
  MD5_STEP(MD5_H, c,d,a,b, W[ 7], 0xf6bb4b60, 16)
  MD5_STEP(MD5_H, b,c,d,a, W[10], 0xbebfbc70, 23)
  MD5_STEP(MD5_H, a,b,c,d, W[13], 0x289b7ec6,  4)
  MD5_STEP(MD5_H, d,a,b,c, W[ 0], 0xeaa127fa, 11)
  MD5_STEP(MD5_H, c,d,a,b, W[ 3], 0xd4ef3085, 16)
  MD5_STEP(MD5_H, b,c,d,a, W[ 6], 0x04881d05, 23)
  MD5_STEP(MD5_H, a,b,c,d, W[ 9], 0xd9d4d039,  4)
  MD5_STEP(MD5_H, d,a,b,c, W[12], 0xe6db99e5, 11)
  MD5_STEP(MD5_H, c,d,a,b, W[15], 0x1fa27cf8, 16)
  MD5_STEP(MD5_H, b,c,d,a, W[ 2], 0xc4ac5665, 23)

  /* Round 4 - I function, word access pattern: (7i) mod 16 */
  MD5_STEP(MD5_I, a,b,c,d, W[ 0], 0xf4292244,  6)
  MD5_STEP(MD5_I, d,a,b,c, W[ 7], 0x432aff97, 10)
  MD5_STEP(MD5_I, c,d,a,b, W[14], 0xab9423a7, 15)
  MD5_STEP(MD5_I, b,c,d,a, W[ 5], 0xfc93a039, 21)
  MD5_STEP(MD5_I, a,b,c,d, W[12], 0x655b59c3,  6)
  MD5_STEP(MD5_I, d,a,b,c, W[ 3], 0x8f0ccc92, 10)
  MD5_STEP(MD5_I, c,d,a,b, W[10], 0xffeff47d, 15)
  MD5_STEP(MD5_I, b,c,d,a, W[ 1], 0x85845dd1, 21)
  MD5_STEP(MD5_I, a,b,c,d, W[ 8], 0x6fa87e4f,  6)
  MD5_STEP(MD5_I, d,a,b,c, W[15], 0xfe2ce6e0, 10)
  MD5_STEP(MD5_I, c,d,a,b, W[ 6], 0xa3014314, 15)
  MD5_STEP(MD5_I, b,c,d,a, W[13], 0x4e0811a1, 21)
  MD5_STEP(MD5_I, a,b,c,d, W[ 4], 0xf7537e82,  6)
  MD5_STEP(MD5_I, d,a,b,c, W[11], 0xbd3af235, 10)
  MD5_STEP(MD5_I, c,d,a,b, W[ 2], 0x2ad7d2bb, 15)
  MD5_STEP(MD5_I, b,c,d,a, W[ 9], 0xeb86d391, 21)

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}


/* Generic MD5: handles any plaintext_len up to 55 bytes (fits in one block).
 * MAX_PLAINTEXT_LEN is 16, so this is always sufficient. */
inline void md5_hash(unsigned char *plaintext, unsigned int plaintext_len,
                     unsigned char *hash) {
  uint W[16] = {0};
  uint state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

  /* Pack plaintext as little-endian 32-bit words */
  for (unsigned int i = 0; i < plaintext_len; i++)
    W[i / 4] |= (uint)plaintext[i] << ((i & 3u) * 8u);

  /* Append 0x80 padding byte immediately after message */
  W[plaintext_len / 4] |= 0x80u << ((plaintext_len & 3u) * 8u);

  /* Bit-length field at word 14 (always fits: plaintext_len <= 16, bits <= 128) */
  W[14] = plaintext_len * 8u;
  /* W[15] = 0 (high 32 bits of length; always 0 here) */

  md5_compress(state, W);

  /* Write 16-byte output as little-endian bytes */
  for (int i = 0; i < 4; i++) {
    hash[i * 4 + 0] = (state[i] >>  0) & 0xffu;
    hash[i * 4 + 1] = (state[i] >>  8) & 0xffu;
    hash[i * 4 + 2] = (state[i] >> 16) & 0xffu;
    hash[i * 4 + 3] = (state[i] >> 24) & 0xffu;
  }
}
