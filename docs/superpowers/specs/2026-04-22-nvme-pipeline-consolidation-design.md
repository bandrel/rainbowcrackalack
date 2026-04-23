# NVMe Pipeline Consolidation — Design Spec

**Date:** 2026-04-22
**Author:** brainstormed with Claude
**Status:** approved, ready for implementation planning

## Context

The Mandiant NetNTLMv1 rainbow table dataset (4,096 tables, ~8.8 TB raw `.rt`) is already fully downloaded to NFS. Three bash scripts currently handle the operational pipeline against that dataset using `/mnt/nvme` as a staging layer:

- `scripts/nvme_sort_tables.sh` — sort raw `.rt` tables (NFS → NVMe → `rtsort` → NFS)
- `scripts/nvme_compress_tables.sh` — compact sorted `.rt` → `.rtc` (NFS → NVMe → `rt2rtc` → NFS)
- `scripts/nvme_pipeline_lookup.sh` — run `crackalack_lookup` with NVMe batch staging against a hash file

The scripts are functional but site-specific (hardcoded Synology paths like `/volume1/Public/rainbow/rt2rtc`), duplicate ~40% of their boilerplate, and use three different resume/progress-tracking mechanisms. They are currently untracked in git.

## Goals

1. Consolidate the three scripts into a single in-repo orchestrator with subcommands, retiring the existing files.
2. Replace hardcoded Synology paths with a config-file-driven approach that's portable across gpuhost2/gpuhost3 (and reproducible for any rainbowcrackalack user handling Mandiant tables).
3. Unify resume/progress tracking into a shared state directory so `all` mode is cleanly idempotent.
4. Commit the result with a README explaining setup, runtime expectations, and recovery.

## Non-goals

- No GCS/gsutil download stage (data is already on NFS).
- No Python port. Bash only.
- No multi-host orchestration across gpuhost2 and gpuhost3 in this pass (state dir is host-local; cross-host is a future problem).
- No replacement of upstream `rtsort` / `rt2rtc` — they stay as external dependencies.
- No new pipelining across sort/compact/lookup stages; each runs sequentially in `all` mode.

## Design

### File layout

```
scripts/
  nvme_pipeline.sh          # new: single orchestrator, executable
  pipeline.conf.example     # new: committed template
  pipeline.conf             # new: user-local, gitignored
  README.md                 # new: pipeline docs
```

`.gitignore` gains an entry for `scripts/pipeline.conf`.

The three existing scripts (`nvme_sort_tables.sh`, `nvme_compress_tables.sh`, `nvme_pipeline_lookup.sh`) are deleted.

### CLI shape

```
nvme_pipeline.sh sort     [flags]
nvme_pipeline.sh compact  [flags]
nvme_pipeline.sh lookup   --hashes FILE [flags]
nvme_pipeline.sh all      --hashes FILE [shared flags]
nvme_pipeline.sh status   [--state-dir DIR]
```

Common flags (all subcommands that accept them): `--source DIR`, `--dest DIR`, `--nvme DIR`, `--state-dir DIR`, `--batch-size N`, `--start-index N`, `--end-index N`, `--dry-run`, `--config FILE`.

Per-stage scripts are implemented as shell functions inside the single `nvme_pipeline.sh` file, not separate files. This deduplicates the arg parser, ETA math, staging cleanup, and NVMe space checks that currently appear in three places.

### Config resolution

Resolution order for every setting:

1. CLI flag
2. Environment variable (`RC_*` prefix)
3. `scripts/pipeline.conf` (shell-sourced)
4. Hardcoded default (only where a safe default exists)

`scripts/pipeline.conf.example` fields:

```bash
# External upstream rainbowcrack binaries (not shipped with rainbowcrackalack)
RC_RTSORT="/path/to/rtsort"
RC_RT2RTC="/path/to/rt2rtc"

# crackalack_lookup — auto-detected from repo root if empty
RC_LOOKUP=""

# NVMe staging root (must hold ~2x batch size free)
RC_NVME_ROOT="/mnt/nvme"

# NFS paths
RC_RT_SOURCE=""   # raw/sorted .rt tables (sort input, compact input)
RC_RTC_DEST=""    # .rtc output (compact output, lookup input)

# Per-stage defaults (override via CLI flags)
RC_SORT_BATCH_SIZE=50
RC_COMPACT_QUEUE_MAX=2
RC_LOOKUP_BATCH_SIZE=115

# State directory — auto-derived to $RC_NVME_ROOT/.pipeline-state if empty
RC_STATE_DIR=""
```

Users copy `pipeline.conf.example` → `pipeline.conf` and edit for their environment.

### Shared state directory

Layout of `$RC_STATE_DIR/`:

```
sort.done                 # newline-delimited completed .rt filenames
compact.done              # newline-delimited completed .rt filenames (compacted from)
lookup.<hashkey>.done     # newline-delimited completed .rtc filenames, per hash file
sort.log                  # tee'd stage output
compact.log
lookup.log
cracked.<hashkey>.txt     # append-only cracked hash:plaintext, per hash file
```

