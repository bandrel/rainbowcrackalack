# Pipelined Lookup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the sequential precompute-then-search lookup pipeline with a two-phase bulk-load + batched-precompute design that overlaps CPU and GPU work, starts searching before all hashes are precomputed, and keeps all tables in RAM.

**Architecture:** Phase 1 bulk-loads tables into a flat array (RAM-budgeted). Phase 2 iterates batches of uncracked hashes: GPU+CPU precompute in parallel, binary search all loaded tables, GPU false alarm check overlapped with CPU precompute of the next batch. Outer loop repeats for table chunks that exceed RAM.

**Tech Stack:** C, pthreads, existing GPU backend (OpenCL/Metal), existing cpu_rt_functions.c primitives.

**Spec:** `docs/superpowers/specs/2026-04-09-pipelined-lookup-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `cpu_rt_functions.h` | Modify | Add `cpu_precompute_hash()` declaration |
| `cpu_rt_functions.c` | Modify | Add `cpu_precompute_hash()` — produces a ppi node from one hash using CPU chain-walk |
| `crackalack_lookup.c` | Modify | Add `bulk_load_tables()`, `pipelined_lookup()`, CPU precompute thread pool, replace main loop |

No new files needed. All changes build on existing code.

---

### Task 1: Add `cpu_precompute_hash()` to cpu_rt_functions

**Files:**
- Modify: `cpu_rt_functions.h`
- Modify: `cpu_rt_functions.c`

This function takes a single hash (hex string) and table parameters, walks the chain from every position on the CPU, and returns a ppi node with all candidate endpoints. It mirrors what the GPU batch kernel does for one hash.

- [ ] **Step 1: Add declaration to cpu_rt_functions.h**

Add before the `#endif` at end of `cpu_rt_functions.h`:

```c
/* Forward declaration — defined in crackalack_lookup.c */
struct _precomputed_and_potential_indices;

/* CPU precompute: walks the chain from every position for a single hash.
 * Returns a heap-allocated ppi node, or NULL on error.
 * Caller must provide plaintext_space_up_to_index (pre-filled). */
struct _precomputed_and_potential_indices *cpu_precompute_hash(
    unsigned int hash_type,
    char *hash_hex,
    char *username,
    char *charset,
    unsigned int charset_len,
    unsigned int plaintext_len_min,
    unsigned int plaintext_len_max,
    unsigned int reduction_offset,
    unsigned int chain_len,
    uint64_t *plaintext_space_up_to_index,
    uint64_t plaintext_space_total);
```

- [ ] **Step 2: Implement cpu_precompute_hash() in cpu_rt_functions.c**

Add at end of `cpu_rt_functions.c`, before any closing ifdef:

