#!/usr/bin/env bash
# Profile one role's gen kernel(s) with ncu and merge metrics into profile.json.
# Usage: profile_ncu.sh <bin_dir> <role base|cand> <results_dir>
# Env: PROFILE_CONFIGS (space-separated, default "netntlmv1_7"),
#      NCU_BIN (default ncu), PROFILE_NUM_CHAINS (default 163840).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/gpu_lock.sh"
: "${PROFILE_CONFIGS:=netntlmv1_7}"
: "${NCU_BIN:=ncu}"
: "${PROFILE_NUM_CHAINS:=163840}"
PYTHON="${BENCH_PYTHON:-python3}"

BIN_DIR="$1"; ROLE="$2"; RESULTS_DIR="$3"
NCU_METRICS=$("$PYTHON" -c "import sys; sys.path.insert(0,'$SCRIPT_DIR'); import extract_ncu; print(extract_ncu.NCU_METRICS)")

for name in $PROFILE_CONFIGS; do
    kernel=$("$PYTHON" -c "import sys; sys.path.insert(0,'$SCRIPT_DIR'); import configs; print(configs.CONFIGS['$name']['gen_kernel'])")
    mapfile -t argv < <("$PYTHON" -c "import sys; sys.path.insert(0,'$SCRIPT_DIR'); import configs; print('\n'.join(configs.gen_argv('$name', $PROFILE_NUM_CHAINS)))")
    csv_file="$RESULTS_DIR/ncu_${ROLE}_${name}.csv"
    echo "[profile_ncu] $ROLE/$name kernel=$kernel" >&2
    ( cd "$BIN_DIR" && with_gpu_lock sudo -n "$NCU_BIN" --target-processes all \
        -k "$kernel" -c 1 --csv --metrics "$NCU_METRICS" \
        ./crackalack_gen "${argv[@]}" ) > "$csv_file" 2>/dev/null || \
        echo "[profile_ncu] WARN: ncu failed for $name ($ROLE)" >&2
    "$PYTHON" "$SCRIPT_DIR/extract_ncu.py" --merge "$RESULTS_DIR" \
        --config "$name" --role "$ROLE" --csv "$csv_file" \
        || echo "[profile_ncu] WARN: no metrics parsed for $name ($ROLE)" >&2
    # The profiled gen run writes a .rt table into bin_dir; clean it up.
    rm -f "$BIN_DIR"/*.rt "$BIN_DIR"/*.rt.state 2>/dev/null || true
done
