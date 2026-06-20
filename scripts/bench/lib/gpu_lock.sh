#!/usr/bin/env bash
# GPU access mutex. Serializes crackalack_gen / crackalack_lookup / ncu so
# concurrent harness runs (or parallel agents reaching dell3) queue instead of
# corrupting each other's timings.
#
# Usage:  source lib/gpu_lock.sh ; with_gpu_lock <command> [args...]
# Tunables: GPU_LOCK_FILE (default /tmp/crackalack_bench.lock),
#           GPU_LOCK_TIMEOUT seconds (default 3600).
: "${GPU_LOCK_FILE:=/tmp/crackalack_bench.lock}"
: "${GPU_LOCK_TIMEOUT:=3600}"

# flock(1) is a util-linux tool and is absent on macOS.  Detect it once; if it's
# missing, fall back to a portable mkdir(1)-based spinlock (atomic create) so the
# mutex still serializes GPU access on Metal/macOS hosts.
if command -v flock >/dev/null 2>&1; then
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
else
    with_gpu_lock() {
        local __gpu_lockdir="${GPU_LOCK_FILE}.d"
        local __waited=0
        until mkdir "$__gpu_lockdir" 2>/dev/null; do
            if [[ "$__waited" -ge "$GPU_LOCK_TIMEOUT" ]]; then
                echo "with_gpu_lock: timed out acquiring $__gpu_lockdir" >&2
                return 75
            fi
            sleep 1
            __waited=$((__waited + 1))
        done
        local rc=0
        "$@" || rc=$?
        rmdir "$__gpu_lockdir" 2>/dev/null || true
        return "$rc"
    }
fi
