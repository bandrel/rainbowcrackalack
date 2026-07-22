#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zstd.h>
#include "test_zst.h"
#include "zst_decompress.h"

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
