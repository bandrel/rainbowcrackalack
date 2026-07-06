#!/usr/bin/env bash
# End-to-end regression test for the Markov attack pipeline.
#
# Codex flagged "generic Markov lookup is non-functional" (0 cracks) plus three
# concrete bugs. This test exercises the whole pipeline and asserts the real,
# fixed behaviour so it can't regress:
#
#   * generic Markov lookup actually cracks an in-table hash (the P1 claim);
#   * crackalack_verify --markov honours the -mk<N> keyspace (bug #2 -- the
#     CPU chain recompute used charset_len^plaintext_len instead of the
#     truncated keyspace and failed every -mk table);
#   * the precompute cache key includes the Markov keyspace (bug #3 -- keys
#     omitting -mk<N> reused stale endpoints across keyspaces);
#   * gen_known_hash accepts --markov without --markov-keyspace (bug #4).
#
# Both a truncated keyspace (-mk<N>) and the full Markov keyspace are covered.
# The test is self-contained: it trains a throwaway model with crackalack_plan
# from a synthetic wordlist, so it needs no pre-trained .markov file.
#
# Usage: test_markov_lookup.sh [repo_dir]
set -euo pipefail
REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
GEN="$REPO/crackalack_gen"; LOOKUP="$REPO/crackalack_lookup"; GKH="$REPO/gen_known_hash"
SORT="$REPO/crackalack_sort"; VERIFY="$REPO/crackalack_verify"; PLAN="$REPO/crackalack_plan"
GETCHAIN="$REPO/get_chain"

CHAIN_LEN=1000
NUM_CHAINS=512
TARGET_POS=300

for b in "$GEN" "$LOOKUP" "$GKH" "$SORT" "$VERIFY" "$PLAN" "$GETCHAIN"; do
  [[ -x "$b" ]] || { echo "SKIP: $b not built (build with 'make <platform>' and 'make gen_known_hash')"; exit 0; }
done
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 needed to synthesize a wordlist"; exit 0; }

work="$(mktemp -d)"
# Move potfiles/precompute cache aside so a stale run can't short-circuit, and
# restore on exit.
saved=()
for p in rainbowcrackalack_jtr.pot rainbowcrackalack_hashcat.pot; do
  [[ -f "$REPO/$p" ]] && { mv -f "$REPO/$p" "$REPO/$p.mktestbak"; saved+=("$p"); }
done
cleanup() {
  rm -rf "$work"
  rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
  for p in "${saved[@]:-}"; do [[ -n "$p" && -f "$REPO/$p.mktestbak" ]] && mv -f "$REPO/$p.mktestbak" "$REPO/$p"; done
  return 0
}
trap cleanup EXIT

strip_ansi() { sed -E $'s/\x1b\\[[0-9;]*m//g'; }

# --- Train a throwaway position-aware Markov model over ascii-32-95. --------
echo "[test] training a throwaway Markov model (crackalack_plan train)..."
python3 - "$work/words.txt" <<'PY'
import random, sys
random.seed(1234)
cs = [chr(c) for c in range(32, 127)]   # ascii-32-95
with open(sys.argv[1], "w") as f:
    for _ in range(3000):
        f.write("".join(random.choice(cs) for _ in range(8)) + "\n")
PY
( cd "$work" && "$PLAN" train words.txt ascii-32-95 >/dev/null 2>&1 )
MODEL="$work/words.markov"
[[ -f "$MODEL" ]] || { echo "FAIL: crackalack_plan train produced no model"; exit 1; }

rc=0

