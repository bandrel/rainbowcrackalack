#!/bin/bash
set -euo pipefail

# End-to-end NTLM mask attack tests using mixed 6-character masks.
#
# Each mask combines different character classes per position to exercise
# the mask charset dispatch in GPU kernels. Generates small rainbow tables,
# sorts them, then looks up known NTLM hashes.
#
# NTLM hashes verified with cpu_rt_functions.c ntlm_hash():
#   a1B2c3 -> 5d9faa80b88777d697a58fd01a84feee  (?l?d?u?d?l?d)
#   Ab3!k9 -> a600fdfbd0a347c67db34903b2da1af6  (?u?l?d?s?l?d)
#   xY2@pQ -> 574930e97431ef21239d6fb42ff49426  (?l?u?d?s?l?u)
#   7mN#bR -> 38b2d4b9843b1c446e0f3eae6e08112b  (?d?l?u?s?l?u)
#
# Usage: ./test_mask_e2e.sh [path_to_binaries]

SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"
BINDIR="$(cd "${1:-.}" && pwd)"
TMPDIR=$(mktemp -d)
PASS=0
FAIL=0
TOTAL=0

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

gen="$BINDIR/crackalack_gen"
sort_bin="$BINDIR/crackalack_sort"
lookup="$BINDIR/crackalack_lookup"

for bin in "$gen" "$sort_bin" "$lookup"; do
    if [ ! -x "$bin" ]; then
        echo "ERROR: $bin not found or not executable"
        exit 1
    fi
done

run_test() {
    local test_name="$1"
    local mask="$2"
    local chain_len="$3"
    local num_chains="$4"
    local hash="$5"
    local expected_plaintext="$6"

    TOTAL=$((TOTAL + 1))
    local table_dir="$TMPDIR/test_${TOTAL}"
    mkdir -p "$table_dir"

    echo -n "  Test $TOTAL: $test_name ... "

    # Generate (run from BINDIR so kernel files in Metal/CL/ are found)
    if ! (cd "$BINDIR" && "$gen" ntlm "$mask" 6 6 0 "$chain_len" "$num_chains" 0 >/dev/null 2>&1); then
        echo "FAIL (generation failed)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Move generated table to test dir
    mv "$BINDIR"/ntlm_*#6-6_*.rt "$table_dir/" 2>/dev/null || true

    # Sort
    local rt_file
    rt_file=$(find "$table_dir" -name '*.rt' | head -1)
    if [ -z "$rt_file" ]; then
        echo "FAIL (no .rt file generated)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! (cd "$BINDIR" && "$sort_bin" "$rt_file" >/dev/null 2>&1); then
        echo "FAIL (sort failed)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Write hash file
    local hash_file="$table_dir/hashes.txt"
    echo "$hash" > "$hash_file"

    # Delete stale precalc and pot files that cause false "already cracked"
    rm -f "$BINDIR"/rcracki.precalc.*
    rm -f "$BINDIR"/rainbowcrackalack_jtr.pot
    rm -f "$BINDIR"/rainbowcrackalack_hashcat.pot

    # Lookup
    local lookup_output
    lookup_output=$(cd "$BINDIR" && "$lookup" "$table_dir" "$hash_file" 2>&1) || true

    if echo "$lookup_output" | grep -qi "$expected_plaintext"; then
        echo "passed ($expected_plaintext)"
        PASS=$((PASS + 1))
    else
        echo "FAIL (expected '$expected_plaintext' not found in output)"
        echo "    Lookup output (last 10 lines):"
        echo "$lookup_output" | tail -10 | sed 's/^/    /'
        FAIL=$((FAIL + 1))
    fi
}

echo "NTLM 6-character mixed mask end-to-end tests"
echo "=============================================="
echo ""

# Test 1: ?l?d?u?d?l?d - lower, digit, upper, digit, lower, digit
# keyspace = 26 * 10 * 26 * 10 * 26 * 10 = 175,760,000
# "a1B2c3" -> 5d9faa80b88777d697a58fd01a84feee
# 300K chains * chain_len 3000 = 900M coverage (~5x keyspace)
echo "Test 1: ?l?d?u?d?l?d (keyspace=175,760,000)"
run_test "lookup 'a1B2c3'" \
    '?l?d?u?d?l?d' 3000 300000 \
    "5d9faa80b88777d697a58fd01a84feee" "a1B2c3"

# Test 2: ?u?l?d?s?l?d - upper, lower, digit, symbol, lower, digit
# keyspace = 26 * 26 * 10 * 33 * 26 * 10 = 58,094,400
# "Ab3!k9" -> a600fdfbd0a347c67db34903b2da1af6
# 300K chains * chain_len 3000 = 900M coverage (~15x keyspace)
echo ""
echo "Test 2: ?u?l?d?s?l?d (keyspace=58,094,400)"
run_test "lookup 'Ab3!k9'" \
    '?u?l?d?s?l?d' 3000 300000 \
    "a600fdfbd0a347c67db34903b2da1af6" "Ab3!k9"

# Test 3: ?l?u?d?s?l?u - lower, upper, digit, symbol, lower, upper
# keyspace = 26 * 26 * 10 * 33 * 26 * 26 = 150,645,040 -- wait
# Actually: 26 * 26 * 10 * 33 * 26 * 26 = 26*26*10*33*26*26
# = 676 * 10 * 33 * 676 = 676 * 330 * 676 = 150,766,080
# "xY2@pQ" -> 574930e97431ef21239d6fb42ff49426
# 250K chains * chain_len 3000 = 750M coverage (~5x keyspace)
echo ""
echo "Test 3: ?l?u?d?s?l?u (keyspace=150,766,080)"
run_test "lookup 'xY2@pQ'" \
    '?l?u?d?s?l?u' 3000 250000 \
    "574930e97431ef21239d6fb42ff49426" "xY2@pQ"

# Test 4: ?d?l?u?s?l?u - digit, lower, upper, symbol, lower, upper
# keyspace = 10 * 26 * 26 * 33 * 26 * 26 = 58,064,240 -- let me compute
# 10 * 26 * 26 * 33 * 26 * 26 = 10 * 676 * 33 * 676 = 10 * 22308 * 676
# = 10 * 15,080,208 = 150,802,080... no:
# 10*26=260, 260*26=6760, 6760*33=223080, 223080*26=5800080, 5800080*26=150802080
# "7mN#bR" -> 38b2d4b9843b1c446e0f3eae6e08112b
# 250K chains * chain_len 3000 = 750M coverage (~5x keyspace)
echo ""
echo "Test 4: ?d?l?u?s?l?u (keyspace=150,802,080)"
run_test "lookup '7mN#bR'" \
    '?d?l?u?s?l?u' 3000 250000 \
    "38b2d4b9843b1c446e0f3eae6e08112b" "7mN#bR"

echo ""
echo "=============================================="
echo "Results: $PASS passed, $FAIL failed, $TOTAL total"

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "NOTE: Rainbow table lookups are probabilistic. A failure does not"
    echo "necessarily indicate a bug - the plaintext may not be in the table."
    echo "Re-running the test may produce different results."
    exit 1
fi