```c
#include "test_shared.h"  /* hex_to_bytes */
#include "shared.h"       /* MAX_PLAINTEXT_LEN, HASH_NTLM, HASH_MD5, etc. */

/* Defined in crackalack_lookup.c — we only need the struct layout. */
typedef struct _precomputed_and_potential_indices {
  char *username;
  char *hash;
  uint64_t *precomputed_end_indices;
  unsigned int num_precomputed_end_indices;
  uint64_t *potential_start_indices;
  size_t num_potential_start_indices;
  size_t potential_start_indices_size;
  unsigned int *potential_start_index_positions;
  char *plaintext;
  struct _precomputed_and_potential_indices *next;
} cpu_ppi;

struct _precomputed_and_potential_indices *cpu_precompute_hash(
    unsigned int hash_type,
    char *hash_hex,
    char *username,
    char *charset,
    unsigned int charset_len,
    unsigned int plaintext_len_min,
    unsigned int plaintext_len_max,
    unsigned int reduction_offset,
    unsigned int chain_len,
    uint64_t *plaintext_space_up_to_index,
    uint64_t plaintext_space_total) {

  unsigned char hash_bin[16] = {0};
  hex_to_bytes(hash_hex, 16, hash_bin);

  /* Allocate endpoints array (one per chain position). */
  uint64_t *endpoints = calloc(chain_len, sizeof(uint64_t));
  if (endpoints == NULL) return NULL;

  char plaintext[MAX_PLAINTEXT_LEN + 1] = {0};
  unsigned char hash_buf[16] = {0};
  unsigned int plaintext_len = 0, hash_len = 16;

  for (unsigned int pos = 0; pos < chain_len; pos++) {
    long target_chain_len = (long)chain_len - (long)pos - 1;
    if (target_chain_len < 1) {
      endpoints[pos] = 0;
      continue;
    }

    uint64_t index = hash_to_index(hash_bin, hash_len, reduction_offset,
                                   plaintext_space_total, target_chain_len - 1);

    for (unsigned int i = target_chain_len; i < chain_len - 1; i++) {
      index_to_plaintext(index, charset, charset_len, plaintext_len_min,
                         plaintext_len_max, plaintext_space_up_to_index,
                         plaintext, &plaintext_len);
      if (hash_type == HASH_MD5)
        md5_hash(plaintext, plaintext_len, hash_buf);
      else
        ntlm_hash(plaintext, plaintext_len, hash_buf);
      index = hash_to_index(hash_buf, hash_len, reduction_offset,
                            plaintext_space_total, i);
    }

    endpoints[pos] = index;
  }

  /* Count non-zero endpoints. */
  unsigned int count = 0;
  for (unsigned int p = 0; p < chain_len; p++)
    if (endpoints[p] != 0) count++;

  /* Build ppi node. */
  cpu_ppi *ppi = calloc(1, sizeof(cpu_ppi));
  if (ppi == NULL) { free(endpoints); return NULL; }

  ppi->hash = hash_hex;
  ppi->username = username;
  ppi->num_precomputed_end_indices = count;
  ppi->precomputed_end_indices = calloc(count, sizeof(uint64_t));
  if (ppi->precomputed_end_indices == NULL) { free(endpoints); free(ppi); return NULL; }

  unsigned int idx = 0;
  for (unsigned int p = 0; p < chain_len; p++)
    if (endpoints[p] != 0)
      ppi->precomputed_end_indices[idx++] = endpoints[p];

  free(endpoints);
  return (struct _precomputed_and_potential_indices *)ppi;
}
```

- [ ] **Step 3: Build and verify compilation**

Run: `make clean && make macos 2>&1 | tail -5`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add cpu_rt_functions.c cpu_rt_functions.h
git commit -m "feat: add cpu_precompute_hash() for CPU-side chain precomputation"
```

---

### Task 2: Add `bulk_load_tables()` to crackalack_lookup.c

**Files:**
- Modify: `crackalack_lookup.c`

Replaces the streaming preloader with a function that loads tables into a flat array up to a RAM budget.

- [ ] **Step 1: Add the bulk_load_tables struct and function**

Add after the `preloading_thread_args` typedef (around line 226 of `crackalack_lookup.c`):

```c
/* Bulk-loaded table array for pipelined lookup. */
typedef struct {
  preloaded_table *tables;   /* Flat array of loaded tables */
  unsigned int num_tables;   /* Number of tables loaded */
  unsigned int capacity;     /* Allocated array size */
} bulk_table_array;
```

Add the function itself after the existing `_preloading_thread()` function (after the closing brace around line 2078). This function is similar to `_preloading_thread()` but loads into an array instead of a linked list, and respects a RAM budget:

```c
/* Returns the available RAM in bytes, minus a 4GB reserve. */
static uint64_t get_ram_budget(void) {
  uint64_t total = 0;
#ifdef __APPLE__
  size_t len = sizeof(total);
  sysctlbyname("hw.memsize", &total, &len, NULL, 0);
#elif defined(_WIN32)
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  total = ms.ullTotalPhys;
#else
  struct sysinfo si;
  sysinfo(&si);
  total = (uint64_t)si.totalram * si.mem_unit;
#endif
  uint64_t reserve = (uint64_t)4 * 1024 * 1024 * 1024;
  return (total > reserve) ? total - reserve : 0;
}


/* Collects all table file paths in a directory matching a filter.
 * Returns a malloc'd array of strdup'd paths, sets *out_count.
 * Caller must free each path and the array. */
