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
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include "zst_decompress.h"

int zst_decompress(char *filename, uint64_t **uncompressed_table, uint64_t *num_chains) {
  FILE *f = NULL;
  ZSTD_DCtx *dctx = NULL;
  void *ibuf = NULL, *dbuf = NULL;
  size_t ibuf_size = 0;
  unsigned char hdr[ZSTD_FRAMEHEADERSIZE_MAX];
  size_t hdr_read = 0;
  unsigned long long dsize = 0;
  ZSTD_outBuffer out;
  int ret = 0;

  *uncompressed_table = NULL;
  *num_chains = 0;

  f = fopen(filename, "rb");
  if (f == NULL) { fprintf(stderr, "zst_decompress: cannot open %s\n", filename); return 1; }

  hdr_read = fread(hdr, 1, sizeof(hdr), f);
  if (hdr_read == 0) {
    fprintf(stderr, "zst_decompress: empty/bad file %s\n", filename);
    ret = 2; goto done;
  }
  dsize = ZSTD_getFrameContentSize(hdr, hdr_read);
  if (dsize == ZSTD_CONTENTSIZE_ERROR || dsize == ZSTD_CONTENTSIZE_UNKNOWN) {
    fprintf(stderr, "zst_decompress: not a valid zstd frame / unknown size: %s\n", filename);
    ret = 5; goto done;
  }
  if (dsize == 0 || (dsize % (sizeof(uint64_t) * 2)) != 0) {
    fprintf(stderr, "zst_decompress: size %llu not a positive multiple of 16: %s\n",
            dsize, filename);
    ret = 6; goto done;
  }

  dbuf = malloc((size_t)dsize);
  if (dbuf == NULL) { ret = 7; goto done; }

  dctx = ZSTD_createDCtx();
  if (dctx == NULL) { ret = 3; goto done; }

  ibuf_size = ZSTD_DStreamInSize();
  ibuf = malloc(ibuf_size);
  if (ibuf == NULL) { ret = 3; goto done; }

  out.dst = dbuf; out.size = (size_t)dsize; out.pos = 0;

  /* The header bytes already read are the first input chunk. */
  {
    ZSTD_inBuffer in = { hdr, hdr_read, 0 };
    for (;;) {
      size_t r = ZSTD_decompressStream(dctx, &out, &in);
      if (ZSTD_isError(r)) {
        fprintf(stderr, "zst_decompress: %s: %s\n", filename, ZSTD_getErrorName(r));
        ret = 8; goto done;
      }
      if (in.pos == in.size)
        break;
    }
  }

  while (out.pos < out.size) {
    size_t nread = fread(ibuf, 1, ibuf_size, f);
    ZSTD_inBuffer in = { ibuf, nread, 0 };
    if (nread == 0) {
      fprintf(stderr, "zst_decompress: truncated frame in %s\n", filename);
      ret = 8; goto done;
    }
    while (in.pos < in.size && out.pos < out.size) {
      size_t r = ZSTD_decompressStream(dctx, &out, &in);
      if (ZSTD_isError(r)) {
        fprintf(stderr, "zst_decompress: %s: %s\n", filename, ZSTD_getErrorName(r));
        ret = 8; goto done;
      }
    }
  }

  if (out.pos != (size_t)dsize) {
    fprintf(stderr, "zst_decompress: got %zu of %llu bytes from %s\n",
            out.pos, dsize, filename);
    ret = 8; goto done;
  }

  *uncompressed_table = (uint64_t *)dbuf;
  *num_chains = (uint64_t)(dsize / (sizeof(uint64_t) * 2));
  dbuf = NULL;  /* Ownership transferred to the caller. */

done:
  if (f != NULL) fclose(f);
  if (dctx != NULL) ZSTD_freeDCtx(dctx);
  free(ibuf);
  free(dbuf);
  return ret;
}
