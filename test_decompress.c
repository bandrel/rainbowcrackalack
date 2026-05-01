/*
 * Rainbow Crackalack: test_decompress.c
 * CPU-only regression tests for rtc_decompress.c and rti2_decompress.c.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rtc_decompress.h"
#include "rti2_decompress.h"
#include "test_decompress.h"


/* Build a minimal RTC v0 file in /tmp containing num_chains chains.
 * Filename matches the loader's parsing rule (must contain "...x<num>_<part>.rtc").
 * Returns full path on success (caller frees), NULL on failure. */
static char *write_test_rtc(const char *path,
                            unsigned int s_bits,
                            unsigned int e_bits,
                            uint64_t s_min,
                            uint64_t e_min,
                            uint64_t e_interval,
                            uint64_t num_chains,
                            const uint64_t *starts,
                            const uint64_t *ends) {
  FILE *f = fopen(path, "wb");
  if (!f) return NULL;

  unsigned int version = 0x30435452;  /* "RTC0" */
  unsigned short sb = (unsigned short)s_bits;
  unsigned short eb = (unsigned short)e_bits;

  fwrite(&version, sizeof(version), 1, f);
  fwrite(&sb, sizeof(sb), 1, f);
  fwrite(&eb, sizeof(eb), 1, f);
  fwrite(&s_min, sizeof(s_min), 1, f);
  fwrite(&e_min, sizeof(e_min), 1, f);
  fwrite(&e_interval, sizeof(e_interval), 1, f);

  unsigned int chain_size = (s_bits + e_bits + 7) / 8;
  uint64_t s_mask = (s_bits == 64) ? ~0ULL : (((uint64_t)1 << s_bits) - 1);

  for (uint64_t i = 0; i < num_chains; i++) {
    /* Encode: lower s_bits = (s - s_min); upper e_bits = (e - e_min - i*e_interval) */
    uint64_t s_field = (starts[i] - s_min) & s_mask;
    uint64_t e_field = ends[i] - e_min - (e_interval * i);
    uint8_t buf[16] = {0};
    /* Pack: buf[0..1] = s_field | (e_field << s_bits) over 128 bits */
    /* For test simplicity require s_bits + e_bits <= 64. */
    uint64_t packed = s_field | (e_field << s_bits);
    memcpy(buf, &packed, chain_size);
    fwrite(buf, chain_size, 1, f);
  }
  fclose(f);
  return strdup(path);
}


static int test_rtc_basic(void) {
  /* Build a 5-chain table with s_bits=24, e_bits=32. */
  uint64_t starts[5] = { 100, 250, 1000, 65536, 16777215 };
  uint64_t ends[5]   = { 0x12345678ULL, 0xdeadbeefULL, 0x00010001ULL,
                         0xfedcba98ULL, 0x7fffffffULL };

  /* Filename must end with "x<num_chains>_<part>.rtc" for the loader's sscanf. */
  char path[] = "/tmp/rcrt_test_basic_x5_0.rtc";
  if (write_test_rtc(path, 24, 32, /*s_min*/100, /*e_min*/0,
                     /*e_interval*/1000, 5, starts, ends) == NULL) {
    fprintf(stderr, "RTC-01 write failed\n");
    return 0;
  }

  uint64_t *out = NULL;
  uint64_t n = 0;
  int rc = rtc_decompress(path, &out, &n);
  unlink(path);

  int ok = 1;
  if (rc != 0 || n != 5) {
    fprintf(stderr, "RTC-01 decompress rc=%d n=%"PRIu64"\n", rc, n);
    ok = 0;
  } else {
    for (uint64_t i = 0; i < 5; i++) {
      if (out[i*2] != starts[i] || out[i*2+1] != ends[i]) {
        fprintf(stderr,
                "RTC-01 mismatch at %"PRIu64": got s=%"PRIu64" e=%"PRIu64", want s=%"PRIu64" e=%"PRIu64"\n",
                i, out[i*2], out[i*2+1], starts[i], ends[i]);
        ok = 0;
      }
    }
  }
  free(out);
  return ok;
}


