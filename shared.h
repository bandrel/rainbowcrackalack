/* Constants shared between host programs and OpenCL kernels. */

#ifndef _SHARED_H
#define _SHARED_H

#define HASH_UNDEFINED 0
#define HASH_LM 1
#define HASH_NTLM 2
#define HASH_MD5 3
#define HASH_SHA1 4
#define HASH_NETNTLMV1 9

/* Canonical NetNTLMv1 server challenge.  A challenge equal to this is treated
 * as "default" and produces backward-compatible filenames (plain "byte"). */
#define NETNTLMV1_CHALLENGE_LEN 8
extern const unsigned char NETNTLMV1_DEFAULT_CHALLENGE[NETNTLMV1_CHALLENGE_LEN];

#define MAX_PLAINTEXT_LEN 16
#define MAX_HASH_OUTPUT_LEN 16
#define MAX_CHARSET_LEN 256

#define DEBUG_LEN 32

/* Converts a table index to a reduction offset.
 * reduction_offset is unsigned int (32-bit).  table_index must be <= 65535;
 * values >= 65536 cause silent 32-bit unsigned overflow. */
#define TABLE_INDEX_TO_REDUCTION_OFFSET(_table_index) ((_table_index) * 65536u)

#endif
