#!/usr/bin/env bash
# Benchmark blurbdust/master vs feature/faster-table-loading on dell3.
# Spec: docs/superpowers/specs/2026-05-02-blurbdust-perf-comparison-design.md
#
# Usage:
#   run_benchmark.sh                # prepare + run + report
#   run_benchmark.sh prepare        # clone, build, stage inputs (idempotent)
#   run_benchmark.sh run            # execute the 6 alternating trials
#   run_benchmark.sh report         # parse logs, write summary.md
set -euo pipefail

# ---- Tunables (override via env) ----
: "${BENCH_ROOT:=/mnt/nvme/rainbowcrackalack-bench}"
: "${TABLE_SOURCE:=/mnt/nvme/rainbow/tables/netntlmv1}"
: "${PARTS:=8}"
: "${HASH_COUNT:=100}"
: "${HASH_SEED:=20260502}"
: "${TRIALS:=3}"
: "${TIMEOUT_MIN:=30}"
: "${GPU_INDEX:=0}"
: "${BLURBDUST_REPO:=https://github.com/blurbdust/rainbowcrackalack.git}"
: "${BLURBDUST_REF:=master}"
: "${FEATURE_REPO:=https://github.com/blurbdust/rainbowcrackalack.git}"  # overridden in prepare
: "${FEATURE_REF:=feature/faster-table-loading}"

# Resolved paths (don't override).
BENCH_INPUT="$BENCH_ROOT/bench_input"
BENCH_TABLES="$BENCH_INPUT/tables"
BENCH_HASHES="$BENCH_INPUT/hashes.txt"
BENCH_VENV="$BENCH_ROOT/.venv"
BLURBDUST_DIR="$BENCH_ROOT/blurbdust"
FEATURE_DIR="$BENCH_ROOT/feature"

# Path to this script's dir (for invoking the python helpers).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log() { echo "[bench $(date -u +%H:%M:%S)] $*" >&2; }

