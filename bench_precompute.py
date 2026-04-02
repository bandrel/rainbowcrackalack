#!/usr/bin/env python3
"""
Benchmark precompute time for crackalack_lookup.

Generates N random NTLM hashes, writes a pwdump file, then runs
crackalack_lookup against a table directory.  The lookup will precompute
candidate endpoints for every hash against every table config found in
the directory, and the script parses the timing output.

Usage:
    ./bench_precompute.py <table_dir> <num_hashes> [--markov <model>]

Examples:
    # Benchmark 500 hashes against 8-char tables
    ./bench_precompute.py ./tables/ 500

    # Benchmark 500 hashes with Markov model
    ./bench_precompute.py ./tables/ 500 --markov dynamic-all.markov
"""

import os
import random
import struct
import subprocess
import sys
import tempfile
import time


def md4(message: bytes) -> bytes:
    """Pure-Python MD4 (macOS OpenSSL 3 disables legacy MD4)."""
    def left_rotate(n, b):
        return ((n << b) | (n >> (32 - b))) & 0xFFFFFFFF
    def f(x, y, z):
        return (x & y) | (~x & z)
    def g(x, y, z):
        return (x & y) | (x & z) | (y & z)
    def h(x, y, z):
        return x ^ y ^ z

    msg = bytearray(message)
    orig_len = len(msg)
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack("<Q", orig_len * 8)

    a0, b0, c0, d0 = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476

    for i in range(0, len(msg), 64):
        block = msg[i : i + 64]
        x = list(struct.unpack("<16I", block))
        a, b, c, d = a0, b0, c0, d0

        for k in [0, 4, 8, 12]:
            a = left_rotate((a + f(b, c, d) + x[k]) & 0xFFFFFFFF, 3)
            d = left_rotate((d + f(a, b, c) + x[k + 1]) & 0xFFFFFFFF, 7)
            c = left_rotate((c + f(d, a, b) + x[k + 2]) & 0xFFFFFFFF, 11)
            b = left_rotate((b + f(c, d, a) + x[k + 3]) & 0xFFFFFFFF, 19)

        for k in [0, 1, 2, 3]:
            a = left_rotate((a + g(b, c, d) + x[k] + 0x5A827999) & 0xFFFFFFFF, 3)
            d = left_rotate((d + g(a, b, c) + x[k + 4] + 0x5A827999) & 0xFFFFFFFF, 5)
            c = left_rotate((c + g(d, a, b) + x[k + 8] + 0x5A827999) & 0xFFFFFFFF, 9)
            b = left_rotate((b + g(c, d, a) + x[k + 12] + 0x5A827999) & 0xFFFFFFFF, 13)

        for k in [0, 2, 1, 3]:
            a = left_rotate((a + h(b, c, d) + x[k] + 0x6ED9EBA1) & 0xFFFFFFFF, 3)
            d = left_rotate((d + h(a, b, c) + x[k + 8] + 0x6ED9EBA1) & 0xFFFFFFFF, 9)
            c = left_rotate((c + h(d, a, b) + x[k + 4] + 0x6ED9EBA1) & 0xFFFFFFFF, 11)
            b = left_rotate((b + h(c, d, a) + x[k + 12] + 0x6ED9EBA1) & 0xFFFFFFFF, 15)

        a0 = (a0 + a) & 0xFFFFFFFF
        b0 = (b0 + b) & 0xFFFFFFFF
        c0 = (c0 + c) & 0xFFFFFFFF
        d0 = (d0 + d) & 0xFFFFFFFF

    return struct.pack("<4I", a0, b0, c0, d0)


def ntlm_hash(password: str) -> str:
    return md4(password.encode("utf-16-le")).hex()


def generate_random_password(length=8):
    """Generate a random password from the ascii-32-95 charset."""
    charset = [chr(c) for c in range(32, 127)]
    return "".join(random.choice(charset) for _ in range(length))


def generate_pwdump(num_hashes, filepath, password_len=8):
    """Generate a pwdump file with num_hashes random NTLM hashes."""
    LM_EMPTY = "aad3b435b51404eeaad3b435b51404ee"
    hashes = set()
    with open(filepath, "w") as f:
        i = 0
        while i < num_hashes:
            pw = generate_random_password(password_len)
            h = ntlm_hash(pw)
            if h in hashes:
                continue
            hashes.add(h)
            f.write(f"user{i:04d}:{i}:{LM_EMPTY}:{h}:::\n")
            i += 1
    return filepath


