#!/usr/bin/env bash
# End-to-end test for .hcmask batch generation + verification.
set -euo pipefail
REPO="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
GEN="$REPO/crackalack_gen"; VERIFY="$REPO/crackalack_verify"; SORT="$REPO/crackalack_sort"
LOOKUP="$REPO/crackalack_lookup"; GKH="$REPO/gen_known_hash"; GETCHAIN="$REPO/get_chain"
for b in "$GEN" "$VERIFY" "$SORT" "$LOOKUP" "$GKH" "$GETCHAIN"; do
  [[ -x "$b" ]] || { echo "SKIP: $b not built"; exit 0; }
done
work="$(mktemp -d)"; trap 'rm -rf "$work" "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true' EXIT
strip_ansi() { sed -E $'s/\x1b\\[[0-9;]*m//g'; }
rc=0

tables="$work/tables"; mkdir -p "$tables"
cat > "$work/masks.hcmask" <<'EOF'
# batch test
?u?l?l?d
?d?l,?1?1?1?1
EOF

echo "[test] batch generating from .hcmask..."
( cd "$REPO" && "$GEN" ntlm ascii-32-95 4 4 0 1000 512 0 --hcmask "$work/masks.hcmask" >/dev/null 2>&1 )
# Move the two generated tables into the scratch dir.
mv -f "$REPO"/ntlm_%u%l%l%d#4-4_0_1000x512_0.rt "$tables"/ 2>/dev/null || true
mv -f "$REPO"/ntlm_%1%1%1%1!1-*#4-4_0_1000x512_0.rt "$tables"/ 2>/dev/null || true
rm -f "$REPO"/ntlm_*#4-4_0_1000x512_0.rt.state 2>/dev/null || true
n=$(ls "$tables"/*.rt 2>/dev/null | wc -l | tr -d ' ')
[[ "$n" == "2" ]] && echo "  PASS: 2 tables generated" || { echo "  FAIL: expected 2 tables, got $n"; ls "$tables"; rc=1; }

echo "[test] sorting tables..."
for t in "$tables"/*.rt; do "$SORT" "$t" >/dev/null 2>&1; done

echo "[test] batch verify (all present)..."
vout="$("$VERIFY" --hcmask "$work/masks.hcmask" "$tables" 2>&1 | strip_ansi || true)"
if grep -q "MISSING" <<<"$vout"; then echo "  FAIL: unexpected MISSING"; echo "$vout"; rc=1; else echo "  PASS: no missing tables"; fi

echo "[test] lookup cracks an in-table hash from the batch dir (no mask flags)..."
plain="$tables/ntlm_%u%l%l%d#4-4_0_1000x512_0.rt"
if [[ -f "$plain" ]]; then
  pstart="$("$GETCHAIN" "$plain" 0 2>/dev/null | grep -oE '[0-9]+' | head -1)"
  out="$("$GKH" 1000 0 "$pstart" 300 --algo ntlm --mask '?u?l?l?d' 2>&1)"
  h="$(awk -F= '/^hash=/{print $2}' <<<"$out")"
  if [[ -n "$h" ]]; then
    rm -f "$REPO"/rcracki.precalc.* "$REPO"/*.index 2>/dev/null || true
    printf '%s\n' "$h" > "$work/h.txt"
    ( cd "$REPO" && "$LOOKUP" "$tables" "$work/h.txt" ) >"$work/lk.log" 2>&1 || true
    if grep -qiE "Cracked [1-9][0-9]* of|HASH CRACKED" <(strip_ansi <"$work/lk.log"); then
      echo "  PASS: lookup cracked the batch-generated table"
    else
      echo "  FAIL: lookup did not crack"; strip_ansi <"$work/lk.log" | tail -6; rc=1
    fi
  else
    echo "  FAIL: gen_known_hash produced no hash"; echo "$out" | tail -3; rc=1
  fi
else
  echo "  FAIL: expected plain table missing"; rc=1
fi

echo "[test] batch verify reports MISSING when a table is removed..."
rm -f "$tables"/ntlm_%u%l%l%d#4-4_0_1000x512_0.rt
vout2="$("$VERIFY" --hcmask "$work/masks.hcmask" "$tables" 2>&1 | strip_ansi || true)"
if grep -q "MISSING" <<<"$vout2"; then echo "  PASS: MISSING reported"; else echo "  FAIL: MISSING not reported"; echo "$vout2"; rc=1; fi

echo
if [[ "$rc" -eq 0 ]]; then echo "PASS: .hcmask batch pipeline works."; else echo "FAIL: .hcmask pipeline."; fi
exit "$rc"
