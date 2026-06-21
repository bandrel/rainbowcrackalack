# Few-Hash Bloom Auto-Skip + Decompress Alloc Fix — Design

Date: 2026-06-20
Status: Approved (design)
Branch: `perf/fewhash-bloom-autoskip`

## Problem

A lookup of a *small* number of hashes against a large table set (measured: 2
NetNTLMv1-7 hashes vs 5.0 TB / 16388 tables on dell3) spends the overwhelming
majority of wall-clock building a **per-table bloom filter**, not decompressing,
reading, searching, or false-alarm-checking.

Profiling (40-table / 13 GB subset, dell3):

| run | wall | user CPU |
|-----|------|----------|
| default (bloom on) | 45.6 s | 340 s |
| `--bloom-fpr 0` (bloom off) | 8.1 s | 41 s |

`gdb` sampling showed the loader threads in `rtc_decompress` + `bloom_insert`;
`top -H` showed 8 loader threads pegged while the search/FA thread idled.
`--fa-batch` 16k→1M changed nothing; cold vs warm cache differed by ~6% (not
I/O-bound). So the bloom build is ~85% of wall time for this workload — a
**net pessimization** when there are few queries to amortize it against.

Disabling the bloom is a **5.6× speedup** (full 5 TB run extrapolates ~4 h →
~45 min) and cannot cause false negatives: the bloom is only a pre-filter that
skips definite-misses before binary search; without it, the search simply checks
every candidate.

After the bloom is gone, the residual bottleneck is **decompression, memory-
bandwidth-bound** (~8 s/40 tables; `RCRT_LOAD_THREADS` 8/16/24 all ≈ 8.0 s).
`rtc_decompress` `calloc`-zeroes a ~536 MB output buffer per table that the
decode loop then fully overwrites — wasted write traffic on the BW-bound path.

## Goal

1. **A — Auto-skip the per-table bloom when it is not worth building**, so the
   5.6× is automatic and safe, without users needing `--bloom-fpr 0`, and without
   penalizing many-hash runs (where the bloom does pay off).
2. **B — Stop zeroing the decompression output buffer** (`calloc`→`malloc`) to
   reduce memory-bandwidth waste on the now-dominant decompression step.

### Non-goals
- GPU-offload decompression (a larger follow-on that only attacks the ~45-min
  residual after A).
- Changing the bloom data structure, the search, or the false-alarm path.
- Markov/MD5/NTLM-specific tuning (the logic is hash-type-agnostic).

## Component A — Bloom auto-skip

### Current mechanism (unchanged pieces)
- Global `double bloom_target_fpr` (default `0.01`), set by `--bloom-fpr`.
- `load_single_table()` (crackalack_lookup.c ~2376-2379) builds the per-table
  filter: `pt->bf = bloom_create(num_chains, bloom_target_fpr);` then a loop
  `bloom_insert(pt->bf, endpoint)` over all `num_chains` endpoints.
- `bloom_create` returns `NULL` when `target_fpr <= 0`; `rt_binary_search(...,
  bloom_filter *bf, ...)` already treats `bf == NULL` as "no pre-filter".
- So a fully-functional **bloom-less path already exists** (`--bloom-fpr 0`).

### Break-even
Building touches every endpoint `k` times (`≈ num_chains × k` ops), where `k` is
the bloom's hash count — **the same optimal `k` `bloom_create` computes** for the
given `(num_chains, fpr)` (the observed default run reported `k = 11`; the
decision must derive `k` the same way `bloom_create` does, not hardcode a value).
The bloom's only benefit is letting the search skip queries, worth
`≈ num_queries × log₂(num_chains)`, where `num_queries` is the total number of
precomputed endpoint lookups across all uncracked hashes
(`≈ num_uncracked_hashes × chain_len`).

**Build the bloom only when `num_queries × log₂(num_chains) ≥ num_chains × k`.**
For the published NetNTLMv1-7 tables (`num_chains≈33.5M`, `chain_len=881689`,
`k≈11`) this threshold lands on the order of low-tens of hashes, consistent with
the measured 5.6× at 2 hashes. The implementation must read `k` from the created
filter (or a shared helper) so the break-even tracks the real build cost.

### New pure decision function (in `bloom.c` / `bloom.h`)
```c
/* Returns 1 if building a per-table bloom is expected to save more search work
 * than it costs to build, else 0.  num_queries = total precomputed endpoint
 * lookups across all uncracked hashes; num_chains = endpoints in this table;
 * target_fpr = configured bloom FPR (<=0 always returns 0). */
int bloom_is_worthwhile(uint64_t num_queries, uint64_t num_chains, double target_fpr);
```
Implementation: `target_fpr<=0 || num_chains==0 → 0`; `k = ceil(-log2(target_fpr))`
(min 1); return `(double)num_queries * log2((double)num_chains) >= (double)num_chains * k`.

