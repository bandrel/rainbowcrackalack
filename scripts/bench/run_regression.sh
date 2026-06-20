#!/usr/bin/env bash
# Crack regression / false-negative framework for crackalack.
# Spec: docs/superpowers/specs/2026-06-20-crack-regression-framework-design.md
#
# Usage:
#   run_regression.sh prepare     # venv + build current repo (+ gen_known_hash)
#   run_regression.sh roundtrip   # self-contained crack round-trip (current build)
#   run_regression.sh crackdiff   # BASE-vs-CANDIDATE differential (added in a later task)
#   run_regression.sh report      # write regression_summary.md (added in a later task)
#   run_regression.sh all         # prepare + roundtrip + crackdiff + report
set -euo pipefail

# ---- Tunables (override via env) ----
: "${REG_ROOT:=/tmp/crackalack-regression}"
: "${THIS_REPO:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
: "${BACKEND:=$(uname -s | grep -qi darwin && echo metal || echo cuda)}"
: "${MAKE_TARGET:=$([[ "$BACKEND" == metal ]] && echo macos || echo linux)}"
# Differential (crackdiff) tunables:
: "${BASE_REF:=origin/bench-base-preinnerloop}"
: "${CAND_REF:=$(git -C "$THIS_REPO" rev-parse --abbrev-ref HEAD)}"
: "${REAL_TABLES:=/mnt/nvme/rtc/}"
: "${HASH_COUNT:=200}"
: "${HASH_SEED:=20260620}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/gpu_lock.sh"
VENV="$REG_ROOT/.venv"
PY="$VENV/bin/python3"
RESULTS="$REG_ROOT/results"

log() { echo "[regression $(date -u +%H:%M:%S)] $*" >&2; }

# Round-trip configs: name|gen_args|gkh_flags|chain_len
#
# chain_len MUST match what each optimized precompute kernel walks.  The
# precompute_ntlm8 / precompute_ntlm9 Metal/CL/CUDA kernels HARDCODE the
# published-table chain lengths (422000 and 803000 respectively) and ignore the
# host's chain_len argument, so a round-trip table generated at any other length
# is never reconstructed by precompute (the hash is in the table but lookup
# reports 0 confirmations).  precompute_netntlmv1_7 was fixed to honor the host
# chain_len, so it works at any length (we use a short one to keep gen fast).
# Keep chain_count modest so a single thread can still surface the chain.
ROUNDTRIP_CONFIGS=(
  "ntlm8|ntlm ascii-32-95 8 8 0 422000 256 0|--algo ntlm --charset ascii-32-95 --plaintext-len 8|422000"
  "ntlm9|ntlm ascii-32-95 9 9 0 803000 256 0|--algo ntlm --charset ascii-32-95 --plaintext-len 9|803000"
  "netntlmv1_7|netntlmv1 byte 7 7 0 1000 1024 0||1000"
)
REDUCTION_OFFSET=0

ensure_venv() {
    if [[ ! -x "$PY" ]] || ! "$PY" -c "import pytest, Crypto" 2>/dev/null; then
        log "creating venv at $VENV"
        rm -rf "$VENV"; python3 -m venv "$VENV"
        "$VENV/bin/pip" install --quiet --upgrade pip
        "$VENV/bin/pip" install --quiet pycryptodome pytest
    fi
}

build_repo() {  # build_repo <ref> <dir>
    local ref="$1" dir="$2"
    if [[ ! -d "$dir/.git" ]]; then git clone "$THIS_REPO" "$dir"; fi
    git -C "$dir" fetch "$THIS_REPO" 2>/dev/null || true
    git -C "$dir" fetch origin 2>/dev/null || true
    git -C "$dir" checkout -f "$ref"
    ( cd "$dir" && make clean >/dev/null 2>&1 || true && make "$MAKE_TARGET" )
}

