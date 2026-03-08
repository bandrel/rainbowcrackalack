/*
 * Rainbow Crackalack: crackalack_sort.c
 * Copyright (C) 2018-2021  Joe Testa <jtesta@positronsecurity.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#include <windows.h>
#endif
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_backend.h"
#include "terminal_color.h"
#include "version.h"

#define CHAIN_SIZE (unsigned int)(sizeof(uint64_t) * 2)
#define SORT_KERNEL_PATH "sort.cl"


static int compare_by_end_index(const void *a, const void *b) {
  uint64_t end_a = ((const uint64_t *)a)[1];
  uint64_t end_b = ((const uint64_t *)b)[1];
  if (end_a < end_b) return -1;
  if (end_a > end_b) return 1;
  return 0;
}


/* GPU bitonic sort. Returns 0 on success, -1 to fall back to CPU qsort. */
static int gpu_sort(uint64_t *data, unsigned int num_chains) {
  gpu_platform platforms[MAX_NUM_PLATFORMS];
  gpu_device devices[MAX_NUM_DEVICES];
  gpu_uint num_platforms = 0, num_devices = 0;
  gpu_context context = NULL;
  gpu_queue queue = NULL;
  gpu_program program = NULL;
  gpu_kernel kernel = NULL;
  gpu_buffer data_buf = NULL, k_buf = NULL, j_buf = NULL;
  uint64_t *padded_data = NULL;
  unsigned int n_padded = 1;
  unsigned int i = 0;
  size_t data_size = 0;
  size_t gws = 0;
  gpu_uint k_val = 0, j_val = 0;
  int err = 0;

  get_platforms_and_devices(-1, MAX_NUM_PLATFORMS, platforms, &num_platforms,
                            MAX_NUM_DEVICES, devices, &num_devices, 0);
  if (num_devices == 0)
    return -1;

  while (n_padded < num_chains)
    n_padded <<= 1;

  padded_data = malloc((size_t)n_padded * 2 * sizeof(uint64_t));
  if (padded_data == NULL)
    return -1;

  memcpy(padded_data, data, (size_t)num_chains * 2 * sizeof(uint64_t));
  for (i = num_chains; i < n_padded; i++) {
    padded_data[i * 2]     = 0;
    padded_data[i * 2 + 1] = UINT64_MAX;
  }

  context = CLCREATECONTEXT(context_callback, &devices[0]);
  queue   = CLCREATEQUEUE(context, devices[0]);
  load_kernel(context, 1, &devices[0], SORT_KERNEL_PATH, "bitonic_sort_step",
              &program, &kernel, 0);

  data_size = (size_t)n_padded * 2 * sizeof(uint64_t);
  CLCREATEARG_ARRAY(0, data_buf, GPU_RW, padded_data, data_size);
  k_val = 2;
  j_val = 1;
  CLCREATEARG(1, k_buf, GPU_RW, k_val, sizeof(gpu_uint));
  CLCREATEARG(2, j_buf, GPU_RW, j_val, sizeof(gpu_uint));

  gws = (size_t)n_padded;
  for (k_val = 2; k_val <= (gpu_uint)n_padded; k_val <<= 1) {
    CLWRITEBUFFER(k_buf, sizeof(gpu_uint), &k_val);
    for (j_val = k_val >> 1; j_val > 0; j_val >>= 1) {
      CLWRITEBUFFER(j_buf, sizeof(gpu_uint), &j_val);
      CLRUNKERNEL(queue, kernel, &gws);
      CLFLUSH(queue);
    }
  }

  CLWAIT(queue);
  CLREADBUFFER(data_buf, data_size, padded_data);

  memcpy(data, padded_data, (size_t)num_chains * 2 * sizeof(uint64_t));

  CLFREEBUFFER(data_buf);
  CLFREEBUFFER(k_buf);
  CLFREEBUFFER(j_buf);
  CLRELEASEKERNEL(kernel);
  CLRELEASEPROGRAM(program);
  CLRELEASEQUEUE(queue);
  CLRELEASECONTEXT(context);
  free(padded_data);
  return 0;
}


static int sort_file(const char *filename) {
  FILE *f = NULL;
  uint64_t *data = NULL;
  long file_size = 0;
  unsigned int num_chains = 0;
  int ret = -1;

  f = fopen(filename, "rb");
  if (f == NULL) {
    fprintf(stderr, "%sError: failed to open %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    return -1;
  }

  fseek(f, 0, SEEK_END);
  file_size = ftell(f);

  if (file_size <= 0) {
    fprintf(stderr, "%sError: %s is empty or unreadable.%s\n", REDB, filename, CLR);
    goto done;
  }

  if ((file_size % CHAIN_SIZE) != 0) {
    fprintf(stderr, "%sError: %s size (%" PRId64 ") is not aligned to %u bytes. File may be compressed or corrupt.%s\n",
            REDB, filename, (int64_t)file_size, CHAIN_SIZE, CLR);
    goto done;
  }

  num_chains = (unsigned int)(file_size / CHAIN_SIZE);

  data = malloc((size_t)file_size);
  if (data == NULL) {
    fprintf(stderr, "%sError: failed to allocate %" PRId64 " bytes for %s: %s%s\n",
            REDB, (int64_t)file_size, filename, strerror(errno), CLR);
    goto done;
  }

  fseek(f, 0, SEEK_SET);
  if (fread(data, CHAIN_SIZE, num_chains, f) != num_chains) {
    fprintf(stderr, "%sError: failed to read %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }
  fclose(f);
  f = NULL;

  /* Skip if already sorted. */
  {
    unsigned int j;
    int already_sorted = 1;
    for (j = 0; j + 1 < num_chains; j++) {
      if (data[j * 2 + 1] > data[(j + 1) * 2 + 1]) {
        already_sorted = 0;
        break;
      }
    }
    if (already_sorted) {
      printf("Skipping %s (already sorted).\n", filename);
      ret = 0;
      goto done;
    }
  }

  printf("Sorting %s (%u chains)... ", filename, num_chains);
  fflush(stdout);

  if (gpu_sort(data, num_chains) != 0)
    qsort(data, num_chains, CHAIN_SIZE, compare_by_end_index);

  f = fopen(filename, "wb");
  if (f == NULL) {
    fprintf(stderr, "\n%sError: failed to open %s for writing: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }

  if (fwrite(data, CHAIN_SIZE, num_chains, f) != num_chains) {
    fprintf(stderr, "\n%sError: failed to write %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }

  printf("%sdone.%s\n", GREENB, CLR);
  ret = 0;

done:
  if (f != NULL)
    fclose(f);
  free(data);
  return ret;
}


int main(int ac, char **av) {
  int i = 0;
  int failures = 0;

  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();

  if (ac < 2) {
    printf("Sorts rainbow tables by end index for use with crackalack_lookup.\n\nUsage: %s table1.rt [table2.rt ...]\n\n", av[0]);
    return 0;
  }

  for (i = 1; i < ac; i++) {
    if (sort_file(av[i]) != 0)
      failures++;
  }

  if (failures > 0) {
    fprintf(stderr, "\n%s%d file(s) failed to sort.%s\n", REDB, failures, CLR);
    return 1;
  }

  return 0;
}
