/*
 * Rainbow Crackalack: netntlmv1_capture.c
 *
 * Parses full NetNTLMv1 captures (user::domain:LMresp:NTresp:challenge),
 * detects ESS/NTLM2-Session, splits the NT response into its three DES
 * blocks, brute-forces the 2-byte block3 key, and reassembles a full
 * 16-byte NTLM hash from three recovered blocks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_rt_functions.h"
#include "netntlmv1_capture.h"

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int netntlmv1_hex_decode(const char *hex, size_t hexlen, unsigned char *out) {
  size_t i;

  if (hexlen % 2 != 0)
    return -1;

  for (i = 0; i < hexlen / 2; i++) {
    int hi = hexval(hex[i * 2]);
    int lo = hexval(hex[i * 2 + 1]);

    if (hi < 0 || lo < 0)
      return -1;

    out[i] = (unsigned char)((hi << 4) | lo);
  }

  return 0;
}

void netntlmv1_hex_encode(const unsigned char *in, size_t inlen, char *out) {
  static const char hexchars[] = "0123456789abcdef";
  size_t i;

  for (i = 0; i < inlen; i++) {
    out[i * 2]     = hexchars[(in[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hexchars[in[i] & 0x0F];
  }
  out[inlen * 2] = '\0';
}

int netntlmv1_capture_is_ess(const netntlmv1_capture *cap) {
  int i;

  for (i = 8; i < 24; i++) {
    if (cap->lm_response[i] != 0x00)
      return 0;
  }

  for (i = 0; i < 8; i++) {
    if (cap->lm_response[i] != 0x00)
      return 1;
  }

  return 0;
}

void netntlmv1_effective_challenge(const netntlmv1_capture *cap, unsigned char out[8]) {
  memcpy(out, cap->server_challenge, 8);
}

void netntlmv1_split_nt_response(const unsigned char nt_response[24],
                                  unsigned char block1[8], unsigned char block2[8], unsigned char block3[8]) {
  memcpy(block1, nt_response, 8);
  memcpy(block2, nt_response + 8, 8);
  memcpy(block3, nt_response + 16, 8);
}

int netntlmv1_bruteforce_block3(const unsigned char block3_ct[8], const unsigned char challenge[8], unsigned char out_2bytes[2]) {
  unsigned int hi, lo;

  set_netntlmv1_challenge(challenge);

  for (hi = 0; hi <= 0xFF; hi++) {
    for (lo = 0; lo <= 0xFF; lo++) {
      char key_56[7];
      unsigned char des_key[8], candidate_hash[8];

      key_56[0] = (char)hi;
      key_56[1] = (char)lo;
      key_56[2] = 0; key_56[3] = 0; key_56[4] = 0; key_56[5] = 0; key_56[6] = 0;

      setup_des_key(key_56, des_key);
      netntlmv1_hash(des_key, 8, candidate_hash);

      if (memcmp(candidate_hash, block3_ct, 8) == 0) {
        out_2bytes[0] = (unsigned char)hi;
        out_2bytes[1] = (unsigned char)lo;
        return 0;
      }
    }
  }

  return -1;
}

void netntlmv1_assemble_ntlm_hash(const unsigned char key1[7], const unsigned char key2[7],
                                   const unsigned char key3[2], unsigned char out_ntlm[16]) {
  memcpy(out_ntlm, key1, 7);
  memcpy(out_ntlm + 7, key2, 7);
  memcpy(out_ntlm + 14, key3, 2);
}

int netntlmv1_parse_capture_line(const char *line, netntlmv1_capture *out, char *errbuf, size_t errbuf_len) {
  char *line_copy = NULL;
  char *fields[6] = {0};
  size_t linelen;
  int colon_count = 0;
  int field_idx;
  char *field_start;
  size_t i;

  memset(out, 0, sizeof(*out));

  line_copy = strdup(line);
  if (line_copy == NULL) {
    snprintf(errbuf, errbuf_len, "out of memory");
    return -1;
  }

  linelen = strlen(line_copy);
  while (linelen > 0 && (line_copy[linelen - 1] == '\n' || line_copy[linelen - 1] == '\r'))
    line_copy[--linelen] = '\0';

  for (i = 0; i < linelen; i++) {
    if (line_copy[i] == ':')
      colon_count++;
  }

  if (colon_count != 5) {
    snprintf(errbuf, errbuf_len, "expected 6 colon-separated fields (user::domain:LM:NT:challenge), got %d colon(s)", colon_count);
    free(line_copy);
    return -1;
  }

  field_idx = 0;
  field_start = line_copy;
  for (i = 0; i <= linelen; i++) {
    if (i == linelen || line_copy[i] == ':') {
      line_copy[i] = '\0';
      fields[field_idx++] = field_start;
      field_start = line_copy + i + 1;
    }
  }

  if (strlen(fields[3]) != 48) {
    snprintf(errbuf, errbuf_len, "LM response must be 48 hex chars, got %zu", strlen(fields[3]));
    free(line_copy);
    return -1;
  }
  if (strlen(fields[4]) != 48) {
    snprintf(errbuf, errbuf_len, "NT response must be 48 hex chars, got %zu", strlen(fields[4]));
    free(line_copy);
    return -1;
  }
  if (strlen(fields[5]) != 16) {
    snprintf(errbuf, errbuf_len, "challenge must be 16 hex chars, got %zu", strlen(fields[5]));
    free(line_copy);
    return -1;
  }

  if (netntlmv1_hex_decode(fields[3], 48, out->lm_response) != 0) {
    snprintf(errbuf, errbuf_len, "LM response contains invalid hex");
    free(line_copy);
    return -1;
  }
  if (netntlmv1_hex_decode(fields[4], 48, out->nt_response) != 0) {
    snprintf(errbuf, errbuf_len, "NT response contains invalid hex");
    free(line_copy);
    return -1;
  }
  if (netntlmv1_hex_decode(fields[5], 16, out->server_challenge) != 0) {
    snprintf(errbuf, errbuf_len, "challenge contains invalid hex");
    free(line_copy);
    return -1;
  }

  out->user = strdup(fields[0]);
  out->domain = strdup(fields[2]);
  out->is_ess = netntlmv1_capture_is_ess(out);

  free(line_copy);
  return 0;
}

void netntlmv1_free_capture(netntlmv1_capture *cap) {
  if (cap == NULL)
    return;

  free(cap->user);
  free(cap->domain);
  cap->user = NULL;
  cap->domain = NULL;
}
