#!/bin/bash
set -euo pipefail

# ThreadSanitizer (TSan) smoke test for rainbowcrackalack
#
# Runs the TSan race-detection build (make tsan-sort) with suppressions
# configured and reports PASS/FAIL based on exit code.
#
# Usage: ./tsan_smoke_test.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Set up TSan options to use the suppression file.
# TSAN_OPTIONS is read by TSan-instrumented binaries at runtime.
export TSAN_OPTIONS="suppressions=${PROJECT_ROOT}/tsan.supp"

echo "=== ThreadSanitizer Smoke Test ==="
echo "  Project root: $PROJECT_ROOT"
echo "  Suppression file: ${PROJECT_ROOT}/tsan.supp"
echo ""
echo "Running: make tsan-sort"
echo ""

# Run make tsan-sort and capture the exit code.
# Note: we temporarily disable errexit to capture the exit code for reporting.
set +e
make -C "$PROJECT_ROOT" tsan-sort
RC=$?
set -e

echo ""
echo "=== Test Result ==="
if [ $RC -eq 0 ]; then
    echo "PASS: TSan race detection completed with no fatal errors."
    exit 0
else
    echo "FAIL: TSan detected issues (exit code: $RC)."
    exit "$RC"
fi
