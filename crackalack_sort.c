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
#    include <mach/mach.h>
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
#include "parallel_sort.h"
#include "sort_utils.h"
#include "terminal_color.h"
#include "version.h"
#ifdef HAVE_ZSTD
#include "zst_compress.h"
#endif

#define CHAIN_SIZE (unsigned int)(sizeof(uint64_t) * 2)
#define SORT_KERNEL_PATH "sort.cl"

#ifdef _WIN32
#  define RT_FSEEK(f, o, w) _fseeki64(f, o, w)
#  define RT_FTELL(f)        _ftelli64(f)
typedef __int64 rt_off_t;
#else
#  define RT_FSEEK(f, o, w) fseeko(f, o, w)
#  define RT_FTELL(f)        ftello(f)
typedef off_t rt_off_t;
#endif


static uint64_t get_free_ram(void) {
#ifdef _WIN32
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  return (uint64_t)ms.ullAvailPhys;
#elif defined(__APPLE__)
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vmstat;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info64_t)&vmstat, &count) != KERN_SUCCESS)
    return 0;
  {
    uint64_t page_sz = (uint64_t)vm_kernel_page_size;
    return ((uint64_t)vmstat.free_count + (uint64_t)vmstat.inactive_count) * page_sz;
  }
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
  int              zst_level;   /* 0 = leave tables uncompressed; otherwise the zstd level. */
} sort_work_queue_t;


static int compare_by_end_index(const void *a, const void *b) {
  uint64_t end_a = ((const uint64_t *)a)[1];
  uint64_t end_b = ((const uint64_t *)b)[1];
  if (end_a < end_b) return -1;
  if (end_a > end_b) return 1;
  return 0;
}


