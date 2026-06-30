/*
 * Rainbow Crackalack: fa_batch.h
 *
 * Accumulator for false-alarm candidates pooled across many rainbow
 * tables.  Pooling candidates raises GPU occupancy on the false-alarm
 * dispatch by 1-2 orders of magnitude.
 */
#ifndef _FA_BATCH_H
#define _FA_BATCH_H

#include <stdint.h>

#include "shared.h"

#include "ppi.h"

typedef struct {
  /* Flat candidate arrays, parallel to one another. */
  uint64_t          *start_indices;
  unsigned int      *start_index_positions;
  uint64_t          *hash_base_indices;
  precomputed_and_potential_indices **ppi_refs;

  unsigned int       num_candidates;
  unsigned int       capacity;

  /* Number of tables whose candidates have been appended since last reset. */
  unsigned int       tables_in_batch;

  /* Flush threshold (candidate count).  1 disables batching. */
  unsigned int       flush_threshold;
} fa_batch_t;

/* Initialize an empty batch with the given flush threshold and initial
 * capacity hint (the batch grows geometrically beyond this).  Returns 0
 * on success, -1 on allocation failure. */
int  fa_batch_init(fa_batch_t *b, unsigned int flush_threshold, unsigned int initial_capacity);

/* Free internal arrays.  Safe to call on a zero-initialized batch. */
void fa_batch_free(fa_batch_t *b);

/* Reset to empty without freeing the backing arrays (keeps capacity). */
void fa_batch_reset(fa_batch_t *b);

/* Append all of `ppi_head`'s currently-collected potential start indices
 * (across all uncracked hashes) into the batch.  Computes hash_base_index
 * for each hash once.  Returns 0 on success, -1 on allocation failure.
 *
 * `reduction_offset` and `plaintext_space_total` are the current config
 * group's values, used to compute hash_base_index. */
int  fa_batch_append(fa_batch_t *b,
                     precomputed_and_potential_indices *ppi_head,
                     unsigned int reduction_offset,
                     uint64_t plaintext_space_total);

/* True when the batch is large enough to flush, or when `force` is set
 * AND the batch is non-empty.  An empty batch never flushes.
 * Caller passes `force=1` at end-of-config-group / all-cracked. */
int  fa_batch_should_flush(const fa_batch_t *b, int force);

/* Sort the batch's parallel arrays in place by start_index_positions
 * (ascending).  Reduces GPU warp divergence by clustering candidates
 * with similar chain-walk lengths into the same warp.  Safe to call
 * on an empty batch (no-op). */
void fa_batch_sort_by_position(fa_batch_t *b);

/* Map a per-device false-alarm RESULT index to the absolute CANDIDATE index in
 * the harvest ppi_refs snapshot.  Each GPU device writes its results at
 * ABSOLUTE candidate positions (positions outside its stride stay UINT64_MAX),
 * so the candidate index equals result_index regardless of which device
 * produced it -- deliberately NOT a running counter across devices, which would
 * climb to total_devices*num_candidates and overrun the num_candidates-entry
 * snapshot (heap corruption).  Returns 1 and sets *out_index when result_index
 * is in bounds (< num_candidates); returns 0 (caller must skip) when out of
 * bounds.  Note: there is intentionally NO device parameter -- the mapping must
 * not depend on device order. */
int fa_harvest_candidate_index(unsigned int result_index,
                               unsigned int num_candidates,
                               unsigned int *out_index);

#endif /* _FA_BATCH_H */
