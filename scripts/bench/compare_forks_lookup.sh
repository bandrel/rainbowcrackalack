#!/usr/bin/env bash
# Apples-to-apples crackalack_lookup comparison: the upstream blurbdust fork
# (OpenCL) vs this/your fork (CUDA on Linux, Metal on macOS), on the SAME GPU,
# the SAME rainbow tables, and the SAME known hash.
#
# It answers one question for *your* hardware: how much faster is your fork than
# upstream blurbdust at looking up a hash against a directory of rainbow tables?
#
# This is intentionally self-contained and portable -- unlike run_benchmark.sh
# (which compares two refs of THIS repo via a Python venv + configs on gpuhost3),
# this script only needs bash, git, make, and the relevant GPU toolchain.
#
# ---------------------------------------------------------------------------
# Quick start
# ---------------------------------------------------------------------------
#   # Look up one known NTLM/NetNTLMv1 hash against a table dir, both forks:
#   TABLES=/path/to/tables HASH=a52b9cdedae86934 EXPECTED=17ad06bdd830b7 \
#     scripts/bench/compare_forks_lookup.sh all
#
#   # Bound the scan to a subdirectory (faster; same subset for both forks):
#   TABLES=/path/to/tables SCOPE=0-1000 HASH=<hex> \
#     scripts/bench/compare_forks_lookup.sh all
#
#   # Phases can be run separately: prepare (clone+build) | run | report
#   scripts/bench/compare_forks_lookup.sh prepare
#
# ---------------------------------------------------------------------------
# Why the potfile matters (the gotcha this script handles for you)
# ---------------------------------------------------------------------------
# crackalack_lookup writes cracked results to rainbowcrackalack_jtr.pot /
# rainbowcrackalack_hashcat.pot in its working directory, and this fork will
# SHORT-CIRCUIT ("Specified hash has already been cracked!") if the hash is
# already in the potfile -- giving a bogus 0-second "win". This script moves
# both potfiles aside before each run and restores them after, so every run is
# a genuine end-to-end crack.
#
# ---------------------------------------------------------------------------
# Methodology notes (for a fair number)
# ---------------------------------------------------------------------------
#   * The two forks run SEQUENTIALLY (never concurrently) under a flock mutex,
#     so they don't contend for the GPU and timings stay honest.
#   * Each fork runs from its own checkout so it finds its own kernels
#     (blur loads CL/, this fork loads CUDA/ or Metal/).
#   * Page cache: if your table set is much larger than RAM, cache carryover
#     between the two runs is negligible. For small table sets, set
#     DROP_CACHES=1 (Linux, needs passwordless sudo) for a cold second run.
#   * A lookup against a directory must scan every table to GUARANTEE a crack,
#     but stops early once the (single) hash is cracked. Time-to-crack
#     therefore depends on where in the traversal the answer lives -- identical
#     for both forks, so the comparison is fair.
set -euo pipefail

# ---- Tunables (override via env) ----
: "${CMP_ROOT:=/tmp/crackalack-fork-compare}"      # workspace for checkouts + results
: "${TABLES:?set TABLES=/path/to/rainbow/table/dir}"
: "${HASH:?set HASH=<hex hash to look up>}"
: "${EXPECTED:=}"                                  # optional: expected plaintext (hex or text) for pass/fail
: "${SCOPE:=}"                                      # optional: subdir under TABLES to limit the scan
: "${GPU_INDEX:=0}"                                 # which GPU (CUDA_VISIBLE_DEVICES / OpenCL device order)
: "${GWS:=}"                                        # optional: -gws value passed to both forks
: "${TIMEOUT_MIN:=0}"                               # per-run wall cap in minutes (0 = unbounded)
: "${DROP_CACHES:=0}"                               # 1 = drop page cache before each run (Linux, sudo -n)
: "${COLD:=1}"                                      # 1 = clear precompute cache (rcracki.precalc.*) before each run

