# Crack Regression / False-Negative Framework — Design

Date: 2026-06-20
Status: Approved (design)
Branch: `test/crack-regression-framework`

## Problem

Recent NetNTLMv1 work merged to example `master` touches chain math and the
precompute path: `perf/netntlmv1-inner-loop` (DES inner-loop rewrite),
`perf/netntlmv1-kernel-review` (honor `chain_len` in precompute fallback),
`fix/netntlmv1-cpu-verify`, `feature/configurable-netntlmv1-challenge`, and
`fix/precompute-cache-challenge-key`. A hash that should have cracked on gpuhost3
did not, suggesting a regression that introduces **false negatives** (a crack
that should succeed silently fails).

There is no automated check that a crack which *should* succeed actually does.
The existing assets each cover part of the space but none catch this class:

- `crackalack_unit_tests` — GPU unit tests for hash/reduce/chain primitives, but
  not end-to-end cracking.
- `crackalack_tests.py` — end-to-end gen + lookup, but against *synthetic/fake*
  tables and not built for multi-commit differential or cross-backend runs.
- `scripts/bench/run_benchmark.sh` — BASE-vs-CANDIDATE **perf** driver with
  sha256 output-equivalence, but measures speed, not crack correctness.

## Goal

A framework that confirms there is **no regression and no false negatives** in
cracking, for the recently-changed paths, runnable on both GPU backends.

### In scope (paths)
- NTLM 8-char (NTLM8 fast path)
- NTLM 9-char (NTLM9 fast path)
- NetNTLMv1-7, **default challenge** (`1122334455667788`)

### Out of scope (non-goals)
- Markov and MD5 chain variants.
- NetNTLMv1 **non-default** challenge. (The cache-key change only *adds*
  specificity to the cache key and cannot produce a false negative on a clean
  cache; default-challenge keys are unchanged. If the failing crack was a
  default-challenge lookup, the suspects are the chain-math changes, not the
  cache key.)
- Performance measurement (already covered by `run_benchmark.sh`).

## Approach

Extend `scripts/bench/` with a new, single-purpose sibling driver rather than
overloading the perf driver or the synthetic-table Python tests. Reuse the hard
parts the bench harness already solved: git checkout/build of pinned commits,
the GPU flock (`lib/gpu_lock.sh`), and the pycryptodome venv.

### Entry point: `scripts/bench/run_regression.sh`

Mirrors `run_benchmark.sh`'s phase structure:

```
run_regression.sh prepare     # checkout+build BASE & CANDIDATE, venv, stage inputs (idempotent)
run_regression.sh roundtrip   # Oracle 1: self-contained crack round-trip (this build)
run_regression.sh crackdiff   # Oracle 2: BASE-vs-CANDIDATE differential cracking (gpuhost3, real tables)
run_regression.sh report      # write regression_summary.md
run_regression.sh all         # prepare + roundtrip + crackdiff + report
```

Tunables follow the existing `: "${VAR:=default}"` env-override convention
(`BENCH_ROOT`, `BASE_REF`, `CAND_REF`, `GPU_INDEX`, real-table source path, etc.).

## Oracle 1 — Round-trip (absolute correctness)

Backend-agnostic; runs on **gpuhost3 (CUDA)** and **local Metal (M3 Max)**. For
each in-scope path, using a freshly-generated tiny table:

1. **Generate** a small table with fixed, fast params (small `chain_len`, small
   `num_chains`) for the path.
2. **Derive a plaintext provably in that table**, then compute its hash:
   - NetNTLMv1-7: seed `gen_known_hash` from a real chain start index obtained
     via `get_chain` (published tables are perfectified — do not use an
     arbitrary start index; see project memory `gen_known_hash_needs_real_start`).
   - NTLM8 / NTLM9: `gen_known_hash` is currently NetNTLMv1-only. The
     implementation plan resolves this by **either** extending `gen_known_hash`
     to emit NTLM hashes **or** using a `get_chain` → `enumerate_chain` recipe to
     read a plaintext at a known chain position and hashing it. (Open item —
     decided in the plan.)
3. **Look up** the derived hash with `crackalack_lookup` against the generated
   table and assert the `.pot` file contains the expected plaintext.

A path that fails to crack a hash *it constructed from its own table* is an
unambiguous false negative.

### Cross-backend divergence

The identical known hashes are run on CUDA (gpuhost3) and Metal (M3 Max). A path
that cracks on one backend but not the other is reported as a cross-backend
divergence (a strong signal that a kernel port diverged — e.g. the CUDA DES
inner-loop rewrite vs the Metal port).

## Oracle 2 — Differential (regression detection)

gpuhost3 only, against the **real** `/mnt/nvme/rtc/` NetNTLMv1-7 tables.

1. Reuse BASE/CANDIDATE checkout+build. BASE = pinned known-good commit (default
   `bench-base-preinnerloop`, overridable via `BASE_REF`); CANDIDATE = current
   `HEAD`.
2. Run **both** builds against the **same** real tables with the **same** seeded
   hash set (reuse `gen_netntlmv1_hashes.py`), **clearing all `*.index`
   precompute cache files between runs** so a stale cache can't mask a
   recomputation difference (lesson from bench commit `d9306a2`).
3. Diff the cracked-hash sets parsed from each build's `.pot` output.
   **Any hash cracked by BASE but not by CANDIDATE is a regression / false
   negative.** Hashes cracked by CANDIDATE but not BASE are reported as
   informational (improvements / noise), not failures.

## Reporting

`report` writes `regression_summary.md` under the regression root:

- Round-trip: per-path PASS/FAIL.
- Cross-backend matrix: per-path crack result on CUDA vs Metal.
- Differential: BASE vs CANDIDATE cracked counts, plus the explicit list of any
  hashes regressed (cracked by BASE, missed by CANDIDATE).

Exit non-zero if any round-trip path fails, any cross-backend divergence is
found, or any differential regression is detected — so the framework is usable
as a pass/fail gate and is git-bisectable.

## Components and boundaries

| Unit | Purpose | Depends on |
|------|---------|------------|
| `run_regression.sh` | Phase orchestration, GPU lock, build, exit status | `lib/gpu_lock.sh`, built binaries, venv |
| round-trip helper (py or sh) | Derive known-in-table hash per path; assert `.pot` | `crackalack_gen`, `get_chain`, `gen_known_hash`/`enumerate_chain`, `crackalack_lookup` |
| crack-set diff (py) | Parse `.pot`, compute BASE−CANDIDATE set difference | pure logic (no GPU) |
| report writer (py or sh) | Render `regression_summary.md`, set exit code | round-trip + crackdiff outputs |

### Harness's own tests

Following the existing `scripts/bench/test_*.py` convention, the pure-logic
pieces (`.pot` parsing, cracked-set diff) get unit tests with fixtures and
require no GPU.

## Risks / open items

- **NTLM8/9 known-hash derivation** — `gen_known_hash` is NetNTLMv1-only today.
  Resolved in the implementation plan (extend the tool vs. chain-walk recipe).
- **Tiny-table generation params** — must be large enough that the constructed
  chain is actually covered, small enough to run in seconds on both backends.
  Concrete params chosen in the plan.
- **Metal availability** — cross-backend phase requires the M3 Max; on a
  CUDA-only host the round-trip still runs single-backend and the matrix notes
  Metal as "not run" rather than failing.
- Round-trip uses synthetic tables, so scale-only behavior is covered only by
  the differential phase against real tables.
