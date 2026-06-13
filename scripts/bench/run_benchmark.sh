#!/usr/bin/env bash
# Benchmark BASE vs CANDIDATE crackalack builds on dell3 (lookup, gen, profile, equivalence).
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
# This repo is the candidate by default; master is the base.
: "${THIS_REPO:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
: "${BASE_REPO:=$THIS_REPO}"
: "${BASE_REF:=master}"
: "${CAND_REPO:=$THIS_REPO}"
: "${CAND_REF:=$(git -C "$THIS_REPO" rev-parse --abbrev-ref HEAD)}"
: "${PROFILE_CONFIGS:=netntlmv1_7}"
export PROFILE_CONFIGS

BENCH_INPUT="$BENCH_ROOT/bench_input"
BENCH_TABLES="$BENCH_INPUT/tables"
BENCH_HASHES="$BENCH_INPUT/hashes.txt"
BENCH_VENV="$BENCH_ROOT/.venv"
BASE_DIR="$BENCH_ROOT/base"
CAND_DIR="$BENCH_ROOT/cand"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/gpu_lock.sh"
role_dir() { [[ "$1" == "base" ]] && echo "$BASE_DIR" || echo "$CAND_DIR"; }

log() { echo "[bench $(date -u +%H:%M:%S)] $*" >&2; }

