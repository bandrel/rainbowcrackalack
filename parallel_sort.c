/*
 * Rainbow Crackalack: parallel_sort.c
 * Parallel chunk-sort + parallel pairwise merge for rainbow table chains.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parallel_sort.h"

#define CHAIN_SIZE_U64 2
#define CHAIN_BYTES (CHAIN_SIZE_U64 * sizeof(uint64_t))


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
        a->count, CHAIN_BYTES, compare_by_end_index);
  return NULL;
}


typedef struct {
  const uint64_t *src_a;
  unsigned int    count_a;
  const uint64_t *src_b;
  unsigned int    count_b;
  uint64_t       *dst;
} merge_pair_arg_t;


static void merge_two(const uint64_t *a, unsigned int na,
                       const uint64_t *b, unsigned int nb,
                       uint64_t *dst) {
  unsigned int ia = 0, ib = 0, out = 0;
  while (ia < na && ib < nb) {
    if (a[ia * CHAIN_SIZE_U64 + 1] <= b[ib * CHAIN_SIZE_U64 + 1]) {
      dst[out * CHAIN_SIZE_U64]     = a[ia * CHAIN_SIZE_U64];
      dst[out * CHAIN_SIZE_U64 + 1] = a[ia * CHAIN_SIZE_U64 + 1];
      ia++;
    } else {
      dst[out * CHAIN_SIZE_U64]     = b[ib * CHAIN_SIZE_U64];
      dst[out * CHAIN_SIZE_U64 + 1] = b[ib * CHAIN_SIZE_U64 + 1];
      ib++;
    }
    out++;
  }
  if (ia < na)
    memcpy(dst + (size_t)out * CHAIN_SIZE_U64,
           a + (size_t)ia * CHAIN_SIZE_U64,
           (size_t)(na - ia) * CHAIN_BYTES);
  if (ib < nb)
    memcpy(dst + (size_t)out * CHAIN_SIZE_U64,
           b + (size_t)ib * CHAIN_SIZE_U64,
           (size_t)(nb - ib) * CHAIN_BYTES);
}


static void *merge_pair_worker(void *arg) {
  merge_pair_arg_t *m = (merge_pair_arg_t *)arg;
  merge_two(m->src_a, m->count_a, m->src_b, m->count_b, m->dst);
  return NULL;
}


int parallel_sort_rt(uint64_t *data, unsigned int num_chains, int num_threads) {
  chunk_sort_arg_t *sort_args = NULL;
  pthread_t *threads = NULL;
  uint64_t *buf = NULL;
  unsigned int *seg_starts = NULL;
  unsigned int *seg_counts = NULL;
  merge_pair_arg_t *merge_args = NULL;
  unsigned int base_chunk, remainder, offset;
  int i, started, num_segs;

  if (num_chains < 1024 || num_threads <= 1) {
    qsort(data, num_chains, CHAIN_BYTES, compare_by_end_index);
    return 0;
  }

  if ((unsigned int)num_threads > num_chains)
    num_threads = (int)num_chains;

  sort_args  = malloc((size_t)num_threads * sizeof(chunk_sort_arg_t));
  threads    = malloc((size_t)num_threads * sizeof(pthread_t));
  buf        = malloc((size_t)num_chains * CHAIN_BYTES);
  seg_starts = malloc((size_t)num_threads * sizeof(unsigned int));
  seg_counts = malloc((size_t)num_threads * sizeof(unsigned int));
  merge_args = malloc((size_t)num_threads * sizeof(merge_pair_arg_t));

  if (!sort_args || !threads || !buf || !seg_starts || !seg_counts || !merge_args) {
    free(sort_args); free(threads); free(buf);
    free(seg_starts); free(seg_counts); free(merge_args);
    return -1;
  }

  /* Divide into num_threads chunks. */
  base_chunk = num_chains / (unsigned int)num_threads;
  remainder  = num_chains % (unsigned int)num_threads;
  offset = 0;
  for (i = 0; i < num_threads; i++) {
    seg_starts[i] = offset;
    seg_counts[i] = base_chunk + ((unsigned int)i < remainder ? 1 : 0);
    offset += seg_counts[i];
  }

  /* Phase 1: parallel chunk sort. */
  started = 0;
  for (i = 0; i < num_threads; i++) {
    sort_args[i].data  = data;
    sort_args[i].start = seg_starts[i];
    sort_args[i].count = seg_counts[i];
    if (pthread_create(&threads[i], NULL, chunk_sort_worker, &sort_args[i]) != 0)
      break;
    started++;
  }
  for (i = 0; i < started; i++)
    pthread_join(threads[i], NULL);
  if (started < num_threads) {
    free(sort_args); free(threads); free(buf);
    free(seg_starts); free(seg_counts); free(merge_args);
    return -1;
  }

  /* Phase 2: parallel pairwise merge tree.
   * Each round merges adjacent pairs in parallel. After log2(num_segs)
   * rounds, a single sorted segment remains. Data ping-pongs between
   * data and buf to avoid extra copies. */
  num_segs = num_threads;
  uint64_t *src = data;
  uint64_t *dst = buf;

  while (num_segs > 1) {
    int pairs = num_segs / 2;
    int odd   = num_segs % 2;

    started = 0;
    for (i = 0; i < pairs; i++) {
      int left  = i * 2;
      int right = i * 2 + 1;
      merge_args[i].src_a  = src + (size_t)seg_starts[left] * CHAIN_SIZE_U64;
      merge_args[i].count_a = seg_counts[left];
      merge_args[i].src_b  = src + (size_t)seg_starts[right] * CHAIN_SIZE_U64;
      merge_args[i].count_b = seg_counts[right];
      merge_args[i].dst    = dst + (size_t)seg_starts[left] * CHAIN_SIZE_U64;
      if (pthread_create(&threads[i], NULL, merge_pair_worker, &merge_args[i]) != 0)
        break;
      started++;
    }
    for (i = 0; i < started; i++)
      pthread_join(threads[i], NULL);

    /* Copy the odd segment that had no partner. */
    if (odd) {
      int last = num_segs - 1;
      memcpy(dst + (size_t)seg_starts[last] * CHAIN_SIZE_U64,
             src + (size_t)seg_starts[last] * CHAIN_SIZE_U64,
             (size_t)seg_counts[last] * CHAIN_BYTES);
    }

    /* Coalesce segment metadata for the next round. */
    {
      int new_segs = 0;
      for (i = 0; i < pairs; i++) {
        int left = i * 2;
        int right = i * 2 + 1;
        seg_starts[new_segs] = seg_starts[left];
        seg_counts[new_segs] = seg_counts[left] + seg_counts[right];
        new_segs++;
      }
      if (odd) {
        int last = num_segs - 1;
        seg_starts[new_segs] = seg_starts[last];
        seg_counts[new_segs] = seg_counts[last];
        new_segs++;
      }
      num_segs = new_segs;
    }

    /* Swap src and dst for the next round. */
    {
      uint64_t *tmp = src;
      src = dst;
      dst = tmp;
    }
  }

  /* If the final result ended up in buf, copy back to data. */
  if (src != data)
    memcpy(data, src, (size_t)num_chains * CHAIN_BYTES);

  free(sort_args);
  free(threads);
  free(buf);
  free(seg_starts);
  free(seg_counts);
  free(merge_args);
  return 0;
}