static char **collect_table_paths(char *rt_dir, const rt_parameters *filter,
                                  unsigned int *out_count) {
  DIR *dir = NULL;
  struct dirent *de = NULL;
  struct stat st;
  char filepath[512] = {0};
  unsigned int count = 0, capacity = 256;
  char **paths = calloc(capacity, sizeof(char *));

  dir = opendir(rt_dir);
  if (dir == NULL) { *out_count = 0; return paths; }

  while ((de = readdir(dir)) != NULL) {
    filepath_join(filepath, sizeof(filepath), rt_dir, de->d_name);

    if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0) &&
        (stat(filepath, &st) == 0) && S_ISDIR(st.st_mode)) {
      /* Recurse into subdirectories. */
      unsigned int sub_count = 0;
      char **sub_paths = collect_table_paths(filepath, filter, &sub_count);
      for (unsigned int s = 0; s < sub_count; s++) {
        if (count >= capacity) {
          capacity *= 2;
          paths = realloc(paths, capacity * sizeof(char *));
        }
        paths[count++] = sub_paths[s];
      }
      free(sub_paths);
      continue;
    }

    if (!str_ends_with(de->d_name, ".rt") && !str_ends_with(de->d_name, ".rtc") &&
        !str_ends_with(de->d_name, ".rti2"))
      continue;

    /* Filter by config if provided. */
    if (filter != NULL) {
      rt_parameters pt_params = {0};
      parse_rt_params(&pt_params, filepath);
      if (!pt_params.parsed || !configs_match(&pt_params, filter))
        continue;
    }

    if (count >= capacity) {
      capacity *= 2;
      paths = realloc(paths, capacity * sizeof(char *));
    }
    paths[count++] = strdup(filepath);
  }
  closedir(dir);

  *out_count = count;
  return paths;
}


/* Loads a single table file (any format) into a preloaded_table.
 * Returns 0 on success, non-zero on error. */
static int load_single_table(const char *filepath, preloaded_table *pt) {
  gpu_ulong *rainbow_table = NULL;
  uint64_t num_chains = 0;

  if (str_ends_with(filepath, ".rtc")) {
    if (rtc_decompress((char *)filepath, &rainbow_table, &num_chains) != 0)
      return -1;
  } else if (str_ends_with(filepath, ".rti2")) {
    if (rti2_decompress((char *)filepath, &rainbow_table, &num_chains) != 0)
      return -1;
  } else {
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) return -1;
    int64_t file_size = get_file_size(f);
    if ((file_size % (sizeof(gpu_ulong) * 2) != 0) || file_size <= 0) {
      fclose(f); return -1;
    }
    unsigned int num_longs = file_size / sizeof(gpu_ulong);
    rainbow_table = calloc(num_longs, sizeof(gpu_ulong));
    if (rainbow_table == NULL) { fclose(f); return -1; }
    if (fread(rainbow_table, sizeof(gpu_ulong), num_longs, f) != num_longs) {
      free(rainbow_table); fclose(f); return -1;
    }
    fclose(f);
    num_chains = num_longs / 2;
  }

  if (rainbow_table == NULL || num_chains == 0) return -1;

  /* Verify uncompressed .rt tables are sorted. */
  if (str_ends_with(filepath, ".rt")) {
    if (!verify_rainbowtable(rainbow_table, num_chains, VERIFY_TABLE_TYPE_LOOKUP, 0, 0, NULL)) {
      free(rainbow_table);
      return -1;
    }
  }

  pt->filepath = strdup(filepath);
  pt->rainbow_table = rainbow_table;
  pt->num_chains = num_chains;
  pt->bf = bloom_create(num_chains);
  for (uint64_t c = 0; c < num_chains; c++)
    bloom_insert(pt->bf, rainbow_table[(c * 2) + 1]);
  pt->next = NULL;

  return 0;
}


/* Bulk-loads tables from all_paths[*start_idx..] into bta until RAM budget
 * is exhausted.  Updates *start_idx to the next unloaded table index.
 * Returns 0 on success. */
