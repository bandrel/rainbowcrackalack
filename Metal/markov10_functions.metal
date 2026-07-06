#include "ntlm.metal"

constant char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";


inline void index_to_plaintext_markov10(
    ulong index,
    constant char *charset_str,
    unsigned int charset_len,
    constant unsigned char *sorted_pos0,
    device const unsigned char *sorted_bigram,
    thread unsigned char *plaintext)
{
    ulong cs2 = (ulong)charset_len * charset_len;

    unsigned int charset_idx = sorted_pos0[index % charset_len];
    plaintext[0] = charset_str[charset_idx];
    index /= charset_len;
    unsigned int prev_charset_idx = charset_idx;

    ulong offset = (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[1] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[2] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 2UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[3] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 3UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[4] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 4UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[5] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 5UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[6] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 6UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[7] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 7UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[8] = charset_str[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    offset = 8UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[9] = charset_str[charset_idx];
}


inline ulong hash_ntlm10(thread unsigned char *plaintext) {
  unsigned int key[16] = {0};
  unsigned int output[4];

  for (int i = 0; i < 5; i++)
    key[i] = plaintext[i * 2] | (plaintext[(i * 2) + 1] << 16);

  key[5] = 0x80;
  key[14] = 0xa0;

  md4_encrypt(output, key);

  return ((ulong)output[1]) << 32 | (ulong)output[0];
}


inline ulong hash_to_index_markov10(ulong hash, unsigned int reduction_offset, ulong pspace_total, unsigned int pos) {
  hash += reduction_offset + pos;

  if (hash >= pspace_total)
    hash %= pspace_total;

  return hash;
}


inline ulong hash_char_to_index_markov10(device unsigned char *hash_value, unsigned int reduction_offset, ulong pspace_total, unsigned int pos) {
  ulong ret = hash_value[7];
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
