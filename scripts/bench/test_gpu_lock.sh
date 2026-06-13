#!/usr/bin/env bash
# Self-test for gpu_lock.sh (no GPU required).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "$DIR/lib/gpu_lock.sh"

# 1. with_gpu_lock runs a command and returns its output/exit code.
out="$(with_gpu_lock echo hello)"
[[ "$out" == "hello" ]] || { echo "FAIL: expected hello, got '$out'"; exit 1; }

# 2. with_gpu_lock propagates non-zero exit.
rc=0; with_gpu_lock false || rc=$?
[[ "$rc" -eq 1 ]] || { echo "FAIL: expected rc=1, got $rc"; exit 1; }

# 3. The lock serializes: a second holder waits. Background a 1s holder, then a
#    second call must observe the lock is held (>0.5s wall) before running.
GPU_LOCK_FILE="$(mktemp -u)"; export GPU_LOCK_FILE
with_gpu_lock sleep 1 &
sleep 0.2
start=$(date +%s.%N)
with_gpu_lock true
end=$(date +%s.%N)
waited=$(echo "$end - $start" | bc)
awk -v w="$waited" "BEGIN{exit !(w > 0.5)}" || { echo "FAIL: second caller did not wait (waited ${waited}s)"; exit 1; }
echo "PASS"
