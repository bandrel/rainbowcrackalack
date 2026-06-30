#include <stdio.h>
#include <string.h>
#include "shared.h"
#include "misc.h"

int test_challenge_host(void) {
  unsigned char out[8];
  int fails = 0;

  if (parse_challenge_str("1122334455667788", out) != 0) { printf("FAIL parse valid rc\n"); fails++; }
  unsigned char want[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
  if (memcmp(out, want, 8) != 0) { printf("FAIL parse value\n"); fails++; }

  if (parse_challenge_str("1122", out) == 0) { printf("FAIL short accepted\n"); fails++; }
  if (parse_challenge_str("zz22334455667788", out) == 0) { printf("FAIL nonhex accepted\n"); fails++; }

  char hex[17];
  format_challenge_hex(want, hex);
  if (strcmp(hex, "1122334455667788") != 0) { printf("FAIL format '%s'\n", hex); fails++; }

  if (!challenge_is_default(want)) { printf("FAIL default not detected\n"); fails++; }
  unsigned char other[8] = {0,1,2,3,4,5,6,7};
  if (challenge_is_default(other)) { printf("FAIL non-default flagged default\n"); fails++; }

  {
    rt_parameters p; unsigned char dft[8];
    memcpy(dft, NETNTLMV1_DEFAULT_CHALLENGE, 8);

    parse_rt_params(&p, "netntlmv1_byte#7-7_0_881689x29000_0.rt");
    if (!p.parsed) { printf("FAIL parse plain\n"); fails++; }
    if (strcmp(p.charset_name, "byte") != 0) { printf("FAIL charset plain '%s'\n", p.charset_name); fails++; }
    if (memcmp(p.challenge, dft, 8) != 0) { printf("FAIL default challenge\n"); fails++; }

    parse_rt_params(&p, "netntlmv1_byte-chalaabbccddeeff0011#7-7_0_881689x29000_0.rt");
    if (!p.parsed) { printf("FAIL parse chal\n"); fails++; }
    if (strcmp(p.charset_name, "byte") != 0) { printf("FAIL charset chal '%s'\n", p.charset_name); fails++; }
    unsigned char wantc[8] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11};
    if (memcmp(p.challenge, wantc, 8) != 0) { printf("FAIL chal value\n"); fails++; }
  }

  if (fails == 0) printf("ALL PASS\n");
  return fails ? 0 : 1;
}
