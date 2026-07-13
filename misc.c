/*
 * Rainbow Crackalack: misc.c
 * Copyright (C) 2018-2020  Joe Testa <jtesta@positronsecurity.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#include <windows.h>
/*#include <versionhelpers.h>*/
#define STATUS_SUCCESS 0
#elif defined(__APPLE__)
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#else
#include <string.h>
#include <sys/sysinfo.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpu_backend.h"

#include "charset.h"
#include "mask_parse.h"
#include "misc.h"
#include "shared.h"


const unsigned char NETNTLMV1_DEFAULT_CHALLENGE[NETNTLMV1_CHALLENGE_LEN] =
    {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* Parse exactly 16 hex chars into 8 bytes (MSB first).  Returns 0 on success,
 * non-zero on malformed input. */
int parse_challenge_str(const char *s, unsigned char out[8]) {
  if (s == NULL || strlen(s) != 16) return 1;
  for (int i = 0; i < 8; i++) {
    int hi = hexval(s[i * 2]), lo = hexval(s[i * 2 + 1]);
    if (hi < 0 || lo < 0) return 1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

/* Format 8 bytes as 16 lowercase hex chars plus NUL (buf must be >= 17). */
void format_challenge_hex(const unsigned char in[8], char *buf) {
  static const char *h = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    buf[i * 2]     = h[(in[i] >> 4) & 0xF];
    buf[i * 2 + 1] = h[in[i] & 0xF];
  }
  buf[16] = '\0';
}

int challenge_is_default(const unsigned char c[8]) {
  return memcmp(c, NETNTLMV1_DEFAULT_CHALLENGE, 8) == 0;
}

/* Composes the charset segment of a precompute cache key.  Default challenge
 * -> charset_name verbatim (interchange-compatible with blurbdust caches).
 * Non-default -> "charset_name-chal<16-hex>", mirroring the table-filename
 * convention so cross-challenge lookups never collide on the same key.
 * out_size must be at least strlen(charset_name) + 22 (the "-chal" prefix is
 * 5 chars + 16 hex + NUL); a smaller buffer truncates the key. */
void build_precompute_cache_charset(char *out, size_t out_size,
                                    const char *charset_name,
                                    const unsigned char challenge[8]) {
  if (challenge_is_default(challenge)) {
    snprintf(out, out_size, "%s", charset_name);
  } else {
    char hex[17] = {0};
    format_challenge_hex(challenge, hex);
    snprintf(out, out_size, "%s-chal%s", charset_name, hex);
  }
}


/* Given a rainbow table filename, delete its associated log, if any exists. */
void delete_rt_log(char *rt_filename) {
  char log_filename[256] = {0};

  get_rt_log_filename(log_filename, sizeof(log_filename), rt_filename);
  unlink(log_filename);
}


/* Joins two file paths together in a platform-independent way. */
void filepath_join(char *filepath_result, unsigned int filepath_result_size, const char *path1, const char *path2) {
#ifdef _WIN32
  snprintf(filepath_result, filepath_result_size, "%s\\%s", path1, path2);
#else
  snprintf(filepath_result, filepath_result_size, "%s/%s", path1, path2);
#endif
}


/* Returns an open file's size. Uses 64-bit seek/tell on all platforms to
 * handle files larger than 2 GB. */
int64_t get_file_size(FILE *f) {
#ifdef _WIN32
#define FS_FSEEK(f, o, w) _fseeki64(f, o, w)
#define FS_FTELL(f)        _ftelli64(f)
#else
#define FS_FSEEK(f, o, w) fseeko(f, o, w)
#define FS_FTELL(f)        ftello(f)
#endif

  int64_t ret = 0;
  int64_t original_pos = FS_FTELL(f);  /* Save the file pointer's current position. */


  /* Seek to the end of the file. */
  if (FS_FSEEK(f, 0, SEEK_END) < 0) {
    fprintf(stderr, "Failed to seeking in file.\n");
    exit(-1);
  }

  ret = FS_FTELL(f);

  /* Restore the file pointer to its original position. */
  if (FS_FSEEK(f, original_pos, SEEK_SET) < 0) {
    fprintf(stderr, "Failed to seeking in file.\n");
    exit(-1);
  }

  return ret;

#undef FS_FSEEK
#undef FS_FTELL
}


/* Returns the OS name. */
char *get_os_name() {
#ifdef _WIN32
  return "Windows";

  /* We can't get accurate info on Windows 10 without specifying an application XML manifest, which is too much of a pain at the moment... */
  /*
  if (IsWindowsVersionOrGreater(10, 0, 0))  * Ubuntu 18's MinGW doesn't have IsWindows10OrGreater(). *
    return "Windows 10";
  else if (IsWindows8Point1OrGreater())
    return "Windows 8.1";
  else if (IsWindows8OrGreater())
    return "Windows 8";
  else if (IsWindows7OrGreater())
    return "Windows 7";
  else
    return "An old version of Windows";
  */
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}


/* Returns the amount of system RAM, in bytes.  Returns zero on error. */
uint64_t get_total_memory() {
  uint64_t total_memory = 0;
#ifdef _WIN32
  MEMORYSTATUSEX ms = {0};

  ms.dwLength = sizeof(MEMORYSTATUSEX);
  if (!GlobalMemoryStatusEx(&ms)) {
    windows_print_error("GlobalMemoryStatusEx");
    return 0;
  }
  total_memory = ms.ullTotalPhys;
#elif defined(__APPLE__)
  int mib[2] = {CTL_HW, HW_MEMSIZE};
  size_t len = sizeof(total_memory);
  if (sysctl(mib, 2, &total_memory, &len, NULL, 0) != 0) {
    fprintf(stderr, "\nFailed to call sysctl(HW_MEMSIZE): %s (%d)\n", strerror(errno), errno);
    return 0;
  }
#else
  struct sysinfo si = {0};

  if (sysinfo(&si) < 0) {
    fprintf(stderr, "\nFailed to call sysinfo(): %s (%d)\n", strerror(errno), errno);
    return 0;
  }
  total_memory = si.totalram;
#endif
  return total_memory;
}


/* Returns a random number between 0 and max - 1. */
uint64_t get_random(uint64_t max) {
  uint64_t ret = 0;
  unsigned int i = 0;
  unsigned char random_byte = 0;
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlgorithm = NULL;


  /* Get a handle to the random number generator. */
  if (BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_RNG_ALGORITHM, NULL, 0) != STATUS_SUCCESS) {
    fprintf(stderr, "Error: failed to obtain handle to random number generator!\n");
    exit(-1);
  }

  for (i = 0; i < 8; i++) {
    /* Get a single random byte. */
    if (BCryptGenRandom(hAlgorithm, &random_byte, sizeof(unsigned char), 0) != STATUS_SUCCESS) {
      fprintf(stderr, "Error: failed to obtain random bytes from random number generator!\n");
      exit(-1);
    }

    /* Shift our return value up by 8 bits and OR in the new random byte. */
    ret <<= 8;
    ret |= random_byte;

    /* If we exceeded the max value wanted by the caller, we're done reading random bytes. */
    if (ret > max)
      break;
  }

  /* Close the RNG handle. */
  if (BCryptCloseAlgorithmProvider(hAlgorithm, 0) != STATUS_SUCCESS)
    fprintf(stderr, "Warning: failed to close handle to random number generator.\n");
#else
  FILE *urandom = fopen("/dev/urandom", "r");


  /* Ensure that we opened a handle to /dev/urandom. */
  if (urandom == NULL) {
    fprintf(stderr, "Error: failed to open /dev/urandom!\n");
    exit(-1);
  }

  for (i = 0; i < 8; i++) {

    /* Get a single random byte. */
    if (fread(&random_byte, sizeof(unsigned char), 1, urandom) != 1) {
      fprintf(stderr, "Error: failed to obtain random bytes from random number generator!\n");
      exit(-1);
    }

    /* Shift our return value up by 8 bits and OR in the new random byte. */
    ret <<= 8;
    ret |= random_byte;

    /* If we exceeded the max value wanted by the caller, we're done reading random bytes. */
    if (ret > max)
      break;
  }

  FCLOSE(urandom);
#endif

  return ret % max;
}


/* Given a rainbow table filename, get its associated log filename. */
void get_rt_log_filename(char *log_filename, size_t log_filename_size, char *rt_filename) {
  snprintf(log_filename, log_filename_size - 1, "%s.log", rt_filename);
}


/* Returns 1 if the parameters form the standard NTLM 8 set, otherwise 0. */
unsigned int is_ntlm8(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len) {
  if ((hash_type == HASH_NTLM) && \
      (strcmp(charset, CHARSET_ASCII_32_95) == 0) && \
      (plaintext_len_min == 8) && \
      (plaintext_len_max == 8))
    return 1;
  else
    return 0;
}


/* Returns 1 if the parameters form the standard NTLM 9 set, otherwise 0. */
unsigned int is_ntlm9(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len) {
  if ((hash_type == HASH_NTLM) && \
      (strcmp(charset, CHARSET_ASCII_32_95) == 0) && \
      (plaintext_len_min == 9) && \
      (plaintext_len_max == 9) && \
      (reduction_offset == 0) && \
      (chain_len == 803000))
    return 1;
  else
    return 0;
}


/* Returns 1 if the parameters form the standard NetNTLMv1 7-byte set, otherwise 0. */
unsigned int is_netntlmv1_7(unsigned int hash_type, char *charset_name, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int chain_len) {
  if ((hash_type == HASH_NETNTLMV1) && \
      (strcmp(charset_name, "byte") == 0) && \
      (plaintext_len_min == 7) && \
      (plaintext_len_max == 7) && \
      (chain_len == 881689))
    return 1;
  else
    return 0;
}


/* Optimized Markov NTLM8/9/10 path DISABLED: the optimized kernels hardcode keyspace 95^9 (ignoring --markov-keyspace) and their lookup false-alarm check rejects in-table hashes. Returning 0 routes all Markov through the generic crackalack_markov/precompute_markov/false_alarm_check_markov kernels, which honor --markov-keyspace and are verified correct. */
/* Returns 1 if the parameters form the Markov NTLM 8 set, otherwise 0. */
unsigned int is_markov_ntlm8(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len, int use_markov) {
  return 0;
}


/* Optimized Markov NTLM8/9/10 path DISABLED: the optimized kernels hardcode keyspace 95^9 (ignoring --markov-keyspace) and their lookup false-alarm check rejects in-table hashes. Returning 0 routes all Markov through the generic crackalack_markov/precompute_markov/false_alarm_check_markov kernels, which honor --markov-keyspace and are verified correct. */
/* Returns 1 if the parameters form the Markov NTLM 9 set, otherwise 0. */
unsigned int is_markov_ntlm9(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, unsigned int reduction_offset, unsigned int chain_len, int use_markov) {
  return 0;
}


/* Returns 1 if the parameters form the standard NTLM 10 set, otherwise 0. */
unsigned int is_ntlm10(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max) {
  return (hash_type == HASH_NTLM)
      && (strcmp(charset, CHARSET_ASCII_32_95) == 0)
      && (plaintext_len_min == 10)
      && (plaintext_len_max == 10);
}


/* Optimized Markov NTLM8/9/10 path DISABLED: the optimized kernels hardcode keyspace 95^9 (ignoring --markov-keyspace) and their lookup false-alarm check rejects in-table hashes. Returning 0 routes all Markov through the generic crackalack_markov/precompute_markov/false_alarm_check_markov kernels, which honor --markov-keyspace and are verified correct. */
/* Returns 1 if the parameters form the Markov NTLM 10 set, otherwise 0. */
unsigned int is_markov_ntlm10(unsigned int hash_type, char *charset, unsigned int plaintext_len_min, unsigned int plaintext_len_max, int use_markov) {
  return 0;
}


/* Returns 1 if the parameters form the MD5 8-character ascii-32-95 set, otherwise 0. */
unsigned int is_md5_8(unsigned int hash_type, char *charset,
                       unsigned int plaintext_len_min,
                       unsigned int plaintext_len_max) {
  return (hash_type == HASH_MD5)
      && (strcmp(charset, CHARSET_ASCII_32_95) == 0)
      && (plaintext_len_min == 8)
      && (plaintext_len_max == 8);
}


/* Returns 1 if the parameters form the MD5 9-character ascii-32-95 set, otherwise 0. */
unsigned int is_md5_9(unsigned int hash_type, char *charset,
                       unsigned int plaintext_len_min,
                       unsigned int plaintext_len_max) {
  return (hash_type == HASH_MD5)
      && (strcmp(charset, CHARSET_ASCII_32_95) == 0)
      && (plaintext_len_min == 9)
      && (plaintext_len_max == 9);
}


/* Given a filename for a rainbow table, parse its parameters.  On success the
 * rt_parameters' parsed flag is set to 1, otherwise it is zero. */
void parse_rt_params(rt_parameters *rt_params, char *rt_filename_orig) {
  /* Filename is in the following format: "%s_%s#%u-%u_%u_%ux%u_%u" */
  char *hpos = NULL;
  char rt_filename[512] = {0};


  rt_params->parsed = 0;
  rt_params->is_mask = 0;
  rt_params->mask[0] = '\0';
  memcpy(rt_params->challenge, NETNTLMV1_DEFAULT_CHALLENGE, 8);

  /* Skip the directory path, if this filename is absolute. */
#ifdef _WIN32
  hpos = strrchr(rt_filename_orig, '\\');
#else
  hpos = strrchr(rt_filename_orig, '/');
#endif
  if (hpos != NULL)
    strncpy(rt_filename, hpos + 1, sizeof(rt_filename) - 1);
  else
    strncpy(rt_filename, rt_filename_orig, sizeof(rt_filename) - 1);

  /* Ensure that the filename ends in .rt, .rtc, or .rti2. */
  if (!str_ends_with(rt_filename, ".rt") && !str_ends_with(rt_filename, ".rtc") && !str_ends_with(rt_filename, ".rti2"))
    return;

  /* Strip "_distrrtgen" and any bracketed tags from the part field (e.g.
   * "..._distrrtgen[p][i]_066.rti2" → "..._066.rti2") so the sscanf below
   * can parse the numeric part number. */
  {
    char *dg = strstr(rt_filename, "_distrrtgen");
    if (dg != NULL) {
      /* Find the last underscore after distrrtgen to locate the part number. */
      char *last_us = strrchr(dg + 1, '_');
      if (last_us != NULL)
        memmove(dg, last_us, strlen(last_us) + 1);
    }
  }

  /* Manually pick out the strings from the filename.  sscanf() can't be used because
   * a buffer overflow can occur (note that the MinGW system doesn't support the
   * "m" format modifier, which would have been a good and portable solution...). */
  hpos = strchr(rt_filename, '#');
  if (hpos) {
    char *suffix = hpos + 1;
    char *upos = NULL;


    *hpos = '\0';
    upos = strchr(rt_filename, '_');
    if (upos) {
      char *hash_name_ptr = rt_filename;
      char *charset_name_ptr = upos + 1;


      *upos = '\0';
      strncpy(rt_params->hash_name, hash_name_ptr, sizeof(rt_params->hash_name) - 1);
      rt_params->hash_name[sizeof(rt_params->hash_name) - 1] = '\0';

      strncpy(rt_params->charset_name, charset_name_ptr, sizeof(rt_params->charset_name) - 1);
      rt_params->charset_name[sizeof(rt_params->charset_name) - 1] = '\0';

      /* Extract NetNTLMv1 challenge from charset name if present
       * (e.g. "byte-chalaabbccddeeff0011"). */
      {
        char *ch = strstr(rt_params->charset_name, "-chal");
        if (ch) {
          unsigned char parsed_chal[8];
          if (parse_challenge_str(ch + 5, parsed_chal) == 0)
            memcpy(rt_params->challenge, parsed_chal, 8);
          *ch = '\0';
        }
      }

      /* Extract Markov keyspace from charset name if present (e.g. "ascii-32-95-mk1000000"). */
      {
        char *mk = strstr(rt_params->charset_name, "-mk");
        if (mk) {
          rt_params->markov_keyspace = strtoull(mk + 3, NULL, 10);
          *mk = '\0';
        } else {
          rt_params->markov_keyspace = 0;
        }
      }

      /* Detect a mask-encoded charset field (e.g. "%u%l%d" or
       * "%1%1%d!1-616263").  Check AFTER markov stripping so a future
       * markov+mask combo could be distinguished.  Store the RAW encoded
       * field; consumers call mask_decode_charset_field() to reconstruct the
       * Mask (custom sets are carried in trailing !N-<hex> blocks, which must
       * not be mangled by the in-place %x->?x decode used only to TEST). */
      {
        char decoded[sizeof(rt_params->mask)];
        strncpy(decoded, charset_name_ptr, sizeof(decoded) - 1);
        decoded[sizeof(decoded) - 1] = '\0';
        mask_decode_from_filename(decoded);      /* only to TEST for '?' */
        if (is_mask_string(decoded)) {
          rt_params->is_mask = 1;
          /* Copy the RAW encoded field from the ORIGINAL untruncated filename
           * source (charset_name_ptr), NOT from rt_params->charset_name, which
           * is capped at char[64] and would truncate long custom-charset fields
           * (up to MASK_FIELD_MAX=200 chars) before they reach mask[256].
           * charset_name_ptr points into rt_filename and is unmodified by the
           * -chal/-mk stripping above (which mutate the separate charset_name
           * buffer), so it holds the full field NUL-terminated at the old '#'. */
          strncpy(rt_params->mask, charset_name_ptr,   /* RAW, not decoded */
                  sizeof(rt_params->mask) - 1);
          rt_params->mask[sizeof(rt_params->mask) - 1] = '\0';
          /* Strip any trailing -mk<N> suffix so that rt_params->mask holds
           * only the pure mask field (e.g. "%u%l%l%d"), not the markov tag. */
          {
            char *mk_in_mask = strstr(rt_params->mask, "-mk");
            if (mk_in_mask)
              *mk_in_mask = '\0';
          }
        } else {
          rt_params->is_mask = 0;
          rt_params->mask[0] = '\0';
        }
      }

      /* Now parse the unsigned integers. */
      if (sscanf(suffix, "%u-%u_%u_%ux%"SCNu64"_%u", &rt_params->plaintext_len_min, &rt_params->plaintext_len_max, &rt_params->table_index, &rt_params->chain_len, &rt_params->num_chains, &rt_params->table_part) == 6) {


	/* Calculate the reduction offset from the table index. */
	rt_params->reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(rt_params->table_index);
	/* Validate the hash type. & character set name. */
	rt_params->hash_type = hash_str_to_type(rt_params->hash_name);

	/* Ensure that the hash type and character set is valid, the plaintext
	 * length min & max are set properly, and the chain length is set.
	 * For mask tables the charset field is the encoded mask, not a named
	 * charset, so skip validate_charset and rely on is_mask instead. */
	if ((rt_params->hash_type != HASH_UNDEFINED) && \
	    (rt_params->is_mask || validate_charset(rt_params->charset_name) != NULL) && \
	    (rt_params->plaintext_len_min > 0) && \
	    (rt_params->plaintext_len_min <= rt_params->plaintext_len_max) && \
	    (rt_params->plaintext_len_max <= MAX_PLAINTEXT_LEN) && \
	    (rt_params->chain_len > 0) && \
	    (rt_params->num_chains > 0))
	  rt_params->parsed = 1;
      }
    }
  }
}


/* Parses a CLI argument as a non-negative unsigned int.
 * Exits with an error message if the value is not a valid integer or
 * overflows unsigned int range. */
unsigned int parse_uint_arg(const char *s, const char *name) {
  char *end;
  errno = 0;
  unsigned long val = strtoul(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') {
    fprintf(stderr, "Error: %s must be a valid non-negative integer, got '%s'.\n", name, s);
    exit(-1);
  }
  if (val > UINT32_MAX) {
    fprintf(stderr, "Error: %s value %lu exceeds maximum (%u).\n", name, val, UINT32_MAX);
    exit(-1);
  }
  return (unsigned int)val;
}


/* Parses a CLI argument as a non-negative uint64_t.
 * Exits with an error message if the value is not a valid integer. */
uint64_t parse_uint64_arg(const char *s, const char *name) {
  char *end;
  errno = 0;
  unsigned long long val = strtoull(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') {
    fprintf(stderr, "Error: %s must be a valid non-negative integer, got '%s'.\n", name, s);
    exit(-1);
  }
  return (uint64_t)val;
}


/* Combines realloc() with calloc(). */
/* Default position-chunk size for the batched precompute kernel dispatch.
 *
 * Wall time of the heaviest (first) chunk scales as num_hashes * chunk_size,
 * because the lowest-position chunk walks ~chain_len steps for every work
 * item.  The 2-hash / 8192 configuration was measured at ~5 s for that chunk
 * (comfortably under NVIDIA's 30 s TDR watchdog), so we hold the product
 * num_hashes * chunk_size constant at 2 * 8192 = 16384 and clamp the result
 * to [256, 8192].  Total GPU work is chunk-invariant, so shrinking the chunk
 * for large hash batches costs nothing but keeps each dispatch TDR-safe; the
 * 256 floor still yields >= 16384 work items per dispatch (>=64 hashes), more
 * than enough to saturate the SMs. */
unsigned int compute_batch_chunk_size(unsigned int num_hashes) {
  const unsigned int target_work_items = 16384;  /* 2 hashes * 8192 */
  const unsigned int min_chunk = 256;
  const unsigned int max_chunk = 8192;

  if (num_hashes == 0)
    return max_chunk;

  unsigned int chunk = target_work_items / num_hashes;
  if (chunk < min_chunk) chunk = min_chunk;
  if (chunk > max_chunk) chunk = max_chunk;
  return chunk;
}


void *recalloc(void *ptr, size_t new_size, size_t old_size) {
  ptr = realloc(ptr, new_size);
  if (ptr == NULL) {
    fprintf(stderr, "Failed to realloc buffer.\n");
    exit(-1);
  }

  memset((unsigned char *)ptr + old_size, 0, new_size - old_size);
  return ptr;
}


/* Logs a message to the rainbow table log. */
size_t rt_log(rc_file f, const char *fmt, ...) {
  char buf[256] = {0};
  size_t len = 0;

  va_list args;
  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
  va_end(args);

  if (len > 0)
    len = rc_fwrite(buf, len, 1, f);

  return len;
}


/* Returns 1 if the string ends with the specified suffix, otherwise 0. */
int str_ends_with(const char *str, const char *suffix) {
  size_t str_len;
  size_t suffix_len;


  if ((str == NULL) || (suffix == NULL))
    return 0;

  str_len = strlen(str);
  suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return 0;

  return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}


/* Converts a string to lowercase. */
void str_to_lowercase(char *s) {
  unsigned int i = 0;

  for (; i < strlen(s); i++)
    s[i] = tolower(s[i]);
}


/* Parse hash-file contents into newly-allocated hashes/usernames arrays.
 * See misc.h for full parameter documentation.
 * On error, all internal allocations are freed and the out-params are set to
 * NULL/0 before returning non-zero. */
int parse_hash_file_data(char *file_data,
                         const char *pot_contents,
                         char ***out_hashes,
                         char ***out_usernames,
                         unsigned int *out_num_hashes,
                         unsigned int *out_num_previously_cracked,
                         int *out_file_format) {
  unsigned int i = 0;
  unsigned int max_num_hashes = 0;
  unsigned int num_colons = 0;
  unsigned int num_hashes = 0;
  unsigned int previously_cracked = 0;
  int file_format = 0;
  char **hashes = NULL;
  char **usernames = NULL;
  char *line = NULL;
  char *line_copy = NULL;  /* PWDUMP per-line scratch; freed at cleanup_err */
  size_t file_data_len = strlen(file_data);

  /* Count newlines to determine array size. */
  for (i = 0; i < file_data_len; i++) {
    if (file_data[i] == '\n')
      max_num_hashes++;
  }
  max_num_hashes++;  /* In case the last line doesn't end with an LF. */

  /* Detect format by counting colons in the first line. */
  num_colons = 0;
  for (i = 0; i < file_data_len; i++) {
    if (file_data[i] == ':')
      num_colons++;
    else if (file_data[i] == '\n')
      break;
  }

  if (num_colons == 0) {
    file_format = HASH_FILE_FORMAT_PLAIN;
    printf("Hash file contains plain hashes.\n");
  } else if (num_colons == 6) {
    file_format = HASH_FILE_FORMAT_PWDUMP;
    printf("Hash file is pwdump format.\n");
  } else {
    fprintf(stderr, "Error: hash file format is not recognized (number of colons in first line is %u, instead of 0 or 6).\n", num_colons);
    goto cleanup_err;
  }

  usernames = calloc(max_num_hashes, sizeof(char *));
  hashes = calloc(max_num_hashes, sizeof(char *));
  if ((usernames == NULL) || (hashes == NULL)) {
    fprintf(stderr, "Error while allocating buffer for hashes.\n");
    goto cleanup_err;
  }

  /* Tokenize the hash file by line.  Store each hash in the array. */
  num_hashes = 0;
  line = strtok(file_data, "\n");
  while (line && (num_hashes < max_num_hashes)) {

    /* Skip empty lines. */
    if (strlen(line) > 0) {

      /* Skip previously-cracked hashes. */
      if ((pot_contents != NULL) && strstr(pot_contents, line) != NULL)
        previously_cracked++;
      else {
        /* If we're dealing with CRLF line endings, cut off the trailing CR. */
        if (line[strlen(line) - 1] == '\r')
          line[strlen(line) - 1] = '\0';

        if (file_format == HASH_FILE_FORMAT_PLAIN) {
          /* Ensure that hash is lowercase. */
          str_to_lowercase(line);

          hashes[num_hashes] = strdup(line);
          if (hashes[num_hashes] == NULL) {
            fprintf(stderr, "Error while allocating buffer for hashes.\n");
            goto cleanup_err;
          }
          num_hashes++;
        } else {  /* HASH_FILE_FORMAT_PWDUMP */
          char *hash = NULL;
          unsigned int line_copy_len = 0;
          unsigned int hash_start = 0, hash_end = 0;

          line_copy = strdup(line);
          if (line_copy == NULL) {
            fprintf(stderr, "Error while allocating buffer for hashes.\n");
            goto cleanup_err;
          }
          line_copy_len = strlen(line_copy);

          /* Get the username from position zero until the first colon. */
          for (i = 0; i < line_copy_len; i++) {
            if (line_copy[i] == ':') {
              line_copy[i] = '\0';
              usernames[num_hashes] = strdup(line_copy);
              if (usernames[num_hashes] == NULL) {
                fprintf(stderr, "Error while allocating buffer for usernames.\n");
                goto cleanup_err;
              }
              break;
            }
          }

          /* Find the start and end positions of the hash, based on the number of colons. */
          num_colons = 1;
          hash_start = 0;
          hash_end = 0;
          for (i = i + 1; i < line_copy_len; i++) {
            if (line_copy[i] == ':')
              num_colons++;

            if ((num_colons == 3) && (hash_start == 0))
              hash_start = i + 1;
            else if (num_colons == 4) {
              hash_end = i;
              break;
            }
          }

          if ((hash_start == 0) || (hash_end == 0)) {
            fprintf(stderr, "Error: failed to extract hash from line: [%s]\n", line);
            goto cleanup_err;
          }

          *(line_copy + hash_end) = '\0';
          hash = line_copy + hash_start;

          /* Make sure the hash is 32 bytes. */
          if (strlen(hash) != 32) {
            fprintf(stderr, "Error: hash is length %u instead of 32: [%s]\n", (unsigned int)strlen(hash), hash);
            goto cleanup_err;
          }

          str_to_lowercase(hash);  /* Ensure hash is lowercase. */

          if ((pot_contents != NULL) && strstr(pot_contents, hash) != NULL) {
            previously_cracked++;
          } else {
            hashes[num_hashes] = strdup(hash);
            if (hashes[num_hashes] == NULL) {
              fprintf(stderr, "Error while allocating buffer for hashes.\n");
              goto cleanup_err;
            }
            num_hashes++;
          }
          FREE(line_copy);
        }
      }
    }
    line = strtok(NULL, "\n");
  }

  *out_hashes = hashes;
  *out_usernames = usernames;
  *out_num_hashes = num_hashes;
  *out_num_previously_cracked = previously_cracked;
  *out_file_format = file_format;
  return 0;

cleanup_err:
  /* Free the PWDUMP scratch buffer if we were mid-line when the error hit. */
  FREE(line_copy);
  /* Free all hashes and usernames allocated so far (slots 0..num_hashes-1),
   * plus the just-allocated username in slot num_hashes if present. */
  if (hashes != NULL) {
    for (i = 0; i <= num_hashes; i++)
      free(hashes[i]);
    free(hashes);
  }
  if (usernames != NULL) {
    for (i = 0; i <= num_hashes; i++)
      free(usernames[i]);
    free(usernames);
  }
  *out_hashes = NULL;
  *out_usernames = NULL;
  *out_num_hashes = 0;
  *out_num_previously_cracked = 0;
  *out_file_format = 0;
  return -1;
}


/* On Windows, prints the last error. */
#ifdef _WIN32
void windows_print_error(char *func_name) {
  DWORD err_code = GetLastError();
  LPVOID err_str = NULL;

  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &err_str, 0, NULL);

  fprintf(stderr, "\n%s failed with error %lu: %s\n\n", func_name, err_code, (char *)err_str);
  fflush(stderr);
  LocalFree(err_str);
}
#endif