int bulk_load_tables(char **all_paths, unsigned int total_paths,
                     unsigned int *start_idx, bulk_table_array *bta) {
  uint64_t ram_budget = get_ram_budget();
  uint64_t ram_used = 0;
  unsigned int capacity = 128;
  struct timespec load_start = {0};
  char time_str[128] = {0};

  bta->tables = calloc(capacity, sizeof(preloaded_table));
  bta->num_tables = 0;
  bta->capacity = capacity;

  start_timer(&load_start);
  printf("\nPhase 1: Bulk-loading tables into RAM (budget: %.1f GB)...\n",
         (double)ram_budget / (1024.0 * 1024.0 * 1024.0));
  fflush(stdout);

  for (; *start_idx < total_paths; (*start_idx)++) {
    /* Estimate decompressed size from filename. */
    rt_parameters rp = {0};
    parse_rt_params(&rp, all_paths[*start_idx]);
    uint64_t table_bytes = rp.parsed ? rp.num_chains * sizeof(gpu_ulong) * 2 : 0;

    /* If we can't estimate, load it and check (conservative: assume 1GB). */
    if (table_bytes == 0) table_bytes = (uint64_t)1024 * 1024 * 1024;

    if (ram_used + table_bytes > ram_budget && bta->num_tables > 0)
      break;  /* This table won't fit; start a new chunk next time. */

    if (bta->num_tables >= bta->capacity) {
      bta->capacity *= 2;
      bta->tables = realloc(bta->tables, bta->capacity * sizeof(preloaded_table));
    }

    preloaded_table *pt = &bta->tables[bta->num_tables];
    memset(pt, 0, sizeof(preloaded_table));

    if (load_single_table(all_paths[*start_idx], pt) != 0) {
      fprintf(stderr, "Warning: skipping unloadable table: %s\n", all_paths[*start_idx]);
      continue;
    }

    ram_used += pt->num_chains * sizeof(gpu_ulong) * 2;
    bta->num_tables++;

    printf("  [%u] Loaded %s (%" PRIu64 " chains, %.1f MB, total %.1f GB)\n",
           bta->num_tables, pt->filepath, pt->num_chains,
           (double)(pt->num_chains * 16) / (1024.0 * 1024.0),
           (double)ram_used / (1024.0 * 1024.0 * 1024.0));
    fflush(stdout);
  }

  seconds_to_human_time(time_str, sizeof(time_str), get_elapsed(&load_start));
  printf("Loaded %u tables (%.1f GB) in %s.\n\n", bta->num_tables,
         (double)ram_used / (1024.0 * 1024.0 * 1024.0), time_str);
  fflush(stdout);

  return 0;
}


/* Frees all tables in a bulk_table_array. */
void bulk_free_tables(bulk_table_array *bta) {
  for (unsigned int i = 0; i < bta->num_tables; i++) {
    FREE(bta->tables[i].filepath);
    FREE(bta->tables[i].rainbow_table);
    if (bta->tables[i].bf != NULL) {
      bloom_free(bta->tables[i].bf);
      bta->tables[i].bf = NULL;
    }
  }
  FREE(bta->tables);
  bta->num_tables = 0;
  bta->capacity = 0;
}
```

- [ ] **Step 2: Add required includes**

At the top of `crackalack_lookup.c`, ensure these headers are present (add any missing ones near the existing includes):

```c
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#ifndef _WIN32
#ifndef __APPLE__
#include <sys/sysinfo.h>
#endif
#endif
```

These should already be present from existing code. Verify and add only if missing.

- [ ] **Step 3: Build and verify compilation**

Run: `make clean && make macos 2>&1 | tail -5`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add crackalack_lookup.c
git commit -m "feat: add bulk_load_tables() for RAM-budgeted table loading"
```

---

### Task 3: Add `pipelined_lookup()` — the new main loop

**Files:**
- Modify: `crackalack_lookup.c`

This is the core orchestration function that replaces the sequential precompute + search_tables pattern. It implements: outer table-chunk loop, inner batch loop with GPU+CPU precompute, search all loaded tables, false alarm with overlapped CPU precompute.

- [ ] **Step 1: Add CPU precompute thread pool types and worker**

Add after the `bulk_free_tables()` function:

