#!/usr/bin/env bash
#
# AddressSanitizer smoke test for the host-side gen -> sort -> lookup pipeline.
#
# Builds the relevant binaries under ASan and runs a tiny end-to-end crack
# (gen a 1M-chain NTLM8 table -> sort -> mint an in-table hash -> lookup) WITH
# A NON-EMPTY JTR POT FILE PRESENT, then asserts the run completes with ZERO
# AddressSanitizer findings AND that the known hash cracks.
#
# Why the pot file matters: the regression this guards against
# (fix/lookup-heap-corruption) was a heap-buffer-overflow over-read of the pot
# file -- calloc(file_size) with no room for a NUL terminator, then consumed by
# strstr() as a C string.  It only triggers when a NON-EMPTY pot file exists, so
# this test deliberately creates one.  More broadly it catches any heap
# overflow / use-after-free / double-free in the host lookup pipeline.
#
# Requires a GPU (Metal on macOS, CUDA on Linux).
#
# Usage:   scripts/bench/asan_smoke_test.sh [repo_dir]
# Env:     CUDA_PATH (linux build), SKIP_BUILD=1 (reuse existing ASan build),
#          KEEP=1 (keep scratch dir)
#
set -uo pipefail

REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$REPO"

case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  Linux)  PLATFORM=linux ;;
  *) echo "SKIP: unsupported platform $(uname -s)"; exit 0 ;;
esac

GEN="$REPO/crackalack_gen"; LOOKUP="$REPO/crackalack_lookup"
GKH="$REPO/gen_known_hash"; SORT="$REPO/crackalack_sort"

CHAIN_LEN=100000
NUM_CHAINS=1000000
START_INDEX=0
TARGET_POS=50000

fail() { echo "FAIL: $*"; exit 1; }

# ---- build under ASan -------------------------------------------------------
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  echo "[smoke] building ($PLATFORM) under AddressSanitizer..."
  MK=( "$PLATFORM"
       "CFLAGS_common=-Wall -O1 -g -fno-omit-frame-pointer -fsanitize=address"
       "LDFLAGS_common=-fsanitize=address" )
  [[ -n "${CUDA_PATH:-}" ]] && MK+=( "CUDA_PATH=$CUDA_PATH" )
  make clean >/dev/null 2>&1
  make "${MK[@]}" >/tmp/asan_smoke_build.log 2>&1 \
    || { tail -20 /tmp/asan_smoke_build.log; fail "ASan build failed (see /tmp/asan_smoke_build.log)"; }
  # gen_known_hash is an on-demand target (needs OpenSSL); build it under ASan too.
  make gen_known_hash "${MK[@]}" >>/tmp/asan_smoke_build.log 2>&1 \
    || { tail -20 /tmp/asan_smoke_build.log; fail "gen_known_hash ASan build failed"; }
fi
for b in "$GEN" "$LOOKUP" "$GKH" "$SORT"; do [[ -x "$b" ]] || fail "$b not built"; done

# CUDA reserves address ranges that collide with ASan's shadow gap.
export ASAN_OPTIONS="protect_shadow_gap=0:detect_leaks=0:abort_on_error=1:halt_on_error=1:allocator_may_return_null=1"

work="$(mktemp -d)"
[[ "${KEEP:-0}" == "1" ]] || trap 'rm -rf "$work"' EXIT

# ---- gen + sort + mint a known in-table hash --------------------------------
genname="ntlm_ascii-32-95#8-8_0_${CHAIN_LEN}x${NUM_CHAINS}_0.rt"
echo "[smoke] gen tiny NTLM8 table..."
# gen/lookup load kernels CWD-relative on Metal, so run from the repo, then move.
( cd "$REPO" && "$GEN" ntlm ascii-32-95 8 8 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 >/dev/null 2>&1 )
[[ -f "$REPO/$genname" ]] || fail "no table generated ($genname)"
mv -f "$REPO/$genname" "$work/"; rm -f "$REPO/$genname.state"
table="$work/$genname"

echo "[smoke] sort table..."
"$SORT" "$table" >/dev/null 2>&1 || fail "crackalack_sort failed"

echo "[smoke] mint in-table known hash..."
out="$("$GKH" "$CHAIN_LEN" 0 "$START_INDEX" "$TARGET_POS" --algo ntlm --charset ascii-32-95 --plaintext-len 8)"
hash="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$out")"
[[ -n "$hash" ]] || fail "gen_known_hash produced no hash"
echo "[smoke] known hash $hash should crack to '$pt'"

# ---- the key bit: a NON-EMPTY pot file that does NOT contain the target -----
# (non-empty => exercises the calloc/strstr pot-file parse path; not-the-target
#  => lookup proceeds to crack instead of short-circuiting as already-cracked.)
potfile="$work/rainbowcrackalack_jtr.pot"
printf '$NT$deadbeefdeadbeefdeadbeefdeadbeef:dummyplaintext\n' > "$potfile"
hashes="$work/hashes.txt"
printf '%s\n' "$hash" > "$hashes"

echo "[smoke] running lookup under ASan (pot file present)..."
log="$work/lookup.log"
# args: <table_dir> <hashes_file> <pot_file_override> [flags]  (pot path is the
# undocumented 3rd positional).  Use the hash-FILE path so the strstr at
# crackalack_lookup.c:3988 (the one ASan flagged) is exercised.
( cd "$REPO" && "$LOOKUP" "$work" "$hashes" "$potfile" --bloom-fpr 0 ) >"$log" 2>&1
rc=$?

# ---- assertions -------------------------------------------------------------
if grep -qiE "AddressSanitizer|heap-buffer-overflow|double free|corruption|use-after-free" "$log"; then
  echo "----- ASan report -----"
  sed -n '/ERROR: AddressSanitizer/,/SUMMARY/p' "$log" | head -30
  fail "AddressSanitizer reported a memory error (exit=$rc)"
fi
clean="$(sed -E $'s/\x1b\\[[0-9;]*m//g' "$log")"
if printf '%s\n' "$clean" | grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED"; then
  echo "PASS: ASan-clean end-to-end lookup, known hash cracked (pot-file path exercised)"
  exit 0
fi
echo "----- lookup tail -----"; tail -12 "$log"
fail "known in-table hash did NOT crack (exit=$rc)"
