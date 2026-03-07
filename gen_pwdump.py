#!/usr/bin/env python3
"""Generate a pwdump-format hash file from rockyou.txt for testing crackalack_lookup."""

import struct
import sys


def md4(message: bytes) -> bytes:
    """Pure-Python MD4 implementation (macOS OpenSSL 3 disables legacy MD4)."""

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

    a0 = 0x67452301
    b0 = 0xEFCDAB89
    c0 = 0x98BADCFE
    d0 = 0x10325476

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


LM_EMPTY = "aad3b435b51404eeaad3b435b51404ee"
WORDLIST = "/Users/justinbollinger/wordlists/rockyou.txt"
OUTPUT = "test_hashes.pwdump"


MAX_HASHES = 100
PASSWORD_LEN = 8


def main():
    count = 0
    with (
        open(WORDLIST, "r", encoding="latin-1") as infile,
        open(OUTPUT, "w") as outfile,
    ):
        for line in infile:
            if count >= MAX_HASHES:
                break
            password = line.rstrip("\n\r")
            if len(password) != PASSWORD_LEN:
                continue
            nt = ntlm_hash(password)
            outfile.write(f"user{count:04d}:{count}:{LM_EMPTY}:{nt}:::\n")
            count += 1

    print(f"Wrote {count} hashes to {OUTPUT}")


if __name__ == "__main__":
    main()