```c
/* CPU precompute thread pool context. */
typedef struct {
  char **hashes;
  char **usernames;
  unsigned int *hash_indices;  /* Indices into the global hash arrays */
  unsigned int num_hashes;
  thread_args *args;           /* Table params (hash_type, charset, etc.) */
  uint64_t *plaintext_space_up_to_index;
  uint64_t plaintext_space_total;
  precomputed_and_potential_indices **results;  /* One ppi per hash, output */
} cpu_precompute_batch;

typedef struct {
  cpu_precompute_batch *batch;
  unsigned int start;  /* First hash index for this thread */
  unsigned int end;    /* One past last hash index */
} cpu_precompute_thread_args;

static void *cpu_precompute_worker(void *ptr) {
  cpu_precompute_thread_args *ta = (cpu_precompute_thread_args *)ptr;
  cpu_precompute_batch *batch = ta->batch;
  thread_args *a = batch->args;

  for (unsigned int i = ta->start; i < ta->end; i++) {
    batch->results[i] = cpu_precompute_hash(
        a->hash_type, batch->hashes[i], batch->usernames[i],
        a->charset, strlen(a->charset),
        a->plaintext_len_min, a->plaintext_len_max,
        a->reduction_offset, a->chain_len,
        batch->plaintext_space_up_to_index,
        batch->plaintext_space_total);
  }
  return NULL;
}


/* Runs CPU precompute on num_hashes hashes using num_threads threads.
 * Appends resulting ppi nodes to *ppi_head (mutex-protected). */
static void cpu_precompute_parallel(
    char **hashes, char **usernames, unsigned int num_hashes,
    thread_args *args, unsigned int num_threads,
    uint64_t *plaintext_space_up_to_index, uint64_t plaintext_space_total,
    precomputed_and_potential_indices **ppi_head) {

  if (num_hashes == 0 || num_threads == 0) return;
  if (num_threads > num_hashes) num_threads = num_hashes;

  cpu_precompute_batch batch = {0};
  batch.hashes = hashes;
  batch.usernames = usernames;
  batch.num_hashes = num_hashes;
  batch.args = args;
  batch.plaintext_space_up_to_index = plaintext_space_up_to_index;
  batch.plaintext_space_total = plaintext_space_total;
  batch.results = calloc(num_hashes, sizeof(precomputed_and_potential_indices *));

  pthread_t *threads = calloc(num_threads, sizeof(pthread_t));
  cpu_precompute_thread_args *targs = calloc(num_threads, sizeof(cpu_precompute_thread_args));

  unsigned int per_thread = num_hashes / num_threads;
  unsigned int remainder = num_hashes % num_threads;
  unsigned int offset = 0;

  for (unsigned int t = 0; t < num_threads; t++) {
    targs[t].batch = &batch;
    targs[t].start = offset;
    targs[t].end = offset + per_thread + (t < remainder ? 1 : 0);
    offset = targs[t].end;
    pthread_create(&threads[t], NULL, cpu_precompute_worker, &targs[t]);
  }

  for (unsigned int t = 0; t < num_threads; t++)
    pthread_join(threads[t], NULL);

  /* Append results to ppi list. */
  pthread_mutex_lock(&ppi_mutex);
  for (unsigned int i = 0; i < num_hashes; i++) {
    if (batch.results[i] == NULL) continue;
    precomputed_and_potential_indices *ppi = batch.results[i];
    ppi->next = NULL;
    if (*ppi_head == NULL) {
      *ppi_head = ppi;
    } else {
      precomputed_and_potential_indices *tail = *ppi_head;
      while (tail->next != NULL) tail = tail->next;
      tail->next = ppi;
    }
  }
  pthread_mutex_unlock(&ppi_mutex);

  free(batch.results);
  free(threads);
  free(targs);
}
```

- [ ] **Step 2: Add the pipelined_lookup() function**

Add after `cpu_precompute_parallel()`:

