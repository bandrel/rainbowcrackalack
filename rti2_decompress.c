/*
 * Rainbow Crackalack: rti2_decompress.c
 * Copyright (C) 2018-2019  Joe Testa <jtesta@positronsecurity.com>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "rti2_decompress.h"


typedef struct __attribute__((packed)) {
  uint32_t tag;              /* "RTI2" = 0x32495452 */
  uint8_t  minor;            /* Must be 0 */
  uint8_t  startPointBits;
  uint8_t  endPointBits;
  uint8_t  checkPointBits;
  uint32_t fileIndex;
  uint32_t files;
  uint64_t minimumStartPoint;
  uint32_t chainLength;
  uint32_t tableIndex;
  uint8_t  algorithm;
  uint8_t  reductionFunction;
} rti2_header;


/* Reads and discards the variable-length sub key space section of the header. */
static int skip_subkeyspaces(FILE *f) {
  uint8_t num_subkeys = 0, num_hybrid = 0, pw_len = 0, flags = 0, sz = 0;

  if (fread(&num_subkeys, 1, 1, f) != 1)
    return -1;

  for (uint8_t sk = 0; sk < num_subkeys; sk++) {
    if (fread(&num_hybrid, 1, 1, f) != 1)
      return -1;

    for (uint8_t h = 0; h < num_hybrid; h++) {
      if (fread(&pw_len, 1, 1, f) != 1) return -1;
      if (fread(&flags, 1, 1, f) != 1) return -1;

      /* For each character width (1, 2, 3, 4 bytes), read size + data if flag set. */
      for (int width = 1; width <= 4; width++) {
        if (flags & (1 << (width - 1))) {
          if (fread(&sz, 1, 1, f) != 1) return -1;
          unsigned int actual_sz = width * (sz + 1);
          if (fseek(f, actual_sz, SEEK_CUR) != 0) return -1;
        }
      }
    }
  }

  return 0;
}


/* Decompresses an RTI2 file and returns a pointer to the rainbow table (as
 * alternating start/end uint64_t pairs), along with the number of chains.
 * Returns 0 on success, or a negative error code. */
