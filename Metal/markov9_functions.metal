#include "ntlm.metal"

constant char charset[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

#define PLAINTEXT_SPACE_TOTAL 630249409724609375UL


inline void index_to_plaintext_markov9(
    ulong index,
    constant char *charset,
    unsigned int charset_len,
    constant unsigned char *sorted_pos0,
    device const unsigned char *sorted_bigram,
    thread unsigned char *plaintext)
{
    ulong cs2 = (ulong)charset_len * charset_len;

    /* Position 0: rank within the most-probable first characters. */
    unsigned int charset_idx = sorted_pos0[index % charset_len];
    plaintext[0] = charset[charset_idx];
    index /= charset_len;
    unsigned int prev_charset_idx = charset_idx;

    /* Position 1 (bigram table at position 0) */
    ulong offset = (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[1] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 2 (bigram table at position 1) */
    offset = cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[2] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 3 (bigram table at position 2) */
    offset = 2UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[3] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 4 (bigram table at position 3) */
    offset = 3UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[4] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 5 (bigram table at position 4) */
    offset = 4UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[5] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 6 (bigram table at position 5) */
    offset = 5UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[6] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 7 (bigram table at position 6) */
    offset = 6UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[7] = charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 8 (bigram table at position 7) */
    offset = 7UL * cs2 + (ulong)prev_charset_idx * charset_len;
    charset_idx = sorted_bigram[offset + (index % charset_len)];
    plaintext[8] = charset[charset_idx];
}


inline ulong hash_ntlm9(thread unsigned char *plaintext) {
  unsigned int key[16] = {0};
  unsigned int output[4];

  for (int i = 0; i < 4; i++)
    key[i] = plaintext[i * 2] | (plaintext[(i * 2) + 1] << 16);

  key[4] = plaintext[8] | 0x800000;
  key[14] = 0x90;

  md4_encrypt(output, key);

  return ((ulong)output[1]) << 32 | (ulong)output[0];
}


inline ulong hash_to_index_markov9(ulong hash, unsigned int pos) {
  unsigned int tmp;

  hash += pos;

  tmp = ((hash >> 58) * 29) >> 6;

  hash -= PLAINTEXT_SPACE_TOTAL * tmp;
  if (hash >= PLAINTEXT_SPACE_TOTAL) {
    hash -= PLAINTEXT_SPACE_TOTAL;
  }

  return hash;
}


inline ulong hash_char_to_index_markov9(device unsigned char *hash_value, unsigned int pos) {
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

  return hash_to_index_markov9(ret, pos);
}
