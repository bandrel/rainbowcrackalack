# Parallel Sort Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the sequential file loop in `crackalack_sort` with a dynamic thread pool so multiple `.rt` files sort concurrently, with automatic job count based on available RAM and CPU cores and an optional `--jobs N` override.

**Architecture:** A shared `sort_work_queue_t` holds all filenames and two mutexes - one protecting the queue index, one serializing GPU access. Worker threads pull files from the queue until empty. CPU qsort runs without the GPU mutex so multiple CPU sorts run in parallel. Dynamic job count = `min(free_ram * 0.8 / max_file_size, cpu_cores, num_files)`.

**Tech Stack:** C, pthreads (already linked on all platforms), `sys/stat.h`, platform-specific RAM detection (`sysctl` / `sysconf` / `GlobalMemoryStatusEx`).

---

### Task 1: Add presorted check to `sort_file`

Skip sorting if the file's endpoints are already in ascending order. Avoids writing back a file that didn't change.

**Files:**
- Modify: `crackalack_sort.c:119-188`

**Step 1: Add the presorted check after reading data**

In `sort_file`, after the `fread` call (line 156) and before the `printf("Sorting...")` line (163), insert:

```c
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
```

**Step 2: Build to verify no errors**

```bash
make clean; make macos   # or: make linux
```
Expected: zero warnings, zero errors.

**Step 3: Commit**

```bash
git add crackalack_sort.c
git commit -m "feat: skip sort_file if file is already sorted"
```

---

### Task 2: Add platform-specific resource helpers and `compute_sort_jobs`

**Files:**
- Modify: `crackalack_sort.c:17-30` (includes section)

**Step 1: Add required includes**

Replace the existing includes block at the top of `crackalack_sort.c` (lines 18-29) with:

```c
#ifdef _WIN32
#include <windows.h>
#else
#  ifdef __APPLE__
#    include <sys/sysctl.h>
#  else
#    include <unistd.h>
#  endif
#endif
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
```

**Step 2: Add `get_free_ram`, `get_cpu_cores`, and `compute_sort_jobs` after the `#include` block, before `compare_by_end_index`**

```c
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
  int cpu_cores = get_cpu_cores();
  int jobs;
  int i;
  struct stat st;

  for (i = 0; i < num_files; i++) {
    if (stat(files[i], &st) == 0 && (uint64_t)st.st_size > max_file_size)
      max_file_size = (uint64_t)st.st_size;
  }

  if (max_file_size == 0 || free_ram == 0)
    return 1;

  jobs = (int)((free_ram * 8 / 10) / max_file_size);
  if (jobs > cpu_cores) jobs = cpu_cores;
  if (jobs > num_files) jobs = num_files;
  if (jobs < 1)         jobs = 1;
  return jobs;
}
```

**Step 3: Build to verify no errors**

```bash
make clean; make macos
```
Expected: zero warnings, zero errors.

**Step 4: Commit**

```bash
git add crackalack_sort.c
git commit -m "feat: add compute_sort_jobs with dynamic RAM/CPU detection"
```

---

### Task 3: Add work queue struct and worker thread function

**Files:**
- Modify: `crackalack_sort.c` - add after `compute_sort_jobs`, before `sort_file`

**Step 1: Add `sort_work_queue_t` struct and `sort_worker`**

```c
typedef struct {
  char           **files;
  int              num_files;
  int              next_file;
  int              failures;
  pthread_mutex_t  queue_mutex;
  pthread_mutex_t  gpu_mutex;
  pthread_mutex_t  failures_mutex;
} sort_work_queue_t;
```

**Step 2: Modify `sort_file` signature to accept `gpu_mutex`**

Change the signature from:
```c
static int sort_file(const char *filename) {
```
to:
```c
static int sort_file(const char *filename, pthread_mutex_t *gpu_mutex) {
```

