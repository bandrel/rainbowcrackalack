# Pipelined Lookup with CPU+GPU Precompute and Bulk Table Loading

## Problem

The current `crackalack_lookup` pipeline is strictly sequential: precompute ALL hashes on GPU, then stream tables one at a time from NFS and search. This leaves the GPU idle during table I/O, the CPU idle during GPU precompute, and forces the full precompute to complete before any search begins. With 5M+ hashes and 8TB of tables on slow NFS (~39 MB/s), this wastes significant time.

## Design

### Overall Architecture

Two-phase pipeline with an outer loop for table chunks that fit in RAM (~96GB on dell3, ~4GB reserved for overhead):

```
For each table chunk that fits in RAM:
  Phase 1: LOAD
    Bulk-load tables from NFS into RAM (blocking I/O, no GPU)

  Phase 2: BATCH LOOP
    For each batch of uncracked hashes:
      1. GPU precomputes batch_gpu hashes
         + CPU threads precompute batch_cpu hashes    (parallel)
      2. Binary search all loaded tables              (CPU)
      3. GPU false alarm check
         + CPU precomputes next batch's CPU slice      (parallel)
      4. Harvest false alarm results
      5. Mark cracked hashes, remove from work set
```

After all batches complete for a chunk, free the tables and load the next chunk.

### Steady-State Timeline

```
GPU:  [false alarm prev]  [batch precompute]         [false alarm]
CPU:  [precompute next ]  [binary search all tables] [precompute next]
```

## Component Details

### 1. Hash Partitioning and CPU Precompute

Each batch splits uncracked hashes into GPU and CPU slices:

- **GPU slice:** Passed to existing `batch_precompute_all_hashes()`. Gets the vast majority of hashes.
- **CPU slice:** Distributed across 14 worker threads (16 cores minus main thread and I/O margin). Each thread calls `generate_rainbow_chain()` from `cpu_rt_functions.c` to walk the chain from each position, producing candidate endpoints.

Both produce `precomputed_and_potential_indices` (ppi) nodes appended to the same linked list, protected by the existing `ppi_mutex`.

**Auto-tuning the GPU/CPU split:**
1. First batch: GPU-only, measure wall time.
2. Second batch: CPU-only on 1 hash, measure wall time.
3. Compute ratio: `cpu_hashes = (gpu_time / cpu_per_hash_time) * num_cpu_threads`.
4. Subsequent batches use this split. Re-tune periodically if needed.

**CPU precompute worker function** (per hash, per thread):
```
For position p in 0..chain_len-1:
  index = hash_to_index(hash_bytes, hash_len, reduction_offset, pspace_total, p)
  For i in (p+1)..chain_len-1:
    index_to_plaintext(index, ...) -> plaintext
    ntlm_hash(plaintext, ...) -> hash_buf
    index = hash_to_index(hash_buf, ..., i)
  Store index as candidate endpoint at position p
```

This mirrors the GPU kernel logic using existing CPU functions.

### 2. Bulk Table Loading

Replaces the current streaming preloader (MAX_PRELOAD_NUM=4) with a bulk loader:

- Query available RAM via `sysinfo()` (Linux) / `sysctl()` (macOS), subtract 4GB reserve.
- Scan table directory recursively, collect `.rt`/`.rtc`/`.rti2` file paths.
- Parse filenames to get expected decompressed size (`num_chains * 16` bytes).
- Load tables greedily until the next table would exceed the RAM budget.
- Store in a **flat array** of `preloaded_table` structs (random access, not linked list).
- Build bloom filters on endpoints during loading (same as current).

When a table chunk is exhausted, free the array and load the next chunk.

The existing mutex/condvar preloading synchronization is removed for this path. Loading is fully blocking (Phase 1 completes before Phase 2 starts).

### 3. Search Phase

For each batch after precompute completes:

- Iterate over all loaded tables in the array.
- For each table, run existing multi-threaded CPU binary search (`rt_binary_search`) against all ppi nodes from this batch.
- Collect all candidate (start_index, position, ppi) tuples for false alarm checking.

Unchanged from current logic except it loops over the table array instead of pulling from a preloading queue.

### 4. False Alarm Pipeline

- Launch existing GPU false alarm kernel (`launch_false_alarm_check`) on all candidates.
- **While GPU is busy:** CPU threads start precomputing the next batch's CPU slice.
- The "cracked" set is frozen at false alarm dispatch time — cracks found by this false alarm check apply to the batch after next (the one currently being CPU-precomputed is based on the frozen set, and any newly cracked hashes are removed before the full next-batch precompute begins).
- On GPU completion, harvest results, save cracked hashes, update the uncracked hash set.

### 5. Cracked Hash Skipping

Maintain a boolean array `is_cracked[num_hashes]`. After each false alarm harvest:
- Set `is_cracked[i] = 1` for each cracked hash.
- Before each batch's precompute, rebuild the uncracked hash list by skipping cracked entries.
- The CPU "next batch" precompute that started during false alarm uses the frozen set — if a hash it precomputed gets cracked, the ppi node is simply skipped during search (wasted work but no correctness issue).

### 6. Batch Size

All uncracked hashes per batch (no artificial sub-batching). The GPU batch kernel already handles chunked dispatch internally for watchdog limits. The only limit is GPU memory for the output buffer, which the existing code already checks and falls back to per-hash if exceeded.

## Files Modified

| File | Change |
|------|--------|
| `crackalack_lookup.c` | New `pipelined_lookup()` function replacing the current main loop; bulk table loader; CPU precompute thread pool; batch orchestration |
| `cpu_rt_functions.c` | New `cpu_precompute_hash()` function that produces a ppi node from a single hash (wraps existing chain-walk primitives). Takes `thread_args *` for table parameters (hash_type, charset, reduction_offset, chain_len, plaintext_space, etc.) |
| `cpu_rt_functions.h` | Declare `cpu_precompute_hash()` |

## Files NOT Modified

- GPU kernels (unchanged)
- `batch_precompute_all_hashes()` (called as-is)
- `rt_binary_search()` (called as-is)
- `launch_false_alarm_check()` / `harvest_false_alarm_results()` (called as-is)
- `rti2_decompress.c`, `rtc_decompress.c` (called as-is during bulk load)

## Testing

1. **Correctness:** Run against a small known table set with known-crackable hashes. Verify same cracks found as current sequential pipeline.
2. **Memory:** Verify RAM budget calculation works on dell3 (96GB). Load ~75 tables, confirm no OOM.
3. **Performance:** Compare wall-clock time against current pipeline with same table set and hash file. Expect significant speedup from I/O overlap and CPU+GPU parallelism.
4. **Edge cases:**
   - Single hash (CPU slice = 0, GPU-only path)
   - All hashes cracked before all table chunks processed (early exit)
   - Table chunk too large for any table to fit (error message)
   - Mixed .rt/.rtc/.rti2 tables in the same directory

## Non-Goals

- Multi-GPU support (single GPU assumed, same as current)
- Changing the false alarm check to CPU (stays GPU)
- Changing the binary search algorithm (stays CPU)
