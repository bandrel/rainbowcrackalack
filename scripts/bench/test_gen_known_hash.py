#!/usr/bin/env python3
"""Tests for the gen_known_hash CLI binary (no GPU required).

Run from the repo root with the binary already built:
    make macos   # or: make linux
    .venv/bin/python -m pytest scripts/bench/test_gen_known_hash.py
The binary path is the repo-root ./gen_known_hash (override with GEN_KNOWN_HASH).
"""
import os
import subprocess
import pytest

from Crypto.Hash import MD4  # pycryptodome

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.environ.get("GEN_KNOWN_HASH", os.path.join(REPO_ROOT, "gen_known_hash"))


def _run(args):
    proc = subprocess.run([BIN] + args, capture_output=True, text=True)
    assert proc.returncode == 0, f"gen_known_hash failed: {proc.stderr}"
    out = {}
    for line in proc.stdout.splitlines():
        if line.startswith("hash="):
            out["hash"] = line[len("hash="):].strip()
        elif line.startswith("plaintext="):
            out["plaintext"] = line[len("plaintext="):].strip()
    return out


def ntlm_hash_hex(plaintext_bytes: bytes) -> str:
    return MD4.new(plaintext_bytes.decode("latin-1").encode("utf-16-le")).hexdigest()


@pytest.mark.skipif(not os.path.exists(BIN), reason="gen_known_hash not built")
def test_netntlmv1_default_still_works():
    # No flags => NetNTLMv1/byte/len-7: 16-hex hash, 14-hex plaintext.
    out = _run(["1000", "0", "12345", "10"])
    assert len(out["hash"]) == 16
    assert len(out["plaintext"]) == 14


@pytest.mark.skipif(not os.path.exists(BIN), reason="gen_known_hash not built")
def test_ntlm_algo_emits_ntlm_hash():
    # --algo ntlm => 32-hex NTLM hash that equals MD4(UTF-16LE(plaintext)).
    out = _run(["1000", "0", "12345", "10",
                "--algo", "ntlm", "--charset", "ascii-32-95", "--plaintext-len", "8"])
    assert len(out["hash"]) == 32
    pt = bytes.fromhex(out["plaintext"])
    assert len(pt) == 8
    assert out["hash"] == ntlm_hash_hex(pt)
