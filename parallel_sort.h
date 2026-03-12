#ifndef PARALLEL_SORT_H
#define PARALLEL_SORT_H

#include <stdint.h>

/* Sort an array of rainbow table chains (start, end pairs) by end index
 * using parallel chunk-sort + k-way merge.
 *
 * data:        array of num_chains * 2 uint64_t values (start, end pairs)
 * num_chains:  number of chains
 * num_threads: number of parallel sort threads to use
 *
 * Returns 0 on success, -1 on allocation failure (caller should fall back
 * to single-threaded qsort). Falls back internally to single-threaded
 * qsort when num_chains < 1024 or num_threads <= 1. */
int parallel_sort_rt(uint64_t *data, unsigned int num_chains, int num_threads);

#endif /* PARALLEL_SORT_H */
