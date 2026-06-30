#!/usr/bin/env bash
#
# coverage.sh — Build crackalack_unit_tests with gcov instrumentation, run it,
# and generate a per-file line-coverage report for the key host modules.
#
# Requires a GPU (Metal on macOS, CUDA on Linux).
#
# Usage:   scripts/bench/coverage.sh [repo_dir]
# Env:     CUDA_PATH (linux build)
#
set -uo pipefail

REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$REPO"

case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  Linux)  PLATFORM=linux ;;
  *)      echo "SKIP: unsupported platform $(uname -s)"; exit 0 ;;
esac

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Install a trap to always restore the normal build on exit (covers build
# failures, test failures, and successful runs alike).
_restore_build() {
  echo "==> Restoring normal build ($PLATFORM)..." >&2
  if [[ "$PLATFORM" == "linux" && -n "${CUDA_PATH:-}" ]]; then
    ( cd "$REPO" && make clean >/dev/null 2>&1 && make "$PLATFORM" CUDA_PATH="$CUDA_PATH" >/tmp/coverage_restore.log 2>&1 ) \
      || echo "WARN: normal build restore failed (see /tmp/coverage_restore.log)" >&2
  else
    ( cd "$REPO" && make clean >/dev/null 2>&1 && make "$PLATFORM" >/tmp/coverage_restore.log 2>&1 ) \
      || echo "WARN: normal build restore failed (see /tmp/coverage_restore.log)" >&2
  fi
}
trap _restore_build EXIT

# Build with coverage instrumentation
echo "==> Building crackalack_unit_tests with COVERAGE=1 ($PLATFORM)..."
if [[ "$PLATFORM" == "linux" && -n "${CUDA_PATH:-}" ]]; then
  if ! ( cd "$REPO" && make clean && make "$PLATFORM" COVERAGE=1 CUDA_PATH="$CUDA_PATH" ) > /tmp/coverage_build.log 2>&1; then
    tail -40 /tmp/coverage_build.log >&2
    fail "Coverage build failed. See /tmp/coverage_build.log"
  fi
else
  if ! ( cd "$REPO" && make clean && make "$PLATFORM" COVERAGE=1 ) > /tmp/coverage_build.log 2>&1; then
    tail -40 /tmp/coverage_build.log >&2
    fail "Coverage build failed. See /tmp/coverage_build.log"
  fi
fi

# Determine object directory
OBJDIR="$REPO/build/$PLATFORM/obj"

# Run unit tests to generate .gcda files
echo "==> Running crackalack_unit_tests..."
if ! "$REPO/crackalack_unit_tests" > /tmp/coverage_run.log 2>&1; then
  tail -40 /tmp/coverage_run.log >&2
  fail "crackalack_unit_tests exited nonzero. See /tmp/coverage_run.log"
fi

# Find gcov tool — on macOS prefer xcrun llvm-cov gcov (Apple's gcov stub
# exits 0 on --version but fails on real .gcda files).
GCOV_CMD=""
if [[ "$PLATFORM" == "macos" ]]; then
  if command -v xcrun >/dev/null 2>&1 && xcrun llvm-cov gcov --version >/dev/null 2>&1; then
    GCOV_CMD="xcrun llvm-cov gcov"
  elif command -v gcov >/dev/null 2>&1 && gcov --version 2>&1 | grep -qv "Apple"; then
    GCOV_CMD="gcov"
  else
    fail "no usable gcov / llvm-cov gcov found"
  fi
else
  # Linux: plain gcov (gcc), fallback to llvm-cov gcov
  if command -v gcov >/dev/null 2>&1; then
    GCOV_CMD="gcov"
  elif command -v llvm-cov >/dev/null 2>&1; then
    GCOV_CMD="llvm-cov gcov"
  else
    fail "no usable gcov found"
  fi
fi

echo "==> Using gcov command: $GCOV_CMD"

# Create coverage output directory
mkdir -p "$REPO/coverage"

# Source files of interest
SOURCE_FILES=(
  misc.c
  fa_batch.c
  cpu_rt_functions.c
  bloom.c
  markov.c
  crackalack_lookup.c
)

declare -A FILE_PCT
declare -A FILE_FRAC

# Process each source file
for src in "${SOURCE_FILES[@]}"; do
  src_path="$REPO/$src"
  gcov_err_log="/tmp/gcov_${src}.err"

  if [[ ! -f "$src_path" ]]; then
    FILE_PCT[$src]="no data"
    FILE_FRAC[$src]="(file not found)"
    continue
  fi

  # Run gcov; allow failure (no data is normal for crackalack_lookup.c)
  gcov_out=$($GCOV_CMD -o "$OBJDIR" "$src_path" 2>"$gcov_err_log") || true

  if [[ -z "$gcov_out" ]] || \
     echo "$gcov_out" | grep -q "No executable lines" || \
     ! echo "$gcov_out" | grep -q "Lines executed"; then
    FILE_PCT[$src]="no data"
    FILE_FRAC[$src]=""
  else
    # Parse "Lines executed:XX.XX% of N"
    pct=$(echo "$gcov_out" | grep "Lines executed" | head -1 | sed 's/.*Lines executed:\([0-9.]*\)%.*/\1/')
    total=$(echo "$gcov_out" | grep "Lines executed" | head -1 | sed 's/.*of \([0-9]*\).*/\1/')
    if [[ -n "$pct" && -n "$total" ]]; then
      # Calculate executed lines
      executed=$(awk "BEGIN { printf \"%d\", ($pct * $total / 100 + 0.5) }")
      FILE_PCT[$src]="${pct}%"
      FILE_FRAC[$src]="(${executed}/${total})"
    else
      FILE_PCT[$src]="no data"
      FILE_FRAC[$src]=""
    fi
  fi

  # Move generated .gcov files to coverage/
  for gcov_file in *.gcov; do
    [[ -f "$gcov_file" ]] && mv "$gcov_file" "$REPO/coverage/" || true
  done
done

# Print summary table
SUMMARY_FILE="$REPO/coverage/summary.txt"
{
  printf "%-28s %-10s %s\n" "FILE" "LINES%" "(executed/total)"
  printf "%s\n" "─────────────────────────────────────────────────────"
  for src in "${SOURCE_FILES[@]}"; do
    printf "%-28s %-10s %s\n" "$src" "${FILE_PCT[$src]}" "${FILE_FRAC[$src]}"
  done
} | tee "$SUMMARY_FILE"

echo ""
echo "Coverage report written to $REPO/coverage/"

# Print headline (restore happens via trap on EXIT)
misc_pct="${FILE_PCT[misc.c]}"
fabatch_pct="${FILE_PCT[fa_batch.c]}"
echo "misc.c: ${misc_pct} | fa_batch.c: ${fabatch_pct}"

exit 0
