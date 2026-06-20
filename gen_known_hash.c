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
#include "charset.h"
#include "shared.h"

extern void setup_des_key(char key_56[], unsigned char *key);

static char charset_byte[256];

/* Minimal inline hex parser: parse exactly 16 hex chars into 8 bytes.
 * Returns 0 on success, non-zero on malformed input.  Mirrors misc.c's
 * parse_challenge_str without pulling in the gpu_backend.h dependency chain. */
static int parse_challenge_str_local(const char *s, unsigned char out[8]) {
  if (s == NULL || strlen(s) != 16) return 1;
  for (int i = 0; i < 8; i++) {
    char hi_c = s[i * 2], lo_c = s[i * 2 + 1];
    int hi, lo;
    if      (hi_c >= '0' && hi_c <= '9') hi = hi_c - '0';
    else if (hi_c >= 'a' && hi_c <= 'f') hi = hi_c - 'a' + 10;
    else if (hi_c >= 'A' && hi_c <= 'F') hi = hi_c - 'A' + 10;
    else return 1;
    if      (lo_c >= '0' && lo_c <= '9') lo = lo_c - '0';
    else if (lo_c >= 'a' && lo_c <= 'f') lo = lo_c - 'a' + 10;
    else if (lo_c >= 'A' && lo_c <= 'F') lo = lo_c - 'A' + 10;
    else return 1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

/* Correct NetNTLMv1 hash: 7-byte plaintext → setup_des_key → 8-byte DES key →
 * DES-ECB encrypt challenge. */
static void netntlmv1_hash_correct(unsigned char *plaintext, unsigned char *hash,
                                   const unsigned char challenge[8]) {
  unsigned char des_key[8] = {0};
  setup_des_key((char *)plaintext, des_key);
  DES_key_schedule ks;
  DES_set_key_unchecked((const_DES_cblock *)des_key, &ks);
  DES_ecb_encrypt((const_DES_cblock *)challenge, (DES_cblock *)hash, &ks, DES_ENCRYPT);
}

int main(int ac, char **av) {
  /* Default challenge matches the hardcoded value used by the GPU kernels and
   * the rest of the toolchain when no --challenge flag is supplied. */
  static const unsigned char default_challenge[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  unsigned char challenge[8];
  memcpy(challenge, default_challenge, 8);

  enum { ALGO_NETNTLMV1, ALGO_NTLM } algo = ALGO_NETNTLMV1;
  const char *charset_name = "byte";
  unsigned int plaintext_len = 7;
  int charset_set = 0, plen_set = 0;

  const char *positional[4] = {NULL, NULL, NULL, NULL};
  int pos_count = 0;

  for (int i = 1; i < ac; i++) {
    if (strcmp(av[i], "--challenge") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --challenge requires a 16-hex argument\n", av[0]); return 1; }
      if (parse_challenge_str_local(av[i + 1], challenge) != 0) {
        fprintf(stderr, "%s: invalid challenge '%s' (need exactly 16 hex chars)\n", av[0], av[i + 1]);
        return 1;
      }
      i++;
    } else if (strcmp(av[i], "--algo") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --algo requires a value (ntlm|netntlmv1)\n", av[0]); return 1; }
      if (strcmp(av[i + 1], "ntlm") == 0) algo = ALGO_NTLM;
      else if (strcmp(av[i + 1], "netntlmv1") == 0) algo = ALGO_NETNTLMV1;
      else { fprintf(stderr, "%s: unknown --algo '%s'\n", av[0], av[i + 1]); return 1; }
      i++;
    } else if (strcmp(av[i], "--charset") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --charset requires a value\n", av[0]); return 1; }
      charset_name = av[++i];
      charset_set = 1;
    } else if (strcmp(av[i], "--plaintext-len") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --plaintext-len requires a value\n", av[0]); return 1; }
      plaintext_len = (unsigned int)strtoul(av[++i], NULL, 10);
      plen_set = 1;
    } else {
      if (pos_count < 4) positional[pos_count] = av[i];
      pos_count++;
    }
  }

  if (pos_count != 4) {
    fprintf(stderr,
            "Usage: %s chain_len reduction_offset start_index target_position\n"
            "          [--algo ntlm|netntlmv1] [--charset NAME] [--plaintext-len N] [--challenge <16hex>]\n",
            av[0]);
    return 1;
  }

  unsigned int chain_len        = (unsigned int)strtoul(positional[0], NULL, 10);
  unsigned int reduction_offset = (unsigned int)strtoul(positional[1], NULL, 10);
  uint64_t start_index          = strtoull(positional[2], NULL, 10);
  unsigned int target_pos       = (unsigned int)strtoul(positional[3], NULL, 10);

  if (algo == ALGO_NTLM && (plaintext_len == 0 || plaintext_len > MAX_PLAINTEXT_LEN - 1)) {
    fprintf(stderr, "%s: --plaintext-len must be 1..%u\n", av[0], MAX_PLAINTEXT_LEN - 1);
    return 1;
  }

  /* Select charset + hash length per algorithm. */
  for (int i = 0; i < 256; i++) charset_byte[i] = (char)i;
  char *charset;
  unsigned int charset_len, hash_len;
  if (algo == ALGO_NTLM) {
    charset = validate_charset((char *)charset_name);
    if (charset == NULL) { fprintf(stderr, "%s: invalid charset '%s'\n", av[0], charset_name); return 1; }
    charset_len = (unsigned int)strlen(charset);
    hash_len = 16;
  } else {
    if (charset_set || plen_set)
      fprintf(stderr, "%s: warning: --charset/--plaintext-len ignored for --algo netntlmv1 (forced to byte/7)\n", av[0]);
    charset = charset_byte;
    charset_len = 256;
    plaintext_len = 7;   /* NetNTLMv1 is fixed at 7 bytes. */
    hash_len = 8;
  }

  uint64_t plaintext_space_up_to_index[16] = {0};
  uint64_t plaintext_space_total =
      fill_plaintext_space_table(charset_len, plaintext_len, plaintext_len, plaintext_space_up_to_index);

  uint64_t index = start_index;
  char plaintext[16] = {0};
  unsigned char hash[16] = {0};
  unsigned int out_plaintext_len = plaintext_len;

  for (unsigned int pos = 0; pos < chain_len - 1; pos++) {
    index_to_plaintext(index, charset, charset_len, plaintext_len, plaintext_len,
                       plaintext_space_up_to_index, plaintext, &out_plaintext_len);
    if (algo == ALGO_NTLM)
      ntlm_hash(plaintext, out_plaintext_len, hash);
    else
      netntlmv1_hash_correct((unsigned char *)plaintext, hash, challenge);

    if (pos == target_pos) {
      printf("hash=");
      for (unsigned int j = 0; j < hash_len; j++) printf("%02x", hash[j]);
      printf("\nplaintext=");
      for (unsigned int j = 0; j < out_plaintext_len; j++) printf("%02x", (unsigned char)plaintext[j]);
      printf("\npos=%u start_index=%" PRIu64 "\n", pos, start_index);
      return 0;
    }
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, pos);
  }
  fprintf(stderr, "Error: target_pos=%u >= chain_len-1=%u\n", target_pos, chain_len - 1);
  return 1;
}
