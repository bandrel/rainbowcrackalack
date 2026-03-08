#include "md5.metal"

constant char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";


/* Identical to index_to_plaintext_ntlm8 - same base-95 decomposition. */
inline void index_to_plaintext_md5_8(ulong index, thread unsigned char *plaintext) {
  unsigned int indexHi = (unsigned int)(index / 81450625u);   /* 95^4 */
  unsigned int indexLo = (unsigned int)(index - (ulong)81450625u * indexHi);

  unsigned short index0 = (unsigned short)(indexHi / 9025u);  /* 95^2 */
  unsigned short index1 = (unsigned short)(indexHi - (unsigned int)9025u * index0);
  unsigned short index2 = (unsigned short)(indexLo / 9025u);
  unsigned short index3 = (unsigned short)(indexLo - (unsigned int)9025u * index2);

  unsigned char tmp;
  tmp = (unsigned char)(index0 / 95u); plaintext[0] = tmp + 32u;
  plaintext[1] = (unsigned char)(index0 - (unsigned short)95u * tmp + 32u);
  tmp = (unsigned char)(index1 / 95u); plaintext[2] = tmp + 32u;
  plaintext[3] = (unsigned char)(index1 - (unsigned short)95u * tmp + 32u);
  tmp = (unsigned char)(index2 / 95u); plaintext[4] = tmp + 32u;
  plaintext[5] = (unsigned char)(index2 - (unsigned short)95u * tmp + 32u);
  tmp = (unsigned char)(index3 / 95u); plaintext[6] = tmp + 32u;
  plaintext[7] = (unsigned char)(index3 - (unsigned short)95u * tmp + 32u);
}


/* MD5 of exactly 8 ASCII bytes with inlined padding.
 * Block layout: W[0..1] = plaintext, W[2] = 0x80 (pad), W[3..13] = 0,
 * W[14] = 64 (bit length), W[15] = 0.
 * Returns first 8 bytes of hash as little-endian uint64. */
inline ulong hash_md5_8(thread unsigned char *plaintext) {
  uint W[16] = {0};
  uint state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

  W[0] = (uint)plaintext[0] | ((uint)plaintext[1] << 8)
       | ((uint)plaintext[2] << 16) | ((uint)plaintext[3] << 24);
  W[1] = (uint)plaintext[4] | ((uint)plaintext[5] << 8)
       | ((uint)plaintext[6] << 16) | ((uint)plaintext[7] << 24);
  W[2] = 0x00000080u;  /* 0x80 pad byte at position 8 */
  W[14] = 64u;         /* 8 bytes * 8 bits */

  md5_compress(state, W);

  return (ulong)state[0] | ((ulong)state[1] << 32);
}


inline ulong hash_to_index_md5_8(ulong hash, unsigned int pos) {
  return (hash + pos) % 6634204312890625UL;  /* 95^8 */
}


inline ulong hash_char_to_index_md5_8(device unsigned char *hash_value, unsigned int pos) {
  ulong ret = hash_value[7]; ret <<= 8;
  ret |= hash_value[6]; ret <<= 8;
  ret |= hash_value[5]; ret <<= 8;
  ret |= hash_value[4]; ret <<= 8;
  ret |= hash_value[3]; ret <<= 8;
  ret |= hash_value[2]; ret <<= 8;
  ret |= hash_value[1]; ret <<= 8;
  ret |= hash_value[0];
  return (ret + pos) % 6634204312890625UL;
}