# Fork-under-test ("mine") defaults to THIS checkout; "blur" is upstream blurbdust.
: "${MINE_REPO:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
: "${MINE_REF:=$(git -C "$MINE_REPO" rev-parse --abbrev-ref HEAD 2>/dev/null || echo master)}"
: "${BLUR_REPO:=https://github.com/blurbdust/rainbowcrackalack.git}"
: "${BLUR_REF:=master}"

# Build target per platform (each repo's own Makefile picks its backend):
#   linux -> CUDA for this fork, OpenCL for upstream blurbdust
#   macos -> Metal (this fork; upstream blurbdust has no macos target)
: "${MAKE_TARGET:=$(uname -s | grep -qi darwin && echo macos || echo linux)}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/gpu_lock.sh
source "$SCRIPT_DIR/lib/gpu_lock.sh"

BLUR_DIR="$CMP_ROOT/blur"
MINE_DIR="$CMP_ROOT/mine"
RESULTS="$CMP_ROOT/results"
SCAN_DIR="$TABLES${SCOPE:+/$SCOPE}"

log() { echo "[compare $(date -u +%H:%M:%S)] $*" >&2; }

# --- clone (or reuse) a repo at a ref, then build it with its own Makefile ---
prepare_role() {  # role_name  dir  repo  ref
    local name="$1" dir="$2" repo="$3" ref="$4"
    if [[ -d "$repo/.git" && "$repo" != "$dir" ]]; then
        # Local checkout: build it in place (don't clone over the user's repo).
        dir="$repo"
    elif [[ ! -d "$dir/.git" ]]; then
        log "$name: cloning $repo -> $dir"
        git clone "$repo" "$dir"
    fi
    log "$name: checkout $ref + build ($MAKE_TARGET)"
    git -C "$dir" fetch --all --quiet 2>/dev/null || true
    git -C "$dir" checkout --quiet "$ref" 2>/dev/null || log "$name: WARN could not checkout '$ref', using current HEAD"
    ( cd "$dir" && make clean >/dev/null 2>&1 || true && make "$MAKE_TARGET" ) >"$RESULTS/build_$name.log" 2>&1 || {
        log "$name: BUILD FAILED -- see $RESULTS/build_$name.log"
        if [[ "$name" == blur && "$MAKE_TARGET" == macos ]]; then
            log "       (upstream blurbdust has no macOS/Metal backend; this comparison needs a Linux+OpenCL host)"
        fi
        return 1
    }
    [[ -x "$dir/crackalack_lookup" ]] || { log "$name: no crackalack_lookup after build"; return 1; }
    # Record the resolved build dir for the run phase.
    echo "$dir" > "$RESULTS/dir_$name"
    log "$name: built $(git -C "$dir" rev-parse --short HEAD 2>/dev/null) at $dir"
}

prepare() {
    mkdir -p "$RESULTS"
    [[ -d "$SCAN_DIR" ]] || { log "TABLES scan dir not found: $SCAN_DIR"; exit 1; }
    prepare_role blur "$BLUR_DIR" "$BLUR_REPO" "$BLUR_REF"
    prepare_role mine "$MINE_DIR" "$MINE_REPO" "$MINE_REF"
    log "prepare complete"
}

