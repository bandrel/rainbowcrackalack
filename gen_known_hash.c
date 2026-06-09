/* gen_known_hash: walk a rainbow chain via NetNTLMv1 hash + reduction and print
 * the (plaintext, hash) at a chosen chain position.  The hash is guaranteed
 * crackable against any rainbow table that contains the chain. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Use OpenSSL DES so weak keys (e.g. \x01*8 from all-zero plaintext) are not
 * rejected.  gcrypt enforces the DES spec and refuses weak keys; the GPU
 * kernels do not check, so gcrypt would produce wrong hashes for those chains. */
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/des.h>
#include "cpu_rt_functions.h"
#include "shared.h"

extern void setup_des_key(char key_56[], unsigned char *key);

static char charset_byte[256];

/* Correct NetNTLMv1 hash: 7-byte plaintext → setup_des_key → 8-byte DES key →
 * DES-ECB encrypt magic. */
static void netntlmv1_hash_correct(unsigned char *plaintext, unsigned char *hash) {
  static const unsigned char magic[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  unsigned char des_key[8] = {0};
  setup_des_key((char *)plaintext, des_key);
  DES_key_schedule ks;
  DES_set_key_unchecked((const_DES_cblock *)des_key, &ks);
  DES_ecb_encrypt((const_DES_cblock *)magic, (DES_cblock *)hash, &ks, DES_ENCRYPT);
}

int main(int ac, char **av) {
  if (ac != 5) {
    fprintf(stderr, "Usage: %s chain_len reduction_offset start_index target_position\n", av[0]);
    return 1;
  }
  unsigned int chain_len = (unsigned int)strtoul(av[1], NULL, 10);
  unsigned int reduction_offset = (unsigned int)strtoul(av[2], NULL, 10);
  uint64_t start_index = strtoull(av[3], NULL, 10);
  unsigned int target_pos = (unsigned int)strtoul(av[4], NULL, 10);

  for (int i = 0; i < 256; i++) charset_byte[i] = (char)i;
  uint64_t plaintext_space_up_to_index[16] = {0};
  uint64_t plaintext_space_total =
      fill_plaintext_space_table(256, 7, 7, plaintext_space_up_to_index);

  uint64_t index = start_index;
  char plaintext[16] = {0};
  unsigned char hash[16] = {0};
  unsigned int plaintext_len = 7, hash_len = 8;

  for (unsigned int pos = 0; pos < chain_len - 1; pos++) {
    index_to_plaintext(index, charset_byte, 256, 7, 7,
                       plaintext_space_up_to_index, plaintext, &plaintext_len);
    netntlmv1_hash_correct((unsigned char *)plaintext, hash);
    if (pos == target_pos) {
      printf("hash=");
      for (unsigned int j = 0; j < hash_len; j++) printf("%02x", hash[j]);
      printf("\nplaintext=");
      for (unsigned int j = 0; j < plaintext_len; j++) printf("%02x", (unsigned char)plaintext[j]);
      printf("\npos=%u start_index=%" PRIu64 "\n", pos, start_index);
      return 0;
    }
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, pos);
  }
  fprintf(stderr, "Error: target_pos=%u >= chain_len-1=%u\n", target_pos, chain_len - 1);
  return 1;
}