int rti2_decompress(char *filename, uint64_t **ret_uncompressed_table, uint64_t *ret_num_chains) {
  FILE *f = NULL;
  int ret = 0;
  uint64_t *uncompressed_table = NULL;
  uint8_t *prefix_counts = NULL;
  uint8_t *chain_buf = NULL;

  *ret_uncompressed_table = NULL;
  *ret_num_chains = 0;

  f = fopen(filename, "rb");
  if (f == NULL) {
    fprintf(stderr, "Error: failed to open RTI2 file %s: %s\n", filename, strerror(errno));
    return -1;
  }

  /* Read the fixed header. */
  rti2_header hdr;
  if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
    fprintf(stderr, "Error reading RTI2 header: %s\n", filename);
    ret = -2; goto done;
  }

  if (hdr.tag != 0x32495452) {
    fprintf(stderr, "Error: %s is not a valid RTI2 file (bad magic).\n", filename);
    ret = -3; goto done;
  }

  if (hdr.minor != 0) {
    fprintf(stderr, "Error: unsupported RTI2 minor version %u in %s\n", hdr.minor, filename);
    ret = -4; goto done;
  }

  if (hdr.startPointBits == 0 || hdr.startPointBits > 64 ||
      hdr.endPointBits == 0 || hdr.endPointBits > 64 ||
      hdr.checkPointBits > 64 ||
      hdr.startPointBits + hdr.endPointBits + hdr.checkPointBits > 64) {
    fprintf(stderr, "Error: invalid bit widths in RTI2 header (SP=%u EP=%u CP=%u): %s\n",
            hdr.startPointBits, hdr.endPointBits, hdr.checkPointBits, filename);
    ret = -5; goto done;
  }

  /* Skip salt (only for certain algorithms). */
  if (hdr.algorithm == 0 || (hdr.algorithm >= 15 && hdr.algorithm <= 19)) {
    uint8_t salt_len = 0;
    if (fread(&salt_len, 1, 1, f) != 1) { ret = -6; goto done; }
    if (fseek(f, salt_len, SEEK_CUR) != 0) { ret = -6; goto done; }
  }

  /* Skip sub key spaces. */
  if (skip_subkeyspaces(f) != 0) {
    fprintf(stderr, "Error parsing sub key spaces in %s\n", filename);
    ret = -7; goto done;
  }

  /* Skip checkpoint positions (4 bytes each, checkPointBits entries). */
  if (hdr.checkPointBits > 0) {
    if (fseek(f, 4 * hdr.checkPointBits, SEEK_CUR) != 0) {
      ret = -8; goto done;
    }
  }

  /* Read index section. */
  uint64_t firstPrefix = 0;
  uint32_t numPrefixIndexes = 0;

  if (fread(&firstPrefix, sizeof(uint64_t), 1, f) != 1 ||
      fread(&numPrefixIndexes, sizeof(uint32_t), 1, f) != 1) {
    fprintf(stderr, "Error reading RTI2 index header: %s\n", filename);
    ret = -9; goto done;
  }

  prefix_counts = malloc(numPrefixIndexes);
  if (prefix_counts == NULL) {
    fprintf(stderr, "Error allocating %u bytes for RTI2 prefix index.\n", numPrefixIndexes);
    ret = -10; goto done;
  }

  if (fread(prefix_counts, 1, numPrefixIndexes, f) != numPrefixIndexes) {
    fprintf(stderr, "Error reading RTI2 prefix index: %s\n", filename);
    ret = -11; goto done;
  }

  /* Count total chains. */
  uint64_t num_chains = 0;
  for (uint32_t i = 0; i < numPrefixIndexes; i++)
    num_chains += prefix_counts[i];

  if (num_chains == 0) {
    fprintf(stderr, "Warning: RTI2 file contains 0 chains: %s\n", filename);
    ret = 0; goto done;
  }

  /* Allocate uncompressed table (start/end pairs). */
  uncompressed_table = calloc((size_t)num_chains, sizeof(uint64_t) * 2);
  if (uncompressed_table == NULL) {
    fprintf(stderr, "Error allocating %"PRIu64" bytes for RTI2 decompression.\n",
            num_chains * sizeof(uint64_t) * 2);
    ret = -12; goto done;
  }

  /* Decompress chain data. */
  unsigned int chainSizeBytes = (hdr.startPointBits + hdr.endPointBits + hdr.checkPointBits + 7) / 8;
  uint64_t epMask = ((uint64_t)1 << hdr.endPointBits) - 1;
  uint64_t spMask = ((uint64_t)1 << hdr.startPointBits) - 1;
  uint32_t spShift = hdr.endPointBits;

  /* Bulk-read all chain rows in a single I/O.  Avoids one fread() per chain. */
  size_t chains_bytes = (size_t)num_chains * chainSizeBytes;
  if (chainSizeBytes != 0 && chains_bytes / chainSizeBytes != num_chains) {
    fprintf(stderr, "Error: RTI2 chain byte count overflow (num_chains=%"PRIu64", chainSizeBytes=%u).\n",
            num_chains, chainSizeBytes);
    ret = -13; goto done;
  }

  chain_buf = malloc(chains_bytes);
  if (chain_buf == NULL) {
    fprintf(stderr, "Error allocating %zu bytes for RTI2 chain buffer.\n", chains_bytes);
    ret = -13; goto done;
  }

  if (fread(chain_buf, 1, chains_bytes, f) != chains_bytes) {
    fprintf(stderr, "Error reading RTI2 chain data: %s\n", filename);
    ret = -13; goto done;
  }

  uint64_t table_idx = 0;
  size_t buf_off = 0;

  for (uint32_t p = 0; p < numPrefixIndexes; p++) {
    uint64_t epPrefix = (firstPrefix + p) << hdr.endPointBits;

    for (uint8_t c = 0; c < prefix_counts[p]; c++) {
      uint64_t chainrow = 0;
      memcpy(&chainrow, chain_buf + buf_off, chainSizeBytes);
      buf_off += chainSizeBytes;

      uint64_t ep = epPrefix | (chainrow & epMask);
      uint64_t sp = ((chainrow >> spShift) & spMask) + hdr.minimumStartPoint;

      uncompressed_table[table_idx * 2]     = sp;
      uncompressed_table[table_idx * 2 + 1] = ep;
      table_idx++;
    }
  }

done:
  if (prefix_counts != NULL)
    free(prefix_counts);
  if (chain_buf != NULL) {
    free(chain_buf);
    chain_buf = NULL;
  }
  if (f != NULL)
    fclose(f);
  if (ret != 0 && uncompressed_table != NULL) {
    free(uncompressed_table);
    uncompressed_table = NULL;
    num_chains = 0;
  }

  *ret_uncompressed_table = uncompressed_table;
  *ret_num_chains = num_chains;
  return ret;
}
