/*
 * Rainbow Crackalack: ppi.h
 *
 * Shared definition of precomputed_and_potential_indices used by the
 * lookup pipeline and the false-alarm batching module.
 */
#ifndef _PPI_H
#define _PPI_H

#include <stdint.h>
#include <stddef.h>

#include "gpu_backend.h"

struct _precomputed_and_potential_indices {
  char                                       *username;  /* Non-NULL if loaded file format is pwdump. */
  char                                       *hash;
  gpu_ulong                                  *precomputed_end_indices;
  gpu_uint                                    num_precomputed_end_indices;

  gpu_ulong                                  *potential_start_indices;
  size_t                                      num_potential_start_indices;
  size_t                                      potential_start_indices_size;
  unsigned int                               *potential_start_index_positions; /* Buffer size is always num_potential_start_indices. */

  char                                       *plaintext;        /* Set if hash is cracked. */
  struct _precomputed_and_potential_indices  *next;
};

typedef struct _precomputed_and_potential_indices precomputed_and_potential_indices;

#endif /* _PPI_H */
