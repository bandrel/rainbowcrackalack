#!/usr/bin/env bash
# Regression test for the hardcoded-chain_len bug in precompute_ntlm8.
#
# is_ntlm8() is chain-len-agnostic, so an NTLM8 ascii-32-95 table with a
# NON-standard chain_len (here: 100000, not the hardcoded 422000) is looked up
# with the optimized precompute_ntlm8 kernel. If that kernel hardcodes 422000
# instead of honoring the host's chain_len, the precomputed endpoints don't
# match the table and a KNOWN, in-table hash fails to crack (false negative).
#
# This test gens such a table, mints a guaranteed-in-table hash with
# gen_known_hash, looks it up, and asserts it cracks.
#
# Usage: test_ntlm8_nonstandard_chainlen.sh [repo_dir]
#   repo_dir defaults to the repo this script lives in.
set -euo pipefail
REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
GEN="$REPO/crackalack_gen"; LOOKUP="$REPO/crackalack_lookup"; GKH="$REPO/gen_known_hash"; SORT="$REPO/crackalack_sort"
CHAIN_LEN=100000          # deliberately != 422000
NUM_CHAINS=1000000
START_INDEX=0
TARGET_POS=50000          # somewhere mid-chain

for b in "$GEN" "$LOOKUP" "$GKH" "$SORT"; do [[ -x "$b" ]] || { echo "SKIP: $b not built"; exit 0; }; done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
echo "[test] gen NTLM8 ascii-32-95 8 8 idx0 chain_len=$CHAIN_LEN num_chains=$NUM_CHAINS"
# crackalack_gen writes the .rt to CWD and (for Metal) must run from the repo so
# it can find Metal/*.metal kernels (kernel loading is CWD-relative). Generate in
# the repo, then move the table into the scratch dir we look up against.
genname="ntlm_ascii-32-95#8-8_0_${CHAIN_LEN}x${NUM_CHAINS}_0.rt"
( cd "$REPO" && "$GEN" ntlm ascii-32-95 8 8 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 >/dev/null 2>&1 )
[[ -f "$REPO/$genname" ]] || { echo "FAIL: no table generated ($genname)"; exit 1; }
mv -f "$REPO/$genname" "$work/"; rm -f "$REPO/$genname.state"
table="$work/$genname"
echo "[test] table: $(basename "$table")"

# The lookup binary-searches endpoints, so the freshly-generated table MUST be
# sorted first (raw gen output is unsorted -> binary search finds nothing).
echo "[test] sorting table..."
( "$SORT" "$table" >/dev/null 2>&1 ) || { echo "FAIL: crackalack_sort failed"; exit 1; }

out="$("$GKH" "$CHAIN_LEN" 0 "$START_INDEX" "$TARGET_POS" --algo ntlm --charset ascii-32-95 --plaintext-len 8)"
hash="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$out")"
[[ -n "$hash" ]] || { echo "FAIL: gen_known_hash produced no hash"; exit 1; }
echo "[test] known hash $hash should crack to '$pt'"

# Run lookup (move any potfile aside so it can't short-circuit / pollute).
for p in rainbowcrackalack_jtr.pot rainbowcrackalack_hashcat.pot; do
  [[ -f "$REPO/$p" ]] && mv -f "$REPO/$p" "$REPO/$p.testbak"
done
restore() { for p in rainbowcrackalack_jtr.pot rainbowcrackalack_hashcat.pot; do [[ -f "$REPO/$p.testbak" ]] && mv -f "$REPO/$p.testbak" "$REPO/$p"; done; return 0; }
trap 'restore; rm -rf "$work"' EXIT
rm -f "$REPO"/rcracki.precalc.*

log="$work/lookup.log"
( cd "$REPO" && "$LOOKUP" "$work" "$hash" ) >"$log" 2>&1 || true
clean="$(sed -E $'s/\x1b\\[[0-9;]*m//g' "$log")"
if printf '%s\n' "$clean" | grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED"; then
  echo "PASS: known hash cracked (precompute honored chain_len=$CHAIN_LEN)"
  exit 0
else
  echo "FAIL: known in-table hash did NOT crack -> precompute_ntlm8 ignored chain_len=$CHAIN_LEN"
  echo "----- lookup tail -----"; tail -8 "$log"
  exit 1
fi
