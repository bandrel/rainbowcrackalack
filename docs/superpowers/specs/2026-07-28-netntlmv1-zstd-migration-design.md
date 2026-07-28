# NetNTLMv1 `.rtc` → `.rt.zst` Migration — Design

**Goal:** Convert the existing NetNTLMv1 rainbow table set on dell3 (16,388 `.rtc` parts, ~5.0 TiB) to the `.rt.zst` format built in `feat/zstd-19-rt`, in place, without ever holding a full second copy of the set. Net result: same 5.4988e11 chains, same coverage, ~4.08 TiB instead of ~5.0 TiB, and a lookup path with one decompression step instead of `.rtc`'s own decode.

**Why this format, not `.rtc.zst` or raw `.rt`.** Measured directly on a real 33,554,432-chain part this session:

| Format | Bytes/chain | Full-set size | Notes |
|---|---|---|---|
| `.rtc` (current) | 10.00 | 5.00 TiB | baseline |
| `.rt` (raw) | 16.00 | 8.00 TiB | reference only, never a target |
| `.rtc.zst` | 9.15 | 4.58 TiB | −8.5%; still needs `.rtc`'s own decode *plus* a zstd unwrap at lookup time (double decompression) |
| **`.rt.zst`** | **8.15** | **4.08 TiB** | **−18.5%; single decode at lookup, replaces `.rtc`'s decode entirely** |

`.rt.zst` wins on both size and lookup-time cost, so it's the only format this migration targets.

## Scope

- **In scope:** dell3's NetNTLMv1 set only (the 16,388 `.rtc` parts under `/mnt/nvme/rtc/NetNTLMv1/`). NTLM/MD5 tables elsewhere are untouched.
- **Out of scope:** the parked 99%-coverage regen (separate, much larger effort, blocked on generation-rate viability per [[netntlmv1-99pct-regen-plan]]). This migration re-encodes the *existing* set as-is; it does not add table indices or change coverage.
- **Out of scope:** merging `feat/zstd-19-rt` to master. The branch build is deployed directly to dell3/dell2 for this migration; the PR merge happens later, independently.

## Architecture

A manifest-driven Python conversion pipeline, no new C code. One driver script, run independently (with different arguments) on dell3 and dell2.

**Components:**

1. **NFS export (one-time infra subtask, dell3 → dell2).** dell3 exports `/mnt/nvme/rtc/NetNTLMv1` read-write over NFS; dell2 mounts it. This lets dell2 read `.rtc` source files and write `.rt.zst` output directly into the same directory tree dell3 uses — no staging copies, no NAS involved.

2. **Partition by top-level directory.** The set is already laid out as 4 top-level directories (`0-1000`, `1001-2000`, `2001-3000`, `3001-4095`), ~1000-1095 subdirectories each, ~4 `.rtc` files per subdirectory, 16,388 files total.
   - dell3 (16 cores): `0-1000`, `1001-2000`, `2001-3000` (~12,289 parts)
   - dell2 (8 cores, over NFS): `3001-4095` (~4,099 parts)
   - Whole-directory assignment — no splitting within a directory, no shared bookkeeping between machines beyond "which directories am I responsible for."

3. **`scripts/migrate/migrate_netntlmv1_zst.py`** (new, stdlib only, mirrors the style of `scripts/bench/bench_zstd.py`). Takes a directory range and a worker count. For each `.rtc` file found under its assigned range, does the **per-part pipeline**:
   1. `crackalack_rtc2rt <part>.rtc <scratch>/<part>.rt`
   2. `crackalack_rt2zst -l 19 <scratch>/<part>.rt <part>.rt.zst` (written into the final location, same directory as the source `.rtc`)
   3. Round-trip check: `crackalack_rt2zst -d <part>.rt.zst <scratch>/<part>.rt.check` then `cmp` against `<scratch>/<part>.rt`
   4. On match: delete both scratch `.rt` files, delete the original `<part>.rtc`, append `<part>` to the manifest as done.
   5. On mismatch or any step failing: delete `<part>.rt.zst` (the suspect output) and leave the original `.rtc` untouched; log the part to an error list; move on to the next part rather than aborting the run.
   - Runs N workers concurrently (Python `multiprocessing.Pool`, N = 16 on dell3, 8 on dell2) — each worker claims one `.rtc` file at a time from the manifest so two workers never touch the same part.