phase_prepare() {
    log "PREPARE: root=$REG_ROOT backend=$BACKEND target=$MAKE_TARGET"
    mkdir -p "$REG_ROOT" "$RESULTS"
    ensure_venv
    ( cd "$THIS_REPO" && make "$MAKE_TARGET" && make gen_known_hash )
    local b
    for b in crackalack_gen crackalack_lookup crackalack_sort get_chain gen_known_hash; do
        [[ -x "$THIS_REPO/$b" ]] || { log "ERROR: missing binary $b after build"; exit 1; }
    done
}

# Run one round-trip config in its own clean dir; echo "cracked exp_hash storedpt|exp_pt".
run_one_roundtrip() {
    local name="$1" gen_args="$2" gkh_flags="$3" chain_len="$4"
    local target_pos=$(( chain_len / 2 ))   # provably-in-table hash mid-chain
    local work="$REG_ROOT/rt/$name"
    rm -rf "$work"; mkdir -p "$work"
    local bin="$THIS_REPO"

    # crackalack binaries load GPU kernels relative to CWD (e.g. "Metal/foo.metal",
    # "CL/...", "CUDA/...").  We run them from $work to keep outputs local, so the
    # kernel dir(s) must be reachable from there: symlink them in from the repo.
    local kdir
    for kdir in Metal CL CUDA; do
        [[ -d "$bin/$kdir" ]] && ln -sfn "$bin/$kdir" "$work/$kdir"
    done

    # 1. Generate + sort a tiny table in the work dir.
    ( cd "$work" && with_gpu_lock "$bin/crackalack_gen" $gen_args >/dev/null 2>&1 )
    local table; table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"
    [[ -n "$table" ]] || { log "$name: gen produced no .rt"; echo "false  |"; return; }
    ( cd "$work" && with_gpu_lock "$bin/crackalack_sort" "$table" >/dev/null 2>&1 )
    table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"

    # 2. Read a real stored chain start from the sorted table.
    local start_index
    start_index="$("$bin/get_chain" "$table" 0 | awk '/Start index:/ {print $3}')"
    [[ -n "$start_index" ]] || { log "$name: get_chain failed"; echo "false  |"; return; }

    # 3. Construct a provably-in-table (hash, plaintext) on that chain.
    local gkh_out exp_hash exp_pt
    gkh_out="$("$bin/gen_known_hash" "$chain_len" "$REDUCTION_OFFSET" "$start_index" "$target_pos" $gkh_flags)"
    exp_hash="$(awk -F= '/^hash=/{print $2}' <<<"$gkh_out")"
    exp_pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$gkh_out")"
    [[ -n "$exp_hash" && -n "$exp_pt" ]] || { log "$name: gen_known_hash failed"; echo "false  |"; return; }

    # 4. Look it up against the generated table; pot written into the work dir.
    local pot="$work/result.pot"
    ( cd "$work" && rm -f ./*.index rcracki.precalc.* \
        && with_gpu_lock "$bin/crackalack_lookup" "$work" "$exp_hash" "$pot" >/dev/null 2>&1 ) || true

    # 5. Success = the hash is PRESENT in the (hashcat) pot. A crack only reaches
    #    the pot after the false-alarm check confirms it, so presence == correct.
    #    storedpt is recorded for the report (informational only).
    local line present storedpt cracked
    line="$("$PY" - "$pot.hashcat" "$exp_hash" <<'PY'
import sys
from crack_diff import parse_pot
pot, h = sys.argv[1], sys.argv[2].lower()
d = parse_pot(pot)
print(("1" if h in d else "0"), d.get(h, ""))
PY
)"
    present="${line%% *}"; storedpt="${line#* }"
    [[ "$present" == "1" ]] && cracked=true || cracked=false
    echo "$cracked $exp_hash $storedpt|$exp_pt"
}

phase_roundtrip() {
    log "ROUNDTRIP: backend=$BACKEND"
    mkdir -p "$RESULTS"
    local json="$RESULTS/roundtrip_$BACKEND.json"
    export PYTHONPATH="$SCRIPT_DIR"
    "$PY" -c "from crack_diff import parse_pot" 2>/dev/null \
        || { log "ERROR: crack_diff not importable (PYTHONPATH=$PYTHONPATH)"; exit 1; }
    echo "{" > "$json"
    local first=1
    for cfg in "${ROUNDTRIP_CONFIGS[@]}"; do
        IFS='|' read -r name gen_args gkh_flags chain_len <<<"$cfg"
        log "round-trip: $name"
        local rt_out
        if ! rt_out="$(run_one_roundtrip "$name" "$gen_args" "$gkh_flags" "$chain_len")"; then
            log "$name: run_one_roundtrip failed (exit $?); recording as not cracked"
            rt_out="false  |"
        fi
        read -r cracked exp_hash gotpair <<<"$rt_out"
        local storedpt="${gotpair%%|*}" exp="${gotpair##*|}"
        [[ $first -eq 1 ]] || echo "," >> "$json"; first=0
        local gotjson="null"; [[ -n "$storedpt" ]] && gotjson="\"$storedpt\""
        printf '  "%s": {"cracked": %s, "expected": "%s", "got": %s}' \
            "$name" "$cracked" "$exp" "$gotjson" >> "$json"
    done
    echo "" >> "$json"; echo "}" >> "$json"
    log "wrote $json"
    cat "$json" >&2
}

phase_crackdiff() {
    log "CRACKDIFF: BASE=$BASE_REF CAND=$CAND_REF tables=$REAL_TABLES"
    if [[ ! -d "$REAL_TABLES" ]]; then
        log "WARN: real tables dir '$REAL_TABLES' not found — skipping crackdiff"
        echo '{"base_cracked":0,"cand_cracked":0,"regressions":[],"improvements":[],"skipped":true}' \
            > "$RESULTS/crackdiff.json"
        return
    fi
    mkdir -p "$RESULTS"
    local base_dir="$REG_ROOT/base" cand_dir="$REG_ROOT/cand"
    log "building BASE"; build_repo "$BASE_REF" "$base_dir"
    log "building CANDIDATE"; build_repo "$CAND_REF" "$cand_dir"

    # Generate a shared hash set (NetNTLMv1-7, default challenge) once.
    local hashes="$REG_ROOT/diff_hashes.txt"
    "$PY" "$SCRIPT_DIR/gen_netntlmv1_hashes.py" --seed "$HASH_SEED" --count "$HASH_COUNT" --out "$hashes"

    # Run each build against the SAME real tables with the SAME hashes, clearing
    # the precompute cache between runs so a stale *.index can't mask a diff.
    local role dir pot
    for role in base cand; do
        dir="$base_dir"; [[ "$role" == cand ]] && dir="$cand_dir"
        pot="$RESULTS/${role}.pot"
        rm -f "$pot" "$pot.hashcat"
        ( cd "$dir" && rm -f ./*.index rcracki.precalc.* \
            && with_gpu_lock ./crackalack_lookup "$REAL_TABLES" "$hashes" "$pot" >/dev/null 2>&1 ) || true
        log "$role cracked: $(wc -l < "$pot.hashcat" 2>/dev/null || echo 0)"
    done

    export PYTHONPATH="$SCRIPT_DIR"
    "$PY" "$SCRIPT_DIR/crack_diff.py" \
        --base-pot "$RESULTS/base.pot.hashcat" \
        --cand-pot "$RESULTS/cand.pot.hashcat" \
        --out "$RESULTS/crackdiff.json" || log "crackdiff: regressions detected"
}

main() {
    local phase="${1:-all}"
    case "$phase" in
        prepare)   phase_prepare ;;
        roundtrip) phase_roundtrip ;;
        crackdiff) phase_crackdiff ;;
        report)    phase_report ;;
        all)       phase_prepare; phase_roundtrip; phase_crackdiff; phase_report ;;
        *) echo "Unknown phase: $phase" >&2; exit 2 ;;
    esac
}
main "$@"
