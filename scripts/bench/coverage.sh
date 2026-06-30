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

# Build with coverage instrumentation
echo "==> Building crackalack_unit_tests with COVERAGE=1 ($PLATFORM)..."
BUILD_CMD="make clean && make $PLATFORM COVERAGE=1"
if [[ "$PLATFORM" == "linux" && -n "${CUDA_PATH:-}" ]]; then
  BUILD_CMD="make clean && make $PLATFORM COVERAGE=1 CUDA_PATH=$CUDA_PATH"
fi
if ! eval "$BUILD_CMD" > /tmp/coverage_build.log 2>&1; then
  tail -40 /tmp/coverage_build.log >&2
  fail "Coverage build failed. See /tmp/coverage_build.log"
fi

# Determine object directory
OBJDIR="$REPO/build/$PLATFORM/obj"

# Run unit tests to generate .gcda files
echo "==> Running crackalack_unit_tests..."
if ! "$REPO/crackalack_unit_tests" > /tmp/coverage_run.log 2>&1; then
  tail -40 /tmp/coverage_run.log >&2
  fail "crackalack_unit_tests exited nonzero. See /tmp/coverage_run.log"
fi

# Find gcov tool
GCOV_CMD=""
if command -v gcov > /dev/null 2>&1; then
  if [[ "$PLATFORM" == "macos" ]]; then
    # On macOS, gcov may be Apple's stub — verify it actually works
    if gcov --version > /dev/null 2>&1; then
      GCOV_CMD="gcov"
    fi
  else
    GCOV_CMD="gcov"
  fi
fi

if [[ -z "$GCOV_CMD" ]]; then
  # Fall back to xcrun llvm-cov gcov on macOS
  if command -v xcrun > /dev/null 2>&1 && xcrun llvm-cov gcov --version > /dev/null 2>&1; then
    GCOV_CMD="xcrun llvm-cov gcov"
  else
    fail "No working gcov found. Install llvm or ensure xcrun llvm-cov gcov is available."
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

# Restore normal build
echo "==> Restoring normal build ($PLATFORM)..."
RESTORE_CMD="make clean && make $PLATFORM"
if [[ "$PLATFORM" == "linux" && -n "${CUDA_PATH:-}" ]]; then
  RESTORE_CMD="make clean && make $PLATFORM CUDA_PATH=$CUDA_PATH"
fi
if ! eval "$RESTORE_CMD" > /tmp/coverage_restore.log 2>&1; then
  echo "WARN: normal build restore failed. See /tmp/coverage_restore.log" >&2
fi

# Print headline
misc_pct="${FILE_PCT[misc.c]}"
fabatch_pct="${FILE_PCT[fa_batch.c]}"
echo "misc.c: ${misc_pct} | fa_batch.c: ${fabatch_pct}"

exit 0
