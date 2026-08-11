#ifndef _NETNTLMV1_CAPTURE_H
#define _NETNTLMV1_CAPTURE_H

#include <stddef.h>

typedef struct {
  char *user;
  char *domain;
  unsigned char lm_response[24];
  unsigned char nt_response[24];
  unsigned char server_challenge[8];
  int is_ess;
} netntlmv1_capture;

/* Parses "user::domain:LMresp:NTresp:challenge" (LMresp/NTresp = 48 hex
 * chars, challenge = 16 hex chars).  On success, fills *out (caller must
 * eventually call netntlmv1_free_capture()) and sets out->is_ess.  On
 * failure, writes a human-readable reason into errbuf and returns -1. */
int netntlmv1_parse_capture_line(const char *line, netntlmv1_capture *out, char *errbuf, size_t errbuf_len);

/* True if lm_response[8..23] are all zero -- the ESS/NTLM2-Session padding
 * pattern (the real 8-byte client challenge occupies lm_response[0..7]). */
int netntlmv1_capture_is_ess(const netntlmv1_capture *cap);

/* Classic NTLMv1 only: the effective challenge is the raw server challenge.
 * Callers must not call this for an ESS capture (cap->is_ess == 1) -- ESS's
 * effective challenge depends on a random per-session client challenge that
 * defeats precomputed tables regardless of how it's derived. */
void netntlmv1_effective_challenge(const netntlmv1_capture *cap, unsigned char out[8]);

void netntlmv1_split_nt_response(const unsigned char nt_response[24],
                                  unsigned char block1[8], unsigned char block2[8], unsigned char block3[8]);

/* Exhaustively searches all 65536 2-byte keys (padded to a 7-byte DES key
 * with 5 zero bytes) for the one whose DES encryption of `challenge` equals
 * block3_ct.  Returns 0 and fills out_2bytes on success.  This is a
 * complete keyspace, so a nonzero return indicates an internal bug (e.g. a
 * blocks/challenge mismatch), not a normal "not found" case. */
int netntlmv1_bruteforce_block3(const unsigned char block3_ct[8], const unsigned char challenge[8], unsigned char out_2bytes[2]);

/* key1/key2 are the two 7-byte DES keys recovered from the rainbow tables
 * (bytes 0-6 and 7-13 of the target NTLM hash); key3 is the 2 recovered
 * bytes (bytes 14-15).  out_ntlm receives the reassembled 16-byte hash. */
void netntlmv1_assemble_ntlm_hash(const unsigned char key1[7], const unsigned char key2[7],
                                   const unsigned char key3[2], unsigned char out_ntlm[16]);

void netntlmv1_free_capture(netntlmv1_capture *cap);

/* out must be at least inlen*2+1 bytes.  Lowercase hex, NUL-terminated. */
void netntlmv1_hex_encode(const unsigned char *in, size_t inlen, char *out);

/* Decodes exactly hexlen hex chars (hexlen must be even) into hexlen/2
 * bytes at out.  Returns 0 on success, -1 on odd length or invalid hex. */
int netntlmv1_hex_decode(const char *hex, size_t hexlen, unsigned char *out);

#endif /* _NETNTLMV1_CAPTURE_H */