Then wrap the `gpu_sort` call with the mutex. Replace:
```c
  if (gpu_sort(data, num_chains) != 0)
    qsort(data, num_chains, CHAIN_SIZE, compare_by_end_index);
```
with:
```c
  pthread_mutex_lock(gpu_mutex);
  {
    int gpu_ok = gpu_sort(data, num_chains);
    pthread_mutex_unlock(gpu_mutex);
    if (gpu_ok != 0)
      qsort(data, num_chains, CHAIN_SIZE, compare_by_end_index);
  }
```

**Step 3: Add `sort_worker` function after `sort_file`**

```c
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
```

**Step 4: Build to verify no errors**

```bash
make clean; make macos
```
Expected: zero warnings, zero errors. (The old `main` still calls `sort_file` with one arg - fix that next.)

**Step 5: Commit**

```bash
git add crackalack_sort.c
git commit -m "feat: add sort_work_queue_t and sort_worker thread function"
```

---

### Task 4: Update `main` to parse `--jobs` and dispatch thread pool

Replace the existing `main` (lines 191-214) entirely.

**Files:**
- Modify: `crackalack_sort.c:191-214`

**Step 1: Replace `main` with thread pool dispatch**

```c
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
```

**Step 2: Build**

```bash
make clean; make macos
```
Expected: zero warnings, zero errors.

**Step 3: Commit**

```bash
git add crackalack_sort.c
git commit -m "feat: replace sequential sort loop with dynamic thread pool"
```

---

### Task 5: Functional test

Verify the binary works correctly - sorts files, skips already-sorted files, and respects `--jobs`.

**Step 1: Generate a small synthetic unsorted `.rt` file**

```bash
python3 -c "
import struct, random
chains = [(random.randint(0, 2**63), random.randint(0, 2**63)) for _ in range(1000)]
with open('/tmp/test_unsorted.rt', 'wb') as f:
    for start, end in chains:
        f.write(struct.pack('<QQ', start, end))
print('Written 1000 chains to /tmp/test_unsorted.rt')
"
```

**Step 2: Sort it and verify output is sorted**

```bash
./crackalack_sort /tmp/test_unsorted.rt
python3 -c "
import struct
with open('/tmp/test_unsorted.rt', 'rb') as f:
    data = f.read()
chains = [struct.unpack_from('<QQ', data, i*16) for i in range(len(data)//16)]
ends = [c[1] for c in chains]
assert ends == sorted(ends), 'NOT SORTED'
print('OK: file is sorted')
"
```

**Step 3: Run again to verify presorted skip**

```bash
./crackalack_sort /tmp/test_unsorted.rt
```
Expected: `Skipping /tmp/test_unsorted.rt (already sorted).`

**Step 4: Test `--jobs` override**

```bash
cp /tmp/test_unsorted.rt /tmp/a.rt
cp /tmp/test_unsorted.rt /tmp/b.rt
# Shuffle b.rt to make it unsorted again
python3 -c "
import struct, random
with open('/tmp/b.rt', 'rb') as f:
    data = f.read()
chains = [struct.unpack_from('<QQ', data, i*16) for i in range(len(data)//16)]
random.shuffle(chains)
with open('/tmp/b.rt', 'wb') as f:
    for s, e in chains:
        f.write(struct.pack('<QQ', s, e))
"
./crackalack_sort --jobs 2 /tmp/a.rt /tmp/b.rt
```
Expected: `Sorting 2 file(s) with 2 parallel worker(s).`

**Step 5: Commit**

No source changes - nothing to commit.

---

### Task 6: Update README

**Files:**
- Modify: `README.md` - find the `crackalack_sort` usage section

**Step 1: Update usage docs**

Find the existing `crackalack_sort` documentation and add the `--jobs` flag:

```
crackalack_sort [--jobs N] table1.rt [table2.rt ...]

  Sorts one or more rainbow table files by end index, required before lookup.
  --jobs N  use N parallel workers; 0 or omitted = auto-detect based on
            available RAM and CPU cores.
```

**Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document --jobs flag for crackalack_sort"
```
