#ifndef SORT_UTILS_H
#define SORT_UTILS_H

#include <stdint.h>

/* Returns 1 if the endpoint column of data[0..num_chains-1] is in
 * non-descending order, 0 otherwise. An empty or single-chain array
 * is considered sorted. */
int is_sorted_rt(const uint64_t *data, unsigned int num_chains);

/* Compute parallel worker count from pre-measured resource values.
 * free_ram:      available bytes of RAM
 * max_file_size: size of the largest file to be sorted, in bytes
 * cpu_cores:     logical CPU core count
 * num_files:     number of files to sort
 * Returns a job count in [1, min(cpu_cores, num_files)]. */
int compute_sort_jobs_from_params(uint64_t free_ram, uint64_t max_file_size,
                                  int cpu_cores, int num_files);

#endif /* SORT_UTILS_H */