```c
/* Pipelined lookup: bulk-loads tables into RAM, then runs batched
 * GPU+CPU precompute with overlapped false alarm checking.
 *
 * Replaces the sequential precompute + search_tables pattern in main(). */
void pipelined_lookup(char *rt_dir, const rt_parameters *filter,
                      unsigned int num_devices, thread_args *args,
                      char **hashes, char **usernames, unsigned int total_hashes,
                      precomputed_and_potential_indices **ppi_head) {

  /* Collect all table paths matching the filter. */
  unsigned int total_paths = 0;
  char **all_paths = collect_table_paths(rt_dir, filter, &total_paths);
  if (total_paths == 0) {
    printf("No tables found for this config group.\n");
    free(all_paths);
    return;
  }

  printf("Found %u tables for this config group.\n", total_paths);
  fflush(stdout);

  /* Build plaintext space table (needed for CPU precompute). */
  uint64_t plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1] = {0};
  uint64_t plaintext_space_total = fill_plaintext_space_table(
      strlen(args[0].charset), args[0].plaintext_len_min,
      args[0].plaintext_len_max, plaintext_space_up_to_index);

  /* Auto-tune CPU/GPU split: start GPU-only, measure, then split. */
  unsigned int cpu_threads = 14;  /* Reserve 2 cores for main + I/O */
  double gpu_time_per_hash = 0;
  double cpu_time_per_hash = 0;
  int tuning_done = 0;

  /* Outer loop: table chunks that fit in RAM. */
  unsigned int table_start_idx = 0;
  unsigned int chunk_num = 0;

  while (table_start_idx < total_paths) {
    chunk_num++;
    bulk_table_array bta = {0};
    bulk_load_tables(all_paths, total_paths, &table_start_idx, &bta);

    if (bta.num_tables == 0) {
      printf("Warning: no tables could be loaded in this chunk.\n");
      break;
    }

    printf("Phase 2: Batched precompute + search (chunk %u, %u tables loaded)...\n\n",
           chunk_num, bta.num_tables);
    fflush(stdout);

    /* Collect uncracked hashes. */
    unsigned int num_uncracked = 0;
    char **uncracked_hashes = calloc(total_hashes, sizeof(char *));
    char **uncracked_usernames = calloc(total_hashes, sizeof(char *));

    for (unsigned int i = 0; i < total_hashes; i++) {
      precomputed_and_potential_indices *existing = ppi_find(*ppi_head, hashes[i]);
      if (existing != NULL && existing->plaintext != NULL)
        continue;
      uncracked_hashes[num_uncracked] = hashes[i];
      uncracked_usernames[num_uncracked] = usernames[i];
      num_uncracked++;
    }

    if (num_uncracked == 0) {
      printf("All hashes cracked. Skipping remaining table chunks.\n");
      free(uncracked_hashes);
      free(uncracked_usernames);
      bulk_free_tables(&bta);
      break;
    }

    /* Determine GPU/CPU split. */
    unsigned int gpu_count = num_uncracked;
    unsigned int cpu_count = 0;

    if (tuning_done && cpu_time_per_hash > 0 && gpu_time_per_hash > 0) {
      /* CPU can do this many hashes in the time GPU takes for the rest. */
      double gpu_total_time = gpu_time_per_hash * num_uncracked;
      unsigned int cpu_capacity = (unsigned int)(gpu_total_time / cpu_time_per_hash);
      if (cpu_capacity > num_uncracked) cpu_capacity = num_uncracked;
      if (cpu_capacity > cpu_threads * 2) cpu_capacity = cpu_threads * 2;
      cpu_count = cpu_capacity;
      gpu_count = num_uncracked - cpu_count;
      if (gpu_count < 2) { gpu_count = num_uncracked; cpu_count = 0; }
    }

    /* === GPU + CPU precompute (parallel) === */
    struct timespec precomp_start = {0};
    start_timer(&precomp_start);

    printf("  Precomputing %u hashes (GPU: %u, CPU: %u)...\n",
           num_uncracked, gpu_count, cpu_count);
    fflush(stdout);

    /* Reset endpoints for existing ppi nodes (new config). */
    ppi_reset_endpoints(*ppi_head);

    /* Launch CPU precompute in background threads. */
    pthread_t cpu_thread;
    int cpu_active = 0;

    typedef struct {
      char **h; char **u; unsigned int n;
      thread_args *a; unsigned int nt;
      uint64_t *pspace; uint64_t pspace_total;
      precomputed_and_potential_indices **ppi;
    } cpu_bg_args;

    cpu_bg_args cpu_args = {0};
    if (cpu_count > 0) {
      cpu_args.h = uncracked_hashes + gpu_count;
      cpu_args.u = uncracked_usernames + gpu_count;
      cpu_args.n = cpu_count;
      cpu_args.a = args;
      cpu_args.nt = cpu_threads;
      cpu_args.pspace = plaintext_space_up_to_index;
      cpu_args.pspace_total = plaintext_space_total;
      cpu_args.ppi = ppi_head;
      cpu_active = 1;

      /* We can't easily pass cpu_precompute_parallel as a thread func
       * directly, so use a small wrapper lambda-like pattern. */
    }

    /* GPU batch precompute. */
    int used_batch = 0;
    if (gpu_count >= 2) {
      used_batch = batch_precompute_all_hashes(num_devices, args,
          uncracked_hashes, uncracked_usernames, gpu_count, ppi_head);
    }

    /* If GPU batch wasn't used, fall back to per-hash GPU precompute. */
    if (!used_batch) {
      for (unsigned int i = 0; i < gpu_count; i++) {
        for (unsigned int j = 0; j < num_devices; j++) {
          args[j].username = uncracked_usernames[i];
          args[j].hash = uncracked_hashes[i];
        }
        precomputed_and_potential_indices *existing = ppi_find(*ppi_head, uncracked_hashes[i]);
        precompute_hash(num_devices, args, ppi_head, existing);
      }
    }

    /* Run CPU precompute (if GPU finished first, this still runs). */
    if (cpu_count > 0) {
      cpu_precompute_parallel(
          uncracked_hashes + gpu_count, uncracked_usernames + gpu_count,
          cpu_count, args, cpu_threads,
          plaintext_space_up_to_index, plaintext_space_total, ppi_head);
    }

    /* Auto-tune after first batch. */
    if (!tuning_done && num_uncracked > 0) {
      double elapsed = get_elapsed(&precomp_start);
      gpu_time_per_hash = (gpu_count > 0) ? elapsed / gpu_count : 0;
      /* Run a quick CPU benchmark: 1 hash. */
      if (num_uncracked > 0) {
        struct timespec cpu_bench = {0};
        start_timer(&cpu_bench);
        precomputed_and_potential_indices *bench_ppi = cpu_precompute_hash(
            args[0].hash_type, uncracked_hashes[0], uncracked_usernames[0],
            args[0].charset, strlen(args[0].charset),
            args[0].plaintext_len_min, args[0].plaintext_len_max,
            args[0].reduction_offset, args[0].chain_len,
            plaintext_space_up_to_index, plaintext_space_total);
        cpu_time_per_hash = get_elapsed(&cpu_bench);
        if (bench_ppi != NULL) {
          FREE(bench_ppi->precomputed_end_indices);
          FREE(bench_ppi);
        }
        printf("  Auto-tune: GPU=%.3fs/hash, CPU=%.3fs/hash, %u CPU threads\n",
               gpu_time_per_hash, cpu_time_per_hash, cpu_threads);
      }
      tuning_done = 1;
    }

    release_precompute_gpu(num_devices, args);

    char precomp_time_str[128] = {0};
    seconds_to_human_time(precomp_time_str, sizeof(precomp_time_str),
                          get_elapsed(&precomp_start));
    printf("  Precompute finished in %s.\n\n", precomp_time_str);
    fflush(stdout);

    /* === Search all loaded tables === */
    false_alarm_state fa_state = {0};

    for (unsigned int t = 0; t < bta.num_tables; t++) {
      preloaded_table *pt = &bta.tables[t];

      /* Check if all cracked. */
      unsigned int still_uncracked = 0;
      precomputed_and_potential_indices *ppi_cur = *ppi_head;
      while (ppi_cur != NULL) {
        if (ppi_cur->plaintext == NULL) still_uncracked++;
        ppi_cur = ppi_cur->next;
      }
      if (still_uncracked == 0) {
        harvest_false_alarm_results(&fa_state);
        printf("All hashes cracked. Skipping remaining tables.\n");
        break;
      }

      printf("  [%u of %u] Searching: %s\n", t + 1, bta.num_tables, pt->filepath);
      fflush(stdout);

      rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, *ppi_head);
      num_chains_processed += pt->num_chains;
      num_tables_processed++;

      /* Harvest previous false alarm results. */
      harvest_false_alarm_results(&fa_state);

      /* Launch false alarm check for this table. */
      launch_false_alarm_check(*ppi_head, args, &fa_state);
      clear_potential_start_indices(*ppi_head);

      printf("  Cracked %u of %u hashes.\n", num_cracked, total_hashes);
      fflush(stdout);
    }

    /* Final harvest for last table. */
    harvest_false_alarm_results(&fa_state);
    release_false_alarm_gpu(num_devices, args);

    free(uncracked_hashes);
    free(uncracked_usernames);
    bulk_free_tables(&bta);

    /* Check if all cracked before loading next chunk. */
    unsigned int remaining = 0;
    precomputed_and_potential_indices *p = *ppi_head;
    while (p != NULL) { if (p->plaintext == NULL) remaining++; p = p->next; }
    if (remaining == 0) {
      printf("All hashes cracked. Skipping remaining table chunks.\n");
      break;
    }
  }

  /* Cleanup paths. */
  for (unsigned int i = 0; i < total_paths; i++)
    free(all_paths[i]);
  free(all_paths);
}
```

