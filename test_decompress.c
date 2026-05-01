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


int test_decompress(void) {
  int ok = 1;
  ok &= test_rtc_basic();
  ok &= test_rtc_large();
  return ok;
}