# --- run one fork's lookup, timed, with potfiles moved aside ---
run_role() {  # role_name
    local name="$1"
    local dir; dir="$(cat "$RESULTS/dir_$name" 2>/dev/null || true)"
    [[ -x "$dir/crackalack_lookup" ]] || { log "$name: not built (run 'prepare' first)"; return 1; }

    # Move both potfiles aside so the fork can't short-circuit; restore on exit.
    local pots=(rainbowcrackalack_jtr.pot rainbowcrackalack_hashcat.pot)
    local p
    for p in "${pots[@]}"; do [[ -f "$dir/$p" ]] && mv -f "$dir/$p" "$dir/$p.cmpbak"; done
    # shellcheck disable=SC2317
    restore_pots() { for p in "${pots[@]}"; do [[ -f "$dir/$p.cmpbak" ]] && mv -f "$dir/$p.cmpbak" "$dir/$p"; done; return 0; }
    trap restore_pots RETURN

    # Cold, repeatable runs: clear the precompute disk cache (rcracki.precalc.*)
    # in the working dir so each run computes from scratch. Otherwise a warm
    # cache from a prior run makes the second fork look artificially fast.
    if [[ "${COLD:=1}" == 1 ]]; then
        rm -f "$dir"/rcracki.precalc.* 2>/dev/null || true
    fi

    if [[ "$DROP_CACHES" == 1 ]]; then
        sync; echo 3 | sudo -n tee /proc/sys/vm/drop_caches >/dev/null 2>&1 || log "$name: WARN drop_caches failed (need passwordless sudo)"
    fi

    local cmd=(./crackalack_lookup "$SCAN_DIR" "$HASH")
    [[ -n "$GWS" ]] && cmd+=(-gws "$GWS")
    [[ "$TIMEOUT_MIN" != 0 ]] && cmd=(timeout "${TIMEOUT_MIN}m" "${cmd[@]}")

    local logf="$RESULTS/run_$name.log"
    log "$name: looking up $HASH against $SCAN_DIR"
    local start=$SECONDS rc=0
    ( cd "$dir" && CUDA_VISIBLE_DEVICES="$GPU_INDEX" GPU_DEVICE="$GPU_INDEX" \
        with_gpu_lock "${cmd[@]}" ) >"$logf" 2>&1 || rc=$?
    local elapsed=$(( SECONDS - start ))

    # Did it crack? Detect from the LOG, not the potfile -- the two forks write
    # INCOMPATIBLE potfiles: upstream blurbdust writes "<hash[:8]>:<raw binary
    # plaintext>", this fork writes "<full hash hex>:<plaintext hex>". The console
    # output is parseable for both:
    #   blurbdust:  "HASH CRACKED => <hash>:<challenge>:<plaintext>"
    #   this fork:  a green line "<hash>  <plaintext>"
    # Both also print "Cracked N of M hashes."
    local cracked=NO plaintext="" clean
    clean="$(sed -E $'s/\x1b\\[[0-9;]*m//g' "$logf" 2>/dev/null)"   # strip ANSI colour codes
    if printf '%s\n' "$clean" | grep -qiE "HASH CRACKED|Cracked [1-9][0-9]* of|[1-9][0-9]* were cracked"; then
        cracked=YES
    fi
    plaintext="$(printf '%s\n' "$clean" | grep -aiE "HASH CRACKED.*$HASH" | sed -E 's/.*:([0-9a-fA-F]+)[[:space:]]*$/\1/' | tail -1)"
    if [[ -z "$plaintext" ]]; then
        plaintext="$(printf '%s\n' "$clean" | grep -aiE "^[[:space:]]*$HASH[[:space:]]+[0-9a-fA-F]+[[:space:]]*$" | awk '{print $NF}' | tail -1)"
    fi
    [[ -n "$plaintext" ]] && cracked=YES
    cp -f "$dir/rainbowcrackalack_jtr.pot" "$RESULTS/pot_$name" 2>/dev/null || true
    restore_pots; trap - RETURN

    # Tables processed + throughput (the "[N of M] Processing table:" progress lines).
    local processed total
    processed="$(grep -c "Processing table:" "$logf" 2>/dev/null || echo 0)"
    total="$(grep -oE "\[[0-9]+ of [0-9]+\]" "$logf" 2>/dev/null | tail -1 | grep -oE "of [0-9]+" | grep -oE "[0-9]+" || echo "?")"
    local tps="n/a"
    [[ "$elapsed" -gt 0 && "$processed" =~ ^[0-9]+$ ]] && tps="$(awk -v p="$processed" -v e="$elapsed" 'BEGIN{printf "%.2f", p/e}')"

    # One line of machine-readable result for the report phase. Empty fields are
    # written as "-" because read(1) with IFS=tab collapses adjacent tabs (tab is
    # an IFS-whitespace char), which would otherwise shift the columns.
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$name" "$elapsed" "$cracked" "${plaintext:--}" "$processed" "$total" "$tps" \
        > "$RESULTS/result_$name.tsv"
    log "$name: ${elapsed}s, cracked=$cracked${plaintext:+ ($plaintext)}, tables=$processed/${total}, ${tps} tbl/s (exit $rc)"
}