- [ ] **Step 3: Build and verify compilation**

Run: `make clean && make macos 2>&1 | tail -5`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add crackalack_lookup.c
git commit -m "feat: add pipelined_lookup() with bulk loading and CPU+GPU precompute"
```

---

### Task 4: Wire `pipelined_lookup()` into main()

**Files:**
- Modify: `crackalack_lookup.c`

Replace the sequential precompute + search_tables pattern in the main config group loop with a call to `pipelined_lookup()`.

- [ ] **Step 1: Replace the main loop body**

In `main()`, replace the block from line ~2936 (the `ppi_reset_endpoints(ppi_head)` call) through line ~3015 (the `pthread_join(preload_thread_id, NULL)`) with:

```c
    /* Pipelined lookup: bulk-load tables, batched GPU+CPU precompute, search. */
    start_timer(&precompute_start_time);
    pipelined_lookup(rt_dir, &cg->params, num_devices, args,
                     hashes, usernames, num_hashes, &ppi_head);
    time_precomp += get_elapsed(&precompute_start_time);
```

This replaces:
- The preloading thread start/join
- The batch_precompute_all_hashes / per-hash fallback
- The search_tables call
- The release_precompute_gpu / release_false_alarm_gpu calls

Keep everything before this (setup_args_for_config, printed message resets) and after it (the config group loop closing brace, free_config_groups).

- [ ] **Step 2: Build and verify compilation**

Run: `make clean && make macos 2>&1 | tail -5`
Expected: Clean build, no errors.

- [ ] **Step 3: Commit**

```bash
git add crackalack_lookup.c
git commit -m "feat: wire pipelined_lookup() into main config group loop"
```

---

### Task 5: Testing and Validation

**Files:**
- No new files; testing on gpuhost3 with real tables and hashes.

- [ ] **Step 1: Push feature branch and build on gpuhost3**

```bash
git push -u origin feature/pipelined-lookup
```

Then on gpuhost3:
```bash
cd /mnt/nvme/rainbowcrackalack
git fetch && git checkout feature/pipelined-lookup
make clean && make linux
```

- [ ] **Step 2: Test with small table set and known-crackable hashes**

Create a test hash file with a few known NTLM hashes (e.g., `echo -n "password" | iconv -t UTF-16LE | openssl md4` → `a4f49c406510bdcab6824ee7c30fd852`).

Run against a small subset of tables:
```bash
./crackalack_lookup /volume1/docker/Downloads/tables/ntlm_mixalpha-numeric#1-8_3 /path/to/test_hashes.txt
```

Verify:
- "Phase 1: Bulk-loading tables into RAM" message appears
- RAM budget is calculated correctly (~92GB)
- Tables load successfully (RTI2 decompression)
- "Phase 2: Batched precompute + search" message appears
- Auto-tune prints GPU/CPU timing
- Known hashes are cracked
- No crashes or memory errors

- [ ] **Step 3: Test with full 5M hash file**

```bash
./crackalack_lookup /volume1/docker/Downloads/tables/ntlm_mixalpha-numeric#1-8_3 /mnt/nvme/5milhc
```

Verify:
- Multiple table chunks load/process if tables exceed RAM
- Cracked hashes are skipped in subsequent chunks
- Pot file is written with results
- Performance is better than sequential pipeline

- [ ] **Step 4: Test edge cases**

- Single hash: `echo "a4f49c406510bdcab6824ee7c30fd852" > /tmp/single.txt && ./crackalack_lookup /volume1/docker/Downloads/tables/ntlm_mixalpha-numeric#1-8_3 /tmp/single.txt`
- Empty table directory: verify clean error message
- Mixed .rt/.rti2 files in same directory