4. **Manifest file** (one per machine, e.g. `migration-manifest-dell3.jsonl`, `migration-manifest-dell2.jsonl`, stored outside the table tree so it survives a `.rtc`-directory scan). One line per part: `{"part": "...", "status": "done"|"error", "orig_bytes": N, "new_bytes": N}`. On restart, the driver skips any part already marked `done` and retries any marked `error`. This is what makes the whole migration resumable if a machine reboots or the script is killed mid-run.

**Data flow (per part):**
```
source.rtc --[rtc2rt]--> scratch.rt --[rt2zst -l19]--> dest.rt.zst
                             |                              |
                             +------[rt2zst -d]--> scratch.rt.check
                                                              |
                                                    cmp(scratch.rt, scratch.rt.check)
                                                       |match          |mismatch
                                                  delete scratch,   delete dest.rt.zst,
                                                  delete source.rtc  keep source.rtc,
                                                  mark done          mark error
```

**Error handling:** every failure mode (rtc2rt fails, rt2zst fails, decompress-check fails, cmp mismatch, disk full mid-write) lands in the "leave the original alone, log an error, continue" branch — the original `.rtc` is only ever deleted after its replacement round-trips byte-identical. This mirrors the "raw `.rt` is only ever deleted after a successful rename of the finished output" invariant from the zstd-19-rt plan itself.

**Testing:**
- Dry run on a handful of parts (e.g. one subdirectory, ~4 files) on dell3 before the full run, confirming the manifest/skip/resume logic and the round-trip check actually catch a deliberately-corrupted case (same negative-control pattern used in Task 5 of the zstd-19-rt plan).
- After dell3's dry run passes, same dry run on dell2 across the NFS mount, to confirm the export/mount actually works end-to-end (permissions, path visibility, no silent NFS caching issues) before committing dell2 to its full directory range.
- Post-migration: `crackalack_lookup` against the fully migrated set with a known-crackable hash (same technique as the zstd branch's Task 5 — `gen_known_hash`), confirming a real crack still works from the migrated table set.

## Deployment steps

1. Build `feat/zstd-19-rt` on dell3 (already has the checkout at `6a4e17be`+branch fetch needed) and on dell2.
2. Set up the NFS export from dell3 to dell2; verify dell2 can read and write into the shared path.
3. Write and dry-run `migrate_netntlmv1_zst.py` on a small subset on dell3.
4. Dry-run the same script on dell2 over NFS.
5. Launch the full run: dell3 on its 3 directories, dell2 on its 1 directory, both in the background, both logging to their own manifest.
6. Monitor manifests for completion / error counts.
7. Once both machines report their assigned directories fully `done`, run the post-migration `crackalack_lookup` crack-verification pass.
8. Report final size, error count (if any parts failed and were left as `.rtc`), and total wall time.

## Risks

- **NFS reliability for dell2's leg.** If the export/mount proves flaky mid-run, dell2's directory can simply be picked up by dell3 afterward (or re-run later) — the manifest makes this safe, just slower.
- **Concurrent NVMe I/O contention on dell3** running 16 workers against the same volume that also holds the live/queryable table set — if dell3 needs to serve a `crackalack_lookup` run during the migration window, that lookup will compete for disk bandwidth. Out of scope for this design to solve; note it as an operational consideration when scheduling the run.
- **Partial-error parts.** Any part that fails its round-trip check is left in `.rtc` form and logged — this migration does not force 100% conversion; a small residual of `.rtc` parts alongside a majority `.rt.zst` set is an acceptable end state, since `crackalack_lookup` already reads both formats.
