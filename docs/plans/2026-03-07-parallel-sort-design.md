# Parallel Sort Design

**Date:** 2026-03-07
**File:** `crackalack_sort.c`

## Problem

`crackalack_sort` currently processes files sequentially even when given multiple arguments. Sorting large rainbow table files (~1 GB each) one at a time leaves CPU cores and I/O bandwidth unused.

## Solution

Replace the sequential loop with a thread pool. Workers pull filenames from a shared queue and sort concurrently. GPU access is serialized via a mutex; CPU qsort runs fully parallel.

## Architecture

```
main
 ├─ parse --jobs N (optional; 0 = auto)
 ├─ compute job count
 ├─ init work_queue { filenames[], gpu_mutex, queue_mutex, failures, failures_mutex }
 ├─ spawn N pthreads → each loops: lock queue, grab next file, unlock, sort, repeat
 └─ join threads, print failure count, return 1 if any failed
```

Two mutexes:
- `queue_mutex` - protects the shared file index
- `gpu_mutex` - serializes `gpu_sort()` calls (one bitonic sort at a time; concurrent GPU sorts hurt throughput)

CPU `qsort` runs without holding `gpu_mutex`, so multiple CPU sorts can run in parallel.

## Dynamic Job Count

When `--jobs` is not given (or `--jobs 0`):

1. **Free RAM** - `sysconf(_SC_AVPHYS_PAGES) * page_size` (POSIX) or `GlobalMemoryStatusEx` (Windows)
2. **Max file size** - `stat()` all input files, take the largest
3. **CPU cores** - `sysconf(_SC_NPROCESSORS_ONLN)` (POSIX) or `GetSystemInfo` (Windows)
4. **Compute**: `jobs = min(free_ram * 0.8 / max_file_size, cpu_cores, num_files)`, clamped to at least 1

Using max file size (not average) prevents worst-case OOM when all large files are in flight simultaneously. The 0.8 factor leaves headroom for OS and other processes.

`--jobs N` where N > num_files is silently capped to num_files.

## CLI

```
crackalack_sort [--jobs N] table1.rt [table2.rt ...]
```

`--jobs 0` is equivalent to omitting `--jobs` (auto-detect).

## Error Handling

- Per-worker failure count, merged into shared `failures` (mutex-protected) at thread exit
- Same error messages and color output as today
- Progress lines may interleave across threads; each `printf`/`fflush` pair is atomic at the line level

## Platform Compatibility

- Uses pthreads throughout (already a project dependency via `compat.h`)
- RAM and core detection use `#ifdef _WIN32` / `#else` branches, matching the existing pattern in the file
- No new dependencies
