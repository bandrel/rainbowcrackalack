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

# ---- Custom-charset case (?1?1?l?d with -1 abcxyz) ----
# Exercises hashcat-style custom charsets end-to-end.  crackalack_lookup gets
# NO -1 flag: it must reconstruct the custom charset from the self-describing
# filename (ntlm_%1%1%l%d!1-<hex>#4-4_...).
CC_MASK='?1?1?l?d'; CC1='abcxyz'
CC_CHAIN_LEN=1000; CC_NUM_CHAINS=512; CC_TARGET_POS=300
ccdir="$work/case_custom"; mkdir -p "$ccdir"
echo
echo "===== Custom-charset end-to-end test (mask='$CC_MASK' -1 '$CC1') ====="

# 1. Generate the custom-charset table (length 4, table_index 0, part 0).
echo "[test] generating custom-charset table..."
( cd "$REPO" && "$GEN" ntlm ascii-32-95 4 4 0 "$CC_CHAIN_LEN" "$CC_NUM_CHAINS" 0 \
    --mask "$CC_MASK" -1 "$CC1" >/dev/null 2>&1 )
# Filename hex suffix is deterministic but matched by glob for robustness.
ccname="$(cd "$REPO" && ls 'ntlm_%1%1%l%d!1-'*'_0.rt' 2>/dev/null | head -1)"
[[ -n "$ccname" && -f "$REPO/$ccname" ]] || { echo "FAIL: no custom-charset table generated"; exit 1; }
mv -f "$REPO/$ccname" "$ccdir/"; rm -f "$REPO/$ccname.state"
cctable="$ccdir/$ccname"
echo "  generated $ccname"

# 2. Sort.
echo "[test] sorting custom-charset table..."
"$SORT" "$cctable" >/dev/null 2>&1 || { echo "FAIL: crackalack_sort failed (custom)"; exit 1; }

# 3. Verify with --mask + -1.
echo "[test] verifying custom-charset table..."
ccv="$("$VERIFY" --quick --mask "$CC_MASK" -1 "$CC1" "$cctable" 2>&1 | strip_ansi || true)"
if grep -qi "successfully verified" <<<"$ccv"; then
  echo "  PASS: crackalack_verify --mask -1 (custom)"
else
  echo "  FAIL: crackalack_verify (custom) did not pass"
  echo "$ccv" | tail -5
  rc=1
fi

# 4. Mint an in-table hash using gen_known_hash --mask + -1.
echo "[test] minting in-table custom hash with gen_known_hash --mask -1..."
ccstart="$("$GETCHAIN" "$cctable" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
[[ -n "$ccstart" ]] || { echo "FAIL: get_chain failed to read chain 0 start (custom)"; rc=1; ccstart=0; }
cco="$("$GKH" "$CC_CHAIN_LEN" 0 "$ccstart" "$CC_TARGET_POS" --algo ntlm --mask "$CC_MASK" -1 "$CC1" 2>&1)"
cchash="$(awk -F= '/^hash=/{print $2}' <<<"$cco")"
ccpt="$(awk -F= '/^plaintext=/{print $2}' <<<"$cco")"
[[ -n "$cchash" ]] || { echo "FAIL: gen_known_hash (custom) produced no hash"; echo "$cco" | tail -3; exit 1; }
echo "  minted in-table custom hash $cchash (start=$ccstart, pos=$CC_TARGET_POS) -> '$ccpt'"

# 5. Lookup positive control -- lookup auto-detects the custom charset from the
#    filename (NO -1 flag passed here).
echo "[test] running custom lookup (positive control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "$cchash" > "$work/cchashes.txt"
cclog="$work/cclookup.log"
( cd "$REPO" && "$LOOKUP" "$ccdir" "$work/cchashes.txt" ) >"$cclog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$cclog"); then
  echo "  PASS: lookup cracked the in-table custom hash"
else
  echo "  FAIL: in-table custom hash did NOT crack"
  strip_ansi <"$cclog" | tail -8
  rc=1
fi

# 6. Negative control for the custom case.
echo "[test] running custom lookup (negative control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "deadbeefdeadbeefdeadbeefdeadbeef" > "$work/ccneg.txt"
ccnlog="$work/ccneg.log"
( cd "$REPO" && "$LOOKUP" "$ccdir" "$work/ccneg.txt" ) >"$ccnlog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$ccnlog"); then
  echo "  FAIL: custom negative-control hash CRACKED (false positive -- FA check is broken)"
  rc=1
else
  echo "  PASS: custom negative-control hash not cracked"
fi

