# Benchmark Harness (`scripts/bench/`)

Repeatable performance + correctness comparison of two crackalack builds
(BASE vs CANDIDATE) on a GPU host. Answers: *did this change regress lookup
precompute or gen throughput, change the kernel's compute profile, or change
outputs?*

> Runs fully only on a Linux GPU host (dell3): needs the CUDA toolchain to
> `make linux`, passwordless `sudo` for page-cache drops and `ncu`, and the
> NetNTLMv1 `.rtc` tables. The Python modules are unit-tested host-independently.

## Quick start

```bash
# Compare current branch against master (defaults), end to end:
TABLE_SOURCE=/mnt/nvme/rtc/NetNTLMv1/0-1000 \
  scripts/bench/run_benchmark.sh all

# Compare two explicit refs:
BASE_REF=master CAND_REF=my-optimization \
TABLE_SOURCE=/mnt/nvme/rtc/NetNTLMv1/0-1000 \
  scripts/bench/run_benchmark.sh all
```

`all` = `prepare` → `run` → `report`. You can run the phases separately
(`run_benchmark.sh prepare|run|report`); `report` re-parses the latest run.

The summary prints to stdout and is saved to
`$BENCH_ROOT/bench_results/<UTC-timestamp>/summary.md` (with `results.json`).

## Phases

- **prepare** — clone BASE and CANDIDATE into `$BENCH_ROOT/{base,cand}`,
  `make linux` both, create a venv with `pycryptodome`, generate deterministic
  NetNTLMv1 hashes, stage `$PARTS` `.rtc` table parts, smoke-test the base build.
- **run** — UTC-timestamped results dir + provenance. Alternating BASE/CAND trials:
  drop page cache + clear precompute cache (`*.index`, `rcracki.precalc.*`) for a
  cold run, time `crackalack_lookup` under `/usr/bin/time -v`. Then per role:
  gen-throughput across all paths and `ncu` profiling of `$PROFILE_CONFIGS`.
  Finally an output-equivalence check (one seeded gen table, BASE vs CAND sha256).
- **report** — parse trial logs + `gen.json`/`profile.json`/`equivalence.json`
  into `results.json` and `summary.md`.

All GPU commands run under a `flock` mutex (`lib/gpu_lock.sh`) so concurrent runs
queue instead of corrupting each other's timings.

## Environment variables

| Var | Default | Purpose |
|-----|---------|---------|
| `BENCH_ROOT` | `/mnt/nvme/rainbowcrackalack-bench` | Workspace (checkouts, inputs, results) |
| `TABLE_SOURCE` | `/mnt/nvme/rainbow/tables/netntlmv1` | Dir whose `<subdir>/*.rtc` are staged for lookup |
| `BASE_REPO` / `BASE_REF` | this repo / `master` | Baseline build |
| `CAND_REPO` / `CAND_REF` | this repo / current branch | Candidate build |
| `PARTS` | `8` | Number of `.rtc` parts to stage |
| `HASH_COUNT` | `100` | NetNTLMv1 hashes to look up (1 → non-batch path) |
| `HASH_SEED` | `20260502` | Deterministic hash-generation seed |
| `TRIALS` | `3` | Trials per role (need ≥2 for a median/speedup) |
| `TIMEOUT_MIN` | `30` | Per-lookup wall cap (minutes) |
| `PROFILE_CONFIGS` | `netntlmv1_7` | Space-separated configs to `ncu`-profile |
| `GEN_NUM_CHAINS` | `1000000` | Chains per gen-throughput probe |
| `PROFILE_NUM_CHAINS` | `163840` | Chains for the gen run profiled by `ncu` |
| `NCU_BIN` | `ncu` | Path to Nsight Compute |
| `BENCH_PYTHON` | `python3` | Python used by the shell wrappers |
| `GPU_LOCK_FILE` / `GPU_LOCK_TIMEOUT` | `/tmp/crackalack_bench.lock` / `3600` | GPU mutex |

## Components

| File | Responsibility |
|------|----------------|
| `run_benchmark.sh` | Orchestrator (prepare/run/report) |
| `configs.py` | Optimized gen-path catalog (args + kernel name per path) |
| `gen_netntlmv1_hashes.py` | Deterministic seeded NetNTLMv1 hash generator |
| `bench_gen.sh` + `parse_gen_bench.py` | Gen throughput (chains/s) → `gen.json` |
| `profile_ncu.sh` + `extract_ncu.py` | `ncu` kernel metrics → `profile.json` |
| `check_equivalence.py` | sha256 base-vs-cand of a gen artifact → `equivalence.json` |
| `parse_results.py` | Merge trials + sections → `results.json` / `summary.md` |
| `lib/gpu_lock.sh` | `flock` GPU mutex (`with_gpu_lock <cmd>`) |

## Tests

Host-independent unit tests (no GPU); `gen_netntlmv1_hashes` needs the venv's
`pycryptodome`:

```bash
for t in scripts/bench/test_*.py; do python3 "$t"; done
bash scripts/bench/test_gpu_lock.sh   # needs Linux flock
```

## Notes / gotchas

- `crackalack_gen -bench` is **disabled** in this release; throughput is measured
  from a normal gen run (part index 0) by parsing its `Rate: N/s` line (which is
  ANSI-colored — the parser strips it). Generated tables are cleaned up after.
- NetNTLMv1 lookup precompute is O(chain_len²) per hash; with the standard
  881689 chain length even a handful of hashes can exceed `TIMEOUT_MIN`. Tune
  `HASH_COUNT`/`TIMEOUT_MIN` for the lookup phase accordingly.
- `ncu` profiling defaults to `netntlmv1_7` only — profiling every path under
  `ncu` is very slow for little signal. Gen throughput still covers all paths.