phase_prepare() {
    log "PREPARE: bench root $BENCH_ROOT"
    mkdir -p "$BENCH_ROOT" "$BENCH_INPUT" "$BENCH_TABLES"

    for role in base cand; do
        local repo ref dir
        if [[ "$role" == "base" ]]; then repo="$BASE_REPO"; ref="$BASE_REF"; dir="$BASE_DIR"
                                    else repo="$CAND_REPO"; ref="$CAND_REF"; dir="$CAND_DIR"; fi
        if [[ ! -d "$dir/.git" ]]; then log "cloning $role ($repo)"; git clone "$repo" "$dir"; fi
        git -C "$dir" fetch origin "$ref" || git -C "$dir" fetch "$repo" "$ref"
        git -C "$dir" checkout "$ref"
        git -C "$dir" reset --hard "$(git -C "$dir" rev-parse FETCH_HEAD 2>/dev/null || echo "origin/$ref")"
        log "building $role at $dir"
        ( cd "$dir" && make clean >/dev/null 2>&1 || true && make linux )
        [[ -x "$dir/crackalack_lookup" && -x "$dir/crackalack_gen" ]] || { log "ERROR: $role build incomplete"; exit 1; }
    done

    # Set up Python venv with pycryptodome.  Recreate from scratch if the venv
    # is missing OR if pycryptodome can't be imported (a partial venv from a
    # prior failed run would otherwise be silently reused).
    if [[ ! -x "$BENCH_VENV/bin/python3" ]] \
       || ! "$BENCH_VENV/bin/python3" -c "from Crypto.Cipher import DES" 2>/dev/null; then
        log "creating venv at $BENCH_VENV"
        rm -rf "$BENCH_VENV"
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
        local -a subdirs
        mapfile -t subdirs < <(ls -1 "$TABLE_SOURCE" | sort -n | head -2)
        local count=0
        for sd in "${subdirs[@]}"; do
            for f in "$TABLE_SOURCE/$sd"/*.rtc; do
                [[ -f "$f" ]] || continue
                ln -sf "$f" "$BENCH_TABLES/$(basename "$f")"
                count=$((count+1))
                if [[ "$count" -ge "$PARTS" ]]; then break 2; fi
            done
        done
        log "staged $count parts"
    fi

    # Smoke test: run base against one part for 30s to confirm it doesn't
    # crash on the directory layout.
    log "smoke-testing base on staged inputs (30s cap)"
    local smoke_dir="$BENCH_ROOT/smoke"
    mkdir -p "$smoke_dir"
    ln -sf "$(readlink -f "$BENCH_TABLES"/*.rtc | head -1)" "$smoke_dir/" 2>/dev/null || \
        cp -l "$(ls "$BENCH_TABLES"/*.rtc | head -1)" "$smoke_dir/" 2>/dev/null || true
    if ! compgen -G "$smoke_dir/*.rtc" >/dev/null; then
        log "ERROR: could not stage any .rtc file into smoke_dir"; exit 1
    fi
    # Exit codes 0 (clean), 124 (timeout's own exit), 137 (SIGKILL = 128+9, used
     # by `timeout --kill-after`), 143 (SIGTERM = 128+15, default timeout signal),
     # and 255 (some binaries' ungraceful response to SIGTERM) are all expected
     # ways for a 30s-capped run to end without a real crash.
    local rc=0
    timeout 30 "$BASE_DIR/crackalack_lookup" "$smoke_dir" \
        "$BENCH_HASHES" >/dev/null 2>&1 || rc=$?
    case "$rc" in
        0|124|137|143|255) ;;
        *) log "WARN: base smoke test exited $rc — investigate before running full bench" ;;
    esac

    log "PREPARE done."
}

phase_run() {
    log "RUN: $TRIALS trials per role, alternating"
    local n bin_dir

    # Verify prepare outputs exist.
    if [[ ! -x "$BASE_DIR/crackalack_lookup" ]] \
       || [[ ! -x "$CAND_DIR/crackalack_lookup" ]] \
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
        echo "base_ref=$BASE_REF"
        echo "base_sha=$(git -C "$BASE_DIR" rev-parse HEAD)"
        echo "cand_ref=$CAND_REF"
        echo "cand_sha=$(git -C "$CAND_DIR" rev-parse HEAD)"
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

    for ((n=1; n<=TRIALS; n++)); do
        for role in base cand; do
            bin_dir="$(role_dir "$role")"
            log "trial $n/$TRIALS role=$role — dropping cache"
            sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
            # Clear the precompute cache for a cold run. crackalack_lookup reads
            # ANY *.index file in its cwd as a cached precompute, so clear those
            # too (not just rcracki.precalc.*), or a stale cache yields a 0.0s
            # precompute and non-comparable timing.
            rm -f "$bin_dir"/rcracki.precalc.* "$bin_dir"/*.index 2>/dev/null || true
            local log_file="$results_dir/trial_$(printf '%02d' "$n")_${role}.log"
            local time_file="$results_dir/trial_$(printf '%02d' "$n")_${role}.time"
            ( cd "$bin_dir" && with_gpu_lock /usr/bin/time -v -o "$time_file" \
                timeout "${TIMEOUT_MIN}m" \
                ./crackalack_lookup "$BENCH_TABLES" "$BENCH_HASHES" > "$log_file" 2>&1 ) || true
            log "trial $n/$TRIALS role=$role — exit=$(awk -F': ' '/Exit status/{print $2}' "$time_file" 2>/dev/null || echo '?')"
        done
    done

    # Gen throughput (all paths) + ncu profile (PROFILE_CONFIGS) per role.
    for role in base cand; do
        bin_dir="$(role_dir "$role")"
        bash "$SCRIPT_DIR/bench_gen.sh" "$bin_dir" "$role" "$results_dir" || log "WARN: bench_gen $role failed"
        bash "$SCRIPT_DIR/profile_ncu.sh" "$bin_dir" "$role" "$results_dir" || log "WARN: profile_ncu $role failed"
    done

    # Output equivalence: generate one small seeded table from each build, compare sha.
    local eq_args="netntlmv1 byte 7 7 0 881689 256 0"
    for role in base cand; do
        bin_dir="$(role_dir "$role")"
        rm -f "$bin_dir"/netntlmv1_byte#7-7_0_881689x256_0.rt* 2>/dev/null || true
        # shellcheck disable=SC2086
        ( cd "$bin_dir" && with_gpu_lock ./crackalack_gen $eq_args >/dev/null 2>&1 ) || true
    done
    local base_rt cand_rt
    base_rt="$(ls "$BASE_DIR"/netntlmv1_byte#7-7_0_881689x256_0.rt 2>/dev/null || true)"
    cand_rt="$(ls "$CAND_DIR"/netntlmv1_byte#7-7_0_881689x256_0.rt 2>/dev/null || true)"
    if [[ -f "$base_rt" && -f "$cand_rt" ]]; then
        "$BENCH_VENV/bin/python3" "$SCRIPT_DIR/check_equivalence.py" --results "$results_dir" \
            --check netntlmv1_7_gen_table --base-file "$base_rt" --cand-file "$cand_rt" || true
    else
        log "WARN: equivalence skipped — table(s) not produced"
    fi

    log "RUN done. Results at $results_dir"
}
phase_report() {
    local stamp
    stamp=$(cat "$BENCH_ROOT/bench_results/LATEST" 2>/dev/null || echo "")
    if [[ -z "$stamp" ]]; then
        log "ERROR: no LATEST run found — run '$0 run' first"
        exit 1
    fi
    local results_dir="$BENCH_ROOT/bench_results/$stamp"
    log "REPORT: parsing $results_dir"

    # Build meta JSON from provenance.txt.
    local meta_json
    meta_json=$(PROVENANCE_PATH="$results_dir/provenance.txt" "$BENCH_VENV/bin/python3" - <<'EOF'
import json, os, sys
meta = {}
with open(os.environ["PROVENANCE_PATH"]) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        k, _, v = line.partition("=")
        meta[k] = v
print(json.dumps(meta))
EOF
)

    "$BENCH_VENV/bin/python3" "$SCRIPT_DIR/parse_results.py" \
        "$results_dir" --meta "$meta_json"

    log "REPORT done."
    echo
    cat "$results_dir/summary.md"
}

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