# ---- Hex-token mask case (?h?h?l?d, built-in tokens only) ----
# Exercises the ?h / ?H built-in hex tokens end-to-end.  No custom charsets.
# crackalack_lookup gets NO mask flags: it must auto-detect the mask from the
# self-describing filename (ntlm_%h%h%l%d#4-4_...).
HX_MASK='?h?h?l?d'
HX_CHAIN_LEN=1000; HX_NUM_CHAINS=512; HX_TARGET_POS=300
hxdir="$work/case_hex"; mkdir -p "$hxdir"
echo
echo "===== Hex-token mask e2e (?h?h?l?d) ====="

# 1. Generate the hex-token table (length 4, table_index 0, part 0).
echo "[test] generating hex-token table..."
( cd "$REPO" && "$GEN" ntlm ascii-32-95 4 4 0 "$HX_CHAIN_LEN" "$HX_NUM_CHAINS" 0 \
    --mask "$HX_MASK" >/dev/null 2>&1 )
hxname="ntlm_%h%h%l%d#4-4_0_${HX_CHAIN_LEN}x${HX_NUM_CHAINS}_0.rt"
if [[ ! -f "$REPO/$hxname" ]]; then
  hxname="$(cd "$REPO" && ls 'ntlm_%h%h%l%d#4-4_0_'*'_0.rt' 2>/dev/null | head -1)"
fi
[[ -n "$hxname" && -f "$REPO/$hxname" ]] || { echo "FAIL: no hex-token table generated"; exit 1; }
mv -f "$REPO/$hxname" "$hxdir/"; rm -f "$REPO/$hxname.state"
hxtable="$hxdir/$hxname"
echo "  generated $hxname"

# 2. Sort.
echo "[test] sorting hex-token table..."
"$SORT" "$hxtable" >/dev/null 2>&1 || { echo "FAIL: crackalack_sort failed (hex)"; exit 1; }

# 3. Verify with --mask.
echo "[test] verifying hex-token table..."
hxv="$("$VERIFY" --quick --mask "$HX_MASK" "$hxtable" 2>&1 | strip_ansi || true)"
if grep -qi "successfully verified" <<<"$hxv"; then
  echo "  PASS: crackalack_verify --mask (hex)"
else
  echo "  FAIL: crackalack_verify (hex) did not pass"
  echo "$hxv" | tail -5
  rc=1
fi

# 4. Mint an in-table hash using gen_known_hash --mask.
echo "[test] minting in-table hex hash with gen_known_hash --mask..."
hxstart="$("$GETCHAIN" "$hxtable" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
[[ -n "$hxstart" ]] || { echo "FAIL: get_chain failed to read chain 0 start (hex)"; rc=1; hxstart=0; }
hxo="$("$GKH" "$HX_CHAIN_LEN" 0 "$hxstart" "$HX_TARGET_POS" --algo ntlm --mask "$HX_MASK" 2>&1)"
hxhash="$(awk -F= '/^hash=/{print $2}' <<<"$hxo")"
hxpt="$(awk -F= '/^plaintext=/{print $2}' <<<"$hxo")"
[[ -n "$hxhash" ]] || { echo "FAIL: gen_known_hash (hex) produced no hash"; echo "$hxo" | tail -3; exit 1; }
echo "  minted in-table hex hash $hxhash (start=$hxstart, pos=$HX_TARGET_POS) -> '$hxpt'"

# 5. Lookup positive control -- lookup auto-detects the mask from the filename.
echo "[test] running hex lookup (positive control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "$hxhash" > "$work/hxhashes.txt"
hxlog="$work/hxlookup.log"
( cd "$REPO" && "$LOOKUP" "$hxdir" "$work/hxhashes.txt" ) >"$hxlog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$hxlog"); then
  echo "  PASS: lookup cracked the in-table hex hash"
else
  echo "  FAIL: in-table hex hash did NOT crack"
  strip_ansi <"$hxlog" | tail -8
  rc=1
fi

# 6. Negative control for the hex case.
echo "[test] running hex lookup (negative control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "deadbeefdeadbeefdeadbeefdeadbeef" > "$work/hxneg.txt"
hxnlog="$work/hxneg.log"
( cd "$REPO" && "$LOOKUP" "$hxdir" "$work/hxneg.txt" ) >"$hxnlog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$hxnlog"); then
  echo "  FAIL: hex negative-control hash CRACKED (false positive -- FA check is broken)"
  rc=1
else
  echo "  PASS: hex negative-control hash not cracked"
fi

# ---- Multi-slot custom charset case (?1?2?l?d, -1 ?d -2 abc) ----
# Exercises multiple custom charset slots AND a token-in-definition (-1 '?d'
# expands to the digits 0-9).  crackalack_lookup gets NO -N flags: it must
# reconstruct BOTH custom charsets from the self-describing filename
# (ntlm_%1%2%l%d!1-<hex>!2-<hex>#4-4_...).
MS_MASK='?1?2?l?d'; MS1='?d'; MS2='abc'
MS_CHAIN_LEN=1000; MS_NUM_CHAINS=512; MS_TARGET_POS=300
msdir="$work/case_multislot"; mkdir -p "$msdir"
echo
echo "===== Multi-slot custom charset e2e (?1?2?l?d, -1 ?d -2 abc) ====="