# run_case <label> <keyspace_or_empty>
#   keyspace empty  -> full Markov keyspace  (filename has no -mk suffix)
#   keyspace set    -> truncated -mk<N> table
run_case() {
  local label="$1" keyspace="$2"
  local ks_gen=() ks_gkh=() mk_suffix=""
  if [[ -n "$keyspace" ]]; then
    ks_gen=(--markov-keyspace "$keyspace"); ks_gkh=(--markov-keyspace "$keyspace")
    mk_suffix="-mk${keyspace}"
  fi
  echo
  echo "===== case: $label (keyspace='${keyspace:-full}') ====="

  # Each case gets its own lookup dir so the lookup only ever sees this case's
  # single table (mixing keyspaces in one dir would cross-pollute the cache key
  # check below).
  local casedir="$work/case_${label}"; mkdir -p "$casedir"
  local genname="ntlm_ascii-32-95${mk_suffix}#8-8_0_${CHAIN_LEN}x${NUM_CHAINS}_0.rt"
  # gen must run from the repo (Metal loads kernels CWD-relative); then move the
  # table into the scratch dir we look up against.
  ( cd "$REPO" && "$GEN" ntlm ascii-32-95 8 8 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 --markov "$MODEL" "${ks_gen[@]}" >/dev/null 2>&1 )
  [[ -f "$REPO/$genname" ]] || { echo "FAIL[$label]: no table generated ($genname)"; rc=1; return; }
  mv -f "$REPO/$genname" "$casedir/"; rm -f "$REPO/$genname.state"
  local table="$casedir/$genname"
  "$SORT" "$table" >/dev/null 2>&1 || { echo "FAIL[$label]: crackalack_sort failed"; rc=1; return; }

  # (bug #2) verify --markov must honour the -mk keyspace.
  local vout
  vout="$("$VERIFY" --quick --markov "$MODEL" "$table" 2>&1 | strip_ansi || true)"
  if grep -qi "successfully verified" <<<"$vout"; then
    echo "  PASS: crackalack_verify --markov"
  else
    echo "  FAIL[$label]: crackalack_verify --markov did not pass"; echo "$vout" | tail -3; rc=1
  fi

  # (bug #4) gen_known_hash accepts --markov with or without --markov-keyspace.
  local start out hash pt
  start="$("$GETCHAIN" "$table" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
  out="$("$GKH" "$CHAIN_LEN" 0 "$start" "$TARGET_POS" --algo ntlm --charset ascii-32-95 --plaintext-len 8 --markov "$MODEL" "${ks_gkh[@]}" 2>&1)"
  hash="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
  pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$out")"
  [[ -n "$hash" ]] || { echo "FAIL[$label]: gen_known_hash produced no hash"; echo "$out" | tail -3; rc=1; return; }
  echo "  minted in-table hash $hash (start=$start, pos=$TARGET_POS) -> '$pt'"

  # (P1) lookup must crack the in-table hash. --markov model is REQUIRED.
  rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
  printf '%s\n' "$hash" > "$work/hashes.txt"
  local log="$work/lookup_${label}.log"
  ( cd "$REPO" && "$LOOKUP" "$casedir" "$work/hashes.txt" --markov "$MODEL" ) >"$log" 2>&1 || true
  if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$log"); then
    echo "  PASS: lookup cracked the in-table hash"
  else
    echo "  FAIL[$label]: in-table hash did NOT crack"; strip_ansi <"$log" | tail -8; rc=1
  fi

  # (bug #3) the precompute cache key must encode the Markov keyspace so
  # different keyspaces / standard tables never share stale endpoints.
  local idx
  idx="$(cat "$REPO"/*.index 2>/dev/null | head -1 || true)"
  if [[ -n "$idx" ]]; then
    local want="-mk${keyspace:-0}"
    if grep -qF -- "$want" <<<"$idx"; then
      echo "  PASS: cache key encodes keyspace ($want)"
    else
      echo "  FAIL[$label]: cache key missing '$want' -> got: $idx"; rc=1
    fi
  else
    echo "  NOTE[$label]: no .index cache written (cannot check key)"
  fi
}

run_case truncated 500000
run_case full ""

echo
if [[ "$rc" -eq 0 ]]; then
  echo "PASS: Markov end-to-end pipeline (gen/sort/verify/mint/lookup) works for full and truncated keyspaces."
else
  echo "FAIL: one or more Markov pipeline checks failed."
fi
exit "$rc"
