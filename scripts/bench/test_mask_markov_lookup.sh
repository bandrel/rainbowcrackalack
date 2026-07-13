#!/usr/bin/env bash
# End-to-end regression test for the combined mask+Markov attack pipeline.
#
# Exercises the whole combined pipeline and asserts the correct behaviour:
#
#   * crackalack_gen --mask ... --markov MODEL generates a combined table.  With
#     NO --markov-keyspace the table covers the FULL mask keyspace, so a minted
#     in-table hash is deterministically found (a truncated keyspace crack is
#     probabilistic and unsuitable for a regression).  The filename charset field
#     is the mask with a -mk<K> suffix (e.g. ntlm_%l%l%l%d-mk175760#4-4_...);
#   * crackalack_verify --quick --mask ... --markov MODEL confirms the table;
#   * gen_known_hash --mask ... --markov MODEL mints a provably-in-table
#     (plaintext, hash) using params CONSISTENT with the table (same chain_len,
#     table_index 0, chain-0 start read from the sorted table);
#   * crackalack_lookup --markov MODEL auto-detects the mask from the filename and
#     cracks the minted hash (positive control).  NOTE: lookup gets --markov MODEL
#     but NO --mask (mask is auto-detected from the self-describing filename), and
#     --markov MUST follow the two positional args (dir, hashfile) -- crackalack_lookup
#     parses flags starting at argv[3];
#   * a hash outside the mask keyspace does NOT crack (negative control, guards
#     against a broken false-alarm check that would confirm bogus candidates).
#
# Usage: test_mask_markov_lookup.sh [repo_dir]
set -euo pipefail
REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
GEN="$REPO/crackalack_gen"; LOOKUP="$REPO/crackalack_lookup"; GKH="$REPO/gen_known_hash"
SORT="$REPO/crackalack_sort"; VERIFY="$REPO/crackalack_verify"
GETCHAIN="$REPO/get_chain"
MODEL="$REPO/dynamic-all.markov"

CHAIN_LEN=1000
NUM_CHAINS=512
TARGET_POS=300
MASK='?l?l?l?d'

# Skip gracefully if the model or any required binary is missing.
[[ -f "$MODEL" ]] || { echo "SKIP: markov model not found ($MODEL)"; exit 0; }
for b in "$GEN" "$LOOKUP" "$GKH" "$SORT" "$VERIFY" "$GETCHAIN"; do
  [[ -x "$b" ]] || { echo "SKIP: $b not built (build with 'make <platform>' and 'make gen_known_hash')"; exit 0; }
done

work="$(mktemp -d)"
# Move potfiles aside so a stale run can't short-circuit, and restore on exit.
saved=()
for p in rainbowcrackalack_jtr.pot rainbowcrackalack_hashcat.pot; do
  [[ -f "$REPO/$p" ]] && { mv -f "$REPO/$p" "$REPO/$p.maskmarkovtestbak"; saved+=("$p"); }
done
cleanup() {
  rm -rf "$work"
  rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
  for p in "${saved[@]:-}"; do [[ -n "$p" && -f "$REPO/$p.maskmarkovtestbak" ]] && mv -f "$REPO/$p.maskmarkovtestbak" "$REPO/$p"; done
  return 0
}
trap cleanup EXIT

strip_ansi() { sed -E $'s/\x1b\\[[0-9;]*m//g'; }

rc=0

echo "===== Mask+Markov end-to-end test (mask='$MASK' model=$(basename "$MODEL")) ====="

casedir="$work/case_mask_markov"; mkdir -p "$casedir"

