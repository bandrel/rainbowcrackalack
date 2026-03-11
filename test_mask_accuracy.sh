#!/bin/bash
set -euo pipefail

# Mask vs standard rainbow table accuracy comparison.
#
# Generates a mask-based table and a standard (ascii-32-95) table with
# identical chain count and length, then compares crack rates against
# passwords sampled from a wordlist that match the mask pattern.
#
# Usage: ./test_mask_accuracy.sh <path/to/wordlist> <mask> [num_chains] [chain_len]
#
# Examples:
#   ./test_mask_accuracy.sh rockyou.txt '?l?l?l?l'
#   ./test_mask_accuracy.sh rockyou.txt '?l?l?d?d' 1000 10000
#
# If num_chains is omitted, it is auto-scaled to ~100% mask keyspace coverage.

WORDLIST="${1:-}"
MASK="${2:-}"
CHAIN_LEN="${4:-100000}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINDIR="$SCRIPT_DIR"
SAMPLE_SIZE=500

if [ -z "$WORDLIST" ] || [ -z "$MASK" ]; then
    echo "Usage: $0 <path/to/wordlist> <mask> [num_chains] [chain_len]"
    echo ""
    echo "Masks use hashcat syntax: ?l=lower ?u=upper ?d=digit ?s=special ?a=all printable"
    echo "Examples: ?l?l?l?l  ?l?l?d?d  ?u?l?l?l?d?d"
    exit 1
fi

if [ ! -r "$WORDLIST" ]; then
    echo "ERROR: wordlist not readable: $WORDLIST"
    exit 1
fi

for bin in "$BINDIR/crackalack_gen" "$BINDIR/crackalack_sort" "$BINDIR/crackalack_lookup"; do
    if [ ! -x "$bin" ]; then
        echo "ERROR: $bin not found or not executable"
        exit 1
    fi
done

# Derive mask length and grep pattern from the mask string.
# Each ?X is one position; literal chars are one position.
mask_to_grep_pattern() {
    local m="$1"
    local pattern=""
    local i=0
    local len=${#m}
    while [ "$i" -lt "$len" ]; do
        local ch="${m:$i:1}"
        if [ "$ch" = "?" ] && [ $((i + 1)) -lt "$len" ]; then
            local next="${m:$((i+1)):1}"
            case "$next" in
                l) pattern+="[a-z]" ;;
                u) pattern+="[A-Z]" ;;
                d) pattern+="[0-9]" ;;
                s) pattern+='[[:space:]!"#$%&'"'"'()*+,\-./:;<=>?@\[\\\]^_`{|}~]' ;;
                a) pattern+="[ -~]" ;;
                *) echo "ERROR: unsupported mask token: ?$next" >&2; exit 1 ;;
            esac
            i=$((i + 2))
        else
            # Literal character - escape for grep
            pattern+=$(printf '[%s]' "$ch")
            i=$((i + 1))
        fi
    done
    echo "^${pattern}$"
}

mask_length() {
    local m="$1"
    local count=0
    local i=0
    local len=${#m}
    while [ "$i" -lt "$len" ]; do
        if [ "${m:$i:1}" = "?" ] && [ $((i + 1)) -lt "$len" ]; then
            i=$((i + 2))
        else
            i=$((i + 1))
        fi
        count=$((count + 1))
    done
    echo "$count"
}

mask_keyspace() {
    local m="$1"
    local ks=1
    local i=0
    local len=${#m}
    while [ "$i" -lt "$len" ]; do
        local ch="${m:$i:1}"
        if [ "$ch" = "?" ] && [ $((i + 1)) -lt "$len" ]; then
            local next="${m:$((i+1)):1}"
            case "$next" in
                l) ks=$((ks * 26)) ;;
                u) ks=$((ks * 26)) ;;
                d) ks=$((ks * 10)) ;;
                s) ks=$((ks * 33)) ;;
                a) ks=$((ks * 95)) ;;
                *) echo "ERROR: unsupported mask token: ?$next" >&2; exit 1 ;;
            esac
            i=$((i + 2))
        else
            ks=$((ks * 1))
            i=$((i + 1))
        fi
    done
    echo "$ks"
}

PT_LEN=$(mask_length "$MASK")
MASK_KS=$(mask_keyspace "$MASK")
GREP_PATTERN=$(mask_to_grep_pattern "$MASK")

# Standard keyspace for ascii-32-95 at the same length
STANDARD_KS=$(awk -v pt_len="$PT_LEN" 'BEGIN { ks=1; for(i=0;i<pt_len;i++) ks*=95; print int(ks) }')

# Auto-scale num_chains for ~100% mask keyspace coverage if not provided
if [ -n "${3:-}" ]; then
    NUM_CHAINS="$3"
else
    NUM_CHAINS=$(awk -v ks="$MASK_KS" -v cl="$CHAIN_LEN" 'BEGIN { n=int(ks/cl)+1; if(n<1) n=1; print n }')
fi

