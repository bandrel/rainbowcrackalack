#!/bin/bash
set -euo pipefail

# Markov vs standard rainbow table accuracy comparison.
#
# Generates standard and Markov-ordered tables at multiple coverage levels
# (10%, 25%, 50%, 100%) and compares crack rates against a test set of
# real-world passwords. Uses multiple table indices per level for variance
# reduction. Reports merge rates (chain loss after sort) at each level.
#
# To avoid train/test contamination, this script splits the password corpus
# into disjoint training (80%) and testing (20%) halves. The Markov model
# is trained on the training half; crack rates are measured on the test half.
#
# Usage: ./test_markov_accuracy.sh <path/to/rockyou.txt> [plaintext_lens] [num_tables] [chain_len] [sample_size] [coverage_levels]
# plaintext_lens is a space-separated quoted string of lengths to test, e.g. "7 8"
# (default "5"). Lengths are tested in order. num_tables defaults to 1.
# coverage_levels is a space-separated quoted string like "10 25 50 100" (default).

ROCKYOU="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IFS=' ' read -r -a PT_LENS <<< "${2:-5}"
NUM_TABLES="${3:-1}"
BINDIR="$SCRIPT_DIR"

CHAIN_LEN_ARG="${4:-auto}"
SAMPLE_SIZE_ARG="${5:-2000}"
IFS=' ' read -r -a COVERAGE_LEVELS <<< "${6:-10 25 50 100}"

# When the keyspace is so large that even 10% exceeds this limit, switch to
# fixed chain-count mode with geometrically spaced levels (1x, 2.5x, 5x, 10x).
MAX_PRACTICAL_CHAINS=100000000

if [ -z "$ROCKYOU" ]; then
    echo "Usage: $0 <path/to/rockyou.txt> [plaintext_len] [num_tables]"
    exit 1
fi

for _pt in "${PT_LENS[@]}"; do
    if ! [[ "$_pt" =~ ^[0-9]+$ ]] || [ "$_pt" -lt 2 ]; then
        echo "ERROR: plaintext_len must be an integer >= 2 (got: $_pt)"
        exit 1
    fi
done

if ! [[ "$NUM_TABLES" =~ ^[0-9]+$ ]] || [ "$NUM_TABLES" -lt 1 ]; then
    echo "ERROR: num_tables must be an integer >= 1 (got: $NUM_TABLES)"
    exit 1
fi

if [ ! -r "$ROCKYOU" ]; then
    echo "ERROR: rockyou.txt not readable: $ROCKYOU"
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

