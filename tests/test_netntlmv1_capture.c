/*
 * Rainbow Crackalack: test_netntlmv1_capture.c
 * CPU-only tests for netntlmv1_capture.c.
 */

#include <stdio.h>
#include <string.h>

#include "cpu_rt_functions.h"
#include "netntlmv1_capture.h"
#include "test_netntlmv1_capture.h"

/* --- Group A: hex encode/decode --- */
static int group_a(void)
{
  int ok = 1;
  unsigned char bytes[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
  char hex[9] = {0};
  unsigned char decoded[4] = {0};

  netntlmv1_hex_encode(bytes, 4, hex);
  if (strcmp(hex, "deadbeef") != 0) { fprintf(stderr, "HEX-01 failed: got \"%s\"\n", hex); ok = 0; }

  if (netntlmv1_hex_decode("deadbeef", 8, decoded) != 0) { fprintf(stderr, "HEX-02 failed: decode returned error\n"); ok = 0; }
  if (memcmp(decoded, bytes, 4) != 0) { fprintf(stderr, "HEX-03 failed: round-trip mismatch\n"); ok = 0; }

  if (netntlmv1_hex_decode("abc", 3, decoded) == 0) { fprintf(stderr, "HEX-04 failed: odd length should be rejected\n"); ok = 0; }
  if (netntlmv1_hex_decode("zzzz", 4, decoded) == 0) { fprintf(stderr, "HEX-05 failed: invalid hex should be rejected\n"); ok = 0; }

  return ok;
}

/* --- Group B: capture line parsing --- */
static int group_b(void)
{
  int ok = 1;
  netntlmv1_capture cap;
  char errbuf[256];
  /* 48 zero hex chars = the ambiguous all-zero LM response case, resolved as
   * classic (non-ESS) per the tie-break heuristic; 48 'a' hex chars as an
   * arbitrary NT response; 16 '1' hex chars as the challenge. */
  const char *classic_line_all_zero =
    "alice::CORP:000000000000000000000000000000000000000000000000:"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:1111111111111111";

  if (netntlmv1_parse_capture_line(classic_line_all_zero, &cap, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "CAP-01 failed: %s\n", errbuf);
    ok = 0;
  } else {
    if (strcmp(cap.user, "alice") != 0) { fprintf(stderr, "CAP-02 failed: user=\"%s\"\n", cap.user); ok = 0; }
    if (strcmp(cap.domain, "CORP") != 0) { fprintf(stderr, "CAP-03 failed: domain=\"%s\"\n", cap.domain); ok = 0; }
    if (cap.nt_response[0] != 0xaa) { fprintf(stderr, "CAP-04 failed: nt_response[0]=%02x\n", cap.nt_response[0]); ok = 0; }
    if (cap.server_challenge[0] != 0x11) { fprintf(stderr, "CAP-05 failed: challenge[0]=%02x\n", cap.server_challenge[0]); ok = 0; }
    if (cap.is_ess != 0) { fprintf(stderr, "CAP-06 failed: expected classic (is_ess=0), got %d\n", cap.is_ess); ok = 0; }
    netntlmv1_free_capture(&cap);
  }

  /* Realistic classic NTLMv1: LM response with nonzero bytes across the
   * entire 24-byte field (not the ambiguous all-zero case). */
  const char *classic_line_realistic =
    "bob::CORP:aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbcccccccccccccccc:"
    "dddddddddddddddddddddddddddddddddddddddddddddddd:2222222222222222";

  if (netntlmv1_parse_capture_line(classic_line_realistic, &cap, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "CAP-09 failed: %s\n", errbuf);
    ok = 0;
  } else {
    if (strcmp(cap.user, "bob") != 0) { fprintf(stderr, "CAP-10 failed: user=\"%s\"\n", cap.user); ok = 0; }
    if (cap.lm_response[0] != 0xaa) { fprintf(stderr, "CAP-11 failed: lm_response[0]=%02x\n", cap.lm_response[0]); ok = 0; }
    if (cap.lm_response[8] != 0xbb) { fprintf(stderr, "CAP-12 failed: lm_response[8]=%02x\n", cap.lm_response[8]); ok = 0; }
    if (cap.lm_response[16] != 0xcc) { fprintf(stderr, "CAP-13 failed: lm_response[16]=%02x\n", cap.lm_response[16]); ok = 0; }
    if (cap.is_ess != 0) { fprintf(stderr, "CAP-14 failed: expected classic (is_ess=0), got %d\n", cap.is_ess); ok = 0; }
    netntlmv1_free_capture(&cap);
  }

  /* Malformed: wrong field count. */
  if (netntlmv1_parse_capture_line("alice::CORP:deadbeef", &cap, errbuf, sizeof(errbuf)) == 0) {
    fprintf(stderr, "CAP-07 failed: malformed line should have been rejected\n");
    ok = 0;
  }

  /* Malformed: LM response wrong length. */
  {
    const char *bad_line = "alice::CORP:deadbeef:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:1111111111111111";
    if (netntlmv1_parse_capture_line(bad_line, &cap, errbuf, sizeof(errbuf)) == 0) {
      fprintf(stderr, "CAP-08 failed: short LM response should have been rejected\n");
      ok = 0;
    }
  }

  return ok;
}

/* --- Group C: ESS detection --- */
static int group_c(void)
{
  int ok = 1;
  netntlmv1_capture cap;
  char errbuf[256];
  /* LM response = 8 bytes of client challenge (nonzero) + 16 zero bytes:
   * "aaaaaaaaaaaaaaaa" (16 hex = 8 bytes, nonzero) followed by 32 zero hex
   * chars (16 zero bytes) = the ESS padding pattern. */
  const char *ess_line =
    "bob::CORP:aaaaaaaaaaaaaaaa00000000000000000000000000000000:"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:2222222222222222";

  if (netntlmv1_parse_capture_line(ess_line, &cap, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "ESS-01 failed: %s\n", errbuf);
    ok = 0;
  } else {
    if (cap.is_ess != 1) { fprintf(stderr, "ESS-02 failed: expected is_ess=1, got %d\n", cap.is_ess); ok = 0; }
    if (!netntlmv1_capture_is_ess(&cap)) { fprintf(stderr, "ESS-03 failed: netntlmv1_capture_is_ess disagreed\n"); ok = 0; }
    netntlmv1_free_capture(&cap);
  }

  return ok;
}

/* --- Group D: block split --- */
static int group_d(void)
{
  int ok = 1;
  unsigned char nt_response[24];
  unsigned char block1[8], block2[8], block3[8];
  int i;

  for (i = 0; i < 24; i++)
    nt_response[i] = (unsigned char)i;

  netntlmv1_split_nt_response(nt_response, block1, block2, block3);

  for (i = 0; i < 8; i++) {
    if (block1[i] != (unsigned char)i)      { fprintf(stderr, "SPLIT-01 failed at %d\n", i); ok = 0; }
    if (block2[i] != (unsigned char)(i + 8)) { fprintf(stderr, "SPLIT-02 failed at %d\n", i); ok = 0; }
    if (block3[i] != (unsigned char)(i + 16)) { fprintf(stderr, "SPLIT-03 failed at %d\n", i); ok = 0; }
  }

  return ok;
}

/* --- Group E: hash assembly --- */
static int group_e(void)
{
  int ok = 1;
  unsigned char key1[7], key2[7], key3[2], out_ntlm[16];
  unsigned char expected[16];
  int i;

  for (i = 0; i < 7; i++) { key1[i] = (unsigned char)(0x10 + i); key2[i] = (unsigned char)(0x20 + i); }
  key3[0] = 0x30; key3[1] = 0x31;

  memcpy(expected, key1, 7);
  memcpy(expected + 7, key2, 7);
  memcpy(expected + 14, key3, 2);

  netntlmv1_assemble_ntlm_hash(key1, key2, key3, out_ntlm);

  if (memcmp(out_ntlm, expected, 16) != 0) { fprintf(stderr, "ASM-01 failed\n"); ok = 0; }

  return ok;
}

/* --- Group F: block3 brute force round-trip --- */
static int group_f(void)
{
  int ok = 1;
  unsigned char challenge[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
  char key_56[7] = { 0x12, 0x34, 0, 0, 0, 0, 0 };
  unsigned char des_key[8], ciphertext[8], recovered[2];

  set_netntlmv1_challenge(challenge);
  setup_des_key(key_56, des_key);
  netntlmv1_hash(des_key, 8, ciphertext);

  if (netntlmv1_bruteforce_block3(ciphertext, challenge, recovered) != 0) {
    fprintf(stderr, "BF3-01 failed: brute force returned error\n");
    ok = 0;
  } else {
    if (recovered[0] != 0x12 || recovered[1] != 0x34) {
      fprintf(stderr, "BF3-02 failed: got %02x%02x, expected 1234\n", recovered[0], recovered[1]);
      ok = 0;
    }
  }

  /* Reset the global challenge to a known default (all-zero) to avoid
   * leaving stale state for subsequent test groups. netntlmv1_bruteforce_block3
   * mutates the global challenge as a side effect. */
  {
    unsigned char default_challenge[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    set_netntlmv1_challenge(default_challenge);
  }

  return ok;
}

/* --- Group G: effective challenge --- */
static int group_g(void)
{
  int ok = 1;
  netntlmv1_capture cap;
  char errbuf[256];
  unsigned char out_challenge[8];
  const char *classic_line =
    "alice::CORP:000000000000000000000000000000000000000000000000:"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:1111111111111111";

  if (netntlmv1_parse_capture_line(classic_line, &cap, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "EFFCH-01 failed: %s\n", errbuf);
    ok = 0;
  } else {
    netntlmv1_effective_challenge(&cap, out_challenge);
    if (memcmp(out_challenge, cap.server_challenge, 8) != 0) {
      fprintf(stderr, "EFFCH-02 failed: effective challenge did not match server_challenge\n");
      ok = 0;
    }
    netntlmv1_free_capture(&cap);
  }

  return ok;
}

int test_netntlmv1_capture(void)
{
  int ok = 1;

  if (!group_a()) ok = 0;
  if (!group_b()) ok = 0;
  if (!group_c()) ok = 0;
  if (!group_d()) ok = 0;
  if (!group_e()) ok = 0;
  if (!group_f()) ok = 0;
  if (!group_g()) ok = 0;

  return ok;
}