static int test_rtc_large(void) {
  /* 10000 chains, deterministic content. Exercises bulk-read path. */
  uint64_t n = 10000;
  uint64_t *starts = malloc(n * sizeof(uint64_t));
  uint64_t *ends   = malloc(n * sizeof(uint64_t));
  for (uint64_t i = 0; i < n; i++) {
    starts[i] = 1000 + i;                    /* ascending within s_min=1000 range */
    ends[i]   = 0x100000ULL + (i * 7) + i*3; /* arbitrary endpoint pattern */
  }

  char path[] = "/tmp/rcrt_test_large_x10000_0.rtc";
  if (write_test_rtc(path, 20, 36, 1000, 0x100000ULL, 10, n, starts, ends) == NULL) {
    free(starts); free(ends);
    fprintf(stderr, "RTC-02 write failed\n");
    return 0;
  }

  uint64_t *out = NULL;
  uint64_t got_n = 0;
  int rc = rtc_decompress(path, &out, &got_n);
  unlink(path);

  int ok = 1;
  if (rc != 0 || got_n != n) {
    fprintf(stderr, "RTC-02 rc=%d n=%"PRIu64"\n", rc, got_n);
    ok = 0;
  } else {
    for (uint64_t i = 0; i < n; i++) {
      if (out[i*2] != starts[i] || out[i*2+1] != ends[i]) {
        fprintf(stderr, "RTC-02 mismatch at %"PRIu64"\n", i);
        ok = 0;
        break;
      }
    }
  }
  free(out); free(starts); free(ends);
  return ok;
}


/* Build a minimal RTI2 v0 file. Constraints for this helper:
 * - algorithm != 0 and not in [15..19] so no salt is written
 * - num_subkeys = 0 (no subkeyspace section beyond the count byte)
 * - checkPointBits = 0 (no checkpoint section)
 * - sp_bits + ep_bits <= 56 so chainSizeBytes fits in 7 bytes
 *
 * starts[i] must be >= sp_min and (starts[i] - sp_min) must fit in sp_bits.
 * ends[i] must be sorted by their high bits in (ep_bits + ?) prefix bucket --
 * for simplicity this helper takes pre-bucketed input via prefix_counts. */
static char *write_test_rti2(const char *path,
                             unsigned int sp_bits,
                             unsigned int ep_bits,
                             uint64_t sp_min,
                             uint64_t first_prefix,
                             uint32_t num_prefix_indexes,
                             const uint8_t *prefix_counts,
                             const uint64_t *starts,
                             const uint64_t *ends) {
  FILE *f = fopen(path, "wb");
  if (!f) return NULL;

  /* Fixed header (28 bytes packed). */
  uint32_t tag = 0x32495452;  /* "RTI2" */
  uint8_t  minor = 0;
  uint8_t  spb = (uint8_t)sp_bits;
  uint8_t  epb = (uint8_t)ep_bits;
  uint8_t  cpb = 0;
  uint32_t fileIndex = 0;
  uint32_t files = 1;
  uint64_t minimumStartPoint = sp_min;
  uint32_t chainLength = 1000;
  uint32_t tableIndex = 0;
  uint8_t  algorithm = 2;   /* NTLM, no salt */
  uint8_t  reductionFunction = 0;

  fwrite(&tag, 4, 1, f);
  fwrite(&minor, 1, 1, f);
  fwrite(&spb, 1, 1, f);
  fwrite(&epb, 1, 1, f);
  fwrite(&cpb, 1, 1, f);
  fwrite(&fileIndex, 4, 1, f);
  fwrite(&files, 4, 1, f);
  fwrite(&minimumStartPoint, 8, 1, f);
  fwrite(&chainLength, 4, 1, f);
  fwrite(&tableIndex, 4, 1, f);
  fwrite(&algorithm, 1, 1, f);
  fwrite(&reductionFunction, 1, 1, f);

  /* No salt because algorithm != 0 and not in [15..19]. */

  /* Sub key spaces: just a count of 0. */
  uint8_t num_subkeys = 0;
  fwrite(&num_subkeys, 1, 1, f);

  /* No checkpoint section since cpb = 0. */

  /* Index header. */
  fwrite(&first_prefix, 8, 1, f);
  fwrite(&num_prefix_indexes, 4, 1, f);

  /* Prefix counts. */
  fwrite(prefix_counts, 1, num_prefix_indexes, f);

  /* Chain rows. */
  unsigned int chain_bytes = (sp_bits + ep_bits + 7) / 8;
  uint64_t sp_mask = ((uint64_t)1 << sp_bits) - 1;
  uint64_t ep_mask = ((uint64_t)1 << ep_bits) - 1;

  uint64_t idx = 0;
  for (uint32_t p = 0; p < num_prefix_indexes; p++) {
    uint64_t epPrefix = (first_prefix + p) << ep_bits;
    for (uint8_t c = 0; c < prefix_counts[p]; c++) {
      uint64_t ep_low = (ends[idx] - epPrefix) & ep_mask;
      uint64_t sp_field = (starts[idx] - sp_min) & sp_mask;
      uint64_t row = ep_low | (sp_field << ep_bits);
      uint8_t buf[8] = {0};
      memcpy(buf, &row, chain_bytes);
      fwrite(buf, chain_bytes, 1, f);
      idx++;
    }
  }
  fclose(f);
  return strdup(path);
}


