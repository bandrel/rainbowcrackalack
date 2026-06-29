#!/usr/bin/env bash
#
# ASan / heap-corruption repro harness for the intermittent NetNTLMv1 lookup
# crash in the false-alarm-check phase:
#
#     double free or corruption (!prev)
#
# The crash is racy (~50% on identical inputs) and timing-dependent, which is
# the classic fingerprint of a heap *buffer overflow* whose victim chunk
# depends on run-to-run heap layout -- NOT a deterministic double free.  This
# script rebuilds crackalack_lookup under AddressSanitizer and runs the
# documented repro in a loop so ASan names the exact overflowing buffer and
# source line on the first hit.
#
# MUST be run on the CUDA box that reproduces it (dell3) -- the crash is
# CUDA-only and cannot be reproduced on the Metal/macOS build.
#
# Usage:
#   scripts/bench/asan_repro_netntlmv1_fa.sh [repo_dir]
#
# Environment overrides:
#   TABLES_DIR    dir of .rtc/.rt NetNTLMv1 tables   (default: bench staging)
#   HASHES_FILE   newline-separated NetNTLMv1 hashes (default: generated below)
#   ITERATIONS    number of lookup runs              (default: 12)
#   CUDA_PATH     passed through to `make linux`     (default: Makefile default)
#   FA_BATCH      --fa-batch value                   (default: 65536, per CUDA notes)
#   BLOOM_FPR     --bloom-fpr value (0 disables bloom; fast for few-hash runs)
#   EXTRA_ARGS    extra args appended to the lookup invocation
#   SKIP_BUILD    set =1 to reuse an existing ASan build
#   MODE          asan | mallocheck                  (default: asan)
#                 mallocheck = no rebuild, glibc MALLOC_CHECK_=3 (quick first pass)
#
set -uo pipefail

REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$REPO"

TABLES_DIR="${TABLES_DIR:-/mnt/nvme/rainbowcrackalack-bench/bench_input/tables}"
HASHES_FILE="${HASHES_FILE:-}"
ITERATIONS="${ITERATIONS:-12}"
FA_BATCH="${FA_BATCH:-65536}"
BLOOM_FPR="${BLOOM_FPR:-}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
MODE="${MODE:-asan}"
SKIP_BUILD="${SKIP_BUILD:-0}"

LOOKUP="$REPO/crackalack_lookup"
GENHASH_PY="$REPO/scripts/bench/gen_netntlmv1_hashes.py"

log() { printf '[asan-repro] %s\n' "$*"; }
die() { printf '[asan-repro] FATAL: %s\n' "$*" >&2; exit 1; }

