#include "ntlm.cl"

__constant char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";


inline void index_to_plaintext_markov10(
    unsigned long index,
    __constant char *charset_str,
    unsigned int charset_len,
    __constant unsigned char *sorted_pos0,
    __global const unsigned char *sorted_bigram,
    unsigned char *plaintext)
{
    unsigned long cs2 = (unsigned long)charset_len * charset_len;

    /* Position 0: rank within the most-probable first characters. */
    unsigned int charset_idx = sorted_pos0[index % charset_len];
    plaintext[0] = charset_str[charset_idx];
    index /= charset_len;
    unsigned int prev_charset_idx = charset_idx;

    /* Position 1 (bigram table at position 0) */
    unsigned long offset = (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[1] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 2 (bigram table at position 1) */
    offset = cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[2] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 3 (bigram table at position 2) */
    offset = 2UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[3] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 4 (bigram table at position 3) */
    offset = 3UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[4] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 5 (bigram table at position 4) */
    offset = 4UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[5] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 6 (bigram table at position 5) */
    offset = 5UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[6] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 7 (bigram table at position 6) */
    offset = 6UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[7] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 8 (bigram table at position 7) */
    offset = 7UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[8] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 9 (bigram table at position 8) */
    offset = 8UL * cs2 + (unsigned long)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[9] = charset_str[charset_idx];
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


inline unsigned long hash_to_index_markov10(unsigned long hash, unsigned int reduction_offset, unsigned long pspace_total, unsigned int pos) {
  hash += reduction_offset + pos;

  /* Markov keyspace is always < 2^64, so modulo is needed. */
  if (hash >= pspace_total)
    hash %= pspace_total;

  return hash;
}


inline unsigned long hash_char_to_index_markov10(__global unsigned char *hash_value, unsigned int reduction_offset, unsigned long pspace_total, unsigned int pos) {
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

  return hash_to_index_markov10(ret, reduction_offset, pspace_total, pos);
}
