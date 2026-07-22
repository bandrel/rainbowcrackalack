/*
 * Rainbow Crackalack: zst_decompress.c
 * Copyright (C) 2026  Justin Bollinger
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <zstd.h>
#include "zst_decompress.h"

int zst_decompress(char *filename, uint64_t **uncompressed_table, uint64_t *num_chains) {
  FILE *f = NULL;
  unsigned char *cbuf = NULL;
  void *dbuf = NULL;
  long csize = 0;
  unsigned long long dsize = 0;
  size_t r = 0;

  *uncompressed_table = NULL;
  *num_chains = 0;

  f = fopen(filename, "rb");
  if (f == NULL) { fprintf(stderr, "zst_decompress: cannot open %s\n", filename); return 1; }
  fseek(f, 0, SEEK_END); csize = ftell(f); fseek(f, 0, SEEK_SET);
  if (csize <= 0) { fprintf(stderr, "zst_decompress: empty/bad file %s\n", filename); fclose(f); return 2; }

  cbuf = malloc((size_t)csize);
  if (cbuf == NULL) { fclose(f); return 3; }
  if (fread(cbuf, 1, (size_t)csize, f) != (size_t)csize) { free(cbuf); fclose(f); return 4; }
  fclose(f);

  dsize = ZSTD_getFrameContentSize(cbuf, (size_t)csize);
  if (dsize == ZSTD_CONTENTSIZE_ERROR || dsize == ZSTD_CONTENTSIZE_UNKNOWN) {
    fprintf(stderr, "zst_decompress: not a valid zstd frame / unknown size: %s\n", filename);
    free(cbuf); return 5;
  }
  if ((dsize % (sizeof(uint64_t) * 2)) != 0) {
    fprintf(stderr, "zst_decompress: size %llu not a multiple of 16: %s\n", dsize, filename);
    free(cbuf); return 6;
  }

  dbuf = malloc((size_t)dsize);
  if (dbuf == NULL) { free(cbuf); return 7; }

  r = ZSTD_decompress(dbuf, (size_t)dsize, cbuf, (size_t)csize);
  free(cbuf);
  if (ZSTD_isError(r) || r != (size_t)dsize) {
    fprintf(stderr, "zst_decompress: decompress failed for %s: %s\n", filename,
            ZSTD_isError(r) ? ZSTD_getErrorName(r) : "size mismatch");
    free(dbuf); return 8;
  }

  *uncompressed_table = (uint64_t *)dbuf;
  *num_chains = (uint64_t)(dsize / (sizeof(uint64_t) * 2));
  return 0;
}