/* GPU bitonic sort. Returns 0 on success, -1 to fall back to CPU qsort. */
static int gpu_sort(uint64_t *data, uint64_t num_chains) {
  gpu_platform platforms[MAX_NUM_PLATFORMS];
  gpu_device devices[MAX_NUM_DEVICES];
  gpu_uint num_platforms = 0, num_devices = 0;
  gpu_context context = NULL;
  gpu_queue queue = NULL;
  gpu_program program = NULL;
  gpu_kernel kernel = NULL;
  gpu_buffer data_buf = GPU_BUFFER_NULL, k_buf = GPU_BUFFER_NULL, j_buf = GPU_BUFFER_NULL;
  uint64_t *padded_data = NULL;
  uint64_t n_padded = 1;
  size_t n_alloc = 0;
  unsigned int i = 0;
  size_t j = 0;
  size_t data_size = 0;
  size_t gws = 0;
  gpu_uint k_val = 0, j_val = 0;
  int err = 0;

  get_platforms_and_devices(-1, MAX_NUM_PLATFORMS, platforms, &num_platforms,
                            MAX_NUM_DEVICES, devices, &num_devices, 0);
  if (num_devices == 0)
    return -1;

  while (n_padded < (uint64_t)num_chains)
    n_padded <<= 1;

  if (n_padded > (uint64_t)(SIZE_MAX / (2 * sizeof(uint64_t)))) {
    return -1;  /* table too large to sort on GPU */
  }

  /* The sort kernel stores to data[gid * 2] with only an `if (l <= i) return`
   * guard, no bound against the launch's gws. OpenCL/Metal dispatch exactly
   * `gws` (== n_padded) work items, but CUDA rounds the grid up to a whole
   * multiple of its block size, so up to GPU_LAUNCH_GRANULARITY - 1 extra
   * threads run with gid >= n_padded and write past the end of the buffer.
   * Over-allocate the host and device buffers to the padded launch size so
   * those stores land in slack; gws/n_padded (the count that drives the
   * bitonic sort's k/j loop bounds) is deliberately left unchanged. */
  n_alloc = GPU_GWS_PAD((size_t)n_padded);

  padded_data = malloc(n_alloc * 2 * sizeof(uint64_t));
  if (padded_data == NULL)
    return -1;

  memcpy(padded_data, data, (size_t)num_chains * 2 * sizeof(uint64_t));
  for (i = num_chains; i < n_padded; i++) {
    padded_data[i * 2]     = 0;
    padded_data[i * 2 + 1] = UINT64_MAX;
  }
  for (j = (size_t)n_padded; j < n_alloc; j++) {
    padded_data[j * 2]     = 0;
    padded_data[j * 2 + 1] = UINT64_MAX;
  }

  context = CLCREATECONTEXT(context_callback, &devices[0]);
  queue   = CLCREATEQUEUE(context, devices[0]);

  if (access(SORT_KERNEL_PATH, R_OK) != 0) {
    CLRELEASEQUEUE(queue);
    CLRELEASECONTEXT(context);
    free(padded_data);
    return -1;
  }

  load_kernel(context, 1, &devices[0], SORT_KERNEL_PATH, "bitonic_sort_step",
              &program, &kernel, 0);

  /* Device buffer is sized to n_alloc (the padded launch size), not n_padded
   * (the sort's actual element count), so CUDA's overshoot threads land in
   * slack. gws below is deliberately left at n_padded -- it is a COUNT that
   * drives the bitonic sort's k/j loop bounds, not a buffer size. */
  data_size = n_alloc * 2 * sizeof(uint64_t);
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


#ifdef HAVE_ZSTD
/* Writes `data` out as "<filename>.zst" and removes the raw file.  The raw
 * table is only unlinked once the compressed output is safely in place, so an
 * interrupted run can lose work but never the table. */
static int compress_and_replace(const char *filename, const void *data,
                                uint64_t num_chains, int zst_level) {
  char zst_path[2048] = {0};
  int n = snprintf(zst_path, sizeof(zst_path), "%s.zst", filename);

  if (n < 0 || (size_t)n >= sizeof(zst_path)) {
    fprintf(stderr, "%sError: path too long to compress: %s%s\n", REDB, filename, CLR);
    return -1;
  }
  if (zst_compress_buf(data, (size_t)num_chains * CHAIN_SIZE, zst_path, zst_level, 0) != 0) {
    fprintf(stderr, "%sError: failed to compress %s%s\n", REDB, filename, CLR);
    return -1;
  }
  if (remove(filename) != 0) {
    fprintf(stderr, "%sWarning: wrote %s but could not remove %s%s\n",
            REDB, zst_path, filename, CLR);
    return -1;
  }
  return 0;
}
#endif /* HAVE_ZSTD */


static int sort_file(const char *filename, pthread_mutex_t *gpu_mutex, int zst_level) {
  FILE *f = NULL;
  uint64_t *data = NULL;
  rt_off_t file_size = 0;
  uint64_t num_chains = 0;
  int ret = -1;

  f = fopen(filename, "rb");
  if (f == NULL) {
    fprintf(stderr, "%sError: failed to open %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    return -1;
  }

  if (RT_FSEEK(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "%sError: failed to seek in %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }
  file_size = RT_FTELL(f);

  if (file_size < 0) {
    fprintf(stderr, "%sError: failed to get size of %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }

  if (file_size == 0) {
    fprintf(stderr, "%sError: %s is empty or unreadable.%s\n", REDB, filename, CLR);
    goto done;
  }

  if ((file_size % CHAIN_SIZE) != 0) {
    fprintf(stderr, "%sError: %s size (%" PRId64 ") is not aligned to %u bytes. File may be compressed or corrupt.%s\n",
            REDB, filename, (int64_t)file_size, CHAIN_SIZE, CLR);
    goto done;
  }

  num_chains = (uint64_t)(file_size / CHAIN_SIZE);

  data = malloc((size_t)file_size);
  if (data == NULL) {
    fprintf(stderr, "%sError: failed to allocate %" PRId64 " bytes for %s: %s%s\n",
            REDB, (int64_t)file_size, filename, strerror(errno), CLR);
    goto done;
  }

  if (RT_FSEEK(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "%sError: failed to seek in %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }
  if (fread(data, CHAIN_SIZE, num_chains, f) != num_chains) {
    fprintf(stderr, "%sError: failed to read %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }
  fclose(f);
  f = NULL;

  /* Skip if already sorted. */
  if (is_sorted_rt(data, num_chains)) {
#ifdef HAVE_ZSTD
    if (zst_level > 0) {
      printf("Compressing %s (already sorted)... ", filename);
      fflush(stdout);
      if (compress_and_replace(filename, data, num_chains, zst_level) != 0)
        goto done;
      printf("%sdone.%s\n", GREENB, CLR);
    } else
#endif
      printf("Skipping %s (already sorted).\n", filename);
    ret = 0;
    goto done;
  }

  printf("Sorting %s (%"PRIu64" chains)... ", filename, num_chains);
  fflush(stdout);

  pthread_mutex_lock(gpu_mutex);
  {
    int gpu_ok = gpu_sort(data, num_chains);
    pthread_mutex_unlock(gpu_mutex);
    if (gpu_ok != 0 || !is_sorted_rt(data, num_chains)) {
      if (parallel_sort_rt(data, num_chains, get_cpu_cores()) != 0)
        qsort(data, num_chains, CHAIN_SIZE, compare_by_end_index);
    }
  }

#ifdef HAVE_ZSTD
  if (zst_level > 0) {
    if (compress_and_replace(filename, data, num_chains, zst_level) != 0)
      goto done;
  } else
#endif
  {
    f = fopen(filename, "wb");
    if (f == NULL) {
      fprintf(stderr, "\n%sError: failed to open %s for writing: %s%s\n", REDB, filename, strerror(errno), CLR);
      goto done;
    }
    if (fwrite(data, CHAIN_SIZE, num_chains, f) != num_chains) {
      fprintf(stderr, "\n%sError: failed to write %s: %s%s\n", REDB, filename, strerror(errno), CLR);
      goto done;
    }
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
    if (sort_file(q->files[idx], &q->gpu_mutex, q->zst_level) != 0) {
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
  int zst_level = 0;
  int num_files;
  sort_work_queue_t q;
  pthread_t *threads = NULL;
  int i, ret = 0;

  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();

  if (ac < 2) {
#ifdef HAVE_ZSTD
    printf("Sorts rainbow tables by end index for use with crackalack_lookup.\n\n"
           "Usage: %s [--jobs N] [--zst[=LEVEL]] table1.rt [table2.rt ...]\n\n"
           "  --jobs N       use N parallel workers (0 = auto-detect, default: auto)\n"
           "  --zst[=LEVEL]  write each sorted table as <name>.rt.zst and remove the\n"
           "                 raw .rt (LEVEL defaults to %d)\n\n",
           av[0], ZST_DEFAULT_LEVEL);
#else
    printf("Sorts rainbow tables by end index for use with crackalack_lookup.\n\n"
           "Usage: %s [--jobs N] table1.rt [table2.rt ...]\n\n"
           "  --jobs N       use N parallel workers (0 = auto-detect, default: auto)\n\n",
           av[0]);
#endif
    return 0;
  }

  for (first_file = 1; first_file < ac; first_file++) {
    if (strcmp(av[first_file], "--jobs") == 0) {
      char *endptr = NULL;
      long val;
      if (first_file + 1 >= ac) {
        fprintf(stderr, "%sError: --jobs requires an argument.%s\n", REDB, CLR);
        return 1;
      }
      val = strtol(av[++first_file], &endptr, 10);
      if (endptr == av[first_file] || *endptr != '\0') {
        fprintf(stderr, "%sError: --jobs requires a non-negative integer.%s\n", REDB, CLR);
        return 1;
      }
      num_jobs = (val < 0) ? 0 : (int)val;
#ifdef HAVE_ZSTD
    } else if (strncmp(av[first_file], "--zst", 5) == 0) {
      if (av[first_file][5] == '=') {
        char *endptr = NULL;
        long val = strtol(av[first_file] + 6, &endptr, 10);
        if (*endptr != '\0' || val < 1 || val > 22) {
          fprintf(stderr, "%sError: --zst level must be 1..22.%s\n", REDB, CLR);
          return 1;
        }
        zst_level = (int)val;
      } else if (av[first_file][5] == '\0') {
        zst_level = ZST_DEFAULT_LEVEL;
      } else {
        fprintf(stderr, "%sError: unknown option %s.%s\n", REDB, av[first_file], CLR);
        return 1;
      }
#endif
    } else
      break;
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
  q.zst_level = zst_level;
  {
    int mrc;
    if ((mrc = pthread_mutex_init(&q.queue_mutex, NULL)) != 0 ||
        (mrc = pthread_mutex_init(&q.gpu_mutex, NULL)) != 0 ||
        (mrc = pthread_mutex_init(&q.failures_mutex, NULL)) != 0) {
      fprintf(stderr, "%sError: mutex init failed: %s%s\n", REDB, strerror(mrc), CLR);
      ret = 1;
      goto done;
    }
  }

  threads = malloc((size_t)num_jobs * sizeof(pthread_t));
  if (!threads) {
    fprintf(stderr, "%sError: failed to allocate thread array.%s\n", REDB, CLR);
    ret = 1;
    goto done;
  }

  {
    int started = 0;
    for (i = 0; i < num_jobs; i++) {
      int rc = pthread_create(&threads[i], NULL, sort_worker, &q);
      if (rc != 0) {
        fprintf(stderr, "%sError: pthread_create failed: %s%s\n", REDB, strerror(rc), CLR);
        break;
      }
      started++;
    }
    for (i = 0; i < started; i++)
      pthread_join(threads[i], NULL);
  }

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
