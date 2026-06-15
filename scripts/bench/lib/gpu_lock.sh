#!/usr/bin/env bash
# GPU access mutex. Serializes crackalack_gen / crackalack_lookup / ncu so
# concurrent harness runs (or parallel agents reaching gpuhost3) queue instead of
# corrupting each other's timings.
#
# Usage:  source lib/gpu_lock.sh ; with_gpu_lock <command> [args...]
# Tunables: GPU_LOCK_FILE (default /tmp/crackalack_bench.lock),
#           GPU_LOCK_TIMEOUT seconds (default 3600).
: "${GPU_LOCK_FILE:=/tmp/crackalack_bench.lock}"
: "${GPU_LOCK_TIMEOUT:=3600}"

with_gpu_lock() {
    local __gpu_fd
    exec {__gpu_fd}>>"$GPU_LOCK_FILE"
    flock -w "$GPU_LOCK_TIMEOUT" "$__gpu_fd" || {
        echo "with_gpu_lock: timed out acquiring $GPU_LOCK_FILE" >&2
        exec {__gpu_fd}>&-
        return 75
    }
    local rc=0
    "$@" || rc=$?
    exec {__gpu_fd}>&-   # release lock
    return "$rc"
}
