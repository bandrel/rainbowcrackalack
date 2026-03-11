#!/bin/bash
set -euo pipefail

# Markov vs standard rainbow table accuracy comparison.
#
# Trains against a rockyou.txt sample, generates a standard table and a
# Markov-ordered table at identical ~100% keyspace coverage, then compares
# crack rates against a held-out set of real-world passwords.
#
# Usage: ./test_markov_accuracy.sh <path/to/rockyou.txt> [path/to/model.markov] [plaintext_len]
# plaintext_len defaults to 5. NUM_CHAINS is auto-scaled to ~100% keyspace coverage.

ROCKYOU="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MARKOV="${2:-$SCRIPT_DIR/rockyou.markov}"
PT_LEN="${3:-5}"
BINDIR="$SCRIPT_DIR"

# Convert MARKOV to absolute path (resolve relative paths before we cd into TMPDIR)
if [[ ! "$MARKOV" = /* ]]; then
    MARKOV="$(cd "$(dirname "$MARKOV")" && pwd)/$(basename "$MARKOV")"
fi

CHAIN_LEN=100000

if [ -z "$ROCKYOU" ]; then
    echo "Usage: $0 <path/to/rockyou.txt> [path/to/model.markov] [plaintext_len]"
    exit 1
fi

if ! [[ "$PT_LEN" =~ ^[0-9]+$ ]] || [ "$PT_LEN" -lt 2 ]; then
    echo "ERROR: plaintext_len must be an integer >= 2 (got: $PT_LEN)"
    exit 1
fi

# Compute NUM_CHAINS and SAMPLE_SIZE dynamically for ~100% coverage
read -r NUM_CHAINS SAMPLE_SIZE < <(awk -v pt_len="$PT_LEN" -v chain_len="$CHAIN_LEN" 'BEGIN {
    keyspace = 1
    for (i = 0; i < pt_len; i++) keyspace *= 95
    n = int(keyspace / chain_len) + 1
    if (n < 1) n = 1
    s = (keyspace < 500 ? int(keyspace) : 500)
    if (s < 1) s = 1
    print n, s
}')

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

echo "=== Markov Accuracy Test (ascii-32-95, len=$PT_LEN, ~100% coverage, N=$SAMPLE_SIZE passwords) ==="
echo ""

# Sample passwords: printable ASCII (0x20-0x7e = ascii-32-95)
echo "Sampling $SAMPLE_SIZE passwords from rockyou.txt..."
LC_ALL=C grep -E "^[ -~]{${PT_LEN}}$" "$ROCKYOU" | shuf -n "$SAMPLE_SIZE" > "$TMPDIR/passwords.txt"
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
# Use uv if available, fall back to python3
if command -v uv &> /dev/null; then
    uv run python3 "$TMPDIR/ntlm_hash.py" < "$TMPDIR/passwords.txt" > "$TMPDIR/hashes.txt"
else
    python3 "$TMPDIR/ntlm_hash.py" < "$TMPDIR/passwords.txt" > "$TMPDIR/hashes.txt"
fi

# --- Standard table ---
echo ""
echo "Generating standard table (chain_len=$CHAIN_LEN, num_chains=$NUM_CHAINS)..."
mkdir -p "$TMPDIR/standard_tables"
(cd "$TMPDIR" && "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$NUM_CHAINS" 0)
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
(cd "$TMPDIR" && "$BINDIR/crackalack_gen" ntlm ascii-32-95 "$PT_LEN" "$PT_LEN" 0 "$CHAIN_LEN" "$NUM_CHAINS" 0 --markov "$MARKOV")
mv "$TMPDIR"/*.rt "$TMPDIR/markov_tables/"

echo "Sorting Markov table..."
"$BINDIR/crackalack_sort" "$TMPDIR"/markov_tables/*.rt

rm -f "$BINDIR"/rcracki.precalc.* "$BINDIR"/rainbowcrackalack_*.pot

echo "Looking up hashes against Markov table..."
markov_output=$("$BINDIR/crackalack_lookup" "$TMPDIR/markov_tables/" "$TMPDIR/hashes.txt" 2>&1) || true

# --- Parse results ---
# When num_cracked > 0: "   Of the N hashes loaded, M were cracked, or PP.PP%."
# When num_cracked == 0: "No hashes were cracked.  :("
parse_cracked() {
    awk '/were cracked, or/{print $6} /No hashes were cracked/{print "0"}'
}
parse_pct() {
    awk '/were cracked, or/{gsub(/%\.?$/,"",$NF); print $NF} /No hashes were cracked/{print "0.00"}'
}

standard_cracked=$(echo "$standard_output" | parse_cracked)
markov_cracked=$(echo "$markov_output" | parse_cracked)

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

standard_pct=$(echo "$standard_output" | parse_pct)
markov_pct=$(echo "$markov_output" | parse_pct)

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
