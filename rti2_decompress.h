#ifndef _RTI2_DECOMPRESS_H
#define _RTI2_DECOMPRESS_H

#include <stdint.h>

int rti2_decompress(char *filename, uint64_t **uncompressed_table, uint64_t *num_chains);

#endif
