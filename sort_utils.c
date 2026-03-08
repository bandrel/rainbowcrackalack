/*
 * Rainbow Crackalack: sort_utils.c
 * Pure helper functions for rainbow table sorting, shared between
 * crackalack_sort and the unit test binary.
 */

#include <stdint.h>
#include "sort_utils.h"


int is_sorted_rt(const uint64_t *data, unsigned int num_chains) {
  unsigned int i;
  for (i = 0; i + 1 < num_chains; i++) {
    if (data[i * 2 + 1] > data[(i + 1) * 2 + 1])
      return 0;
  }
  return 1;
}


int compute_sort_jobs_from_params(uint64_t free_ram, uint64_t max_file_size,
                                  int cpu_cores, int num_files) {
  int jobs;

  if (max_file_size == 0 || free_ram == 0)
    return 1;

  jobs = (int)((free_ram * 8 / 10) / max_file_size);
  if (jobs > cpu_cores) jobs = cpu_cores;
  if (jobs > num_files) jobs = num_files;
  if (jobs < 1)         jobs = 1;
  return jobs;
}
