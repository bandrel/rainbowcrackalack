# NVMe Pipeline for NetNTLMv1 Rainbow Tables

Unified orchestrator for staging and processing the Mandiant NetNTLMv1 rainbow table
dataset (4,096 tables, ~8.8 TB raw `.rt`) using an NVMe device as a high-speed
intermediary between slow NFS storage and GPU lookup.

## What This Pipeline Does

The dataset lives on NFS (too slow for direct GPU lookup). The pipeline uses an NVMe
staging volume to move tables to fast local storage in batches, then hands them off to
the appropriate tool:

| Stage   | Input      | Tool           | Output          |
|---------|------------|----------------|-----------------|
| sort    | `.rt` (NFS)| `rtsort`       | `.rt` sorted (NFS)|
| compact | `.rt` (NFS)| `rt2rtc`       | `.rtc` (NFS)    |
| lookup  | `.rtc` (NFS)| `crackalack_lookup` | cracked hashes |

Run all three in sequence with `nvme_pipeline.sh all`.

## Dependencies

- **bash 4+** — required for associative arrays (Linux default; macOS ships bash 3.2,
  but the pipeline is Linux-only by design)
- **rtsort** — from the upstream [rainbowcrack](https://project-rainbowcrack.com/)
  project; not included in this repo
- **rt2rtc** — also from upstream rainbowcrack; not included in this repo
- **crackalack_lookup** — built from this repo (`make linux`); auto-detected from the
  repo root if not explicitly configured

## Setup

```bash
# 1. Copy the example config and edit for your environment
cp scripts/pipeline.conf.example scripts/pipeline.conf
$EDITOR scripts/pipeline.conf

# 2. Set at minimum:
#   RC_RTSORT  — path to rtsort binary
#   RC_RT2RTC  — path to rt2rtc binary
#   RC_RT_SOURCE — directory of raw .rt tables on NFS
#   RC_RTC_DEST  — directory for compressed .rtc output on NFS
#   RC_NVME_ROOT — NVMe mount point (default: /mnt/nvme)
```

`scripts/pipeline.conf` is gitignored — credentials and local paths stay off the repo.

## Usage

### Sort

Sort raw `.rt` tables in-place (NFS → NVMe → rtsort → NFS):

```bash
scripts/nvme_pipeline.sh sort \
  --source /mnt/nfs/netntlmv1/tables \
  --nvme /mnt/nvme \
  --batch-size 50
```

### Compact

Compress sorted `.rt` → `.rtc` with double-buffered copy/compress pipeline:

```bash
scripts/nvme_pipeline.sh compact \
  --source /mnt/nfs/netntlmv1/tables \
  --dest /mnt/nfs/netntlmv1/tables_rtc \
  --nvme /mnt/nvme
```

### Lookup

Run `crackalack_lookup` against a hash file with double-buffered NVMe staging:

```bash
scripts/nvme_pipeline.sh lookup \
  --hashes /path/to/hashes.pwdump \
  --dest /mnt/nfs/netntlmv1/tables_rtc \
  --nvme /mnt/nvme \
  --batch-size 115
```

Cracked results are appended to `$STATE_DIR/cracked.<hashkey>.txt`.

### All stages in sequence

```bash
scripts/nvme_pipeline.sh all \
  --hashes /path/to/hashes.pwdump \
  --source /mnt/nfs/netntlmv1/tables \
  --dest /mnt/nfs/netntlmv1/tables_rtc \
  --nvme /mnt/nvme
```

Stops on the first failed stage unless `--continue-on-error` is passed.

### Status

```bash
scripts/nvme_pipeline.sh status
# or with an explicit state dir:
scripts/nvme_pipeline.sh status --state-dir /mnt/nvme/.pipeline-state
```

Shows per-stage completion counts and cracked hash totals.

## Config Resolution Order

Every setting is resolved in this order:

1. CLI flag (highest priority)
2. Environment variable (`RC_*` prefix)
3. `scripts/pipeline.conf` (shell-sourced if present)
4. Hardcoded default (lowest priority)

## Expected Runtime and Disk Space

**NVMe space:** The pipeline needs `~2 × batch_size × table_size` free on NVMe for
double-buffering. With 115-table batches and ~2.1 GB per table, that is ~480 GB free.

**Per-table timing (rough, from observed runs):**

| Stage   | Time per table       | Notes                              |
|---------|---------------------|------------------------------------|
| sort    | ~30s on NVMe        | Includes copy to NVMe and write back |
| compact | varies              | Depends on table chain count; rt2rtc |
| lookup  | GPU-bound           | Depends on GPU model and hash count |

**End-to-end for 4,096 tables:** several days per stage for sort/compact; lookup
throughput depends heavily on how many hashes remain uncracked per batch.

## Resume and Recovery

The pipeline is fully resumable. Each stage tracks completed tables in a `.done` file:

```
$STATE_DIR/
  sort.done              # completed .rt filenames
  compact.done           # completed .rt filenames (source side)
  lookup.<hashkey>.done  # completed .rtc filenames, per hash file
  sort.log
  compact.log
  lookup.log
  cracked.<hashkey>.txt  # append-only cracked hash:plaintext
```

**Resuming after a kill:** just re-run the same command. Completed tables are skipped.

**Checking progress:** `nvme_pipeline.sh status`

**Re-processing a specific table:** remove its filename from the relevant `.done` file,
then re-run the stage.

**Restarting a stage from scratch:** remove or empty the stage's `.done` file.

**Changing `--batch-size`:** safe between runs. Done-file markers are per-table, not
per-batch, so batch size can change without invalidating prior progress.

**Different hash file:** passing a different `--hashes FILE` automatically creates a
new `lookup.<hashkey>.done` file keyed to that file's content hash. Prior lookup
progress for a different hash file is preserved separately.