phase_prepare() {
    log "PREPARE: bench root $BENCH_ROOT"
    mkdir -p "$BENCH_ROOT" "$BENCH_INPUT" "$BENCH_TABLES"

    # Clone or update blurbdust.
    if [[ ! -d "$BLURBDUST_DIR/.git" ]]; then
        log "cloning blurbdust"
        git clone --depth 50 "$BLURBDUST_REPO" "$BLURBDUST_DIR"
    fi
    git -C "$BLURBDUST_DIR" fetch origin "$BLURBDUST_REF"
    git -C "$BLURBDUST_DIR" checkout "$BLURBDUST_REF"
    git -C "$BLURBDUST_DIR" reset --hard "origin/$BLURBDUST_REF"

    # Clone or update feature branch from the local repo we ship with.
    # Caller is expected to push feature/faster-table-loading to a reachable
    # remote before running, OR to set FEATURE_REPO to a local path/URL.
    if [[ ! -d "$FEATURE_DIR/.git" ]]; then
        log "cloning feature"
        git clone "$FEATURE_REPO" "$FEATURE_DIR"
    fi
    git -C "$FEATURE_DIR" fetch origin "$FEATURE_REF"
    git -C "$FEATURE_DIR" checkout "$FEATURE_REF"
    git -C "$FEATURE_DIR" reset --hard "origin/$FEATURE_REF"

    # Build both.
    for d in "$BLURBDUST_DIR" "$FEATURE_DIR"; do
        log "building $d"
        ( cd "$d" && make clean >/dev/null 2>&1 || true && make linux )
        if [[ ! -x "$d/crackalack_lookup" ]]; then
            log "ERROR: $d/crackalack_lookup not built"; exit 1
        fi
    done

    # Set up Python venv with pycryptodome.
    if [[ ! -x "$BENCH_VENV/bin/python3" ]]; then
        log "creating venv at $BENCH_VENV"
        python3 -m venv "$BENCH_VENV"
        "$BENCH_VENV/bin/pip" install --quiet --upgrade pip
        "$BENCH_VENV/bin/pip" install --quiet pycryptodome
    fi

    # Generate hashes (idempotent; only regen if missing).
    if [[ ! -s "$BENCH_HASHES" ]]; then
        log "generating $HASH_COUNT hashes (seed=$HASH_SEED)"
        "$BENCH_VENV/bin/python3" "$SCRIPT_DIR/gen_netntlmv1_hashes.py" \
            --seed "$HASH_SEED" --count "$HASH_COUNT" --out "$BENCH_HASHES"
    fi

    # Stage table subset: pick PARTS files from the two lowest-numbered subdirs.
    if [[ -z "$(ls -A "$BENCH_TABLES")" ]]; then
        log "staging $PARTS table parts from $TABLE_SOURCE"
        local subdirs
        subdirs=$(ls -1 "$TABLE_SOURCE" | sort -n | head -2)
        local count=0
        for sd in $subdirs; do
            for f in "$TABLE_SOURCE/$sd"/*.rtc; do
                [[ -f "$f" ]] || continue
                ln -sf "$f" "$BENCH_TABLES/$(basename "$f")"
                count=$((count+1))
                if [[ "$count" -ge "$PARTS" ]]; then break 2; fi
            done
        done
        log "staged $count parts"
    fi

    # Smoke test: run blurbdust against one part for 30s to confirm it doesn't
    # crash on the directory layout.
    log "smoke-testing blurbdust on staged inputs (30s cap)"
    local smoke_dir="$BENCH_ROOT/smoke"
    mkdir -p "$smoke_dir"
    ln -sf "$(readlink -f "$BENCH_TABLES"/*.rtc | head -1)" "$smoke_dir/" 2>/dev/null || \
        cp -l "$(ls "$BENCH_TABLES"/*.rtc | head -1)" "$smoke_dir/" 2>/dev/null || true
    if ! timeout 30 "$BLURBDUST_DIR/crackalack_lookup" "$smoke_dir" \
            "$BENCH_HASHES" >/dev/null 2>&1; then
        local rc=$?
        if [[ "$rc" -ne 124 && "$rc" -ne 0 ]]; then
            log "WARN: blurbdust smoke test exited $rc — investigate before running full bench"
        fi
    fi

    log "PREPARE done."
}

phase_run() {
    log "RUN: $TRIALS trials per branch, alternating"

    # Verify prepare outputs exist.
    if [[ ! -x "$BLURBDUST_DIR/crackalack_lookup" ]] \
       || [[ ! -x "$FEATURE_DIR/crackalack_lookup" ]] \
       || [[ ! -s "$BENCH_HASHES" ]] \
       || [[ -z "$(ls -A "$BENCH_TABLES" 2>/dev/null)" ]]; then
        log "ERROR: prepare outputs missing — run '$0 prepare' first"
        exit 1
    fi

    # Sudo for cache drops — request once up front.
    if ! sudo -n true 2>/dev/null; then
        log "Need sudo to drop caches between trials. Prompting now..."
        sudo true
    fi

    # Create a UTC-timestamped results dir.
    local stamp
    stamp=$(date -u +%Y%m%dT%H%M%SZ)
    local results_dir="$BENCH_ROOT/bench_results/$stamp"
    mkdir -p "$results_dir"
    echo "$stamp" > "$BENCH_ROOT/bench_results/LATEST"
    log "results dir: $results_dir"

    # Capture provenance.
    {
        echo "# Provenance"
        echo "blurbdust_sha=$(git -C "$BLURBDUST_DIR" rev-parse HEAD)"
        echo "feature_sha=$(git -C "$FEATURE_DIR" rev-parse HEAD)"
        echo "host=$(hostname)"
        echo "uname=$(uname -a)"
        echo "started_at=$stamp"
        echo "parts=$PARTS"
        echo "hash_count=$HASH_COUNT"
        echo "hash_seed=$HASH_SEED"
        echo "trials=$TRIALS"
        echo "timeout_min=$TIMEOUT_MIN"
        echo "table_source=$TABLE_SOURCE"
        nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null \
            | head -1 | sed 's/^/gpu=/'
    } > "$results_dir/provenance.txt"

    # Trial loop: alternating blurbdust, feature.
    local n
    for ((n=1; n<=TRIALS; n++)); do
        for branch in blurbdust feature; do
            local bin_dir
            if [[ "$branch" == "blurbdust" ]]; then bin_dir="$BLURBDUST_DIR"; \
                                              else bin_dir="$FEATURE_DIR"; fi

            log "trial $n/$TRIALS branch=$branch — dropping cache"
            sync
            sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

            local log_file="$results_dir/trial_$(printf '%02d' "$n")_${branch}.log"
            local time_file="$results_dir/trial_$(printf '%02d' "$n")_${branch}.time"

            # /usr/bin/time -v captures wall + RSS + exit. timeout caps the run.
            # `cd $bin_dir` so that CL/ kernel dir is found alongside the binary.
            log "trial $n/$TRIALS branch=$branch — running"
            (
                cd "$bin_dir"
                /usr/bin/time -v -o "$time_file" \
                    timeout "${TIMEOUT_MIN}m" \
                    ./crackalack_lookup "$BENCH_TABLES" "$BENCH_HASHES" \
                    > "$log_file" 2>&1
            ) || true   # /usr/bin/time still writes the .time file on non-zero exit

            local exit_status
            exit_status=$(awk -F': ' '/Exit status/{print $2}' "$time_file" 2>/dev/null || echo "?")
            log "trial $n/$TRIALS branch=$branch — exit=$exit_status"
        done
    done

    log "RUN done. Results at $results_dir"
}       # filled in Task 4
phase_report() { :; }    # filled in Task 5

main() {
    local cmd="${1:-all}"
    case "$cmd" in
        prepare) phase_prepare ;;
        run)     phase_run ;;
        report)  phase_report ;;
        all)     phase_prepare; phase_run; phase_report ;;
        *) echo "Usage: $0 [prepare|run|report]" >&2; exit 2 ;;
    esac
}

main "$@"
