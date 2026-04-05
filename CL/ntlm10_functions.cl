#include "ntlm.cl"

__constant char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";


inline void index_to_plaintext_ntlm10(unsigned long index, unsigned char *plaintext) {
  /* Split 10 chars into hi4 (95^4) and lo6 (95^6). */
  unsigned int hi4 = (unsigned int)(index / 735091890625UL);
  unsigned long lo6 = index - 735091890625UL * hi4;

  /* hi4 -> 2+2 */
  unsigned short hi2 = (unsigned short)(hi4 / 9025);
  unsigned short lo2 = (unsigned short)(hi4 - (unsigned int)9025 * hi2);

  unsigned char tmp;
  tmp = (unsigned char)(hi2 / 95);
  plaintext[0] = tmp + 32;
  plaintext[1] = (unsigned char)(hi2 - (unsigned short)95 * tmp) + 32;

  tmp = (unsigned char)(lo2 / 95);
  plaintext[2] = tmp + 32;
  plaintext[3] = (unsigned char)(lo2 - (unsigned short)95 * tmp) + 32;

  /* lo6 -> 3+3 */
  unsigned int hi3 = (unsigned int)(lo6 / 857375);
  unsigned int lo3 = (unsigned int)(lo6 - 857375UL * hi3);

  /* hi3 -> 1+2 */
  unsigned short hi3_lo2 = (unsigned short)(hi3 % 9025);
  tmp = (unsigned char)(hi3 / 9025);
  plaintext[4] = tmp + 32;

  tmp = (unsigned char)(hi3_lo2 / 95);
  plaintext[5] = tmp + 32;
  plaintext[6] = (unsigned char)(hi3_lo2 - (unsigned short)95 * tmp) + 32;

  /* lo3 -> 1+2 */
  unsigned short lo3_lo2 = (unsigned short)(lo3 % 9025);
  tmp = (unsigned char)(lo3 / 9025);
  plaintext[7] = tmp + 32;

  tmp = (unsigned char)(lo3_lo2 / 95);
  plaintext[8] = tmp + 32;
  plaintext[9] = (unsigned char)(lo3_lo2 - (unsigned short)95 * tmp) + 32;
}


inline unsigned long hash_ntlm10(unsigned char *plaintext) {
  unsigned int key[16] = {0};
  unsigned int output[4];

  for (int i = 0; i < 5; i++)
    key[i] = plaintext[i * 2] | (plaintext[(i * 2) + 1] << 16);

  key[5] = 0x80;
  key[14] = 0xa0;

  md4_encrypt(output, key);

  return ((unsigned long)output[1]) << 32 | (unsigned long)output[0];
}


inline unsigned long hash_to_index_ntlm10(unsigned long hash, unsigned int pos) {
  /* 95^10 > 2^64: the 64-bit hash output is always in range, no modulo needed. */
  return hash + pos;
}


inline unsigned long hash_char_to_index_ntlm10(__global unsigned char *hash_value, unsigned int pos) {
  unsigned long ret = hash_value[7];
  ret <<= 8;
  ret |= hash_value[6];
  ret <<= 8;
  ret |= hash_value[5];
  ret <<= 8;
  ret |= hash_value[4];
  ret <<= 8;
  ret |= hash_value[3];
  ret <<= 8;
  ret |= hash_value[2];
  ret <<= 8;
  ret |= hash_value[1];
  ret <<= 8;
  ret |= hash_value[0];

  return hash_to_index_ntlm10(ret, pos);
}
