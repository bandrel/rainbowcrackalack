#include "netntlmv1.cl"


/* For the byte charset (256 values, 0x00-0xFF), each byte of the index IS a
 * plaintext byte.  No division needed -- just shift and mask. */
inline void index_to_plaintext_netntlmv1_7(unsigned long index, unsigned char *plaintext) {
  plaintext[6] = (unsigned char)(index & 0xFF);
  plaintext[5] = (unsigned char)((index >> 8) & 0xFF);
  plaintext[4] = (unsigned char)((index >> 16) & 0xFF);
  plaintext[3] = (unsigned char)((index >> 24) & 0xFF);
  plaintext[2] = (unsigned char)((index >> 32) & 0xFF);
  plaintext[1] = (unsigned char)((index >> 40) & 0xFF);
  plaintext[0] = (unsigned char)((index >> 48) & 0xFF);
}


inline unsigned long hash_netntlmv1_7(unsigned char *plaintext) {
  uint32_t SK[32];
  unsigned char output[8];

  plaintext[7] = '\0';
  netntlmv1_hash(SK, plaintext, output);

  /* Pack in little-endian order to match the generic hash_to_index byte
   * assembly: ret = hash[7]<<56 | hash[6]<<48 | ... | hash[0]. */
  return ((unsigned long)output[7] << 56) |
         ((unsigned long)output[6] << 48) |
         ((unsigned long)output[5] << 40) |
         ((unsigned long)output[4] << 32) |
         ((unsigned long)output[3] << 24) |
         ((unsigned long)output[2] << 16) |
         ((unsigned long)output[1] << 8) |
         ((unsigned long)output[0]);
}


/* Plaintext space = 256^7 = 2^56, so modulo is a bitwise AND.
 * reduction_offset = table_index * 65536. */
inline unsigned long hash_to_index_netntlmv1_7(unsigned long hash, unsigned int reduction_offset, unsigned int pos) {
  return (hash + reduction_offset + pos) & 0x00FFFFFFFFFFFFFFUL;
}


inline unsigned long hash_char_to_index_netntlmv1_7(__global unsigned char *hash_value, unsigned int reduction_offset, unsigned int pos) {
  /* Little-endian assembly, matching the generic hash_to_index in rt.cl. */
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

  return (ret + reduction_offset + pos) & 0x00FFFFFFFFFFFFFFUL;
}