echo "=== Mask Accuracy Test ==="
echo ""
echo "  Mask:             $MASK"
echo "  Plaintext length: $PT_LEN"
echo "  Mask keyspace:    $MASK_KS"
echo "  Standard keyspace (ascii-32-95): $STANDARD_KS"
echo "  Chain length:     $CHAIN_LEN"
echo "  Num chains:       $NUM_CHAINS"
echo "  Grep pattern:     $GREP_PATTERN"
echo ""

TMPDIR=$(mktemp -d)
cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

ln -sfn "$BINDIR/Metal" "$TMPDIR/Metal" 2>/dev/null || true
ln -sfn "$BINDIR/CL" "$TMPDIR/CL" 2>/dev/null || true
ln -sfn "$BINDIR/shared.h" "$TMPDIR/shared.h" 2>/dev/null || true

# Sample passwords matching the mask from the wordlist
echo "Sampling up to $SAMPLE_SIZE passwords matching $MASK from wordlist..."
LC_ALL=C grep -E "$GREP_PATTERN" "$WORDLIST" | shuf -n "$SAMPLE_SIZE" > "$TMPDIR/passwords.txt"
actual_count=$(wc -l < "$TMPDIR/passwords.txt" | tr -d ' ')
echo "  Got $actual_count passwords."

if [ "$actual_count" -eq 0 ]; then
    echo "ERROR: no passwords in wordlist match the mask pattern."
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
    uv run python3 "$TMPDIR/ntlm_hash.py" < "$TMPDIR/passwords.txt" > "$TMPDIR/hashes.txt"
else
    python3 "$TMPDIR/ntlm_hash.py" < "$TMPDIR/passwords.txt" > "$TMPDIR/hashes.txt"
fi

# --- Standard table (ascii-32-95) ---
echo ""
echo "Generating standard table (ascii-32-95, chain_len=$CHAIN_LEN, num_chains=$NUM_CHAINS)..."
mkdir -p "$TMPDIR/standard_tables"
(cd "$TMPDIR" && "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$NUM_CHAINS" 0)
mv "$TMPDIR"/*.rt "$TMPDIR/standard_tables/"

echo "Sorting standard table..."
"$BINDIR/crackalack_sort" "$TMPDIR"/standard_tables/*.rt

rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot

echo "Looking up hashes against standard table..."
standard_output=$(cd "$TMPDIR" && "$BINDIR/crackalack_lookup" "$TMPDIR/standard_tables/" "$TMPDIR/hashes.txt" 2>&1) || true
rm -f "$TMPDIR"/rcracki.precalc.* "$TMPDIR"/rainbowcrackalack_*.pot

# --- Mask table ---
echo ""
echo "Generating mask table ($MASK, chain_len=$CHAIN_LEN, num_chains=$NUM_CHAINS)..."
mkdir -p "$TMPDIR/mask_tables"
(cd "$TMPDIR" && "$BINDIR/crackalack_gen" ntlm "$MASK" "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$NUM_CHAINS" 0)
mv "$TMPDIR"/*.rt "$TMPDIR/mask_tables/"

echo "Sorting mask table..."
"$BINDIR/crackalack_sort" "$TMPDIR"/mask_tables/*.rt

echo "Looking up hashes against mask table..."
mask_output=$(cd "$TMPDIR" && "$BINDIR/crackalack_lookup" "$TMPDIR/mask_tables/" "$TMPDIR/hashes.txt" 2>&1) || true

# --- Parse results ---
parse_cracked() {
    awk '/were cracked, or/{print $6} /No hashes were cracked/{print "0"}'
}
parse_pct() {
    awk '/were cracked, or/{gsub(/%\.?$/,"",$NF); print $NF} /No hashes were cracked/{print "0.00"}'
}

standard_cracked=$(echo "$standard_output" | parse_cracked)
mask_cracked=$(echo "$mask_output" | parse_cracked)

if [ -z "$standard_cracked" ] || [ -z "$mask_cracked" ]; then
    echo ""
    echo "ERROR: Could not parse lookup summary lines."
    echo ""
    echo "Standard lookup output (last 5 lines):"
    echo "$standard_output" | tail -5 | sed 's/^/  /'
    echo ""
    echo "Mask lookup output (last 5 lines):"
    echo "$mask_output" | tail -5 | sed 's/^/  /'
    exit 1
fi

standard_pct=$(echo "$standard_output" | parse_pct)
mask_pct=$(echo "$mask_output" | parse_pct)

diff_count=$((mask_cracked - standard_cracked))
diff_pct=$(awk "BEGIN{printf \"%.2f\", ($mask_cracked - $standard_cracked) * 100.0 / $actual_count}")

echo ""
echo "=== Results ==="
echo ""
printf "  Standard (ascii-32-95): %s / %s cracked (%s%%)\n" "$standard_cracked" "$actual_count" "$standard_pct"
printf "  Mask (%s):        %s / %s cracked (%s%%)\n" "$MASK" "$mask_cracked" "$actual_count" "$mask_pct"
echo ""
if [ "$diff_count" -ge 0 ]; then
    printf "  Mask improvement: +%s hashes (+%s percentage points)\n" "$diff_count" "$diff_pct"
else
    printf "  Mask vs standard: %s hashes (%s percentage points)\n" "$diff_count" "$diff_pct"
fi
echo ""
