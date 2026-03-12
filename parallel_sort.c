/*
 * Rainbow Crackalack: parallel_sort.c
 * Parallel chunk-sort + k-way merge for rainbow table chain arrays.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parallel_sort.h"

#define CHAIN_SIZE_U64 2


static int compare_by_end_index(const void *a, const void *b) {
  uint64_t end_a = ((const uint64_t *)a)[1];
  uint64_t end_b = ((const uint64_t *)b)[1];
  if (end_a < end_b) return -1;
  if (end_a > end_b) return 1;
  return 0;
}


typedef struct {
  uint64_t    *data;
  unsigned int start;
  unsigned int count;
} chunk_sort_arg_t;


static void *chunk_sort_worker(void *arg) {
  chunk_sort_arg_t *a = (chunk_sort_arg_t *)arg;
  qsort(a->data + (size_t)a->start * CHAIN_SIZE_U64,
        a->count,
        sizeof(uint64_t) * CHAIN_SIZE_U64,
        compare_by_end_index);
  return NULL;
}


static void merge_chunks(const uint64_t *src, uint64_t *dst,
                         unsigned int num_chains,
                         const unsigned int *chunk_starts,
                         const unsigned int *chunk_counts,
                         int num_chunks) {
  unsigned int *pos = malloc((size_t)num_chunks * sizeof(unsigned int));
  unsigned int out = 0;
  int i;

  for (i = 0; i < num_chunks; i++)
    pos[i] = 0;

  while (out < num_chains) {
    int best = -1;
    uint64_t best_end = UINT64_MAX;

    for (i = 0; i < num_chunks; i++) {
      if (pos[i] >= chunk_counts[i])
        continue;
      {
        unsigned int idx = chunk_starts[i] + pos[i];
        uint64_t end = src[idx * CHAIN_SIZE_U64 + 1];
        if (best == -1 || end < best_end) {
          best = i;
          best_end = end;
        }
      }
    }

    {
      unsigned int src_idx = chunk_starts[best] + pos[best];
      dst[out * CHAIN_SIZE_U64]     = src[src_idx * CHAIN_SIZE_U64];
      dst[out * CHAIN_SIZE_U64 + 1] = src[src_idx * CHAIN_SIZE_U64 + 1];
    }
    pos[best]++;
    out++;
  }

  free(pos);
}


int parallel_sort_rt(uint64_t *data, unsigned int num_chains, int num_threads) {
  unsigned int *chunk_starts = NULL;
  unsigned int *chunk_counts = NULL;
  chunk_sort_arg_t *args = NULL;
  pthread_t *threads = NULL;
  uint64_t *merged = NULL;
  unsigned int base_chunk, remainder, offset;
  int i, started;

  if (num_chains < 1024 || num_threads <= 1) {
    qsort(data, num_chains, sizeof(uint64_t) * CHAIN_SIZE_U64,
          compare_by_end_index);
    return 0;
  }

  if ((unsigned int)num_threads > num_chains)
    num_threads = (int)num_chains;

  chunk_starts = malloc((size_t)num_threads * sizeof(unsigned int));
  chunk_counts = malloc((size_t)num_threads * sizeof(unsigned int));
  args         = malloc((size_t)num_threads * sizeof(chunk_sort_arg_t));
  threads      = malloc((size_t)num_threads * sizeof(pthread_t));
  merged       = malloc((size_t)num_chains * CHAIN_SIZE_U64 * sizeof(uint64_t));

  if (!chunk_starts || !chunk_counts || !args || !threads || !merged) {
    free(chunk_starts);
    free(chunk_counts);
    free(args);
    free(threads);
    free(merged);
    return -1;
  }

  base_chunk = num_chains / (unsigned int)num_threads;
  remainder  = num_chains % (unsigned int)num_threads;
  offset = 0;
  for (i = 0; i < num_threads; i++) {
    chunk_starts[i] = offset;
    chunk_counts[i] = base_chunk + ((unsigned int)i < remainder ? 1 : 0);
    offset += chunk_counts[i];
  }

  started = 0;
  for (i = 0; i < num_threads; i++) {
    args[i].data  = data;
    args[i].start = chunk_starts[i];
    args[i].count = chunk_counts[i];
    if (pthread_create(&threads[i], NULL, chunk_sort_worker, &args[i]) != 0)
      break;
    started++;
  }

  for (i = 0; i < started; i++)
    pthread_join(threads[i], NULL);

  if (started < num_threads) {
    free(chunk_starts);
    free(chunk_counts);
    free(args);
    free(threads);
    free(merged);
    return -1;
  }

  merge_chunks(data, merged, num_chains, chunk_starts, chunk_counts, num_threads);
  memcpy(data, merged, (size_t)num_chains * CHAIN_SIZE_U64 * sizeof(uint64_t));

  free(chunk_starts);
  free(chunk_counts);
  free(args);
  free(threads);
  free(merged);
  return 0;
}
