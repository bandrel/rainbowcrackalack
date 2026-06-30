/*
 * Rainbow Crackalack: fa_batch.c
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_rt_functions.h"  /* hash_to_index */
#include "test_shared.h"       /* hex_to_bytes */
#include "fa_batch.h"
#include "shared.h"

/* precomputed_and_potential_indices definition lives in ppi.h. */

/* Forward declaration: grow_to calls fa_batch_free on partial failure. */
void fa_batch_free(fa_batch_t *b);

#define MIN_CAPACITY 1024

static int grow_to(fa_batch_t *b, unsigned int needed) {
  if (needed <= b->capacity) return 0;
  unsigned int new_cap = (b->capacity == 0) ? MIN_CAPACITY : b->capacity;
  while (new_cap < needed) new_cap *= 2;

  uint64_t     *si = realloc(b->start_indices,         new_cap * sizeof(*si));
  unsigned int *sp = realloc(b->start_index_positions, new_cap * sizeof(*sp));
  uint64_t     *hb = realloc(b->hash_base_indices,     new_cap * sizeof(*hb));
  precomputed_and_potential_indices **pr =
                  realloc(b->ppi_refs,                 new_cap * sizeof(*pr));

  /* Each successful realloc has consumed the previous pointer in b->*.
   * Write back so the struct never holds a dangling reference, then on
   * partial failure free everything and zero the struct. */
  if (si) b->start_indices         = si;
  if (sp) b->start_index_positions = sp;
  if (hb) b->hash_base_indices     = hb;
  if (pr) b->ppi_refs              = pr;

  if (!si || !sp || !hb || !pr) {
    fa_batch_free(b);
    return -1;
  }

  b->capacity = new_cap;
  return 0;
}

int fa_batch_init(fa_batch_t *b, unsigned int flush_threshold, unsigned int initial_capacity) {
  memset(b, 0, sizeof(*b));
  b->flush_threshold = (flush_threshold == 0) ? 16384 : flush_threshold;
  if (initial_capacity > 0)
    return grow_to(b, initial_capacity);
  return 0;
}

void fa_batch_free(fa_batch_t *b) {
  if (!b) return;
  free(b->start_indices);
  free(b->start_index_positions);
  free(b->hash_base_indices);
  free(b->ppi_refs);
  memset(b, 0, sizeof(*b));
}

void fa_batch_reset(fa_batch_t *b) {
  b->num_candidates  = 0;
  b->tables_in_batch = 0;
}

int fa_batch_append(fa_batch_t *b,
                    precomputed_and_potential_indices *ppi_head,
                    unsigned int reduction_offset,
                    uint64_t plaintext_space_total) {
  /* Count how many candidates this table contributes (uncracked only). */
  unsigned int incoming = 0;
  for (precomputed_and_potential_indices *p = ppi_head; p; p = p->next) {
    if (p->plaintext == NULL)
      incoming += (unsigned int)p->num_potential_start_indices;
  }

  b->tables_in_batch++;
  if (incoming == 0)
    return 0;

  if (grow_to(b, b->num_candidates + incoming) != 0)
    return -1;

  for (precomputed_and_potential_indices *p = ppi_head; p; p = p->next) {
    if (p->plaintext != NULL) continue;
    if (p->num_potential_start_indices == 0) continue;

    /* Compute hash_base_index once per hash. */
    unsigned char hash[MAX_HASH_OUTPUT_LEN] = {0};
    unsigned int  hash_len = hex_to_bytes(p->hash, sizeof(hash), hash);
    uint64_t      hbi = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, 0);

    for (size_t i = 0; i < p->num_potential_start_indices; i++) {
      unsigned int j = b->num_candidates++;
      b->start_indices[j]         = p->potential_start_indices[i];
      b->start_index_positions[j] = p->potential_start_index_positions[i];
      b->hash_base_indices[j]     = hbi;
      b->ppi_refs[j]              = p;
    }
  }
  return 0;
}

int fa_batch_should_flush(const fa_batch_t *b, int force) {
  if (force && b->num_candidates > 0) return 1;
  if (b->flush_threshold <= 1)        return b->num_candidates > 0;
  return b->num_candidates >= b->flush_threshold;
}

/* Packed entry for the sort.  Keeps the four parallel arrays aligned
 * by reordering all four together. */
typedef struct {
  uint64_t      start_index;
  unsigned int  position;
  uint64_t      hash_base_index;
  precomputed_and_potential_indices *ppi_ref;
} fa_batch_entry;

static int fa_batch_entry_cmp(const void *a, const void *b) {
  unsigned int pa = ((const fa_batch_entry *)a)->position;
  unsigned int pb = ((const fa_batch_entry *)b)->position;
  if (pa < pb) return -1;
  if (pa > pb) return  1;
  return 0;
}

int fa_harvest_candidate_index(unsigned int result_index,
                               unsigned int num_candidates,
                               unsigned int *out_index) {
  if (result_index >= num_candidates)
    return 0;
  *out_index = result_index;
  return 1;
}

void fa_batch_sort_by_position(fa_batch_t *b) {
  if (b->num_candidates < 2) return;

  fa_batch_entry *tmp = malloc(b->num_candidates * sizeof(*tmp));
  if (tmp == NULL) return;  /* skip sort on alloc failure; correctness unaffected */

  for (unsigned int i = 0; i < b->num_candidates; i++) {
    tmp[i].start_index     = b->start_indices[i];
    tmp[i].position        = b->start_index_positions[i];
    tmp[i].hash_base_index = b->hash_base_indices[i];
    tmp[i].ppi_ref         = b->ppi_refs[i];
  }

  qsort(tmp, b->num_candidates, sizeof(*tmp), fa_batch_entry_cmp);

  for (unsigned int i = 0; i < b->num_candidates; i++) {
    b->start_indices[i]         = tmp[i].start_index;
    b->start_index_positions[i] = tmp[i].position;
    b->hash_base_indices[i]     = tmp[i].hash_base_index;
    b->ppi_refs[i]              = tmp[i].ppi_ref;
  }

  free(tmp);
}