[[ -d "$TABLES_DIR" ]] || die "TABLES_DIR not found: $TABLES_DIR (set TABLES_DIR=...)"
shopt -s nullglob
tbls=( "$TABLES_DIR"/*.rtc "$TABLES_DIR"/*.rt )
shopt -u nullglob
(( ${#tbls[@]} > 0 )) || die "no .rtc/.rt tables in $TABLES_DIR"
log "tables dir: $TABLES_DIR (${#tbls[@]} table files)"

# ---- hashes -----------------------------------------------------------------
# Default to the exact repro seed from the bug report: 5 NetNTLMv1 hashes.
# The hashes don't need to crack -- they just need to produce FA candidates so
# the false-alarm-check phase (where the corruption lives) actually executes.
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
if [[ -z "$HASHES_FILE" ]]; then
  HASHES_FILE="$WORK/hashes.txt"
  log "generating repro hashes (seed 20260502, count 5)"
  python3 "$GENHASH_PY" --seed 20260502 --count 5 --out "$HASHES_FILE" \
    || die "gen_netntlmv1_hashes.py failed (needs pycryptodome -- run in the bench venv)"
fi
[[ -s "$HASHES_FILE" ]] || die "HASHES_FILE empty: $HASHES_FILE"
log "hashes file: $HASHES_FILE ($(wc -l < "$HASHES_FILE") lines)"

# ---- build ------------------------------------------------------------------
if [[ "$MODE" == "asan" && "$SKIP_BUILD" != "1" ]]; then
  log "rebuilding crackalack_lookup under AddressSanitizer (-O1 -g -fsanitize=address)"
  MAKE_ARGS=( linux
    "CFLAGS_common=-Wall -O1 -g -fno-omit-frame-pointer -fsanitize=address"
    "LDFLAGS_common=-fsanitize=address" )
  [[ -n "${CUDA_PATH:-}" ]] && MAKE_ARGS+=( "CUDA_PATH=$CUDA_PATH" )
  make clean >/dev/null 2>&1
  # NOTE: if -flto=auto (added by the Makefile's linux CFLAGS) conflicts with
  # ASan on your toolchain, append CFLAGS=... LDFLAGS=... to strip it.
  if ! make "${MAKE_ARGS[@]}"; then
    die "ASan build failed (try MODE=mallocheck for a no-rebuild first pass)"
  fi
fi
[[ -x "$LOOKUP" ]] || die "$LOOKUP not built"

# ---- run loop ---------------------------------------------------------------
# CUDA reserves large virtual address ranges that collide with ASan's shadow
# gap; protect_shadow_gap=0 avoids a spurious ASan abort at CUDA init.
# detect_leaks=0 because the GPU/thread teardown has benign at-exit leaks that
# would otherwise drown out the real heap-overflow report.
export ASAN_OPTIONS="protect_shadow_gap=0:detect_leaks=0:abort_on_error=1:halt_on_error=1:print_stacktrace=1:allocator_may_return_null=1"
# glibc allocator hardening for the mallocheck mode (no ASan rebuild needed).
RUNNER=()
if [[ "$MODE" == "mallocheck" ]]; then
  export MALLOC_CHECK_=3
  log "MODE=mallocheck: MALLOC_CHECK_=3 (no rebuild); aborts on the corrupt free"
fi

log "running $ITERATIONS iterations (crash is ~50%/run; expect a hit within a few)"
hit=0
for ((i=1; i<=ITERATIONS; i++)); do
  out="$WORK/run_$i.log"
  log "--- iteration $i/$ITERATIONS ---"
  # NOTE: crackalack_lookup requires the two positional args (table dir, hashes
  # file) FIRST, then optional flags.  Flags-first triggers a usage error.
  LOOKUP_ARGS=( "$TABLES_DIR" "$HASHES_FILE" --fa-batch "$FA_BATCH" )
  [[ -n "$BLOOM_FPR" ]] && LOOKUP_ARGS+=( --bloom-fpr "$BLOOM_FPR" )
  # shellcheck disable=SC2206
  [[ -n "$EXTRA_ARGS" ]] && LOOKUP_ARGS+=( $EXTRA_ARGS )
  set +e
  "$LOOKUP" "${LOOKUP_ARGS[@]}" >"$out" 2>&1
  rc=$?
  set -e 2>/dev/null || true
  if grep -qiE "AddressSanitizer|double free or corruption|heap-buffer-overflow|SUMMARY: AddressSanitizer|free\(\): |corruption" "$out"; then
    hit=1
    echo
    log "!!! CAUGHT corruption on iteration $i (exit=$rc).  Report tail:"
    echo "------------------------------------------------------------------"
    # Show the ASan/glibc report -- the SUMMARY + first stack is the root cause.
    grep -nE "AddressSanitizer|heap-buffer-overflow|double free|corruption|SUMMARY|#[0-9]+ 0x|crackalack_lookup\.c|fa_batch\.c|harvest_false_alarm|launch_false_alarm" "$out" | head -60
    echo "------------------------------------------------------------------"
    log "full log: $out  (copy it off \$WORK before this script exits)"
    cp "$out" "$REPO/asan_repro_hit_$i.log" 2>/dev/null \
      && log "saved a copy to: $REPO/asan_repro_hit_$i.log"
    break
  fi
  log "iteration $i: clean (exit=$rc)"
done

if [[ "$hit" == "0" ]]; then
  log "no corruption caught in $ITERATIONS runs."
  log "Try: more ITERATIONS, MODE=asan if you used mallocheck, or vary FA_BATCH."
  exit 0
fi
log "done -- attach the saved log when reporting the root cause."
