#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <zstd.h>
#include "test_zst.h"
#include "zst_decompress.h"
#include "zst_compress.h"

int test_zst(void) {
  uint64_t table[8] = {0,10, 1,20, 2,30, 3,40};   /* 4 chains = 64 bytes */
  size_t rawbytes = sizeof(table);

  size_t cbound = ZSTD_compressBound(rawbytes);
  void *cbuf = malloc(cbound);
  size_t csize = ZSTD_compress(cbuf, cbound, table, rawbytes, 19);
  if (ZSTD_isError(csize)) { printf("test_zst: ZSTD_compress failed\n"); free(cbuf); return 0; }

  const char *fn = "test_zst_tmp.rt.zst";
  FILE *f = fopen(fn, "wb");
  if (f == NULL) { printf("test_zst: cannot write temp\n"); free(cbuf); return 0; }
  fwrite(cbuf, 1, csize, f); fclose(f); free(cbuf);

  uint64_t *out = NULL, nchains = 0;
  int rc = zst_decompress((char *)fn, &out, &nchains);
  remove(fn);

  if (rc != 0)      { printf("test_zst: zst_decompress rc=%d\n", rc); return 0; }
  if (nchains != 4) { printf("test_zst: num_chains=%llu (want 4)\n", (unsigned long long)nchains); free(out); return 0; }
  if (memcmp(out, table, rawbytes) != 0) { printf("test_zst: data mismatch\n"); free(out); return 0; }
  free(out);
  return 1;
}

/* Round trip: compress an in-memory table, decompress it, expect byte identity. */
int test_zst_compress_roundtrip(void) {
  uint64_t table[8] = {0,10, 1,20, 2,30, 3,40};
  const char *fn = "test_zst_rt_tmp.rt.zst";
  uint64_t *out = NULL, nchains = 0;
  int rc;

  if (zst_compress_buf(table, sizeof(table), fn, ZST_DEFAULT_LEVEL, 0) != 0) {
    printf("test_zst_compress_roundtrip: compress failed\n");
    return 0;
  }

  rc = zst_decompress((char *)fn, &out, &nchains);
  remove(fn);
  if (rc != 0) { printf("test_zst_compress_roundtrip: decompress rc=%d\n", rc); return 0; }
  if (nchains != 4) {
    printf("test_zst_compress_roundtrip: num_chains=%llu (want 4)\n", (unsigned long long)nchains);
    free(out); return 0;
  }
  if (memcmp(out, table, sizeof(table)) != 0) {
    printf("test_zst_compress_roundtrip: data mismatch\n");
    free(out); return 0;
  }
  free(out);
  return 1;
}

/* The frame we write must declare its content size, or zst_decompress()
 * rejects it.  This guards the pledged-source-size call in zst_compress_buf. */
int test_zst_compress_declares_size(void) {
  uint64_t table[8] = {0,10, 1,20, 2,30, 3,40};
  const char *fn = "test_zst_size_tmp.rt.zst";
  unsigned char hdr[64];
  size_t n;
  unsigned long long declared;
  FILE *f;

  if (zst_compress_buf(table, sizeof(table), fn, ZST_DEFAULT_LEVEL, 0) != 0) {
    printf("test_zst_compress_declares_size: compress failed\n");
    return 0;
  }
  f = fopen(fn, "rb");
  if (f == NULL) { printf("test_zst_compress_declares_size: cannot reopen\n"); return 0; }
  n = fread(hdr, 1, sizeof(hdr), f);
  fclose(f);
  remove(fn);

  declared = ZSTD_getFrameContentSize(hdr, n);
  if (declared != (unsigned long long)sizeof(table)) {
    printf("test_zst_compress_declares_size: declared=%llu (want %zu)\n",
           declared, sizeof(table));
    return 0;
  }
  return 1;
}

/* A table length that isn't a whole number of 16-byte chains is corrupt input
 * and must be refused before any output file appears. */
int test_zst_compress_rejects_bad_size(void) {
  unsigned char junk[24] = {0};
  const char *fn = "test_zst_bad_tmp.rt.zst";
  struct stat st;

  if (zst_compress_buf(junk, sizeof(junk), fn, ZST_DEFAULT_LEVEL, 0) == 0) {
    printf("test_zst_compress_rejects_bad_size: accepted a 24-byte table\n");
    remove(fn);
    return 0;
  }
  if (stat(fn, &st) == 0) {
    printf("test_zst_compress_rejects_bad_size: left an output file behind\n");
    remove(fn);
    return 0;
  }
  return 1;
}

/* Regression guard for the hang fixed in zst_compress()'s streaming read
 * loop: a zero-byte fread() before `total` bytes were consumed (source
 * truncated between the size probe and the read loop) used to leave
 * `consumed` stuck below `total` forever, so `last` never became true and
 * the outer for(;;) spun without end.
 *
 * Reproducing the real race deterministically from a test (truncate the
 * file on disk at the exact instant zst_compress() is mid-read) isn't
 * possible without a hook into zst_compress()'s internals or a genuinely
 * racy test. zst_is_premature_eof() is exposed in zst_compress.h precisely
 * so this exact boundary condition can be exercised directly and
 * deterministically instead: this test asserts the predicate returns true
 * for the hang-triggering inputs and false for the two inputs the read
 * loop relies on it NOT firing for (a normal partial read, and the final
 * zero-byte read at legitimate EOF). Runtime is O(1); a regression here
 * fails immediately rather than hanging the suite. */
int test_zst_compress_short_read_guard(void) {
  /* The exact state that used to hang: no bytes read, but we haven't
   * consumed everything we expected yet. */
  if (!zst_is_premature_eof(0, 0)) {
    printf("test_zst_compress_short_read_guard: expected true for (nread=0, last=0)\n");
    return 0;
  }
  /* Legitimate final EOF: consumed reached total, so a trailing zero-byte
   * read is expected and must not be flagged. */
  if (zst_is_premature_eof(0, 1)) {
    printf("test_zst_compress_short_read_guard: expected false for (nread=0, last=1)\n");
    return 0;
  }
  /* Ordinary partial read mid-stream: must not be flagged. */
  if (zst_is_premature_eof(37, 0)) {
    printf("test_zst_compress_short_read_guard: expected false for (nread=37, last=0)\n");
    return 0;
  }
  return 1;
}
