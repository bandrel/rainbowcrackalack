#!/usr/bin/env bash
#
# ThreadSanitizer smoke test for the lookup host-threading pipeline.
#
# Builds the relevant binaries under TSan and runs a tiny end-to-end crack
# (gen a 1M-chain NTLM8 table -> sort -> mint an in-table hash -> lookup)
# WITH A NON-EMPTY JTR POT FILE PRESENT, then asserts the run completes with
# ZERO ThreadSanitizer race reports referencing our own .c source files.
#
# Why we target lookup specifically:  the loader pool + false-alarm worker
# threads in crackalack_lookup.c are where a historical racy heap-corruption
# bug lived.  TSan exercises all three key thread groups:
#   - streaming preloader / table-load worker pool  (pthread_create x N)
#   - GPU precompute threads                        (pthread_create x N)
#   - false-alarm (FA) worker threads               (pthread_create x 1+)
#   - binary-search threads                         (pthread_create x 16)
#
# A KNOWN TIMING-BOUNDED RACE is documented in crackalack_lookup.c around
# line 3609 (the FA worker CLCREATEARG_ARRAY uploads vs the next iteration's
# fa_batch_append).  If TSan flags exactly that location we capture it;
# everything else in our sources is a real finding.
#
# Requires a GPU (Metal on macOS).
#
# Usage:   scripts/bench/tsan_smoke_test.sh [repo_dir]
# Env:     CUDA_PATH  (linux build, if non-default)
#          SKIP_BUILD=1   reuse existing TSan-instrumented build
#          KEEP=1         keep scratch dir after exit
#
set -uo pipefail

REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$REPO"

SUPP="$REPO/scripts/bench/tsan.supp"

case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  Linux)  PLATFORM=linux ;;
  *) echo "SKIP: unsupported platform $(uname -s)"; exit 0 ;;
esac

# On Linux, high-entropy ASLR collides with TSan's fixed shadow mapping
# ("FATAL: ThreadSanitizer: unexpected memory mapping").  Run the TSan-
# instrumented binaries under `setarch -R` to disable ASLR.  Empty on macOS.
TSAN_RUN=""
if [[ "$PLATFORM" == "linux" ]]; then
  if command -v setarch >/dev/null 2>&1; then
    TSAN_RUN="setarch $(uname -m) -R"
  else
    echo "[tsan-smoke] WARNING: setarch not found; TSan may abort on ASLR mapping" >&2
  fi
fi

GEN="$REPO/crackalack_gen"
LOOKUP="$REPO/crackalack_lookup"
GKH="$REPO/gen_known_hash"
SORT="$REPO/crackalack_sort"

CHAIN_LEN=100000
NUM_CHAINS=1000000
TARGET_POS=25000

fail() { echo "FAIL: $*"; exit 1; }

# ---- build under TSan -------------------------------------------------------
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  echo "[tsan-smoke] building ($PLATFORM) under ThreadSanitizer..."

  MK_ARGS=( "$PLATFORM" "TSAN=1" )
  [[ -n "${CUDA_PATH:-}" ]] && MK_ARGS+=( "CUDA_PATH=$CUDA_PATH" )

  make clean >/dev/null 2>&1
  make "${MK_ARGS[@]}" >/tmp/tsan_smoke_build.log 2>&1 \
    || { tail -30 /tmp/tsan_smoke_build.log
         fail "TSan build failed (see /tmp/tsan_smoke_build.log)"; }

  # gen_known_hash is an on-demand target (needs OpenSSL).
  make gen_known_hash "${MK_ARGS[@]}" >>/tmp/tsan_smoke_build.log 2>&1 \
    || { tail -20 /tmp/tsan_smoke_build.log
         fail "gen_known_hash TSan build failed (see /tmp/tsan_smoke_build.log)"; }

  echo "[tsan-smoke] build OK"
fi

for b in "$GEN" "$LOOKUP" "$GKH" "$SORT"; do
  [[ -x "$b" ]] || fail "$b not found/executable; run without SKIP_BUILD=1"
done

# ---- restore normal build at exit (even on failure) -------------------------
_cleanup() {
  if [[ "${KEEP:-0}" != "1" ]] && [[ -n "${work:-}" ]]; then
    rm -rf "$work"
  fi
  if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    echo "[tsan-smoke] restoring normal build..."
    make clean >/dev/null 2>&1
    make "$PLATFORM" >/dev/null 2>&1 \
      && echo "[tsan-smoke] normal build restored OK" \
      || echo "[tsan-smoke] WARNING: normal build restore FAILED"
  fi
}
trap '_cleanup' EXIT

