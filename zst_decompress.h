#ifndef _ZST_DECOMPRESS_H
#define _ZST_DECOMPRESS_H
#include <stdint.h>

/* Decompresses a .rt.zst (a zstd frame wrapping a raw rainbow table of
 * (start,end) uint64 pairs) fully into memory.  Mirrors rtc_decompress():
 * on success allocates *uncompressed_table (caller frees) and sets
 * *num_chains = decompressed_bytes / 16.  Returns 0 on success, non-zero on
 * error. */
int zst_decompress(char *filename, uint64_t **uncompressed_table, uint64_t *num_chains);

#endif
