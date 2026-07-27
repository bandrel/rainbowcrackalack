/*
 * Rainbow Crackalack: zst_compress.c
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
#include <string.h>
#include <zstd.h>

#include "zst_compress.h"

#define CHAIN_BYTES (sizeof(uint64_t) * 2)

/* Sets up a compression context with the level, worker count, and pledged
 * source size.  Pledging is what puts the content size into the frame header;
 * zst_decompress() refuses frames without it. */
static ZSTD_CCtx *make_cctx(int level, int nb_workers, unsigned long long total_bytes) {
  ZSTD_CCtx *cctx = ZSTD_createCCtx();
  if (cctx == NULL)
    return NULL;

  if (ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level))) {
    ZSTD_freeCCtx(cctx);
    return NULL;
  }
  /* Worker threads are best-effort: libzstd built without threading rejects a
   * non-zero value, which is not a reason to fail the compression. */
  if (nb_workers > 0)
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nb_workers);

  if (ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(cctx, total_bytes))) {
    ZSTD_freeCCtx(cctx);
    return NULL;
  }
  return cctx;
}


/* Builds "<path>.tmp" in `tmp`, returning non-zero if it wouldn't fit. */
static int make_tmp_path(char *tmp, size_t tmp_size, const char *path) {
  int n = snprintf(tmp, tmp_size, "%s.tmp", path);
  return (n < 0 || (size_t)n >= tmp_size);
}


int zst_compress_buf(const void *table, size_t len_bytes, const char *zst_path,
                     int level, int nb_workers) {
  ZSTD_CCtx *cctx = NULL;
  FILE *out = NULL;
  void *obuf = NULL;
  size_t obuf_size = 0;
  char tmp_path[2048] = {0};
  ZSTD_inBuffer in;
  int ret = 0;

  if (table == NULL || zst_path == NULL || len_bytes == 0)
    return 1;
  if ((len_bytes % CHAIN_BYTES) != 0) {
    fprintf(stderr, "zst_compress: table size %zu is not a multiple of %zu\n",
            len_bytes, CHAIN_BYTES);
    return 2;
  }
  if (make_tmp_path(tmp_path, sizeof(tmp_path), zst_path))
    return 3;

  cctx = make_cctx(level, nb_workers, (unsigned long long)len_bytes);
  if (cctx == NULL) { ret = 4; goto done; }

  obuf_size = ZSTD_CStreamOutSize();
  obuf = malloc(obuf_size);
  if (obuf == NULL) { ret = 5; goto done; }

  out = fopen(tmp_path, "wb");
  if (out == NULL) {
    fprintf(stderr, "zst_compress: cannot open %s for writing\n", tmp_path);
    ret = 6; goto done;
  }

  in.src = table;
  in.size = len_bytes;
  in.pos = 0;

  for (;;) {
    ZSTD_outBuffer o = { obuf, obuf_size, 0 };
    size_t remaining = ZSTD_compressStream2(cctx, &o, &in, ZSTD_e_end);
    if (ZSTD_isError(remaining)) {
      fprintf(stderr, "zst_compress: %s\n", ZSTD_getErrorName(remaining));
      ret = 7; goto done;
    }
    if (o.pos > 0 && fwrite(obuf, 1, o.pos, out) != o.pos) {
      fprintf(stderr, "zst_compress: short write to %s\n", tmp_path);
      ret = 8; goto done;
    }
    if (remaining == 0)
      break;
  }

  if (fclose(out) != 0) {
    out = NULL;
    fprintf(stderr, "zst_compress: failed to close %s\n", tmp_path);
    ret = 9; goto done;
  }
  out = NULL;

  if (rename(tmp_path, zst_path) != 0) {
    fprintf(stderr, "zst_compress: failed to rename %s -> %s\n", tmp_path, zst_path);
    ret = 10; goto done;
  }
  tmp_path[0] = '\0';  /* Renamed away; nothing to clean up. */

done:
  if (out != NULL)
    fclose(out);
  if (tmp_path[0] != '\0')
    remove(tmp_path);
  free(obuf);
  if (cctx != NULL)
    ZSTD_freeCCtx(cctx);
  return ret;
}


