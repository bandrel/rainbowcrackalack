/* gen_known_hash: walk a rainbow chain via NetNTLMv1 hash + reduction and print
 * the (plaintext, hash) at a chosen chain position.  The hash is guaranteed
 * crackable against any rainbow table that contains the chain. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_rt_functions.h"
#include "shared.h"

static char charset_byte[256];

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
    netntlmv1_hash((unsigned char *)plaintext, plaintext_len, hash);
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
