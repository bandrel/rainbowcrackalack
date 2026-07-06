#!/usr/bin/env bash
# Crack regression / false-negative framework for crackalack.
# Spec: docs/superpowers/specs/2026-06-20-crack-regression-framework-design.md
#
# Usage:
#   run_regression.sh prepare         # venv + build current repo (+ gen_known_hash)
#   run_regression.sh roundtrip       # single-hash crack round-trip (non-batch path)
#   run_regression.sh roundtrip-multi # 2-hash crack round-trip (batched multi-hash path)
#   run_regression.sh crackdiff       # BASE-vs-CANDIDATE differential
#   run_regression.sh asan-smoke      # ASan memory-safety smoke test (gen->sort->lookup)
#   run_regression.sh markov          # Markov end-to-end (gen/sort/verify/mint/lookup)
#   run_regression.sh report          # write regression_summary.md
#   run_regression.sh all             # prepare + roundtrip + roundtrip-multi + crackdiff + asan-smoke + markov + report
set -euo pipefail

# ---- Tunables (override via env) ----
: "${REG_ROOT:=/tmp/crackalack-regression}"
: "${THIS_REPO:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
: "${BACKEND:=$(uname -s | grep -qi darwin && echo metal || echo cuda)}"
: "${MAKE_TARGET:=$([[ "$BACKEND" == metal ]] && echo macos || echo linux)}"
# Number of distinct in-table hashes the multi-hash round-trip looks up
# together (>=2 to exercise the batched precompute + multi-candidate
# false-alarm path).  Per-hash precompute cost scales with this, so keep it
# modest; must be <= the per-table chain count of the smallest config.
: "${REG_MULTI_COUNT:=4}"
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
# Each optimized precompute kernel only engages for a specific HARDCODED chain
# length, and the lookup has no generic fallback kernel for these algos -- so a
# round-trip MUST use the published chain length or the lookup can't reconstruct
# the chain (the hash is in the table but lookup reports 0 confirmations, or the
# generic kernel file simply doesn't exist):
#   precompute_ntlm8      -> 422000   (is_ntlm8)
#   precompute_ntlm9      -> 803000   (is_ntlm9)
#   precompute_netntlmv1_7 -> 881689  (is_netntlmv1_7, misc.c)
# Chain length dominates per-hash precompute cost; keep chain_count modest so a
# single thread can still surface the chain and gen stays fast.
ROUNDTRIP_CONFIGS=(
  "ntlm8|ntlm ascii-32-95 8 8 0 422000 256 0|--algo ntlm --charset ascii-32-95 --plaintext-len 8|422000"
  "ntlm9|ntlm ascii-32-95 9 9 0 803000 256 0|--algo ntlm --charset ascii-32-95 --plaintext-len 9|803000"
  "netntlmv1_7|netntlmv1 byte 7 7 0 881689 1024 0||881689"
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
    local ref="$1" dir="$2" sha
    sha="$(git -C "$THIS_REPO" rev-parse --verify "${ref}^{commit}" 2>/dev/null)" || {
        log "build_repo: cannot resolve ref '$ref' in $THIS_REPO (try: git fetch origin)"; return 1; }
    if [[ ! -d "$dir/.git" ]]; then git clone "$THIS_REPO" "$dir"; fi
    git -C "$dir" fetch "$THIS_REPO" 2>/dev/null || true
    git -C "$dir" checkout -f "$sha"
    git -C "$dir" reset --hard "$sha"
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
    # shared.h (and other root headers the generic kernels #include via rt.cu)
    # lives in the repo root, not CUDA/.  The binaries now resolve it relative to
    # their own location too, but symlink it so the round-trip is robust even if
    # the exe-dir lookup is unavailable.
    [[ -f "$bin/shared.h" ]] && ln -sfn "$bin/shared.h" "$work/shared.h"

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

# Multi-hash round-trip: look up REG_MULTI_COUNT (>=2) provably-in-table hashes
# together so the lookup takes the batched precompute + multi-candidate
# false-alarm path (used whenever >=2 hashes are loaded for ntlm8 /
# netntlmv1_7 / markov_ntlm8).  A regression here (e.g. the batched-precompute
# off-by-one that dropped every multi-hash crack) is invisible to the
# single-hash round-trip above.  Each hash is taken from a distinct chain so
# all REG_MULTI_COUNT are provably in the table.  Prints "<cracked>/<total>".
run_one_roundtrip_multi() {
    local name="$1" gen_args="$2" gkh_flags="$3" chain_len="$4"
    local count="$REG_MULTI_COUNT"
    (( count >= 2 )) || count=2
    local work="$REG_ROOT/rtm/$name"
    rm -rf "$work"; mkdir -p "$work"
    local bin="$THIS_REPO"
    local kdir
    for kdir in Metal CL CUDA; do
        [[ -d "$bin/$kdir" ]] && ln -sfn "$bin/$kdir" "$work/$kdir"
    done
    [[ -f "$bin/shared.h" ]] && ln -sfn "$bin/shared.h" "$work/shared.h"

    ( cd "$work" && with_gpu_lock "$bin/crackalack_gen" $gen_args >/dev/null 2>&1 )
    local table; table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"
    [[ -n "$table" ]] || { log "$name(multi): gen produced no .rt"; echo "0/$count"; return; }
    ( cd "$work" && with_gpu_lock "$bin/crackalack_sort" "$table" >/dev/null 2>&1 )
    table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"

    # One provably-in-table hash per distinct chain (0 .. count-1), each placed
    # at a DIFFERENT chain position spread across the chain (not all mid-chain):
    # hash c sits at column (c+1)*chain_len/(count+1).  Varied positions are
    # essential -- a position-dependent precompute bug (e.g. reversed column
    # order) can be invisible at the symmetric mid-chain column yet drop every
    # off-center hash, so a mid-chain-only test gives false confidence.
    local hashfile="$work/hashes.txt"; : > "$hashfile"
    local c gkh h si tp
    for (( c = 0; c < count; c++ )); do
        tp=$(( (c + 1) * chain_len / (count + 1) ))
        (( tp >= 1 )) || tp=1
        si="$("$bin/get_chain" "$table" "$c" | awk '/Start index:/ {print $3}')"
        [[ -n "$si" ]] || { log "$name(multi): get_chain $c failed"; echo "0/$count"; return; }
        gkh="$("$bin/gen_known_hash" "$chain_len" "$REDUCTION_OFFSET" "$si" "$tp" $gkh_flags)"
        h="$(awk -F= '/^hash=/{print $2}' <<<"$gkh")"
        [[ -n "$h" ]] || { log "$name(multi): gen_known_hash $c failed"; echo "0/$count"; return; }
        echo "$h" >> "$hashfile"
    done

    # File-mode lookup (>=2 hashes -> batched path).  Default pot names land in CWD.
    ( cd "$work" && rm -f ./*.index rcracki.precalc.* rainbowcrackalack_*.pot \
        && with_gpu_lock "$bin/crackalack_lookup" "$work" "$hashfile" >/dev/null 2>&1 ) || true

    # Count how many of the input hashes reached the (hashcat) pot.
    local cracked
    cracked="$("$PY" - "$work/rainbowcrackalack_hashcat.pot" "$hashfile" <<'PY'
import sys
from crack_diff import parse_pot
d = parse_pot(sys.argv[1])
wanted = [l.strip().lower() for l in open(sys.argv[2]) if l.strip()]
print(sum(1 for h in wanted if h in d))
PY
)"
    echo "${cracked:-0}/$count"
}

phase_roundtrip_multi() {
    log "ROUNDTRIP-MULTI (batched multi-hash path, $REG_MULTI_COUNT hashes): backend=$BACKEND"
    mkdir -p "$RESULTS"
    local json="$RESULTS/roundtrip_multi_$BACKEND.json"
    export PYTHONPATH="$SCRIPT_DIR"
    "$PY" -c "from crack_diff import parse_pot" 2>/dev/null \
        || { log "ERROR: crack_diff not importable (PYTHONPATH=$PYTHONPATH)"; exit 1; }
    echo "{" > "$json"
    local first=1 fail=0
    for cfg in "${ROUNDTRIP_CONFIGS[@]}"; do
        IFS='|' read -r name gen_args gkh_flags chain_len <<<"$cfg"
        log "round-trip-multi: $name"
        local res cracked total
        res="$(run_one_roundtrip_multi "$name" "$gen_args" "$gkh_flags" "$chain_len")"
        cracked="${res%%/*}"; total="${res##*/}"
        [[ "$cracked" == "$total" ]] || { fail=1; log "round-trip-multi: $name cracked $res (expected all)"; }
        [[ $first -eq 1 ]] || echo "," >> "$json"; first=0
        printf '  "%s": {"cracked": %s, "expected": %s}' "$name" "${cracked:-0}" "${total:-0}" >> "$json"
    done
    echo "" >> "$json"; echo "}" >> "$json"
    log "wrote $json"
    cat "$json" >&2
    [[ $fail -eq 0 ]] || log "ROUNDTRIP-MULTI: at least one config did not crack all $REG_MULTI_COUNT hashes"
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

# ASan memory-safety smoke test.  Rebuilds $THIS_REPO under AddressSanitizer and
# runs the gen->sort->lookup pipeline with a NON-EMPTY pot file present (the
# condition that triggers the fix/lookup-heap-corruption pot-file over-read),
# asserting zero ASan findings + a successful crack.  Guards the whole host
# pipeline against heap overflow / use-after-free / double-free regressions --
# the round-trip/crackdiff phases verify correctness but run uninstrumented and
# would sail past a latent heap bug.
#
# The smoke build is ASan-instrumented, so we ALWAYS restore a normal build
# afterward; otherwise the repo's binaries would be left needing the ASan
# runtime (and far slower) for any later real lookups.  CUDA_PATH is inherited
# from the environment (same as phase_prepare).
phase_asan_smoke() {
    log "ASAN-SMOKE: backend=$BACKEND target=$MAKE_TARGET"
    mkdir -p "$RESULTS"
    local json="$RESULTS/asan_smoke_$BACKEND.json"
    local rc=0
    with_gpu_lock bash "$SCRIPT_DIR/asan_smoke_test.sh" "$THIS_REPO" >&2 || rc=$?
    log "ASAN-SMOKE: restoring normal (non-ASan) build"
    ( cd "$THIS_REPO" && make clean >/dev/null 2>&1 && make "$MAKE_TARGET" >/dev/null 2>&1 && make gen_known_hash >/dev/null 2>&1 ) \
        || log "WARN: normal rebuild after asan-smoke failed -- repo binaries may be stale, run 'prepare'"
    if [[ $rc -eq 0 ]]; then
        echo '{"passed": true}' > "$json";  log "ASAN-SMOKE: PASS"
    else
        echo '{"passed": false}' > "$json"; log "ASAN-SMOKE: FAIL (rc=$rc) -- ASan found a memory error or the crack failed"
    fi
    return $rc
}

# Markov end-to-end pipeline (gen/sort/verify/mint/lookup + negative control)
# for full and truncated keyspaces.  Delegates to the self-contained
# test_markov_lookup.sh (it trains its own throwaway model), wrapped in the GPU
# lock so it serialises with the other GPU phases.  Skips cleanly if the Markov
# binaries (crackalack_plan) aren't built.
phase_markov() {
    log "MARKOV: end-to-end pipeline backend=$BACKEND"
    mkdir -p "$RESULTS"
    local json="$RESULTS/markov_$BACKEND.json"
    local rc=0
    with_gpu_lock bash "$SCRIPT_DIR/test_markov_lookup.sh" "$THIS_REPO" >&2 || rc=$?
    if [[ $rc -eq 0 ]]; then
        echo '{"passed": true}' > "$json";  log "MARKOV: PASS"
    else
        echo '{"passed": false}' > "$json"; log "MARKOV: FAIL (rc=$rc)"
    fi
    return $rc
}

phase_report() {
    log "REPORT"
    export PYTHONPATH="$SCRIPT_DIR"
    local args=()
    local f
    for f in "$RESULTS"/roundtrip_*.json; do
        [[ -e "$f" ]] || continue
        local backend; backend="$(basename "$f" .json)"; backend="${backend#roundtrip_}"
        args+=(--roundtrip "$backend=$f")
    done
    [[ -f "$RESULTS/crackdiff.json" ]] && args+=(--crackdiff "$RESULTS/crackdiff.json")
    local rc=0
    "$PY" "$SCRIPT_DIR/render_report.py" "${args[@]}" --out "$RESULTS/regression_summary.md" || rc=$?
    log "summary at $RESULTS/regression_summary.md (exit $rc)"
    return $rc
}

main() {
    local phase="${1:-all}"
    case "$phase" in
        prepare)        phase_prepare ;;
        roundtrip)      phase_roundtrip ;;
        roundtrip-multi) phase_roundtrip_multi ;;
        crackdiff)      phase_crackdiff ;;
        asan-smoke)     phase_asan_smoke ;;
        markov)         phase_markov ;;
        report)         phase_report ;;
        all)            phase_prepare; phase_roundtrip; phase_roundtrip_multi; phase_crackdiff; phase_asan_smoke || true; phase_markov || true; phase_report ;;
        *) echo "Unknown phase: $phase" >&2; exit 2 ;;
    esac
}
main "$@"
