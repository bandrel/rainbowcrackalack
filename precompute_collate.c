/*
 * Rainbow Crackalack: precompute_collate.c
 *
 * See precompute_collate.h for the contract.  Kept free of any GPU backend
 * dependency so it can be unit-tested on the CPU.
 */
#include "precompute_collate.h"

void collate_batched_precompute_endpoints(const uint64_t *hash_output,
                                          unsigned int positions_per_hash,
                                          uint64_t *out) {
  unsigned int p;

  for (p = 0; p < positions_per_hash; p++)
    out[p] = PRECOMPUTE_COLLATE_SENTINEL;

  /* p + 2 <= positions_per_hash  =>  column = positions_per_hash - 2 - p >= 0. */
  for (p = 0; p + 2 <= positions_per_hash; p++) {
    if (hash_output[p] != 0)
      out[positions_per_hash - 2 - p] = hash_output[p];
  }
}