### Wiring
- Track whether `--bloom-fpr` was given explicitly with a value `> 0`
  (`bloom_fpr_forced`). Semantics:
  - `--bloom-fpr 0` → never build (existing behavior).
  - `--bloom-fpr X` (`X>0`, explicit) → **always** build with `X` (manual
    override; skips the auto-decision).
  - flag absent → **auto** (the new default): build iff `bloom_is_worthwhile`.
- Expose `num_queries`: after batch precompute completes (which happens before
  the streaming loader consumes tables), compute and store a global
  `g_total_queries` = sum of `num_precomputed_end_indices` over the ppi list.
- In `load_single_table()`, gate the build:
  ```c
  if (bloom_fpr_forced ||
      (bloom_target_fpr > 0.0 && bloom_is_worthwhile(g_total_queries, num_chains, bloom_target_fpr))) {
      pt->bf = bloom_create(num_chains, bloom_target_fpr);
      for (... ) bloom_insert(pt->bf, rainbow_table[(c*2)+1]);
  } else {
      pt->bf = NULL;   /* search handles NULL exactly like --bloom-fpr 0 */
  }
  ```
  `bloom_free(NULL)` must be a safe no-op (verify; add a guard if not).
- The startup banner line that reports bloom settings should state whether the
  bloom was auto-skipped (for operator visibility).

### Edge cases
- `g_total_queries == 0` (all hashes already cracked / nothing to do): skip bloom
  (worthwhile returns 0); the run does little work anyway.
- Many-hash run above threshold: behaves exactly as today (bloom built).
- Per-table `num_chains` varies slightly across a config group; the decision is
  evaluated per table with that table's `num_chains`, so it is always consistent
  with the table actually being searched.

## Component B — `rtc_decompress` allocation fix

`rtc_decompress.c`: change
```c
uncompressed_table = calloc((size_t)num_chains, sizeof(uint64_t) * 2);
```
to a `malloc` of the same byte count **with an explicit overflow guard** (mirror
the existing `chains_bytes` overflow check):
```c
size_t out_bytes = (size_t)num_chains * sizeof(uint64_t) * 2;
if (num_chains != 0 && out_bytes / (sizeof(uint64_t) * 2) != num_chains) { /* overflow → error path */ }
uncompressed_table = malloc(out_bytes);
```
Safe because the decode loop writes both `s` and `e` for every chain, fully
populating all `2 * num_chains` entries — nothing reads an uninitialized slot.
The error path that `free()`s `uncompressed_table` on failure is unchanged.

(Optional, lower priority, separate task: replace the per-iteration
`buf[0]=0;buf[1]=0;memcpy(buf, …, chain_size)` with direct 64-bit loads, guarding
the final chain against a 16-byte over-read. Kept out of the core change because
it needs careful bounds handling; only pursue if B's malloc fix doesn't move the
needle enough.)

## Testing

- **A — unit (no GPU):** test `bloom_is_worthwhile` in `test_bloom.c` with
  synthetic params: few queries + huge table → 0; many queries → 1; `fpr<=0` → 0;
  `num_chains==0` → 0; a boundary case around the threshold.
- **A — integration (dell3, GPU):** on the 40-table subset, a 2-hash run must
  auto-skip the bloom and match the `--bloom-fpr 0` wall (~8 s, not ~45 s); an
  explicit `--bloom-fpr 0.01` must still build it (~45 s); cracks identical in
  both cases.
- **B — correctness:** `rtc_decompress` output must be **byte-identical** before
  and after. Verify by sha256 of a `crackalack_rtc2rt` `.rt` output on a fixed
  `.rtc` (pre-change vs post-change), and/or the existing `test_decompress`
  unit test.
- **Both — benchmark:** re-run the 40-table subset (default vs this build) to
  confirm the auto-skip reproduces the 5.6× and the malloc fix shaves the
  decompression residual, with no correctness regression.

## Risks
- Misjudging the break-even could disable the bloom for a workload where it
  helps. Mitigation: the formula is conservative (build only when savings clearly
  exceed cost), `--bloom-fpr X` forces it on, and many-hash behavior is unchanged.
- `g_total_queries` must be populated **before** the loader builds blooms.
  Implementation must confirm precompute precedes table loading in
  `streaming_lookup` (it does today); if a config group re-precomputes per group,
  recompute `g_total_queries` per group before its loader starts.
