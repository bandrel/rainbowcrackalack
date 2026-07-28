# zstd-19 on `.rt` — Native Round Trip + Benchmarks (Design)

**Date:** 2026-07-27
**Status:** approved for planning
**Supersedes/extends:** `docs/superpowers/plans/2026-07-21-zstd-ingest.md` (read side only, branch `feat/zstd-ingest`, unmerged)

## Goal

Own the full `.rt` ↔ `.rt.zst` round trip inside the codebase at zstd level 19, instead of telling
users to shell out to `zstd -19 -T0 --rm`, and produce a reproducible benchmark of what that
costs and buys.

Two halves:

1. **Write side (new).** A `zst_compress` module exposed as a `crackalack_rt2zst` binary and as a
   `--zst` flag on `crackalack_sort`.
2. **Read side (exists, unmerged).** Rebase `feat/zstd-ingest` onto master and merge it, resolving
   the conflict the RAR-ingest feature introduced in the meantime.

Then benchmark: compression ratio and time across levels, decompress throughput, end-to-end lookup
load time, peak RSS, and byte-identical round-trip correctness.

## Current state

`feat/zstd-ingest` (3 commits, cut from master on 2026-07-21) adds `zst_decompress.c/.h`, a CPU
unit test, Makefile wiring (`-lzstd` on linux/cuda/macos), and `.rt.zst` recognition at four
dispatch points in `crackalack_lookup.c`. It was GPU-verified on dell5. It has never been merged,
and master has moved: PR #128 added RAR ingest, which touches the exact same dispatch predicates
and introduced its own "strip the wrapper suffix before parsing params" helper. The branch's
inline `parse_name` copies now conflict with that helper, and the branch's `count_tables` predicate
is missing `.rti2` (master's has it).

There is no write side at all. `README.md` on the branch documents `zstd -19 -T0 --rm` as the way
to create tables.

## Architecture

### `zst_compress.c` / `.h` — the new module

```c
/* Compress a raw .rt file to a .rt.zst. */
int zst_compress(const char *rt_path, const char *zst_path, int level, int nb_workers,
                 uint64_t *num_chains);

/* Compress an in-memory table (start,end uint64 pairs) to a .rt.zst. */
int zst_compress_buf(const void *table, size_t len_bytes, const char *zst_path,
                     int level, int nb_workers);
```

Both are streaming (`ZSTD_CCtx` + `ZSTD_compressStream2` over a `ZSTD_CStreamInSize()` window), so
compressing a 16 GiB table does not require a 16 GiB input buffer plus a multi-GiB output buffer
resident at once.

**Load-bearing constraint:** streaming compression omits the frame content size from the header by
default, and `zst_decompress()` on the read side rejects a frame whose size is unknown
(`ZSTD_CONTENTSIZE_UNKNOWN` → error 5). So the compressor MUST call
`ZSTD_CCtx_setPledgedSrcSize(cctx, total_bytes)` before the first chunk. Every table we write is a
known, fixed size, so this is always possible. A unit test asserts that
`ZSTD_getFrameContentSize()` on our own output returns the exact original byte count — without that
test, a future refactor to streaming silently produces files our own loader refuses.

Other behavior:

- Reject input whose length is not a multiple of 16 bytes (`CHAIN_SIZE`) before doing any work; a
  truncated `.rt` should fail loudly, not become a valid-looking `.zst`.
- Write to `<zst_path>.tmp`, `fflush`+`fclose`, then `rename()` into place. A killed compressor
  must never leave a half-written `.rt.zst` that looks complete to the loader.
- Never delete the source. Deleting is the caller's decision (`crackalack_rt2zst --rm`), and only
  after the rename succeeds.
- `level` defaults to 19. `nb_workers` maps to `ZSTD_c_nbWorkers`; 0 means single-threaded
  (libzstd's default when built without threading, which is also the safe value inside
  `crackalack_sort`'s already-parallel workers).

### `crackalack_rt2zst` — the standalone tool

Mirrors `crackalack_rt2rtc.c` in structure, GPL header, `PRINT_PROJECT_HEADER()`, and error style.

```
crackalack_rt2zst [-l LEVEL] [-T N] [--rm] in.rt out.rt.zst
crackalack_rt2zst -d in.rt.zst out.rt
```

`-d` (decompress) exists for two reasons that justify it over YAGNI: it makes the round trip
verifiable with `cmp`, and it lets the benchmark measure decompress throughput through the *same*
`zst_decompress()` the lookup loader uses, rather than through the `zstd` CLI, which would measure
a different implementation path.

Defaults: `-l 19`, `-T 0`, no `--rm`.

### `crackalack_sort --zst[=LEVEL]`

`crackalack_sort` already reads each table fully into `data`, sorts it, and rewrites it in place.
With `--zst`, the rewrite emits `<name>.rt.zst` via `zst_compress_buf(data, ...)` — no re-read of
the file — then unlinks the raw `.rt` only after the temp-file rename succeeds.

Two details the current code makes easy to get wrong:

- **The already-sorted early return.** `sort_file()` prints "Skipping (already sorted)" and returns
  before the write. With `--zst`, an already-sorted table must still be compressed; the skip
  applies to sorting, not to output.
- **Argument parsing.** Today `main()` only checks `av[1] == "--jobs"` and treats everything from
  `av[3]` on as files. This needs a small flag loop so `--jobs N` and `--zst[=L]` can appear in
  either order. Keep it a hand-rolled loop matching the existing style; no getopt.

Compression runs with `nb_workers = 0` because sort already runs one worker per file; layering
zstd's own thread pool on top would oversubscribe the box.

### `crackalack_gen` — deliberately untouched

Gen writes chains incrementally and resumes by seeking to a byte offset — the exact machinery whose
corruption bug was fixed in v1.5.4 (PR #131). A zstd stream has no meaningful byte offset to resume
from, so `--zst` on gen would mean either buffering the whole table (defeats resumability) or
reworking checkpoint/resume (out of proportion to the benefit). Tables get compressed after
generation, at the sort step, where they are already being rewritten.

### Read-side merge

Rebase `feat/zstd-ingest` onto master. The conflict resolution is a generalization, not a merge of
two copies: master's RAR feature has a helper that copies a path while stripping a trailing
`.rar`. Widen it to strip a trailing `.rar` **or** `.zst`, and call it at every dispatch point
instead of the branch's four inline `parse_name` blocks. While there, make the `count_tables`
predicate match the other three (the branch's copy dropped `.rti2`).

`.rtc.zst` is out of scope even though the generalized helper would nearly support it — no such
files exist and no one has asked for them.

### Peak-RSS question the benchmark must answer

The current `zst_decompress()` is one-shot: it mallocs the compressed file *and* the decompressed
table, so a 16 GiB table at ~2× ratio peaks around 24 GiB. `crackalack_lookup` bulk-loads tables
against an auto-detected RAM budget that is computed from the *decompressed* size, so the
compressed buffer is invisible to the budget and can push the process past it.

Plan: measure the one-shot peak first, then convert `zst_decompress()` to streaming
(`ZSTD_decompressStream` over a fixed input window, output written straight into the destination
buffer), which caps the peak at decompressed size plus ~128 KB, and re-measure. The before/after
RSS numbers are a deliverable, not a footnote — they decide whether the streaming version ships.

## Benchmark harness

`scripts/bench/bench_zstd.py` — python3 stdlib only, no third-party deps. Runs locally on the M3
Max against `tables/ntlm_ascii-32-95#8-8_0_422000x1000000_0.rt` (16 MB) and
`tables_mask_test/` (16 MB).

Measures, per level in {1, 3, 9, 12, 19, 22} plus 19 single-threaded vs `-T0`:

| Metric | How |
|---|---|
| Compressed size, ratio | `stat` on the output |
| Compression wall time, MB/s | `time.monotonic()` around `crackalack_rt2zst` |
| Decompress throughput | `crackalack_rt2zst -d`, timed |
| Peak RSS | `/usr/bin/time -l`, parse "maximum resident set size" |
| Round-trip correctness | `cmp` the decompressed output against the original `.rt` |

Plus a format comparison at fixed level 19: end-to-end `crackalack_lookup` wall time and peak RSS
against four directories holding the same table as `.rt`, `.rtc`, `.rti2`, and `.rt.zst`, cracking
a known in-table hash minted with `gen_known_hash`.

Caveat to state in the results: 16 MB is small for load-time numbers — it fits entirely in page
cache, so the `.rt` baseline is close to a memcpy and the compressed formats look worse than they
would at 16 GiB. The ratio and throughput numbers are size-independent and transfer; the
end-to-end lookup numbers are directional only.

Output: a markdown table to stdout and `docs/benchmarks/2026-07-27-zstd-rt.md`, plus raw JSON
alongside it for later comparison.

## Testing

- **CPU unit tests** (`crackalack_cpu_tests`, no GPU): extend `tests/test_zst.c` with a
  `zst_compress_buf` → `zst_decompress` round trip asserting byte identity; a
  `ZSTD_getFrameContentSize()` check asserting the pledged size landed in the header; and a
  rejection test for input whose length is not a multiple of 16.
- **GPU end-to-end** (Metal, local): crack a `gen_known_hash` hash from a directory containing only
  `.rt.zst`, and confirm the result matches the raw `.rt` run. Negative control: truncate the
  `.zst` and confirm the loader reports a clear error and skips the file rather than crashing.
- **Existing suites** must stay green: `crackalack_cpu_tests` and `crackalack_unit_tests`.

## Out of scope

Gen-side compression; `.rtc.zst`; Windows `-lzstd` wiring (the Windows block stays untouched, as on
the ingest branch); zstd dictionaries; the zstd seekable format (which would allow ranged reads of
a compressed table without full decompression — genuinely interesting for the bulk loader, but a
separate design); deprecating `.rtc`.

## Risks

- **Streaming compress without a pledged size produces files our own loader rejects.** Mitigated by
  the explicit frame-content-size unit test.
- **`crackalack_sort --zst` deletes the raw `.rt`.** Given the perfectify incident where a
  self-comparison unlinked a whole table set, the unlink happens only after a successful rename,
  and never on the error path.
- **Level 22 needs `ZSTD_c_windowLog` beyond the default max** and may fail or warn on some
  builds; the harness records the failure rather than aborting the sweep.
