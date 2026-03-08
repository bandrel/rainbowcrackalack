#include "md5.metal"

constant char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";


inline void index_to_plaintext_md5_9(ulong index, thread unsigned char *plaintext) {
  for (int i = 8; i >= 0; i--) {
    plaintext[i] = (unsigned char)(index % 95u) + 32u;
    index /= 95u;
  }
}


/* MD5 of exactly 9 ASCII bytes with inlined padding.
 * Block layout: W[0..1] = bytes 0-7, W[2] = byte8 | (0x80 << 8), W[3..13] = 0,
 * W[14] = 72 (bit length), W[15] = 0.
 * Returns first 8 bytes of hash as little-endian uint64. */
inline ulong hash_md5_9(thread unsigned char *plaintext) {
  uint W[16] = {0};
  uint state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

  W[0] = (uint)plaintext[0] | ((uint)plaintext[1] << 8)
       | ((uint)plaintext[2] << 16) | ((uint)plaintext[3] << 24);
  W[1] = (uint)plaintext[4] | ((uint)plaintext[5] << 8)
       | ((uint)plaintext[6] << 16) | ((uint)plaintext[7] << 24);
  W[2] = (uint)plaintext[8] | (0x80u << 8);  /* p[8] then 0x80 pad at byte 9 */
  W[14] = 72u;  /* 9 bytes * 8 bits */

  md5_compress(state, W);

  return (ulong)state[0] | ((ulong)state[1] << 32);
}


inline ulong hash_to_index_md5_9(ulong hash, unsigned int pos) {
  return (hash + pos) % 630249409724609375UL;  /* 95^9 */
}