static int test_rti2_basic(void) {
  /* 6 chains across 2 prefix buckets (3 each). */
  uint64_t sp_min = 1000;
  unsigned int sp_bits = 20;   /* sp range: sp_min..sp_min + 2^20 - 1 */
  unsigned int ep_bits = 28;
  uint64_t first_prefix = 5;

  /* ep within prefix 5: high bits = 5 << 28 = 0x50000000.
   * ep within prefix 6: high bits = 6 << 28 = 0x60000000. */
  uint64_t starts[6] = { 1000, 1500, 2000, 1100, 1200, 1300 };
  uint64_t ends[6]   = {
    0x50000001ULL, 0x50000abcULL, 0x5fffffffULL,   /* prefix 5 */
    0x60000010ULL, 0x60000abcULL, 0x6ffffffeULL,   /* prefix 6 */
  };
  uint8_t counts[2] = { 3, 3 };

  char path[] = "/tmp/rcrt_test_rti2_basic.rti2";
  if (write_test_rti2(path, sp_bits, ep_bits, sp_min, first_prefix, 2,
                      counts, starts, ends) == NULL) {
    fprintf(stderr, "RTI2-01 write failed\n");
    return 0;
  }

  uint64_t *out = NULL;
  uint64_t n = 0;
  int rc = rti2_decompress(path, &out, &n);
  unlink(path);

  int ok = 1;
  if (rc != 0 || n != 6) {
    fprintf(stderr, "RTI2-01 rc=%d n=%"PRIu64"\n", rc, n);
    ok = 0;
  } else {
    for (uint64_t i = 0; i < 6; i++) {
      if (out[i*2] != starts[i] || out[i*2+1] != ends[i]) {
        fprintf(stderr,
                "RTI2-01 mismatch at %"PRIu64": got (%"PRIu64",%"PRIx64") want (%"PRIu64",%"PRIx64")\n",
                i, out[i*2], out[i*2+1], starts[i], ends[i]);
        ok = 0;
      }
    }
  }
  free(out);
  return ok;
}


static int test_rti2_large(void) {
  /* 5000 chains, distributed across 4 buckets of 1250 each.
   * Exercises bulk-read path on a non-trivial file. */
  uint64_t sp_min = 100;
  unsigned int sp_bits = 24;
  unsigned int ep_bits = 28;
  uint64_t first_prefix = 0;
  uint32_t num_prefixes = 4;

  uint64_t total = 0;
  uint8_t counts[4] = {0};
  for (uint32_t p = 0; p < num_prefixes; p++) {
    counts[p] = 200;  /* keep <= 255 since prefix_counts is uint8_t */
    total += counts[p];
  }

  uint64_t *starts = malloc(total * sizeof(uint64_t));
  uint64_t *ends   = malloc(total * sizeof(uint64_t));
  uint64_t idx = 0;
  for (uint32_t p = 0; p < num_prefixes; p++) {
    uint64_t epPrefix = (first_prefix + p) << ep_bits;
    for (uint8_t c = 0; c < counts[p]; c++) {
      starts[idx] = sp_min + (idx * 17);
      ends[idx]   = epPrefix + (idx * 23 + 1);   /* deterministic, low-bits-only */
      idx++;
    }
  }

  char path[] = "/tmp/rcrt_test_rti2_large.rti2";
  if (write_test_rti2(path, sp_bits, ep_bits, sp_min, first_prefix,
                      num_prefixes, counts, starts, ends) == NULL) {
    free(starts); free(ends);
    fprintf(stderr, "RTI2-02 write failed\n");
    return 0;
  }

  uint64_t *out = NULL;
  uint64_t got_n = 0;
  int rc = rti2_decompress(path, &out, &got_n);
  unlink(path);

  int ok = 1;
  if (rc != 0 || got_n != total) {
    fprintf(stderr, "RTI2-02 rc=%d n=%"PRIu64"\n", rc, got_n);
    ok = 0;
  } else {
    for (uint64_t i = 0; i < total; i++) {
      if (out[i*2] != starts[i] || out[i*2+1] != ends[i]) {
        fprintf(stderr, "RTI2-02 mismatch at %"PRIu64"\n", i);
        ok = 0;
        break;
      }
    }
  }
  free(out); free(starts); free(ends);
  return ok;
}


int test_decompress(void) {
  int ok = 1;
  ok &= test_rtc_basic();
  ok &= test_rtc_large();
  ok &= test_rti2_basic();
  ok &= test_rti2_large();
  return ok;
}