run() {
    mkdir -p "$RESULTS"
    [[ -d "$SCAN_DIR" ]] || { log "scan dir not found: $SCAN_DIR"; exit 1; }
    {
        echo "=== crackalack fork comparison ==="
        echo "host:    $(uname -n)  ($(uname -srm))"
        command -v nvidia-smi >/dev/null 2>&1 && echo "gpu:     $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | sed -n "$((GPU_INDEX+1))p")"
        echo "tables:  $SCAN_DIR"
        echo "hash:    $HASH${EXPECTED:+   expected: $EXPECTED}"
        echo "blur:    $(git -C "$(cat "$RESULTS/dir_blur" 2>/dev/null||echo /nonexistent)" rev-parse --short HEAD 2>/dev/null||echo '?') ($BLUR_REPO @ $BLUR_REF)"
        echo "mine:    $(git -C "$(cat "$RESULTS/dir_mine" 2>/dev/null||echo /nonexistent)" rev-parse --short HEAD 2>/dev/null||echo '?') ($MINE_REPO @ $MINE_REF)"
        echo "started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } | tee "$RESULTS/summary.txt"
    # blur first, then mine (sequential; never concurrent). Don't let one
    # fork's parse hiccup abort the other.
    run_role blur || log "blur run errored (continuing)"
    run_role mine || log "mine run errored (continuing)"
    report
}

report() {
    local f="$RESULTS/summary.txt"
    {
        echo
        echo "=== RESULTS ==="
        printf '%-6s %10s %8s %20s %14s %10s\n' fork "elapsed_s" "cracked" "plaintext" "tables" "tbl/s"
        local r be me
        for r in blur mine; do
            [[ -f "$RESULTS/result_$r.tsv" ]] || continue
            IFS=$'\t' read -r name elapsed cracked plaintext processed total tps < "$RESULTS/result_$r.tsv"
            printf '%-6s %10s %8s %20s %14s %10s\n' "$name" "$elapsed" "$cracked" "${plaintext:--}" "$processed/$total" "$tps"
        done
        be="$(cut -f2 "$RESULTS/result_blur.tsv" 2>/dev/null || echo 0)"
        me="$(cut -f2 "$RESULTS/result_mine.tsv" 2>/dev/null || echo 0)"
        if [[ "${be:-0}" -gt 0 && "${me:-0}" -gt 0 ]]; then
            echo
            awk -v b="$be" -v m="$me" 'BEGIN{printf "speedup (blur/mine wall clock): %.2fx\n", b/m}'
        fi
        if [[ -n "$EXPECTED" ]]; then
            echo
            local mp; mp="$(cut -f4 "$RESULTS/result_mine.tsv" 2>/dev/null || true)"
            if [[ "$mp" == "$EXPECTED" ]]; then echo "correctness: PASS (mine cracked to expected '$EXPECTED')"
            else echo "correctness: mine got '${mp:-<none>}', expected '$EXPECTED'"; fi
        fi
        echo
        echo "logs + potfiles: $RESULTS/"
    } | tee -a "$f"
}

case "${1:-all}" in
    prepare) prepare ;;
    run)     run ;;
    report)  report ;;
    all)     prepare && run ;;
    *) echo "usage: $0 {prepare|run|report|all}" >&2; exit 2 ;;
esac