# ---- scratch dir ------------------------------------------------------------
work="$(mktemp -d)"

export TSAN_OPTIONS="suppressions=${SUPP} halt_on_error=0 history_size=4"

# ---- gen + sort + mint a known in-table hash --------------------------------
genname="ntlm_ascii-32-95#8-8_0_${CHAIN_LEN}x${NUM_CHAINS}_0.rt"
echo "[tsan-smoke] generating tiny NTLM8 table (${NUM_CHAINS} chains x ${CHAIN_LEN} steps)..."
# Metal loads kernels relative to the executable dir; gen from repo root then move.
( cd "$REPO" && $TSAN_RUN "$GEN" ntlm ascii-32-95 8 8 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 >/dev/null 2>&1 )
[[ -f "$REPO/$genname" ]] || fail "no table generated ($genname)"
mv -f "$REPO/$genname" "$work/"
rm -f "$REPO/$genname.state"
table="$work/$genname"

echo "[tsan-smoke] sorting table..."
$TSAN_RUN "$SORT" "$table" >/dev/null 2>&1 || fail "crackalack_sort failed"

echo "[tsan-smoke] minting in-table known hash..."
out="$($TSAN_RUN "$GKH" "$CHAIN_LEN" 0 0 "$TARGET_POS" --algo ntlm --charset ascii-32-95 --plaintext-len 8)"
hash="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$out")"
[[ -n "$hash" ]] || fail "gen_known_hash produced no hash"
echo "[tsan-smoke] will look up $hash (plaintext $pt)"

# Non-empty pot file that does NOT yet contain the target hash.
potfile="$work/rainbowcrackalack_jtr.pot"
printf '$NT$deadbeefdeadbeefdeadbeefdeadbeef:dummyplaintext\n' > "$potfile"
printf '%s\n' "$hash" > "$work/hashes.txt"

# ---- run lookup under TSan --------------------------------------------------
echo "[tsan-smoke] running crackalack_lookup under ThreadSanitizer..."
log="$work/tsan_lookup.log"
( cd "$REPO" && $TSAN_RUN "$LOOKUP" "$work" "$work/hashes.txt" "$potfile" --bloom-fpr 0 ) >"$log" 2>&1
rc=$?

echo "--- TSan/lookup output (tail) ---"
tail -30 "$log"
echo "---------------------------------"

# ---- assert: no TSan data-race reports in OUR sources -----------------------
# Strip ANSI colour codes first, then search for TSan WARNING blocks that cite
# our C source files (not Metal driver or system library frames).
clean_log="$(sed -E $'s/\x1b\\[[0-9;]*m//g' "$log")"

our_src_pattern='crackalack_lookup\.c|fa_batch\.c|gws\.c|cpu_rt_functions\.c|parallel_sort\.c|sort_utils\.c|precompute_collate\.c|bloom\.c|misc\.c|verify\.c'

tsan_our_races="$(printf '%s\n' "$clean_log" \
  | awk '/WARNING: ThreadSanitizer/{f=1} f{print} /^={10,}$/{f=0}' \
  | grep -E "$our_src_pattern" || true)"

# Also collect the full TSan summary line if any.
tsan_summary="$(printf '%s\n' "$clean_log" | grep -E "ThreadSanitizer: [0-9]+ warning" || true)"

if [[ -n "$tsan_our_races" ]]; then
  echo ""
  echo "================================================================"
  echo "FAIL: ThreadSanitizer reported a DATA RACE in OUR source files:"
  echo "----------------------------------------------------------------"
  printf '%s\n' "$clean_log" | grep -E "WARNING: ThreadSanitizer" -A 60 | head -120
  echo "================================================================"
  fail "TSan race in our code — see full report above"
fi

# Check the lookup actually cracked the hash.
if ! printf '%s\n' "$clean_log" | grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED"; then
  echo "--- full log ---"
  cat "$log"
  fail "known in-table hash did NOT crack (exit=$rc)"
fi

echo ""
echo "================================================================"
echo "PASS: ThreadSanitizer found ZERO races in our source files."
[[ -n "$tsan_summary" ]] && echo "TSan summary: $tsan_summary" \
  || echo "TSan summary: 0 warnings (all clear)"
echo "The full lookup pipeline (loader pool, precompute threads,"
echo "  binary-search threads, FA worker threads) ran race-clean."
echo "================================================================"
exit 0
