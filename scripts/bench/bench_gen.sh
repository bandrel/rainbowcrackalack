#!/usr/bin/env bash
# Run gen throughput benchmark for one role across all optimized paths.
# Usage: bench_gen.sh <bin_dir> <role base|cand> <results_dir>
# Env: GEN_NUM_CHAINS (default 1000000)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/gpu_lock.sh"
: "${GEN_NUM_CHAINS:=1000000}"

BIN_DIR="$1"; ROLE="$2"; RESULTS_DIR="$3"
PYTHON="${BENCH_PYTHON:-python3}"

mapfile -t CONFIG_NAMES < <("$PYTHON" -c "import sys; sys.path.insert(0,'$SCRIPT_DIR'); import configs; print('\n'.join(configs.CONFIGS))")

for name in "${CONFIG_NAMES[@]}"; do
    mapfile -t argv < <("$PYTHON" -c "import sys; sys.path.insert(0,'$SCRIPT_DIR'); import configs; print('\n'.join(configs.gen_argv('$name', $GEN_NUM_CHAINS)))")
    log_file="$RESULTS_DIR/gen_${ROLE}_${name}.log"
    echo "[bench_gen] $ROLE/$name: crackalack_gen ${argv[*]}" >&2
    ( cd "$BIN_DIR" && with_gpu_lock ./crackalack_gen "${argv[@]}" ) > "$log_file" 2>&1 || true
    "$PYTHON" "$SCRIPT_DIR/parse_gen_bench.py" --merge "$RESULTS_DIR" \
        --config "$name" --role "$ROLE" --log "$log_file" \
        || echo "[bench_gen] WARN: no rate parsed for $name ($ROLE)" >&2
    # Normal gen writes a .rt table + .rt.state into bin_dir; remove them so
    # the bench doesn't accumulate tables across configs/roles/reruns.
    rm -f "$BIN_DIR"/*.rt "$BIN_DIR"/*.rt.state 2>/dev/null || true
done