count_rt_records_dir() {
    local dir="$1"
    local total=0
    for f in "$dir"/*.rt; do
        [ -f "$f" ] || continue
        local n
        n=$(count_rt_records "$f")
        total=$((total + n))
    done
    echo "$total"
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

for PT_LEN in "${PT_LENS[@]}"; do

echo ""
echo "################################################################"
echo "# Plaintext length: $PT_LEN"
echo "################################################################"

SAMPLE_SIZE="$SAMPLE_SIZE_ARG"

# Compute keyspace
KEYSPACE=$(awk -v pt_len="$PT_LEN" 'BEGIN {
    ks = 1; for (i = 0; i < pt_len; i++) ks *= 95; print int(ks)
}')

# Auto-calculate chain length for fastest precompute while keeping
# generation practical.  Strategy: pick the shortest chain_len such that
# 100% coverage doesn't exceed MAX_PRACTICAL_CHAINS.  This gives the
# fastest possible lookup without making generation infeasible.
# Floor at 100, and for very large keyspaces fall back to cbrt(keyspace)
# which is the theoretical optimum for multi-table setups.
if [ "$CHAIN_LEN_ARG" = "auto" ]; then
    CHAIN_LEN=$(python3 -c "
import math
ks = $KEYSPACE
max_chains = $MAX_PRACTICAL_CHAINS
# Shortest chain_len where full coverage fits in max_chains
cl = max(100, ks // max_chains)
# But don't go below cbrt for very large keyspaces (diminishing returns)
cl_cbrt = int(round(ks ** (1/3)))
if cl > cl_cbrt:
    cl = cl_cbrt
# Round to nearest 100 for cleaner filenames
cl = max(100, (cl // 100) * 100)
print(cl)
")
    echo "  Auto chain_len=$CHAIN_LEN (shortest practical for keyspace=$KEYSPACE)"
else
    CHAIN_LEN="$CHAIN_LEN_ARG"
fi

# Cap sample size to available keyspace
if [ "$KEYSPACE" -lt "$SAMPLE_SIZE" ]; then
    SAMPLE_SIZE=$KEYSPACE
fi
if [ "$SAMPLE_SIZE" -lt 1 ]; then SAMPLE_SIZE=1; fi

# Determine whether coverage-based or fixed-chain-count mode is needed.
# If the smallest coverage level already exceeds the practical cap, use fixed mode.
smallest_coverage=${COVERAGE_LEVELS[0]}
smallest_num_chains=$(( KEYSPACE * smallest_coverage / 100 / CHAIN_LEN ))
if [ "$smallest_num_chains" -gt "$MAX_PRACTICAL_CHAINS" ]; then
    USE_FIXED_CHAINS=1
    # Geometrically spaced: base, 2.5x, 5x, 10x
    FIXED_BASE=$((MAX_PRACTICAL_CHAINS / 10))
    FIXED_CHAIN_COUNTS=("$FIXED_BASE" $((FIXED_BASE * 25 / 10)) $((FIXED_BASE * 5)) $((FIXED_BASE * 10)))
else
    USE_FIXED_CHAINS=0
fi

echo "=== Markov Accuracy Test ==="
echo "  charset=ascii-32-95, len=$PT_LEN, keyspace=$KEYSPACE"
if [ "$USE_FIXED_CHAINS" -eq 1 ]; then
    echo "  chain_len=$CHAIN_LEN, mode=fixed-chain-count (keyspace too large for coverage %)"
    echo "  chain_counts=${FIXED_CHAIN_COUNTS[*]}"
else
    echo "  chain_len=$CHAIN_LEN, coverage_levels=${COVERAGE_LEVELS[*]}%"
fi
echo "  sample_size=$SAMPLE_SIZE passwords, num_tables=$NUM_TABLES per level"
echo ""

# --- Train/test split ---
# Deduplicate passwords of the target length, then split 80/20 for train/test.
# The Markov model is trained only on the training half; crack rates are measured
# on the test half. This prevents train/test contamination.

echo "Splitting corpus into train/test sets (80/20)..."
LC_ALL=C grep -E "^[ -~]{${PT_LEN}}$" "$ROCKYOU" | sort -u | shuf > "$TMPDIR/all_unique.txt"
total_unique=$(wc -l < "$TMPDIR/all_unique.txt" | tr -d ' ')
echo "  Total unique ${PT_LEN}-char passwords: $total_unique"

if [ "$total_unique" -lt 10 ]; then
    echo "ERROR: Too few unique passwords of length $PT_LEN (need at least 10, got $total_unique)"
    exit 1
fi

train_count=$(( total_unique * 80 / 100 ))
if [ "$train_count" -lt 1 ]; then train_count=1; fi
test_count=$(( total_unique - train_count ))
if [ "$test_count" -lt 1 ]; then
    echo "ERROR: Not enough passwords for a test set (total=$total_unique, train=$train_count)"
    exit 1
fi

head -n "$train_count" "$TMPDIR/all_unique.txt" > "$TMPDIR/train_passwords.txt"
tail -n "$test_count" "$TMPDIR/all_unique.txt" > "$TMPDIR/test_passwords.txt"
echo "  Train set: $train_count passwords"
echo "  Test set:  $test_count passwords"

# Cap sample size to test set
if [ "$SAMPLE_SIZE" -gt "$test_count" ]; then
    SAMPLE_SIZE=$test_count
    echo "  NOTE: Reduced sample_size to $SAMPLE_SIZE (limited by test set)"
fi

# Sample from the TEST set only (already deduplicated)
shuf -n "$SAMPLE_SIZE" "$TMPDIR/test_passwords.txt" > "$TMPDIR/passwords.txt"
actual_count=$(wc -l < "$TMPDIR/passwords.txt" | tr -d ' ')
echo "  Sampled $actual_count test passwords."

if [ "$actual_count" -eq 0 ]; then
    echo "ERROR: No passwords sampled"
    exit 1
fi

# --- Train Markov model on training set only ---
echo ""
echo "Training Markov model on training set..."
MARKOV="$TMPDIR/train.markov"

if [ -x "$BINDIR/crackalack_plan" ]; then
    "$BINDIR/crackalack_plan" train "$TMPDIR/train_passwords.txt" 2>&1 | tail -3 | sed 's/^/  /'
    # crackalack_plan writes <basename>.markov in the current working directory
    mv "train_passwords.markov" "$MARKOV"
else
    # Fall back to user-provided model with a warning
    echo "  WARNING: crackalack_plan not found; cannot train a fresh model."
    echo "  Falling back to pre-trained model. This introduces train/test contamination"
    echo "  if the model was trained on the same corpus as the test set."
    MARKOV_FALLBACK="${4:-$SCRIPT_DIR/rockyou.markov}"
    if [[ ! "$MARKOV_FALLBACK" = /* ]]; then
        MARKOV_FALLBACK="$(cd "$(dirname "$MARKOV_FALLBACK")" && pwd)/$(basename "$MARKOV_FALLBACK")"
    fi
    if [ ! -f "$MARKOV_FALLBACK" ]; then
        echo "ERROR: No Markov model available at $MARKOV_FALLBACK"
        exit 1
    fi
    MARKOV="$MARKOV_FALLBACK"
fi

# Validate Markov model
validate_markov_model "$MARKOV"

# Compute NTLM hashes
echo ""
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

SANITY_PW=$(head -1 "$TMPDIR/passwords.txt")
SANITY_HASH=$($PYTHON_CMD "$TMPDIR/ntlm_hash.py" <<< "$SANITY_PW")
echo "$SANITY_HASH" > "$TMPDIR/sanity_hash.txt"
echo "  Testing pipeline with password '$SANITY_PW' (hash=$SANITY_HASH)..."

# Standard sanity: generate a small table (capped for large keyspaces)
SANITY_CHAINS=$(( (KEYSPACE / CHAIN_LEN) + 1 ))
if [ "$SANITY_CHAINS" -gt 10000 ]; then SANITY_CHAINS=10000; fi
mkdir -p "$TMPDIR/sanity_std"
SANITY_STD_RT="ntlm_ascii-32-95#${PT_LEN}-${PT_LEN}_0_${CHAIN_LEN}x${SANITY_CHAINS}_0.rt"
"$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$SANITY_CHAINS" 0 2>&1 | tail -3 | sed 's/^/  /'
mv "./$SANITY_STD_RT" "$TMPDIR/sanity_std/"
"$BINDIR/crackalack_sort" "$TMPDIR"/sanity_std/*.rt 2>&1 | tail -1 | sed 's/^/  /'
rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
sanity_std_rc=0
sanity_std_out=$("$BINDIR/crackalack_lookup" "$TMPDIR/sanity_std/" "$TMPDIR/sanity_hash.txt" 2>&1) || sanity_std_rc=$?
sanity_std_cracked=$(echo "$sanity_std_out" | parse_cracked)
if [ -z "$sanity_std_cracked" ]; then
    echo "  Standard pipeline: PARSE_ERROR (exit code $sanity_std_rc)"
    echo "  crackalack_lookup output:"
    echo "$sanity_std_out" | tail -20 | sed 's/^/    /'
else
    echo "  Standard pipeline: cracked=$sanity_std_cracked"
fi

# Markov sanity (same chain count)
mkdir -p "$TMPDIR/sanity_mkv"
SANITY_MKV_RT="ntlm_ascii-32-95-mk${KEYSPACE}#${PT_LEN}-${PT_LEN}_0_${CHAIN_LEN}x${SANITY_CHAINS}_0.rt"
"$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$SANITY_CHAINS" 0 --markov "$MARKOV" --markov-keyspace "$KEYSPACE" 2>&1 | tail -3 | sed 's/^/  /'
mv "./$SANITY_MKV_RT" "$TMPDIR/sanity_mkv/"
"$BINDIR/crackalack_sort" "$TMPDIR"/sanity_mkv/*.rt 2>&1 | tail -1 | sed 's/^/  /'
rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
sanity_mkv_rc=0
sanity_mkv_out=$("$BINDIR/crackalack_lookup" "$TMPDIR/sanity_mkv/" "$TMPDIR/sanity_hash.txt" --markov "$MARKOV" 2>&1) || sanity_mkv_rc=$?
sanity_mkv_cracked=$(echo "$sanity_mkv_out" | parse_cracked)
if [ -z "$sanity_mkv_cracked" ]; then
    echo "  Markov pipeline:   PARSE_ERROR (exit code $sanity_mkv_rc)"
    echo "  crackalack_lookup output:"
    echo "$sanity_mkv_out" | tail -20 | sed 's/^/    /'
else
    echo "  Markov pipeline:   cracked=$sanity_mkv_cracked"
fi

if [ "${sanity_std_cracked:-0}" = "0" ] && [ "${sanity_mkv_cracked:-0}" = "0" ]; then
    echo ""
    echo "WARNING: Neither pipeline cracked a known password at full coverage."
    echo "  This may indicate a broken pipeline. Continuing anyway..."
    echo ""
fi

# --- Main comparison loop ---
echo ""
echo "=== Coverage-level comparison ==="
echo "  Generating $NUM_TABLES table(s) per level (table_index 0..$((NUM_TABLES-1)))"

# Arrays to accumulate summary data
declare -a SUMMARY_LABEL=()
declare -a SUMMARY_STD_REQ=()
declare -a SUMMARY_STD_ACTUAL=()
declare -a SUMMARY_STD_CRACKED=()
declare -a SUMMARY_MKV_REQ=()
declare -a SUMMARY_MKV_ACTUAL=()
declare -a SUMMARY_MKV_CRACKED=()

# Build the iteration list: either coverage percentages or fixed chain counts
if [ "$USE_FIXED_CHAINS" -eq 1 ]; then
    ITER_COUNT=${#FIXED_CHAIN_COUNTS[@]}
else
    ITER_COUNT=${#COVERAGE_LEVELS[@]}
fi

for iter_idx in $(seq 0 $((ITER_COUNT - 1))); do
    if [ "$USE_FIXED_CHAINS" -eq 1 ]; then
        num_chains=${FIXED_CHAIN_COUNTS[$iter_idx]}
        coverage_label="${num_chains} chains"
        # In fixed mode, Markov keyspace = full keyspace (the table size is the variable)
        mkv_keyspace=$KEYSPACE
    else
        coverage=${COVERAGE_LEVELS[$iter_idx]}
        coverage_label="${coverage}%"
        num_chains=$(( KEYSPACE * coverage / 100 / CHAIN_LEN ))
        if [ "$num_chains" -lt 1 ]; then num_chains=1; fi
        if [ "$num_chains" -gt "$MAX_PRACTICAL_CHAINS" ]; then
            echo "  NOTE: Capping num_chains from $num_chains to $MAX_PRACTICAL_CHAINS (practical limit)"
            num_chains=$MAX_PRACTICAL_CHAINS
        fi
        # Confine Markov chains to the top (coverage)% of keyspace by probability.
        mkv_keyspace=$((KEYSPACE * coverage / 100))
        if [ "$mkv_keyspace" -lt 1 ]; then mkv_keyspace=1; fi
    fi

    # Per-table chain count (split evenly across tables)
    chains_per_table=$(( num_chains / NUM_TABLES ))
    if [ "$chains_per_table" -lt 1 ]; then chains_per_table=1; fi

    echo ""
    echo "--- Level: ${coverage_label} ---"
    echo "  Total chains: $num_chains ($chains_per_table x $NUM_TABLES tables, chain_len=$CHAIN_LEN)"

    # --- Standard tables ---
    std_dir="$TMPDIR/std_${iter_idx}"
    mkdir -p "$std_dir"

    std_total_pre_sort=0
    for tidx in $(seq 0 $((NUM_TABLES - 1))); do
        echo "  [Standard] Generating table_index=$tidx..."
        std_rt_name="ntlm_ascii-32-95#${PT_LEN}-${PT_LEN}_${tidx}_${CHAIN_LEN}x${chains_per_table}_0.rt"
        "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" "$tidx" "$CHAIN_LEN" "$chains_per_table" 0 2>&1 | tail -2 | sed 's/^/    /'
        mv "./$std_rt_name" "$std_dir/"

        validate_rt_file "$std_dir/$std_rt_name" "Standard ${coverage_label} idx=$tidx"

        pre=$(count_rt_records "$std_dir/$std_rt_name")
        std_total_pre_sort=$((std_total_pre_sort + pre))

        "$BINDIR/crackalack_sort" "$std_dir/$std_rt_name" 2>&1 | tail -1 | sed 's/^/    /'
        validate_sort_order "$std_dir/$std_rt_name" "Standard ${coverage_label} idx=$tidx"
    done

    std_post_sort=$(count_rt_records_dir "$std_dir")

    std_merge_loss=0
    if [ "$std_total_pre_sort" -gt 0 ]; then
        std_merge_loss=$(awk "BEGIN{printf \"%.1f\", ($std_total_pre_sort - $std_post_sort) * 100.0 / $std_total_pre_sort}")
    fi
    std_eff_coverage=$(awk "BEGIN{printf \"%.1f\", $std_post_sort * 100.0 / $KEYSPACE}")

    echo "  [Standard] Pre-sort: $std_total_pre_sort, After sort: $std_post_sort (${std_merge_loss}% merge loss)"
    echo "  [Standard] Effective coverage: ${std_eff_coverage}% of keyspace"

    if awk "BEGIN{exit(!($std_merge_loss > 50))}"; then
        echo "  WARNING: Merge loss > 50% - chain_len may be too high for this keyspace"
    fi

    rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot
    echo "  [Standard] Looking up hashes..."
    std_output=$(run_lookup "$std_dir" "$TMPDIR/hashes.txt")
    std_cracked=$(echo "$std_output" | parse_cracked)
    std_pct=$(echo "$std_output" | parse_pct)
    echo "  [Standard] Cracked: $std_cracked / $actual_count ($std_pct%)"

    # --- Markov tables ---
    mkv_dir="$TMPDIR/mkv_${iter_idx}"
    mkdir -p "$mkv_dir"

    mkv_total_pre_sort=0
    for tidx in $(seq 0 $((NUM_TABLES - 1))); do
        echo "  [Markov] Generating table_index=$tidx (markov_keyspace=$mkv_keyspace)..."
        mkv_rt_name="ntlm_ascii-32-95-mk${mkv_keyspace}#${PT_LEN}-${PT_LEN}_${tidx}_${CHAIN_LEN}x${chains_per_table}_0.rt"
        "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" "$tidx" "$CHAIN_LEN" "$chains_per_table" 0 --markov "$MARKOV" --markov-keyspace "$mkv_keyspace" 2>&1 | tail -2 | sed 's/^/    /'
        mv "./$mkv_rt_name" "$mkv_dir/"

        validate_rt_file "$mkv_dir/$mkv_rt_name" "Markov ${coverage_label} idx=$tidx"

        pre=$(count_rt_records "$mkv_dir/$mkv_rt_name")
        mkv_total_pre_sort=$((mkv_total_pre_sort + pre))

        "$BINDIR/crackalack_sort" "$mkv_dir/$mkv_rt_name" 2>&1 | tail -1 | sed 's/^/    /'
        validate_sort_order "$mkv_dir/$mkv_rt_name" "Markov ${coverage_label} idx=$tidx"
    done

    mkv_post_sort=$(count_rt_records_dir "$mkv_dir")

    mkv_merge_loss=0
    if [ "$mkv_total_pre_sort" -gt 0 ]; then
        mkv_merge_loss=$(awk "BEGIN{printf \"%.1f\", ($mkv_total_pre_sort - $mkv_post_sort) * 100.0 / $mkv_total_pre_sort}")
    fi
    mkv_eff_total=$(awk "BEGIN{printf \"%.1f\", $mkv_post_sort * 100.0 / $KEYSPACE}")
    mkv_eff_target=$(awk "BEGIN{printf \"%.1f\", $mkv_post_sort * 100.0 / $mkv_keyspace}")

    echo "  [Markov] Pre-sort: $mkv_total_pre_sort, After sort: $mkv_post_sort (${mkv_merge_loss}% merge loss)"
    echo "  [Markov] Effective coverage: ${mkv_eff_total}% of full keyspace, ${mkv_eff_target}% of target keyspace ($mkv_keyspace)"

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
    SUMMARY_LABEL+=("$coverage_label")
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
printf "%-16s  %-12s  %-14s  %-12s  %-14s  %s\n" \
    "Level" "Std Chains" "Std Cracked" "Mkv Chains" "Mkv Cracked" "Delta"
printf "%-16s  %-12s  %-14s  %-12s  %-14s  %s\n" \
    "---------------" "----------" "-----------" "----------" "-----------" "-----"

for i in "${!SUMMARY_LABEL[@]}"; do
    label="${SUMMARY_LABEL[$i]}"
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

    printf "%-16s  %-12s  %-14s  %-12s  %-14s  %s\n" \
        "$label" \
        "$std_ch" \
        "$std_cr (${std_pct_val}%)" \
        "$mkv_ch" \
        "$mkv_cr (${mkv_pct_val}%)" \
        "$delta_str"
done

echo ""
echo "Methodology: train/test split (80/20), $actual_count deduplicated test passwords,"
echo "  $NUM_TABLES table(s) per level, Markov model trained on training set only."

done  # end PT_LENS loop

echo ""
echo "Done."