# 1. Generate the combined table (full mask keyspace -- no --markov-keyspace).
#    Gen must run from the repo (Metal loads kernels CWD-relative); then move the
#    table into the scratch dir for lookup.  The filename is
#    ntlm_%l%l%l%d-mk<K>#4-4_<chain_len>x<num_chains>_0.rt (K = full keyspace);
#    locate it with a glob rather than hardcoding K.
echo "[test] generating combined mask+markov table..."
( cd "$REPO" && "$GEN" ntlm ascii-32-95 4 4 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 --mask "$MASK" --markov "$MODEL" >/dev/null 2>&1 )
genglob="ntlm_%l%l%l%d-mk*#4-4_0_${CHAIN_LEN}x${NUM_CHAINS}_0.rt"
genname="$(cd "$REPO" && ls $genglob 2>/dev/null | head -1)"
[[ -n "$genname" && -f "$REPO/$genname" ]] || { echo "FAIL: no combined table generated (glob $genglob)"; exit 1; }
mv -f "$REPO/$genname" "$casedir/"; rm -f "$REPO/$genname.state"
table="$casedir/$genname"
echo "  generated $genname"

# 2. Sort the table (required before lookup can binary-search endpoints).
echo "[test] sorting table..."
"$SORT" "$table" >/dev/null 2>&1 || { echo "FAIL: crackalack_sort failed"; exit 1; }

# 3. Verify the table with --mask + --markov.
echo "[test] verifying table..."
vout="$("$VERIFY" --quick --mask "$MASK" --markov "$MODEL" "$table" 2>&1 | strip_ansi || true)"
if grep -qi "successfully verified" <<<"$vout"; then
  echo "  PASS: crackalack_verify --mask --markov"
else
  echo "  FAIL: crackalack_verify --mask --markov did not pass"
  echo "$vout" | tail -5
  rc=1
fi

# 4. Mint an in-table hash using gen_known_hash --mask --markov.
#    Params MUST match the generated table: same chain_len, table_index 0,
#    start from the actual chain-0 start point read from the sorted table.
echo "[test] minting in-table hash with gen_known_hash --mask --markov..."
start="$("$GETCHAIN" "$table" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
[[ -n "$start" ]] || { echo "FAIL: get_chain failed to read chain 0 start"; rc=1; start=0; }
out="$("$GKH" "$CHAIN_LEN" 0 "$start" "$TARGET_POS" --algo ntlm --mask "$MASK" --markov "$MODEL" 2>&1)"
hash="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$out")"
[[ -n "$hash" ]] || { echo "FAIL: gen_known_hash produced no hash"; echo "$out" | tail -3; exit 1; }
echo "  minted in-table hash $hash (start=$start, pos=$TARGET_POS) -> '$pt'"

# 5. Run lookup and assert the hash CRACKS.
#    --markov MODEL follows the two positional args (dir, hashfile); mask is
#    auto-detected from the filename (NO --mask flag here).
echo "[test] running lookup (positive control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "$hash" > "$work/hashes.txt"
log="$work/lookup.log"
( cd "$REPO" && "$LOOKUP" "$casedir" "$work/hashes.txt" --markov "$MODEL" ) >"$log" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$log"); then
  echo "  PASS: lookup cracked the in-table hash"
else
  echo "  FAIL: in-table hash did NOT crack"
  strip_ansi <"$log" | tail -8
  rc=1
fi

# 6. Negative control: a hash that cannot fit the mask keyspace must NOT crack.
echo "[test] running lookup (negative control)..."
neg="deadbeefdeadbeefdeadbeefdeadbeef"
printf '%s\n' "$neg" > "$work/neg.txt"
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
nlog="$work/neg.log"
( cd "$REPO" && "$LOOKUP" "$casedir" "$work/neg.txt" --markov "$MODEL" ) >"$nlog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$nlog"); then
  echo "  FAIL: negative-control hash CRACKED (false positive -- FA check is broken)"
  rc=1
else
  echo "  PASS: negative-control hash not cracked (false-alarm check rejects bogus candidates)"
fi

echo
if [[ "$rc" -eq 0 ]]; then
  echo "PASS: Mask+Markov end-to-end pipeline (gen/sort/verify/mint/lookup) works correctly."
else
  echo "FAIL: one or more Mask+Markov pipeline checks failed."
fi
exit "$rc"
