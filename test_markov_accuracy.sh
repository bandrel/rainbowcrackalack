#!/bin/bash
set -euo pipefail

# Markov vs standard rainbow table accuracy comparison.
#
# Trains against a rockyou.txt sample, generates a standard table and a
# Markov-ordered table at identical ~50% keyspace coverage, then compares
# crack rates against a held-out set of real-world 5-char passwords.
#
# Parameters: ascii-32-95, length 5, chain_len=100000, num_chains=53600
# (~50% coverage: 1-(1-100000/7737809375)^53600 ~= 0.50)
#
# Usage: ./test_markov_accuracy.sh <path/to/rockyou.txt> [path/to/model.markov]

ROCKYOU="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MARKOV="${2:-$SCRIPT_DIR/rockyou.markov}"
BINDIR="$SCRIPT_DIR"

SAMPLE_SIZE=500
CHAIN_LEN=100000
NUM_CHAINS=53600
PT_LEN=5

if [ -z "$ROCKYOU" ]; then
    echo "Usage: $0 <path/to/rockyou.txt> [path/to/model.markov]"
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

TMPDIR=$(mktemp -d)
cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

ln -sfn "$BINDIR/Metal" "$TMPDIR/Metal" 2>/dev/null || true
ln -sfn "$BINDIR/CL" "$TMPDIR/CL" 2>/dev/null || true

echo "=== Markov Accuracy Test (ascii-32-95, len=$PT_LEN, ~50% coverage, N=$SAMPLE_SIZE passwords) ==="
echo ""

# Sample passwords: 5-char printable ASCII (0x20-0x7e = ascii-32-95)
echo "Sampling $SAMPLE_SIZE passwords from rockyou.txt..."
grep -E '^[ -~]{5}$' "$ROCKYOU" | shuf -n "$SAMPLE_SIZE" > "$TMPDIR/passwords.txt"
actual_count=$(wc -l < "$TMPDIR/passwords.txt" | tr -d ' ')
echo "  Got $actual_count passwords."

# Compute NTLM hashes via pure Python MD4 (works on macOS where OpenSSL
# blocks legacy digest algorithms).
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

# Self-test: NTLM("password") == 8846f7eaee8fb117ad06bdd830b7586c
_expected = '8846f7eaee8fb117ad06bdd830b7586c'
_got = ntlm('password')
if _got != _expected:
    sys.exit(f'ERROR: MD4 self-test failed. got={_got} expected={_expected}')

for line in sys.stdin:
    print(ntlm(line.rstrip('\n')))
PYEOF
python3 "$TMPDIR/ntlm_hash.py" < "$TMPDIR/passwords.txt" > "$TMPDIR/hashes.txt"

# --- Standard table ---
echo ""
echo "Generating standard table (chain_len=$CHAIN_LEN, num_chains=$NUM_CHAINS)..."
mkdir -p "$TMPDIR/standard_tables"
(cd "$TMPDIR" && "$BINDIR/crackalack_gen" ntlm ascii-32-95 $PT_LEN $PT_LEN 0 $CHAIN_LEN $NUM_CHAINS 0)
mv "$TMPDIR"/*.rt "$TMPDIR/standard_tables/"

echo "Sorting standard table..."
"$BINDIR/crackalack_sort" "$TMPDIR"/standard_tables/*.rt

# Clean stale precalc/pot files before lookup
rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot

echo "Looking up hashes against standard table..."
standard_output=$("$BINDIR/crackalack_lookup" "$TMPDIR/standard_tables/" "$TMPDIR/hashes.txt" 2>&1) || true

# --- Markov table ---
echo ""
echo "Generating Markov table (chain_len=$CHAIN_LEN, num_chains=$NUM_CHAINS)..."
mkdir -p "$TMPDIR/markov_tables"
(cd "$TMPDIR" && "$BINDIR/crackalack_gen" ntlm ascii-32-95 $PT_LEN $PT_LEN 0 $CHAIN_LEN $NUM_CHAINS 0 --markov "$MARKOV")
mv "$TMPDIR"/*.rt "$TMPDIR/markov_tables/"

echo "Sorting Markov table..."
"$BINDIR/crackalack_sort" "$TMPDIR"/markov_tables/*.rt

rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot

echo "Looking up hashes against Markov table..."
markov_output=$("$BINDIR/crackalack_lookup" "$TMPDIR/markov_tables/" "$TMPDIR/hashes.txt" 2>&1) || true

# --- Parse results ---
# Summary line: "Of the N hashes loaded, M were cracked, or PP.PP%."
standard_cracked=$(echo "$standard_output" | awk '/were cracked, or/{print $6}')
markov_cracked=$(echo "$markov_output" | awk '/were cracked, or/{print $6}')

if [ -z "$standard_cracked" ] || [ -z "$markov_cracked" ]; then
    echo ""
    echo "ERROR: Could not parse lookup summary lines."
    echo ""
    echo "Standard lookup output (last 5 lines):"
    echo "$standard_output" | tail -5 | sed 's/^/  /'
    echo ""
    echo "Markov lookup output (last 5 lines):"
    echo "$markov_output" | tail -5 | sed 's/^/  /'
    exit 1
fi

standard_pct=$(echo "$standard_output" | awk '/were cracked, or/{gsub(/%\.?/,"",$NF); print $NF}')
markov_pct=$(echo "$markov_output" | awk '/were cracked, or/{gsub(/%\.?/,"",$NF); print $NF}')

diff_count=$((markov_cracked - standard_cracked))
diff_pct=$(awk "BEGIN{printf \"%.2f\", ($markov_cracked - $standard_cracked) * 100.0 / $actual_count}")

echo ""
echo "=== Results ==="
echo ""
printf "  Standard:  %s / %s cracked (%s%%)\n" "$standard_cracked" "$actual_count" "$standard_pct"
printf "  Markov:    %s / %s cracked (%s%%)\n" "$markov_cracked" "$actual_count" "$markov_pct"
echo ""
if [ "$diff_count" -ge 0 ]; then
    printf "  Markov improvement: +%s hashes (+%s percentage points)\n" "$diff_count" "$diff_pct"
else
    printf "  Markov vs standard: %s hashes (%s percentage points)\n" "$diff_count" "$diff_pct"
fi
echo ""
