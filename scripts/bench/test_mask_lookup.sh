#!/usr/bin/env bash
# End-to-end regression test for the mask attack pipeline.
#
# Exercises the whole mask pipeline and asserts the correct behaviour:
#
#   * crackalack_gen --mask generates a mask table (charset field is the
#     filename-encoded mask, e.g. ntlm_%u%l%l%l%l%l%l%d#8-8_...);
#   * crackalack_verify --quick --mask "..." confirms the generated table;
#   * gen_known_hash --mask mints a provably-in-table (plaintext, hash) using
#     params CONSISTENT with the generated table (same chain_len, table_index 0);
#   * crackalack_lookup auto-detects the mask from the filename and cracks the
#     minted hash (the positive crack assertion);
#   * a hash outside the mask keyspace does NOT crack (negative control, guards
#     against a broken false-alarm check that would confirm bogus candidates).
#
# Usage: test_mask_lookup.sh [repo_dir]
set -euo pipefail
REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
GEN="$REPO/crackalack_gen"; LOOKUP="$REPO/crackalack_lookup"; GKH="$REPO/gen_known_hash"
SORT="$REPO/crackalack_sort"; VERIFY="$REPO/crackalack_verify"
GETCHAIN="$REPO/get_chain"

CHAIN_LEN=1000
NUM_CHAINS=512
TARGET_POS=300
MASK="?u?l?l?l?l?l?l?d"
# Filename uses '%' in place of '?' per mask_encode_for_filename().
MASK_ENCODED="%u%l%l%l%l%l%l%d"

for b in "$GEN" "$LOOKUP" "$GKH" "$SORT" "$VERIFY" "$GETCHAIN"; do
  [[ -x "$b" ]] || { echo "SKIP: $b not built (build with 'make <platform>' and 'make gen_known_hash')"; exit 0; }
done

work="$(mktemp -d)"
# Move potfiles/precompute cache aside so a stale run can't short-circuit, and
# restore on exit.
saved=()
for p in rainbowcrackalack_jtr.pot rainbowcrackalack_hashcat.pot; do
  [[ -f "$REPO/$p" ]] && { mv -f "$REPO/$p" "$REPO/$p.masktestbak"; saved+=("$p"); }
done
cleanup() {
  rm -rf "$work"
  rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
  for p in "${saved[@]:-}"; do [[ -n "$p" && -f "$REPO/$p.masktestbak" ]] && mv -f "$REPO/$p.masktestbak" "$REPO/$p"; done
  return 0
}
trap cleanup EXIT

strip_ansi() { sed -E $'s/\x1b\\[[0-9;]*m//g'; }

rc=0

echo "===== Mask end-to-end test (mask='$MASK') ====="

casedir="$work/case_mask"; mkdir -p "$casedir"
# Expected filename: ntlm_<encoded_mask>#8-8_0_<chain_len>x<num_chains>_0.rt
genname="ntlm_${MASK_ENCODED}#8-8_0_${CHAIN_LEN}x${NUM_CHAINS}_0.rt"

# 1. Generate the mask table.  Gen must run from the repo (Metal loads kernels
#    CWD-relative); then move the table into the scratch dir for lookup.
echo "[test] generating mask table ($genname)..."
( cd "$REPO" && "$GEN" ntlm ascii-32-95 8 8 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 --mask "$MASK" >/dev/null 2>&1 )
[[ -f "$REPO/$genname" ]] || { echo "FAIL: no table generated ($genname)"; exit 1; }
mv -f "$REPO/$genname" "$casedir/"; rm -f "$REPO/$genname.state"
table="$casedir/$genname"

# 2. Sort the table (required before lookup can binary-search endpoints).
echo "[test] sorting table..."
"$SORT" "$table" >/dev/null 2>&1 || { echo "FAIL: crackalack_sort failed"; exit 1; }

# 3. Verify the table with --mask.
echo "[test] verifying table..."
vout="$("$VERIFY" --quick --mask "$MASK" "$table" 2>&1 | strip_ansi || true)"
if grep -qi "successfully verified" <<<"$vout"; then
  echo "  PASS: crackalack_verify --mask"
else
  echo "  FAIL: crackalack_verify --mask did not pass"
  echo "$vout" | tail -5
  rc=1
fi

# 4. Mint an in-table hash using gen_known_hash --mask.
#    Params MUST match the generated table: same chain_len, table_index 0,
#    start from the actual chain-0 start point read from the sorted table.
echo "[test] minting in-table hash with gen_known_hash --mask..."
start="$("$GETCHAIN" "$table" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
[[ -n "$start" ]] || { echo "FAIL: get_chain failed to read chain 0 start"; rc=1; start=0; }
out="$("$GKH" "$CHAIN_LEN" 0 "$start" "$TARGET_POS" --algo ntlm --mask "$MASK" 2>&1)"
hash="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$out")"
[[ -n "$hash" ]] || { echo "FAIL: gen_known_hash produced no hash"; echo "$out" | tail -3; exit 1; }
echo "  minted in-table hash $hash (start=$start, pos=$TARGET_POS) -> '$pt'"

# 5. Run lookup and assert the hash CRACKS.
echo "[test] running lookup (positive control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "$hash" > "$work/hashes.txt"
log="$work/lookup.log"
( cd "$REPO" && "$LOOKUP" "$casedir" "$work/hashes.txt" ) >"$log" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$log"); then
  echo "  PASS: lookup cracked the in-table hash"
else
  echo "  FAIL: in-table hash did NOT crack"
  strip_ansi <"$log" | tail -8
  rc=1
fi

# 6. Negative control: a hash that cannot fit the mask keyspace must NOT crack.
#    deadbeef... is a fixed 128-bit value astronomically unlikely to be the NTLM
#    of any plaintext in this tiny table.
echo "[test] running lookup (negative control)..."
neg="deadbeefdeadbeefdeadbeefdeadbeef"
printf '%s\n' "$neg" > "$work/neg.txt"
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
nlog="$work/neg.log"
( cd "$REPO" && "$LOOKUP" "$casedir" "$work/neg.txt" ) >"$nlog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$nlog"); then
  echo "  FAIL: negative-control hash CRACKED (false positive -- FA check is broken)"
  rc=1
else
  echo "  PASS: negative-control hash not cracked (false-alarm check rejects bogus candidates)"
fi

echo
if [[ "$rc" -eq 0 ]]; then
  echo "PASS: Mask end-to-end pipeline (gen/sort/verify/mint/lookup) works correctly."
else
  echo "FAIL: one or more Mask pipeline checks failed."
fi
exit "$rc"