int zst_compress(const char *rt_path, const char *zst_path, int level, int nb_workers,
                 uint64_t *num_chains) {
  ZSTD_CCtx *cctx = NULL;
  FILE *in = NULL, *out = NULL;
  void *ibuf = NULL, *obuf = NULL;
  size_t ibuf_size = 0, obuf_size = 0;
  char tmp_path[2048] = {0};
  long long total = 0;
  long long consumed = 0;
  int ret = 0;

  if (num_chains != NULL)
    *num_chains = 0;
  if (rt_path == NULL || zst_path == NULL)
    return 1;

  in = fopen(rt_path, "rb");
  if (in == NULL) {
    fprintf(stderr, "zst_compress: cannot open %s\n", rt_path);
    return 2;
  }
  if (fseeko(in, 0, SEEK_END) != 0) { ret = 3; goto done; }
  total = (long long)ftello(in);
  if (fseeko(in, 0, SEEK_SET) != 0) { ret = 3; goto done; }

  if (total <= 0 || ((size_t)total % CHAIN_BYTES) != 0) {
    fprintf(stderr, "zst_compress: %s has size %lld, not a positive multiple of %zu\n",
            rt_path, total, CHAIN_BYTES);
    ret = 4; goto done;
  }
  if (make_tmp_path(tmp_path, sizeof(tmp_path), zst_path)) { ret = 5; goto done; }

  cctx = make_cctx(level, nb_workers, (unsigned long long)total);
  if (cctx == NULL) { ret = 6; goto done; }

  ibuf_size = ZSTD_CStreamInSize();
  obuf_size = ZSTD_CStreamOutSize();
  ibuf = malloc(ibuf_size);
  obuf = malloc(obuf_size);
  if (ibuf == NULL || obuf == NULL) { ret = 7; goto done; }

  out = fopen(tmp_path, "wb");
  if (out == NULL) {
    fprintf(stderr, "zst_compress: cannot open %s for writing\n", tmp_path);
    ret = 8; goto done;
  }

  for (;;) {
    size_t nread = fread(ibuf, 1, ibuf_size, in);
    int last;
    ZSTD_inBuffer ib = { ibuf, nread, 0 };

    if (nread == 0 && ferror(in)) {
      fprintf(stderr, "zst_compress: read error on %s\n", rt_path);
      ret = 9; goto done;
    }
    consumed += (long long)nread;
    last = (consumed >= total);

    /* Reaching EOF before we've consumed `total` bytes means the file
     * shrank out from under us between the size probe and this read (or
     * some other short-read condition without errno set).  Treat it as a
     * hard error instead of spinning: a zero-byte read with `last` still
     * false would otherwise never advance `consumed`, so this loop would
     * never terminate. */
    if (zst_is_premature_eof(nread, last)) {
      fprintf(stderr,
              "zst_compress: unexpected EOF on %s after %lld of %lld bytes\n",
              rt_path, consumed, total);
      ret = 15; goto done;
    }

    for (;;) {
      ZSTD_outBuffer o = { obuf, obuf_size, 0 };
      size_t remaining = ZSTD_compressStream2(cctx, &o, &ib,
                                              last ? ZSTD_e_end : ZSTD_e_continue);
      if (ZSTD_isError(remaining)) {
        fprintf(stderr, "zst_compress: %s\n", ZSTD_getErrorName(remaining));
        ret = 10; goto done;
      }
      if (o.pos > 0 && fwrite(obuf, 1, o.pos, out) != o.pos) {
        fprintf(stderr, "zst_compress: short write to %s\n", tmp_path);
        ret = 11; goto done;
      }
      if (last ? (remaining == 0) : (ib.pos == ib.size))
        break;
    }
    if (last)
      break;
  }

  if (consumed != total) {
    fprintf(stderr, "zst_compress: read %lld of %lld bytes from %s\n",
            consumed, total, rt_path);
    ret = 12; goto done;
  }

  if (fclose(out) != 0) {
    out = NULL;
    fprintf(stderr, "zst_compress: failed to close %s\n", tmp_path);
    ret = 13; goto done;
  }
  out = NULL;

  if (rename(tmp_path, zst_path) != 0) {
    fprintf(stderr, "zst_compress: failed to rename %s -> %s\n", tmp_path, zst_path);
    ret = 14; goto done;
  }
  tmp_path[0] = '\0';

  if (num_chains != NULL)
    *num_chains = (uint64_t)((size_t)total / CHAIN_BYTES);

done:
  if (in != NULL)
    fclose(in);
  if (out != NULL)
    fclose(out);
  if (tmp_path[0] != '\0')
    remove(tmp_path);
  free(ibuf);
  free(obuf);
  if (cctx != NULL)
    ZSTD_freeCCtx(cctx);
  return ret;
}