# 1. Generate the multi-slot table (length 4, table_index 0, part 0).
echo "[test] generating multi-slot custom-charset table..."
( cd "$REPO" && "$GEN" ntlm ascii-32-95 4 4 0 "$MS_CHAIN_LEN" "$MS_NUM_CHAINS" 0 \
    --mask "$MS_MASK" -1 "$MS1" -2 "$MS2" >/dev/null 2>&1 )
# Filename encodes both custom-charset blocks: !1-<hex(0123456789)> !2-<hex(abc)>.
msname="$(cd "$REPO" && ls 'ntlm_%1%2%l%d!1-'*'!2-'*'#4-4_0_'*'_0.rt' 2>/dev/null | head -1)"
[[ -n "$msname" && -f "$REPO/$msname" ]] || { echo "FAIL: no multi-slot custom-charset table generated"; exit 1; }
mv -f "$REPO/$msname" "$msdir/"; rm -f "$REPO/$msname.state"
mstable="$msdir/$msname"
echo "  generated $msname"

# 2. Sort.
echo "[test] sorting multi-slot table..."
"$SORT" "$mstable" >/dev/null 2>&1 || { echo "FAIL: crackalack_sort failed (multi-slot)"; exit 1; }

# 3. Verify with --mask + -1 + -2.
echo "[test] verifying multi-slot table..."
msv="$("$VERIFY" --quick --mask "$MS_MASK" -1 "$MS1" -2 "$MS2" "$mstable" 2>&1 | strip_ansi || true)"
if grep -qi "successfully verified" <<<"$msv"; then
  echo "  PASS: crackalack_verify --mask -1 -2 (multi-slot)"
else
  echo "  FAIL: crackalack_verify (multi-slot) did not pass"
  echo "$msv" | tail -5
  rc=1
fi

# 4. Mint an in-table hash using gen_known_hash --mask + -1 + -2.
echo "[test] minting in-table multi-slot hash with gen_known_hash --mask -1 -2..."
msstart="$("$GETCHAIN" "$mstable" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
[[ -n "$msstart" ]] || { echo "FAIL: get_chain failed to read chain 0 start (multi-slot)"; rc=1; msstart=0; }
mso="$("$GKH" "$MS_CHAIN_LEN" 0 "$msstart" "$MS_TARGET_POS" --algo ntlm --mask "$MS_MASK" -1 "$MS1" -2 "$MS2" 2>&1)"
mshash="$(awk -F= '/^hash=/{print $2}' <<<"$mso")"
mspt="$(awk -F= '/^plaintext=/{print $2}' <<<"$mso")"
[[ -n "$mshash" ]] || { echo "FAIL: gen_known_hash (multi-slot) produced no hash"; echo "$mso" | tail -3; exit 1; }
echo "  minted in-table multi-slot hash $mshash (start=$msstart, pos=$MS_TARGET_POS) -> '$mspt'"

# 5. Lookup positive control -- lookup reconstructs BOTH custom charsets from the
#    filename (NO -N flags passed here).
echo "[test] running multi-slot lookup (positive control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "$mshash" > "$work/mshashes.txt"
mslog="$work/mslookup.log"
( cd "$REPO" && "$LOOKUP" "$msdir" "$work/mshashes.txt" ) >"$mslog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$mslog"); then
  echo "  PASS: lookup cracked the in-table multi-slot hash"
else
  echo "  FAIL: in-table multi-slot hash did NOT crack"
  strip_ansi <"$mslog" | tail -8
  rc=1
fi

# 6. Negative control for the multi-slot case.
echo "[test] running multi-slot lookup (negative control)..."
rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
printf '%s\n' "deadbeefdeadbeefdeadbeefdeadbeef" > "$work/msneg.txt"
msnlog="$work/msneg.log"
( cd "$REPO" && "$LOOKUP" "$msdir" "$work/msneg.txt" ) >"$msnlog" 2>&1 || true
if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$msnlog"); then
  echo "  FAIL: multi-slot negative-control hash CRACKED (false positive -- FA check is broken)"
  rc=1
else
  echo "  PASS: multi-slot negative-control hash not cracked"
fi

echo
if [[ "$rc" -eq 0 ]]; then
  echo "PASS: Mask end-to-end pipeline (gen/sort/verify/mint/lookup) works correctly."
else
  echo "FAIL: one or more Mask pipeline checks failed."
fi
exit "$rc"
