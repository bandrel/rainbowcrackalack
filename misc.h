#ifndef _MISC_H
#define _MISC_H

#include <inttypes.h>
#include <stdio.h>

/* The quote format specifier (which on UNIX prints numbers with commas in the thousanth's place, i.e.: %'u") can cause crashes in Windows. */
#ifdef _WIN32
#define QUOTE ""
#else
#define QUOTE "'"
#endif

/* This is the longest chain length that a single kernel invokation can produce.  Beyond
 * this, it must be split up into parts.  Set to 0 to auto-calibrate at runtime using a
 * probe dispatch (recommended for Metal/macOS where the GPU watchdog kills long kernels).
 * A non-zero value skips calibration and uses the fixed limit directly. */
#define MAX_CHAIN_LEN 0

#define CHAIN_SIZE (unsigned int)(sizeof(uint64_t) * 2)

#define FREE(_ptr) \
  { free(_ptr); _ptr = NULL; }

#define FCLOSE(_f) \
  { if (_f != NULL) { fclose(_f); _f = NULL; } }

#include "file_lock.h"

#ifdef _WIN32

#if __has_include(<versionhelpers.h>)
#include <versionhelpers.h>

#define PRINT_WIN7_LOOKUP_WARNING() \
  if (IsWindows7OrGreater() && !IsWindows8OrGreater()) { fprintf(stderr, "\n\n\n\t!! WARNING !!\n\n\nPerforming lookups on Windows 7 is known to be very unstable.  Crashes, screen flickering, and/or strange error messages may be observed.  If this happens, unfortunately, there is no solution.  However, a work-around would be to boot the machine into Linux, which does not show these problems.  Lookups on Windows 10 systems work without issue as well.\n\n\n\n"); fflush(stderr); }
#else /* Old MinGW (i.e.: on Ubuntu 16.04 and older) doesn't have versionhelpers.h.  So we will skip the if (IsWindows...()) checks and always print the warning. */
#define PRINT_WIN7_LOOKUP_WARNING() { fprintf(stderr, "\n\n\n\t!! WARNING !!\n\n\nPerforming lookups on Windows 7 is known to be very unstable.  Crashes, screen flickering, and/or strange error messages may be observed.  If this happens, unfortunately, there is no solution.  However, a work-around would be to boot the machine into Linux, which does not show these problems.  Lookups on Windows 10 systems work without issue as well.\n\n\n\n"); fflush(stderr); }
#endif

#define CHECK_MEMORY_SIZE() \
  /* Our code + the OpenCL library does NOT like to run on Windows systems with 4GB \
   * of RAM.  It tends to throw strange errors at strange times, so let's warn the \
   * user ahead of time... */ \
  if (get_total_memory() <= 4294967296) { /* Less than 4GB... */ \
    fprintf(stderr, "\n\n\n\t!! WARNING !!\n\n\nThis system has 4GB of RAM or less.  On Windows systems, this tends to result in strange errors from the OpenCL library.  While it is safe to continue anyway, this would be the prime suspect if any problems occur.  In that case, either run on a system with more memory, or boot this machine in Linux (which has been seen to be much more forgiving in low-memory conditions).\n\n\n\n"); \
    fflush(stderr); \
  }
#else
#define CHECK_MEMORY_SIZE() /* Do nothing: Linux systems don't seem to have memory issues */
#define PRINT_WIN7_LOOKUP_WARNING() /* Do nothing: Linux systems don't have lookup problems. */
#endif


/* Struct to track parameters for rainbow tables found in a target directory. */
struct _rt_parameters {
  char hash_name[16];
  unsigned int hash_type;
  char charset_name[64]; /* 64 bytes: accommodates mask strings up to 32 ?X tokens */
  unsigned int plaintext_len_min;
  unsigned int plaintext_len_max;
  unsigned int table_index;
  unsigned int reduction_offset;
  unsigned int chain_len;
  uint64_t num_chains;
  unsigned int table_part;

