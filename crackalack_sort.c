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
#else
#  include <unistd.h>
#  ifdef __APPLE__
#    include <sys/sysctl.h>
#  endif
#endif
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gpu_backend.h"
#include "sort_utils.h"
#include "terminal_color.h"
#include "version.h"

#define CHAIN_SIZE (unsigned int)(sizeof(uint64_t) * 2)
#define SORT_KERNEL_PATH "sort.cl"


static uint64_t get_free_ram(void) {
#ifdef _WIN32
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  return (uint64_t)ms.ullAvailPhys;
#elif defined(__APPLE__)
  uint64_t total = 0;
  size_t len = sizeof(total);
  sysctlbyname("hw.memsize", &total, &len, NULL, 0);
  return total * 4 / 10;  /* 40% of total as conservative available */
#else
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || page_size <= 0)
    return 0;
  return (uint64_t)pages * (uint64_t)page_size;
#endif
}

static int get_cpu_cores(void) {
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return (int)si.dwNumberOfProcessors;
#else
  int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? n : 1;
#endif
}

/* Compute number of parallel sort workers based on available resources.
 * files[0..num_files-1] are the paths to be sorted. */
static int compute_sort_jobs(int num_files, char **files) {
  uint64_t free_ram = get_free_ram();
  uint64_t max_file_size = 0;
  int i;
  struct stat st;

  for (i = 0; i < num_files; i++) {
    if (stat(files[i], &st) == 0 && (uint64_t)st.st_size > max_file_size)
      max_file_size = (uint64_t)st.st_size;
  }

  return compute_sort_jobs_from_params(free_ram, max_file_size,
                                       get_cpu_cores(), num_files);
}


typedef struct {
  char           **files;
  int              num_files;
  int              next_file;
  int              failures;
  pthread_mutex_t  queue_mutex;
  pthread_mutex_t  gpu_mutex;
  pthread_mutex_t  failures_mutex;
} sort_work_queue_t;


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


static int sort_file(const char *filename, pthread_mutex_t *gpu_mutex) {
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
  if (is_sorted_rt(data, num_chains)) {
    printf("Skipping %s (already sorted).\n", filename);
    ret = 0;
    goto done;
  }

  printf("Sorting %s (%u chains)... ", filename, num_chains);
  fflush(stdout);

  pthread_mutex_lock(gpu_mutex);
  {
    int gpu_ok = gpu_sort(data, num_chains);
    pthread_mutex_unlock(gpu_mutex);
    if (gpu_ok != 0)
      qsort(data, num_chains, CHAIN_SIZE, compare_by_end_index);
  }

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


static void *sort_worker(void *arg) {
  sort_work_queue_t *q = (sort_work_queue_t *)arg;
  while (1) {
    int idx;
    pthread_mutex_lock(&q->queue_mutex);
    idx = q->next_file++;
    pthread_mutex_unlock(&q->queue_mutex);
    if (idx >= q->num_files)
      break;
    if (sort_file(q->files[idx], &q->gpu_mutex) != 0) {
      pthread_mutex_lock(&q->failures_mutex);
      q->failures++;
      pthread_mutex_unlock(&q->failures_mutex);
    }
  }
  return NULL;
}


int main(int ac, char **av) {
  int first_file = 1;
  int num_jobs = 0;
  int num_files;
  sort_work_queue_t q;
  pthread_t *threads = NULL;
  int i, ret = 0;

  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();

  if (ac < 2) {
    printf("Sorts rainbow tables by end index for use with crackalack_lookup.\n\n"
           "Usage: %s [--jobs N] table1.rt [table2.rt ...]\n\n"
           "  --jobs N   use N parallel workers (0 = auto-detect, default: auto)\n\n",
           av[0]);
    return 0;
  }

  if (ac >= 3 && strcmp(av[1], "--jobs") == 0) {
    num_jobs = atoi(av[2]);
    if (num_jobs < 0) num_jobs = 0;
    first_file = 3;
  }

  num_files = ac - first_file;
  if (num_files <= 0) {
    fprintf(stderr, "%sError: no input files specified.%s\n", REDB, CLR);
    return 1;
  }

  if (num_jobs == 0)
    num_jobs = compute_sort_jobs(num_files, av + first_file);

  printf("Sorting %d file(s) with %d parallel worker(s).\n", num_files, num_jobs);

  memset(&q, 0, sizeof(q));
  q.files     = av + first_file;
  q.num_files = num_files;
  pthread_mutex_init(&q.queue_mutex,    NULL);
  pthread_mutex_init(&q.gpu_mutex,      NULL);
  pthread_mutex_init(&q.failures_mutex, NULL);

  threads = malloc((size_t)num_jobs * sizeof(pthread_t));
  if (!threads) {
    fprintf(stderr, "%sError: failed to allocate thread array.%s\n", REDB, CLR);
    ret = 1;
    goto done;
  }

  for (i = 0; i < num_jobs; i++)
    pthread_create(&threads[i], NULL, sort_worker, &q);
  for (i = 0; i < num_jobs; i++)
    pthread_join(threads[i], NULL);

  if (q.failures > 0) {
    fprintf(stderr, "\n%s%d file(s) failed to sort.%s\n", REDB, q.failures, CLR);
    ret = 1;
  }

done:
  pthread_mutex_destroy(&q.queue_mutex);
  pthread_mutex_destroy(&q.gpu_mutex);
  pthread_mutex_destroy(&q.failures_mutex);
  free(threads);
  return ret;
}
