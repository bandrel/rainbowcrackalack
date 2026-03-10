#!/bin/bash
set -euo pipefail

# End-to-end NTLM mask attack tests with mixed masks from 6 to 10 characters.
#
# Each test uses a different mixed mask combining ?l, ?u, ?d, ?s in varied
# positions. Generates rainbow tables, sorts them, then looks up known NTLM
# hashes to verify the full generate-sort-lookup pipeline.
#
# NTLM hashes verified with cpu_rt_functions.c ntlm_hash().
#
# Usage: ./test_mask_e2e.sh [path_to_binaries]
# Set TEST_TIMEOUT (seconds) to override the per-test timeout (default: 600).

BINDIR="$(cd "${1:-.}" && pwd)"
TEST_WORK_DIR=$(mktemp -d)
PASS=0
FAIL=0
TOTAL=0

cleanup() {
    rm -rf "$TEST_WORK_DIR"
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

# Prefer GNU timeout; fall back to gtimeout (macOS coreutils) or no-op wrapper
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
else
    echo "WARNING: neither timeout nor gtimeout found - per-test timeouts disabled"
fi

run_with_timeout() {
    local secs="$1"; shift
    if [ -n "${TIMEOUT_CMD:-}" ]; then
        "$TIMEOUT_CMD" "$secs" "$@"
    else
        "$@"
    fi
}

run_test() {
    local test_name="$1"
    local mask="$2"
    local pt_len="$3"
    local chain_len="$4"
    local num_chains="$5"
    local hash="$6"
    local expected_plaintext="$7"
    local warn_only="${8:-0}"

    TOTAL=$((TOTAL + 1))
    local table_dir="$TEST_WORK_DIR/test_${TOTAL}"
    mkdir -p "$table_dir"

    echo -n "  Test $TOTAL: $test_name ... "

    # Generate (run from BINDIR so kernel files in Metal/CL/ are found)
    if ! (cd "$BINDIR" && run_with_timeout "${TEST_TIMEOUT:-600}" "$gen" ntlm "$mask" "$pt_len" "$pt_len" 0 "$chain_len" "$num_chains" 0 >/dev/null 2>&1); then
        echo "FAIL (generation failed or timed out)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Move generated table to test dir (encode mask ? as % to match filename convention)
    local encoded_mask="${mask//\?/%}"
    mv "$BINDIR"/ntlm_"${encoded_mask}"*"#${pt_len}-${pt_len}"_*.rt "$table_dir/" 2>/dev/null || true

    # Sort
    local rt_file
    rt_file=$(find "$table_dir" -name '*.rt' | head -1)
    if [ -z "$rt_file" ]; then
        echo "FAIL (no .rt file generated)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! (cd "$BINDIR" && run_with_timeout "${TEST_TIMEOUT:-600}" "$sort_bin" "$rt_file" >/dev/null 2>&1); then
        echo "FAIL (sort failed or timed out)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Write hash file
    local hash_file="$table_dir/hashes.txt"
    echo "$hash" > "$hash_file"

    # Lookup
    local lookup_output
    # Delete stale precalc and pot files that cause false "already cracked"
    rm -f "$BINDIR"/rcracki.precalc.*
    rm -f "$BINDIR"/rainbowcrackalack_jtr.pot
    rm -f "$BINDIR"/rainbowcrackalack_hashcat.pot

    lookup_output=$(cd "$BINDIR" && run_with_timeout "${TEST_TIMEOUT:-600}" "$lookup" "$table_dir" "$hash_file" 2>&1) || true

    if echo "$lookup_output" | grep -qF "$expected_plaintext"; then
        echo "passed ($expected_plaintext)"
        PASS=$((PASS + 1))
    else
        if [ "$warn_only" = "1" ]; then
            echo "WARN (expected '$expected_plaintext' not found - probabilistic)"
        else
            echo "FAIL (expected '$expected_plaintext' not found in output)"
            echo "    Lookup output (last 10 lines):"
            echo "$lookup_output" | tail -10 | sed 's/^/    /'
            FAIL=$((FAIL + 1))
        fi
    fi
}

echo "NTLM mixed mask end-to-end tests (6-10 characters)"
echo "===================================================="
echo ""

# -----------------------------------------------------------------------
# 6-character masks (keyspace ~17M-151M)
# -----------------------------------------------------------------------
echo "--- 6-character masks ---"

# ?l?d?u?d?l?d  keyspace = 26*10*26*10*26*10 = 17,576,000
run_test "6-char ?l?d?u?d?l?d 'a1B2c3'" \
    '?l?d?u?d?l?d' 6 3000 300000 \
    "5d9faa80b88777d697a58fd01a84feee" "a1B2c3"

# ?u?l?d?s?l?d  keyspace = 26*26*10*33*26*10 = 58,000,800
run_test "6-char ?u?l?d?s?l?d 'Ab3!k9'" \
    '?u?l?d?s?l?d' 6 3000 300000 \
    "a600fdfbd0a347c67db34903b2da1af6" "Ab3!k9"

# ?l?u?d?s?l?u  keyspace = 26*26*10*33*26*26 = 150,802,080
run_test "6-char ?l?u?d?s?l?u 'xY2@pQ'" \
    '?l?u?d?s?l?u' 6 3000 250000 \
    "574930e97431ef21239d6fb42ff49426" "xY2@pQ"

# ?d?l?u?s?l?u  keyspace = 10*26*26*33*26*26 = 150,802,080
run_test "6-char ?d?l?u?s?l?u '7mN#bR'" \
    '?d?l?u?s?l?u' 6 3000 250000 \
    "38b2d4b9843b1c446e0f3eae6e08112b" "7mN#bR"

echo ""

# -----------------------------------------------------------------------
# 7-character masks (keyspace ~1.5B)
# -----------------------------------------------------------------------
echo "--- 7-character masks ---"

# ?l?u?d?s?l?u?d  keyspace = 26*26*10*33*26*26*10 = 1,508,020,800
run_test "7-char ?l?u?d?s?l?u?d 'aB1!cD2'" \
    '?l?u?d?s?l?u?d' 7 30000 2000000 \
    "86f539cc1ec1f73721ce9938db90b6b3" "aB1!cD2"

# ?d?l?u?s?l?d?u  keyspace = 10*26*26*33*26*10*26 = 1,508,020,800
run_test "7-char ?d?l?u?s?l?d?u '3xY@m5N'" \
    '?d?l?u?s?l?d?u' 7 30000 2000000 \
    "72d3ba850b7b563271ec54b7a8c879d8" "3xY@m5N"

# ?l?u?d?s?u?l?d  keyspace = 26*26*10*33*26*26*10 = 1,508,020,800
run_test "7-char ?l?u?d?s?u?l?d 'kR4#Ew7'" \
    '?l?u?d?s?u?l?d' 7 30000 2000000 \
    "c73aa634f794f4df57b9c4d1f90d4010" "kR4#Ew7"

# ?l?d?u?s?l?u?d  keyspace = 26*10*26*33*26*26*10 = 1,508,020,800
run_test "7-char ?l?d?u?s?l?u?d 'q5Z!hA3'" \
    '?l?d?u?s?l?u?d' 7 30000 2000000 \
    "88389b57a286040bd716e752794d0166" "q5Z!hA3"

echo ""

# -----------------------------------------------------------------------
# 8-character masks (digit-heavy to keep keyspace ~1.7-2.2B)
# -----------------------------------------------------------------------
echo "--- 8-character masks ---"

# ?d?d?l?d?u?d?l?d  keyspace = 10*10*26*10*26*10*26*10 = 1,757,600,000
run_test "8-char ?d?d?l?d?u?d?l?d '53a7B2c8'" \
    '?d?d?l?d?u?d?l?d' 8 30000 2000000 \
    "c8092f5687066e6c4b6e730de407bbd7" "53a7B2c8"

# ?l?d?u?d?d?d?s?d  keyspace = 26*10*26*10*10*10*33*10 = 2,230,800,000
run_test "8-char ?l?d?u?d?d?d?s?d 'r4K100!3'" \
    '?l?d?u?d?d?d?s?d' 8 30000 2000000 \
    "d532d89af0d15dc67934a3fcb513152c" "r4K100!3"

# ?d?l?d?u?d?s?d?l  keyspace = 10*26*10*26*10*33*10*26 = 5,800,080,000
run_test "8-char ?d?l?d?u?d?s?d?l '8m5R3#2k'" \
    '?d?l?d?u?d?s?d?l' 8 50000 3000000 \
    "2126d6b3ab2b484cae82be12a2e75261" "8m5R3#2k"

# ?u?d?l?d?s?d?d?l  keyspace = 26*10*26*10*33*10*10*26 = 5,800,080,000
run_test "8-char ?u?d?l?d?s?d?d?l 'T2b9!00f'" \
    '?u?d?l?d?s?d?d?l' 8 50000 3000000 \
    "4a091b47dd6548ce6180cba603920655" "T2b9!00f"

echo ""

# -----------------------------------------------------------------------
# 9-character masks (digit-heavy to keep keyspace manageable)
# -----------------------------------------------------------------------
echo "--- 9-character masks ---"

# ?d?d?d?d?d?d?d?d?d  keyspace = 10^9 = 1,000,000,000
run_test "9-char ?d^9 '123456789'" \
    '?d?d?d?d?d?d?d?d?d' 9 30000 2000000 \
    "c22b315c040ae6e0efee3518d830362b" "123456789"

# ?l?d?d?d?d?d?d?d?d  keyspace = 26*10^8 = 26,000,000,000
run_test "9-char ?l?d^8 'a12345678'" \
    '?l?d?d?d?d?d?d?d?d' 9 100000 5000000 \
    "1600177b90b941691a5058540729b42e" "a12345678"

# ?d?d?d?d?u?d?d?d?d  keyspace = 26*10^8 = 26,000,000,000
run_test "9-char ?d^4?u?d^4 '1234A5678'" \
    '?d?d?d?d?u?d?d?d?d' 9 100000 5000000 \
    "6daa691f8ac1168f2d4c315f04a5693b" "1234A5678"

# ?d?d?d?s?d?d?d?d?d  keyspace = 33*10^8 = 33,000,000,000
run_test "9-char ?d^3?s?d^5 '123#45678'" \
    '?d?d?d?s?d?d?d?d?d' 9 100000 5000000 \
    "80cc1be2d541e7808d1a0b65ae3eabcf" "123#45678"

echo ""

# -----------------------------------------------------------------------
# 10-character masks (digit-heavy to keep keyspace manageable)
# -----------------------------------------------------------------------
echo "--- 10-character masks ---"

# ?d?d?d?d?d?d?d?d?d?d  keyspace = 10^10 = 10,000,000,000
run_test "10-char ?d^10 '1234567890'" \
    '?d?d?d?d?d?d?d?d?d?d' 10 50000 3000000 \
    "8af326aa4850225b75c592d4ce19ccf5" "1234567890" 1

# ?u?d?d?d?d?d?d?d?d?d  keyspace = 26*10^9 = 26,000,000,000
run_test "10-char ?u?d^9 'A234567890'" \
    '?u?d?d?d?d?d?d?d?d?d' 10 100000 5000000 \
    "84e691a12eda8543b941dc4e95d1f835" "A234567890" 1

echo ""
echo "===================================================="
echo "Results: $PASS passed, $FAIL failed, $TOTAL total"

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "NOTE: Rainbow table lookups are probabilistic. A failure does not"
    echo "necessarily indicate a bug - the plaintext may not be in the table."
    echo "Re-running the test may produce different results."
    exit 1
fi