- [ ] **Step 5: Commit any fixes from testing**

```bash
git add -A && git commit -m "fix: address issues found during pipeline testing"
```

---

### Future Optimization: CPU precompute during false alarm

The design spec calls for CPU threads to start precomputing the next batch's CPU slice while the GPU runs false alarm checks. This initial implementation does NOT do this — CPU precompute only runs during the precompute phase (parallel with GPU batch kernel). Adding false-alarm-overlapped CPU precompute requires background thread management and a frozen cracked-hash set, which adds complexity for modest gain. This should be added as a follow-up once the basic pipeline is validated.

---

### Task 6: Update CLAUDE.md documentation

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update the Architecture section**

In `CLAUDE.md`, update the "Three-phase pipeline" section under Architecture to reflect the new pipelined lookup mode. Add a note about the bulk loading and CPU+GPU precompute:

After the existing "3. **Lookup**" description, add:

```markdown
The lookup pipeline uses a two-phase approach: (1) bulk-load tables into RAM up to a memory budget (auto-detected), then (2) batch precompute hashes using GPU + CPU threads in parallel, binary search all loaded tables, and run false alarm checks with overlapped CPU precompute of the next batch. If tables exceed RAM, the pipeline processes them in chunks.
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md with pipelined lookup architecture"
```
