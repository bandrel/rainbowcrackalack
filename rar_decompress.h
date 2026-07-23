#ifndef _RAR_DECOMPRESS_H
#define _RAR_DECOMPRESS_H

#include <stdint.h>

int rar_decompress(char *filename, uint64_t **uncompressed_table, unsigned int *num_chains);

#endif
