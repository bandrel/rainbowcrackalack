/*
 * Rainbow Crackalack: zst_compress.h
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

#ifndef _ZST_COMPRESS_H
#define _ZST_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

/* Default compression level used by every caller that doesn't specify one. */
#define ZST_DEFAULT_LEVEL 19

/* Compresses the raw rainbow table at `rt_path` into a zstd frame at
 * `zst_path`.  Streams the input so memory use stays flat regardless of table
 * size, but pledges the total source size up front so the frame header carries
 * the content size (zst_decompress() requires it).
 *
 * `level` is a zstd compression level (1..22); pass ZST_DEFAULT_LEVEL for 19.
 * `nb_workers` is the zstd worker-thread count; 0 means single-threaded.
 *
 * On success writes `zst_path` atomically (temp file + rename), sets
 * *num_chains to input_bytes / 16, and returns 0.  The source file is never
 * removed.  Returns non-zero on error, leaving no partial output behind. */
int zst_compress(const char *rt_path, const char *zst_path, int level, int nb_workers,
                 uint64_t *num_chains);

/* Same, for a table already in memory.  `len_bytes` must be a multiple of 16. */
int zst_compress_buf(const void *table, size_t len_bytes, const char *zst_path,
                     int level, int nb_workers);

/* True iff a zero-byte fread() arrived before `total` bytes were consumed,
 * i.e. the source shrank/hit EOF earlier than its probed size promised.
 * zst_compress()'s streaming read loop uses this to fail cleanly instead of
 * spinning forever: without it, a premature EOF leaves `consumed < total`
 * forever, `last` never becomes true, and the outer loop never terminates.
 *
 * Exposed here (rather than kept static in zst_compress.c) so the
 * regression test can exercise this exact boundary condition directly;
 * reproducing a real mid-read truncation deterministically, without racing
 * zst_compress()'s internals from another process/thread, isn't possible
 * from outside the function. */
static inline int zst_is_premature_eof(size_t nread, int last) {
  return (nread == 0 && !last);
}

#endif
