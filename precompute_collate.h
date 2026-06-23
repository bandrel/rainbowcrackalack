/*
 * Rainbow Crackalack: precompute_collate.h
 *
 * Pure (GPU-free) collation of batched-precompute results into the
 * column-aligned endpoint layout the false-alarm phase expects.  Lives in its
 * own translation unit so it can be linked into both crackalack_lookup and the
 * unit-test binary and exercised with known values.
 */
#ifndef _PRECOMPUTE_COLLATE_H
#define _PRECOMPUTE_COLLATE_H

#include <stdint.h>

/* Sentinel written into endpoint slots that have no real source endpoint.
 * Real endpoints are smaller than the plaintext space (< 2^56 in practice), so
 * UINT64_MAX never collides with a genuine endpoint during binary search. */
#define PRECOMPUTE_COLLATE_SENTINEL UINT64_MAX

/* Remap one hash's batched-precompute raw output into a column-aligned endpoint
 * array.
 *
 * The batched precompute kernels emit the endpoint for chain column
 * (positions_per_hash - 2 - p) at output index p -- i.e. REVERSE column order.
 * The false-alarm kernel reconstructs each candidate chain to a depth equal to
 * the endpoint's ARRAY INDEX, so the array must be indexed by chain column to
 * match the single-hash (non-batch) path.  This function performs that remap:
 *
 *   out[positions_per_hash - 2 - p] = hash_output[p]   for nonzero entries,
 *                                                       p in [0, positions_per_hash-2]
 *
 * Every other slot (the final column, and any zeroed source position) is filled
 * with PRECOMPUTE_COLLATE_SENTINEL.  out must hold positions_per_hash entries.
 *
 * Getting this wrong (e.g. a straight-through copy out[p] = hash_output[p])
 * mirrors the index and makes the false-alarm walk stop at the wrong depth, so
 * every batched (>=2-hash) crack is silently dropped.
 */
void collate_batched_precompute_endpoints(const uint64_t *hash_output,
                                          unsigned int positions_per_hash,
                                          uint64_t *out);

#endif /* _PRECOMPUTE_COLLATE_H */