`<hashkey>` is the first 12 hex chars of `sha256(realpath(--hashes))`, prefixed with the hash file's basename for readability (e.g. `corp-dump-pwdump.a1b2c3d4e5f6`). This keys lookup state to the specific hash file, so passing a different `--hashes` starts fresh without clobbering prior runs, and changing `--batch-size` between runs of the same hash file resumes correctly (markers are per-table, not per-batch).

Mechanics:

- On stage entry, read the relevant `.done` file into a bash associative array (`declare -A`) for O(1) membership checks. 4096 lines loads in milliseconds.
- Appends use `flock` on the file to prevent races (cheap insurance in case the state dir is ever shared across hosts).
- `status` subcommand scans all three `.done` files and reports counts + ETA per stage vs. total tables in `$RC_RT_SOURCE`.

This replaces all three existing resume mechanisms:

- `sort`'s `-user root` ownership check → membership in `sort.done`
- `compact`'s `grep -qFx` on a done-file → associative-array lookup
- `lookup`'s complete absence of resume → per-table markers in `lookup.<hashkey>.done`, keyed by hash file identity

### `all` mode flow

```
nvme_pipeline.sh all --hashes HASHFILE
```

Runs sequentially:

1. **sort** — any `$RC_RT_SOURCE/*.rt` not in `sort.done`
2. **compact** — any `.rt` in `$RC_RT_SOURCE` not in `compact.done`, outputs to `$RC_RTC_DEST`
3. **lookup** — any `.rtc` in `$RC_RTC_DEST` not in `lookup.<hashkey>.done`, appends cracks to `cracked.<hashkey>.txt`

Behavior:

- Stops on the first stage failure by default; `--continue-on-error` to override.
- Idempotent — interrupt + re-run resumes where it left off.
- Stages with nothing to do log "stage X: nothing to do" and continue.

### Per-stage behavior changes vs. current scripts

**sort** (renamed internal function, same approach):

- Drop `-user root` unsorted detection → use `sort.done` set membership.
- Keep existing batched NVMe staging and `rtsort` invocation.
- Drop hardcoded `/volume1/Public/rainbow/rtsort` default (comes from config).

**compact**:

- Replace grep-based done-file with associative-array lookup.
- Keep existing double-buffered copy/compress pipelining (next table copies while current compresses).
- Drop hardcoded `/volume1/Public/rainbow/rt2rtc` default.

**lookup**:

- Add resume via per-table markers in `lookup.<hashkey>.done` (hashkey derived from the hash file path). Changing `--batch-size` between runs resumes cleanly; changing `--hashes` starts a fresh state file.
- Keep existing double-buffer A/B batch staging.
- Keep auto-detect of `.rtc` vs. `.rt` in the source dir; prefer `.rtc`.
- Fix `TOTAL_CRACKED` counter: current script updates it inside a function called in a loop, which works only because bash uses dynamic scope. Replace with per-batch count written to a state file and summed at end.

### README contents

`scripts/README.md` covers:

- Context: what the pipeline is for (Mandiant NetNTLMv1 dataset) and its stages
- Dependencies: upstream `rtsort` / `rt2rtc` from rainbowcrack, where to get them
- Setup: copy `pipeline.conf.example` → `pipeline.conf`, edit paths
- Invocation examples for each subcommand (and `all`)
- Expected runtime / disk-space guidance (rough per-stage numbers from existing runs)
- Resume & recovery (kill / restart, interpreting `status`, manually clearing `.done` entries)
- Bash 4+ requirement note (associative arrays); Linux-only by design

## Opinionated design decisions

The following were chosen deliberately; listed here so future maintainers (including me) know why:

- **Single file instead of a dispatcher over three files** — the three existing scripts share enough boilerplate that merging them removes more code than it adds, and the orchestrator becomes trivially one function call per stage.
- **Bash associative arrays for done-file membership** — O(1) startup load vs. the current O(n²) grep loop. Requires bash 4+, which is fine on Linux. Would break macOS's default bash 3.2, but the pipeline never runs on a Mac (lookup targets gpuhost2/gpuhost3 Linux hosts).
- **Per-table `lookup.<hashkey>.done` markers, keyed by hash file identity** — a table is fully looked-up or not, so per-table is the natural granularity. Keying by a hash of the `--hashes` path means a different hash file starts fresh; same hash file resumes. Changing `--batch-size` mid-campaign works correctly because markers are per-table, not per-batch.
- **`flock` on state-file appends** — cheap insurance in case the state dir is ever shared across hosts. Harmless overhead otherwise.
- **`all` stops on failure by default** — a failed sort produces garbage input for compact, which produces garbage for lookup. Flag to override rather than the other direction.
- **Keep upstream `rtsort` / `rt2rtc` as external deps** — replacing them is a much larger project; the config file just points at them.

## Open questions

None blocking. Possible follow-ups not in scope:

- Multi-host state-dir sharing across gpuhost2 + gpuhost3 (would need a shared NFS path for `$RC_STATE_DIR` and real flock semantics across NFS).
- Pipelining sort → compact within a single run (one batch compacts while the next sorts).
- Native `crackalack_rtc2rt` use in place of upstream `rt2rtc` — would remove one external dep, but the opposite direction (forward compact) isn't implemented in this repo yet.
