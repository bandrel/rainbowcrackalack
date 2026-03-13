#!/bin/bash
set -euo pipefail

# Markov vs standard rainbow table accuracy comparison.
#
# Generates standard and Markov-ordered tables at multiple coverage levels
# (10%, 25%, 50%, 100%) and compares crack rates against a held-out set of
# real-world passwords. Reports merge rates (chain loss after sort) at each level.
#
# Usage: ./test_markov_accuracy.sh <path/to/rockyou.txt> [path/to/model.markov] [plaintext_len]
# plaintext_len defaults to 5.

ROCKYOU="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MARKOV="${2:-$SCRIPT_DIR/rockyou.markov}"
PT_LEN="${3:-5}"
BINDIR="$SCRIPT_DIR"

if [[ ! "$MARKOV" = /* ]]; then
    MARKOV="$(cd "$(dirname "$MARKOV")" && pwd)/$(basename "$MARKOV")"
fi

CHAIN_LEN=1000
COVERAGE_LEVELS=(10 25 50 100)

if [ -z "$ROCKYOU" ]; then
    echo "Usage: $0 <path/to/rockyou.txt> [path/to/model.markov] [plaintext_len]"
    exit 1
fi

if ! [[ "$PT_LEN" =~ ^[0-9]+$ ]] || [ "$PT_LEN" -lt 2 ]; then
    echo "ERROR: plaintext_len must be an integer >= 2 (got: $PT_LEN)"
    exit 1
fi

if [ ! -r "$ROCKYOU" ]; then
    echo "ERROR: rockyou.txt not readable: $ROCKYOU"
    exit 1
fi

if [ ! -f "$MARKOV" ]; then
    echo "ERROR: Markov model not found: $MARKOV"
    echo "  Train one with: ./crackalack_plan train rockyou.txt"
    exit 1
fi

for bin in "$BINDIR/crackalack_gen" "$BINDIR/crackalack_sort" "$BINDIR/crackalack_lookup"; do
    if [ ! -x "$bin" ]; then
        echo "ERROR: $bin not found or not executable"
        exit 1
    fi
done

# --- Helper functions ---

file_size_bytes() {
    local f="$1"
    stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null
}

count_rt_records() {
    local rt_file="$1"
    local size
    size=$(file_size_bytes "$rt_file")
    echo $((size / 16))
}

validate_markov_model() {
    local model="$1"
    local magic
    magic=$(head -c 4 "$model")
    if [ "$magic" != "RCLM" ]; then
        echo "ERROR: Markov model '$model' has bad magic (expected RCLM, got '$magic')"
        exit 1
    fi
    echo "  Markov model validated (magic=RCLM)."
}

validate_rt_file() {
    local rt_file="$1"
    local label="$2"
    if [ ! -f "$rt_file" ]; then
        echo "ERROR: $label .rt file not created: $rt_file"
        exit 1
    fi
    local size
    size=$(file_size_bytes "$rt_file")
    if [ "$size" -eq 0 ]; then
        echo "ERROR: $label .rt file is empty: $rt_file"
        exit 1
    fi
    if [ $((size % 16)) -ne 0 ]; then
        echo "ERROR: $label .rt file size ($size) is not a multiple of 16 - corrupt table"
        exit 1
    fi
}

validate_sort_order() {
    local rt_file="$1"
    local label="$2"
    python3 -c "
import struct, sys
with open('$rt_file', 'rb') as f:
    data = f.read()
n = len(data) // 16
if n < 2:
    sys.exit(0)
check_positions = list(range(min(10, n-1))) + list(range(max(0, n-10), n-1))
check_positions = sorted(set(check_positions))
for i in check_positions:
    _, end_a = struct.unpack_from('<QQ', data, i * 16)
    _, end_b = struct.unpack_from('<QQ', data, (i + 1) * 16)
    if end_a > end_b:
        print(f'ERROR: $label table not sorted at record {i}: {end_a} > {end_b}', file=sys.stderr)
        sys.exit(1)
" || { echo "ERROR: $label table sort verification failed"; exit 1; }
}

validate_hashes() {
    local hash_file="$1"
    local pw_file="$2"
    local hash_count pw_count
    hash_count=$(wc -l < "$hash_file" | tr -d ' ')
    pw_count=$(wc -l < "$pw_file" | tr -d ' ')
    if [ "$hash_count" != "$pw_count" ]; then
        echo "ERROR: Hash count ($hash_count) != password count ($pw_count)"
        exit 1
    fi
    local bad_lines
    bad_lines=$(grep -cvE '^[0-9a-f]{32}$' "$hash_file" || true)
    if [ "$bad_lines" -gt 0 ]; then
        echo "ERROR: $bad_lines hashes are not exactly 32 hex chars"
        exit 1
    fi
    echo "  Validated $hash_count hashes (all 32 hex chars)."
}

parse_cracked() {
    awk '/were cracked, or/{print $6} /No hashes were cracked/{print "0"}'
}

parse_pct() {
    awk '/were cracked, or/{gsub(/%\.?$/,"",$NF); print $NF} /No hashes were cracked/{print "0.00"}'
}

run_lookup() {
    local table_dir="$1"
    local hash_file="$2"
    shift 2
    local output rc=0
    output=$("$BINDIR/crackalack_lookup" "$table_dir" "$hash_file" "$@" 2>&1) || rc=$?
    if [ $rc -ne 0 ]; then
        echo "WARNING: crackalack_lookup exited with code $rc"
    fi
    if ! echo "$output" | grep -qE '(were cracked|No hashes were cracked)'; then
        echo "ERROR: crackalack_lookup produced unexpected output:"
        echo "$output" | tail -10 | sed 's/^/  /'
        exit 1
    fi
    local cracked
    cracked=$(echo "$output" | parse_cracked)
    if [ -n "$cracked" ] && [ "$cracked" -gt "$(wc -l < "$hash_file" | tr -d ' ')" ]; then
        echo "ERROR: cracked count ($cracked) > total hashes - parse error"
        exit 1
    fi
    echo "$output"
}

# --- Setup ---

TMPDIR="./tmp/markov_test_$$"
mkdir -p "$TMPDIR"
cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

ln -sfn "$BINDIR/Metal" "$TMPDIR/Metal" 2>/dev/null || true
ln -sfn "$BINDIR/CL" "$TMPDIR/CL" 2>/dev/null || true
ln -sfn "$BINDIR/shared.h" "$TMPDIR/shared.h" 2>/dev/null || true

# Compute keyspace
KEYSPACE=$(awk -v pt_len="$PT_LEN" 'BEGIN {
    ks = 1; for (i = 0; i < pt_len; i++) ks *= 95; print int(ks)
}')

SAMPLE_SIZE=$((KEYSPACE < 500 ? KEYSPACE : 500))
if [ "$SAMPLE_SIZE" -lt 1 ]; then SAMPLE_SIZE=1; fi

echo "=== Markov Accuracy Test ==="
echo "  charset=ascii-32-95, len=$PT_LEN, keyspace=$KEYSPACE"
echo "  chain_len=$CHAIN_LEN, coverage_levels=${COVERAGE_LEVELS[*]}%"
echo "  sample_size=$SAMPLE_SIZE passwords"
echo ""

# Validate Markov model
validate_markov_model "$MARKOV"

# Sample passwords
echo "Sampling $SAMPLE_SIZE passwords from rockyou.txt..."
LC_ALL=C grep -E "^[ -~]{${PT_LEN}}$" "$ROCKYOU" | shuf -n "$SAMPLE_SIZE" > "$TMPDIR/passwords.txt"
actual_count=$(wc -l < "$TMPDIR/passwords.txt" | tr -d ' ')
echo "  Got $actual_count passwords."

if [ "$actual_count" -eq 0 ]; then
    echo "ERROR: No passwords of length $PT_LEN found in rockyou.txt"
    exit 1
fi

# Compute NTLM hashes
echo "Computing NTLM hashes..."
cat > "$TMPDIR/ntlm_hash.py" <<'PYEOF'
import struct, sys

def _md4(data):
    """Pure Python MD4 (RFC 1320)."""
    def F(x, y, z): return (x & y) | (~x & z)
    def G(x, y, z): return (x & y) | (x & z) | (y & z)
    def H(x, y, z): return x ^ y ^ z
    def rotl(v, n): return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF
    def u32(v):     return v & 0xFFFFFFFF

    msg = bytearray(data)
    bit_len = len(data) * 8
    msg.append(0x80)
    msg += b'\x00' * (-(len(msg) + 8) % 64)
    msg += struct.pack('<Q', bit_len)

    A, B, C, D = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476

    for i in range(0, len(msg), 64):
        X = struct.unpack('<16I', msg[i:i+64])
        a, b, c, d = A, B, C, D

        for k, s in [(0,3),(1,7),(2,11),(3,19),(4,3),(5,7),(6,11),(7,19),
                     (8,3),(9,7),(10,11),(11,19),(12,3),(13,7),(14,11),(15,19)]:
            a = rotl(u32(a + F(b,c,d) + X[k]), s)
            a, b, c, d = d, a, b, c

        for k, s in [(0,3),(4,5),(8,9),(12,13),(1,3),(5,5),(9,9),(13,13),
                     (2,3),(6,5),(10,9),(14,13),(3,3),(7,5),(11,9),(15,13)]:
            a = rotl(u32(a + G(b,c,d) + X[k] + 0x5A827999), s)
            a, b, c, d = d, a, b, c

        for k, s in [(0,3),(8,9),(4,11),(12,15),(2,3),(10,9),(6,11),(14,15),
                     (1,3),(9,9),(5,11),(13,15),(3,3),(11,9),(7,11),(15,15)]:
            a = rotl(u32(a + H(b,c,d) + X[k] + 0x6ED9EBA1), s)
            a, b, c, d = d, a, b, c

        A, B, C, D = u32(A+a), u32(B+b), u32(C+c), u32(D+d)

    return struct.pack('<4I', A, B, C, D).hex()

def ntlm(password):
    return _md4(password.encode('utf-16-le'))

_expected = '8846f7eaee8fb117ad06bdd830b7586c'
_got = ntlm('password')
if _got != _expected:
    sys.exit(f'ERROR: MD4 self-test failed. got={_got} expected={_expected}')

for line in sys.stdin:
    print(ntlm(line.rstrip('\n')))
PYEOF

if command -v uv &> /dev/null; then
    PYTHON_CMD="uv run python3"
else
    PYTHON_CMD="python3"
fi
$PYTHON_CMD "$TMPDIR/ntlm_hash.py" < "$TMPDIR/passwords.txt" > "$TMPDIR/hashes.txt"
validate_hashes "$TMPDIR/hashes.txt" "$TMPDIR/passwords.txt"

# --- Pipeline sanity check with a known password ---
echo ""
echo "--- Pipeline sanity check ---"

# Generate a single-chain table for "test1" (len=5) or adapt to PT_LEN
# Use a tiny table: 1 chain of length CHAIN_LEN at table_index=0
SANITY_PW=$(head -1 "$TMPDIR/passwords.txt")
SANITY_HASH=$($PYTHON_CMD "$TMPDIR/ntlm_hash.py" <<< "$SANITY_PW")
echo "$SANITY_HASH" > "$TMPDIR/sanity_hash.txt"
echo "  Testing pipeline with password '$SANITY_PW' (hash=$SANITY_HASH)..."

# Standard sanity: generate a small table (capped for large keyspaces)
SANITY_CHAINS=$(( (KEYSPACE / CHAIN_LEN) + 1 ))
if [ "$SANITY_CHAINS" -gt 10000 ]; then SANITY_CHAINS=10000; fi
mkdir -p "$TMPDIR/sanity_std"
"$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$SANITY_CHAINS" 0 2>&1 | tail -3 | sed 's/^/  /'
mv ./*.rt "$TMPDIR/sanity_std/"
"$BINDIR/crackalack_sort" "$TMPDIR"/sanity_std/*.rt 2>&1 | tail -1 | sed 's/^/  /'
rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
sanity_std_out=$("$BINDIR/crackalack_lookup" "$TMPDIR/sanity_std/" "$TMPDIR/sanity_hash.txt" 2>&1) || true
sanity_std_cracked=$(echo "$sanity_std_out" | parse_cracked)
echo "  Standard pipeline: cracked=${sanity_std_cracked:-PARSE_ERROR}"

# Markov sanity (same chain count)
mkdir -p "$TMPDIR/sanity_mkv"
"$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$SANITY_CHAINS" 0 --markov "$MARKOV" --markov-keyspace "$KEYSPACE" 2>&1 | tail -3 | sed 's/^/  /'
mv ./*.rt "$TMPDIR/sanity_mkv/"
"$BINDIR/crackalack_sort" "$TMPDIR"/sanity_mkv/*.rt 2>&1 | tail -1 | sed 's/^/  /'
rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
sanity_mkv_out=$("$BINDIR/crackalack_lookup" "$TMPDIR/sanity_mkv/" "$TMPDIR/sanity_hash.txt" --markov "$MARKOV" 2>&1) || true
sanity_mkv_cracked=$(echo "$sanity_mkv_out" | parse_cracked)
echo "  Markov pipeline:   cracked=${sanity_mkv_cracked:-PARSE_ERROR}"

if [ "${sanity_std_cracked:-0}" = "0" ] && [ "${sanity_mkv_cracked:-0}" = "0" ]; then
    echo ""
    echo "WARNING: Neither pipeline cracked a known password at full coverage."
    echo "  This may indicate a broken pipeline. Continuing anyway..."
    echo ""
fi

# --- Main comparison loop ---
echo ""
echo "=== Coverage-level comparison ==="

# Arrays to accumulate summary data
declare -a SUMMARY_COVERAGE=()
declare -a SUMMARY_STD_REQ=()
declare -a SUMMARY_STD_ACTUAL=()
declare -a SUMMARY_STD_CRACKED=()
declare -a SUMMARY_MKV_REQ=()
declare -a SUMMARY_MKV_ACTUAL=()
declare -a SUMMARY_MKV_CRACKED=()

for coverage in "${COVERAGE_LEVELS[@]}"; do
    echo ""
    echo "--- Coverage target: ${coverage}% ---"

    num_chains=$(( KEYSPACE * coverage / 100 / CHAIN_LEN ))
    if [ "$num_chains" -lt 1 ]; then num_chains=1; fi
    # Cap at uint32_t max (crackalack_gen uses 32-bit chain counts)
    if [ "$num_chains" -gt 4294967295 ]; then
        echo "  WARNING: Capping num_chains from $num_chains to 4294967295 (uint32_t max)"
        num_chains=4294967295
    fi

    echo "  Requested chains: $num_chains (keyspace=$KEYSPACE, chain_len=$CHAIN_LEN)"

    # --- Standard table ---
    std_dir="$TMPDIR/std_${coverage}"
    mkdir -p "$std_dir"

    echo "  [Standard] Generating..."
    "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$num_chains" 0 2>&1 | tail -2 | sed 's/^/    /'
    mv ./*.rt "$std_dir/"

    std_rt_files=("$std_dir"/*.rt)
    std_rt="${std_rt_files[0]}"
    validate_rt_file "$std_rt" "Standard ${coverage}%"

    std_pre_sort=$(count_rt_records "$std_rt")
    echo "  [Standard] Pre-sort records: $std_pre_sort"

    "$BINDIR/crackalack_sort" "$std_rt" 2>&1 | tail -1 | sed 's/^/    /'

    std_post_sort=$(count_rt_records "$std_rt")
    validate_sort_order "$std_rt" "Standard ${coverage}%"

    std_merge_loss=0
    if [ "$num_chains" -gt 0 ]; then
        std_merge_loss=$(awk "BEGIN{printf \"%.1f\", ($num_chains - $std_post_sort) * 100.0 / $num_chains}")
    fi
    std_eff_coverage=$(awk "BEGIN{printf \"%.1f\", $std_post_sort * 100.0 / $KEYSPACE}")

    echo "  [Standard] Requested: $num_chains, After sort: $std_post_sort (${std_merge_loss}% merge loss)"
    echo "  [Standard] Effective coverage: ${std_eff_coverage}%"

    if awk "BEGIN{exit(!($std_merge_loss > 50))}"; then
        echo "  WARNING: Merge loss > 50% - chain_len may be too high for this keyspace"
    fi

    rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
    echo "  [Standard] Looking up hashes..."
    std_output=$(run_lookup "$std_dir" "$TMPDIR/hashes.txt")
    std_cracked=$(echo "$std_output" | parse_cracked)
    std_pct=$(echo "$std_output" | parse_pct)
    echo "  [Standard] Cracked: $std_cracked / $actual_count ($std_pct%)"

    # --- Markov table ---
    mkv_dir="$TMPDIR/mkv_${coverage}"
    mkdir -p "$mkv_dir"

    # Confine Markov chains to the top (coverage)% of keyspace by probability.
    mkv_keyspace=$((KEYSPACE * coverage / 100))
    if [ "$mkv_keyspace" -lt 1 ]; then mkv_keyspace=1; fi

    echo "  [Markov] Generating (keyspace=$mkv_keyspace)..."
    "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$num_chains" 0 --markov "$MARKOV" --markov-keyspace "$mkv_keyspace" 2>&1 | tail -2 | sed 's/^/    /'
    mv ./*.rt "$mkv_dir/"

    mkv_rt_files=("$mkv_dir"/*.rt)
    mkv_rt="${mkv_rt_files[0]}"
    validate_rt_file "$mkv_rt" "Markov ${coverage}%"

    mkv_pre_sort=$(count_rt_records "$mkv_rt")
    echo "  [Markov] Pre-sort records: $mkv_pre_sort"

    "$BINDIR/crackalack_sort" "$mkv_rt" 2>&1 | tail -1 | sed 's/^/    /'

    mkv_post_sort=$(count_rt_records "$mkv_rt")
    validate_sort_order "$mkv_rt" "Markov ${coverage}%"

    mkv_merge_loss=0
    if [ "$num_chains" -gt 0 ]; then
        mkv_merge_loss=$(awk "BEGIN{printf \"%.1f\", ($num_chains - $mkv_post_sort) * 100.0 / $num_chains}")
    fi
    mkv_eff_coverage=$(awk "BEGIN{printf \"%.1f\", $mkv_post_sort * 100.0 / $KEYSPACE}")

    echo "  [Markov] Requested: $num_chains, After sort: $mkv_post_sort (${mkv_merge_loss}% merge loss)"
    echo "  [Markov] Effective coverage: ${mkv_eff_coverage}%"

    if awk "BEGIN{exit(!($mkv_merge_loss > 50))}"; then
        echo "  WARNING: Merge loss > 50% - chain_len may be too high for this keyspace"
    fi

    rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
    echo "  [Markov] Looking up hashes..."
    mkv_output=$(run_lookup "$mkv_dir" "$TMPDIR/hashes.txt" --markov "$MARKOV")
    mkv_cracked=$(echo "$mkv_output" | parse_cracked)
    mkv_pct=$(echo "$mkv_output" | parse_pct)
    echo "  [Markov] Cracked: $mkv_cracked / $actual_count ($mkv_pct%)"

    diff_count=$((mkv_cracked - std_cracked))
    diff_pp=$(awk "BEGIN{printf \"%.1f\", ($mkv_cracked - $std_cracked) * 100.0 / $actual_count}")
    if [ "$diff_count" -ge 0 ]; then
        echo "  Delta: +$diff_count (+${diff_pp}pp)"
    else
        echo "  Delta: $diff_count (${diff_pp}pp)"
    fi

    # Accumulate for summary
    SUMMARY_COVERAGE+=("$coverage")
    SUMMARY_STD_REQ+=("$num_chains")
    SUMMARY_STD_ACTUAL+=("$std_post_sort")
    SUMMARY_STD_CRACKED+=("$std_cracked")
    SUMMARY_MKV_REQ+=("$num_chains")
    SUMMARY_MKV_ACTUAL+=("$mkv_post_sort")
    SUMMARY_MKV_CRACKED+=("$mkv_cracked")
done

# --- Summary table ---
echo ""
echo "=== Summary ==="
echo ""
printf "%-10s  %-12s  %-14s  %-12s  %-14s  %s\n" \
    "Coverage" "Std Chains" "Std Cracked" "Mkv Chains" "Mkv Cracked" "Delta"
printf "%-10s  %-12s  %-14s  %-12s  %-14s  %s\n" \
    "--------" "----------" "-----------" "----------" "-----------" "-----"

for i in "${!SUMMARY_COVERAGE[@]}"; do
    cov="${SUMMARY_COVERAGE[$i]}"
    std_ch="${SUMMARY_STD_ACTUAL[$i]}"
    std_cr="${SUMMARY_STD_CRACKED[$i]}"
    mkv_ch="${SUMMARY_MKV_ACTUAL[$i]}"
    mkv_cr="${SUMMARY_MKV_CRACKED[$i]}"

    std_pct_val=$(awk "BEGIN{printf \"%.1f\", $std_cr * 100.0 / $actual_count}")
    mkv_pct_val=$(awk "BEGIN{printf \"%.1f\", $mkv_cr * 100.0 / $actual_count}")
    delta=$((mkv_cr - std_cr))
    delta_pp=$(awk "BEGIN{printf \"%.1f\", ($mkv_cr - $std_cr) * 100.0 / $actual_count}")

    if [ "$delta" -ge 0 ]; then
        delta_str="+$delta (+${delta_pp}pp)"
    else
        delta_str="$delta (${delta_pp}pp)"
    fi

    printf "%-10s  %-12s  %-14s  %-12s  %-14s  %s\n" \
        "${cov}%" \
        "$std_ch" \
        "$std_cr (${std_pct_val}%)" \
        "$mkv_ch" \
        "$mkv_cr (${mkv_pct_val}%)" \
        "$delta_str"
done

echo ""
echo "Done."