  uint64_t markov_keyspace; /* 0 = not Markov; >0 = truncated keyspace */
  unsigned char challenge[8]; /* NetNTLMv1 server challenge; default if absent. */
  unsigned int parsed; /* Set to 1 if parameters successfully parsed, otherwise 0. */
};
typedef struct _rt_parameters rt_parameters;


void delete_rt_log(char *rt_filename);
void filepath_join(char *filepath_result, unsigned int filepath_result_size, const char *path1, const char *path2);
int64_t get_file_size(FILE *f);
char *get_os_name();
uint64_t get_random(uint64_t max);
void get_rt_log_filename(char *log_filename, size_t log_filename_size, char *rt_filename);
uint64_t get_total_memory();
unsigned int hash_str_to_type(char *hash_str);
unsigned int is_ntlm8(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len);
unsigned int is_ntlm9(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len);
unsigned int is_netntlmv1_7(unsigned int hash_type, char *charset_name, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int chain_len);
unsigned int is_markov_ntlm8(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len, int use_markov);
unsigned int is_markov_ntlm9(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len, int use_markov);
unsigned int is_ntlm10(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max);
unsigned int is_markov_ntlm10(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, int use_markov);
unsigned int is_md5_8(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max);
unsigned int is_md5_9(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max);
/* Canonical NetNTLMv1 server challenge (defined in misc.c).  Host-only;
 * not safe to expose in shared.h which is included by GPU kernel sources.
 * Size must match NETNTLMV1_CHALLENGE_LEN (8) defined in shared.h. */
extern const unsigned char NETNTLMV1_DEFAULT_CHALLENGE[8];
int parse_challenge_str(const char *s, unsigned char out[8]);
void format_challenge_hex(const unsigned char in[8], char *buf);
int challenge_is_default(const unsigned char c[8]);
void build_precompute_cache_charset(char *out, size_t out_size,
                                    const char *charset_name,
                                    const unsigned char challenge[8]);
unsigned int parse_uint_arg(const char *s, const char *name);
uint64_t parse_uint64_arg(const char *s, const char *name);
void parse_rt_params(rt_parameters *rt_params, char *rt_filename);
unsigned int compute_batch_chunk_size(unsigned int num_hashes);
void *recalloc(void *ptr, size_t new_size, size_t old_size);
size_t rt_log(rc_file f, const char *fmt, ...);
int str_ends_with(const char *str, const char *suffix);
void str_to_lowercase(char *s);

/* Hash file format constants used by parse_hash_file_data(). */
#define HASH_FILE_FORMAT_PLAIN  1
#define HASH_FILE_FORMAT_PWDUMP 2

/* Parse hash-file CONTENTS (NUL-terminated, mutated by tokenization) into
 * newly-allocated hashes[]/usernames[] arrays.
 *
 * file_data       - NUL-terminated buffer of the hash file content.  Will be
 *                   mutated in place by strtok (caller must not use it after).
 *                   Embedded NULs are not expected in a hash file.
 * pot_contents    - NUL-terminated string of already-cracked entries.  Any
 *                   line/hash found via strstr is skipped and counted as
 *                   previously-cracked.  If NULL, treated as empty (no matches).
 * out_hashes      - On success, set to calloc'd array of strdup'd hashes.
 * out_usernames   - On success, set to calloc'd array of strdup'd usernames
 *                   (entries may be NULL for PLAIN format).
 * out_num_hashes  - Set to the count of hashes stored.
 * out_num_previously_cracked - Set to the count of skipped already-cracked hashes.
 * out_file_format - Set to HASH_FILE_FORMAT_PLAIN or HASH_FILE_FORMAT_PWDUMP.
 *
 * Returns 0 on success, non-zero on parse/format/alloc error.
 * On error, out_hashes and out_usernames may be partially allocated.
 * The function does NOT call exit(). */
int parse_hash_file_data(char *file_data,
                         const char *pot_contents,
                         char ***out_hashes,
                         char ***out_usernames,
                         unsigned int *out_num_hashes,
                         unsigned int *out_num_previously_cracked,
                         int *out_file_format);

#ifdef _WIN32
void windows_print_error(char *func_name);
#endif

#endif