def create_dummy_table(table_dir, chain_len=422000, num_chains=1000000, table_index=0):
    """Create a minimal .rt file with valid filename encoding but dummy data.
    The lookup tool parses parameters from the filename; actual table contents
    are only needed for the binary search phase (which runs after precompute)."""
    os.makedirs(table_dir, exist_ok=True)
    filename = f"ntlm_ascii-32-95#8-8_{table_index}_{chain_len}x{num_chains}_0.rt"
    filepath = os.path.join(table_dir, filename)
    if not os.path.exists(filepath):
        # Write minimal valid table: just a few 16-byte (start, end) chain entries
        with open(filepath, "wb") as f:
            for i in range(min(num_chains, 1000)):
                f.write(struct.pack("<QQ", i, i + 1))
    return filepath


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <num_hashes> [<table_dir>] [--chain-len N] [--markov <model>]")
        print(f"       {sys.argv[0]} 500              # auto-creates dummy 8-char NTLM table")
        print(f"       {sys.argv[0]} 500 ./tables/    # uses existing tables in directory")
        print(f"       {sys.argv[0]} 500 --chain-len 100000")
        sys.exit(1)

    # Parse arguments
    positional = []
    markov_args = []
    chain_len = 422000
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--markov" and i + 1 < len(sys.argv):
            markov_args = ["--markov", sys.argv[i + 1]]
            i += 2
        elif sys.argv[i] == "--chain-len" and i + 1 < len(sys.argv):
            chain_len = int(sys.argv[i + 1])
            i += 2
        else:
            positional.append(sys.argv[i])
            i += 1

    num_hashes = int(positional[0])
    use_dummy = len(positional) < 2
    table_dir = positional[1] if len(positional) >= 2 else None

    # Create dummy table if no directory provided
    cleanup_table_dir = False
    if use_dummy:
        table_dir = tempfile.mkdtemp(prefix="bench_rt_")
        create_dummy_table(table_dir, chain_len=chain_len)
        cleanup_table_dir = True
    elif not os.path.isdir(table_dir):
        print(f"Error: table directory '{table_dir}' not found")
        sys.exit(1)

    # Count .rt files
    rt_files = [f for f in os.listdir(table_dir) if f.endswith(".rt")]
    if not rt_files:
        print(f"Error: no .rt files found in '{table_dir}'")
        sys.exit(1)

    print(f"=== Precompute Benchmark ===")
    print(f"Table directory: {table_dir}")
    print(f"Tables found:    {len(rt_files)}")
    for f in rt_files[:5]:
        print(f"  {f}")
    if len(rt_files) > 5:
        print(f"  ... and {len(rt_files) - 5} more")
    print(f"Hashes:          {num_hashes}")
    print(f"Chain length:    {chain_len}")
    if markov_args:
        print(f"Markov model:    {markov_args[1]}")
    print()

    # Generate hashes
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pwdump", delete=False) as tmp:
        pwdump_path = tmp.name
    generate_pwdump(num_hashes, pwdump_path)
    print(f"Generated {num_hashes} random NTLM hashes -> {pwdump_path}")

    # Find crackalack_lookup binary
    script_dir = os.path.dirname(os.path.abspath(__file__))
    lookup_bin = os.path.join(script_dir, "crackalack_lookup")
    if not os.path.isfile(lookup_bin):
        print(f"Error: {lookup_bin} not found. Build first with 'make'.")
        os.unlink(pwdump_path)
        sys.exit(1)

    # Run the lookup, capture output to parse precompute times
    cmd = [lookup_bin, table_dir, pwdump_path] + markov_args
    print(f"Running: {' '.join(cmd)}")
    print(f"{'=' * 60}\n")

    wall_start = time.monotonic()
    result = subprocess.run(cmd, capture_output=True, text=True)
    wall_elapsed = time.monotonic() - wall_start

    # Print the lookup output
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)

    # Parse per-hash precompute times from output
    import re
    per_hash_times = []
    for line in (result.stdout or "").split("\n"):
        m = re.search(r"Completed in (.+)\.", line)
        if m:
            time_str = m.group(1)
            # Parse "38.0 secs" or "1 mins, 30 secs" etc.
            secs = 0.0
            for part in time_str.split(","):
                part = part.strip()
                if "min" in part:
                    secs += float(part.split()[0]) * 60
                elif "sec" in part:
                    secs += float(part.split()[0])
                elif "hour" in part:
                    secs += float(part.split()[0]) * 3600
            per_hash_times.append(secs)

    # Parse total precompute time
    precomp_total = None
    for line in (result.stdout or "").split("\n"):
        m = re.search(r"Precomputation: (.+)", line)
        if m:
            time_str = m.group(1).strip()
            secs = 0.0
            for part in time_str.split(","):
                part = part.strip()
                if "min" in part:
                    secs += float(part.split()[0]) * 60
                elif "sec" in part:
                    secs += float(part.split()[0])
                elif "hour" in part:
                    secs += float(part.split()[0]) * 3600
            precomp_total = secs

    print(f"{'=' * 60}")
    print(f"=== PRECOMPUTE BENCHMARK RESULTS ===")
    print(f"{'=' * 60}")
    print(f"Hashes benchmarked:     {num_hashes}")
    print(f"Chain length:           {chain_len:,}")
    if per_hash_times:
        avg_time = sum(per_hash_times) / len(per_hash_times)
        print(f"Avg precompute/hash:    {avg_time:.1f}s")
        print(f"Min precompute/hash:    {min(per_hash_times):.1f}s")
        print(f"Max precompute/hash:    {max(per_hash_times):.1f}s")
    if precomp_total:
        print(f"Total precompute time:  {precomp_total:.1f}s")
    print(f"Wall time (total):      {wall_elapsed:.2f}s")
    print()

    # Projections
    if per_hash_times:
        avg_time = sum(per_hash_times) / len(per_hash_times)
        for n in [100, 500, 1000, 5000]:
            projected = avg_time * n
            if projected < 60:
                proj_str = f"{projected:.0f}s"
            elif projected < 3600:
                proj_str = f"{projected / 60:.1f} min"
            else:
                proj_str = f"{projected / 3600:.1f} hr"
            print(f"  Projected for {n:>5} hashes: {proj_str}")

    # Cleanup
    os.unlink(pwdump_path)
    if cleanup_table_dir:
        import shutil
        shutil.rmtree(table_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
