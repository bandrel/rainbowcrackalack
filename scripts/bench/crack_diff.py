#!/usr/bin/env python3
"""Pot-file parsing and crack-set diffing for the regression framework.

A "cracked set" maps each cracked hash (lowercase hex) to its plaintext encoded
as hex. NetNTLMv1 plaintexts are already written as hex text by the pot writer,
so they are kept verbatim (lowercased); NTLM ASCII plaintexts are hex-encoded.
This way both hash types land in a hex representation that compares the same.

Pot line format (see crackalack_lookup.c::save_cracked_hash):
    JTR:     [$NT$]<hash>:<plaintext>\n   ($NT$ only for NTLM)
    hashcat: <hash>:<plaintext>\n
We strip a leading "$NT$" so either file parses to the same hash key.
"""
import argparse
import json
import os
import sys


# KNOWN LIMITATION: an NTLM ASCII plaintext that is coincidentally valid
# even-length hex (e.g. "deadbeef") is treated as already-hex and returned
# verbatim instead of being hex-encoded. For random 8-char ascii-32-95
# plaintexts this happens ~1 in 5500 runs (~1 in 240k for 9-char). The
# round-trip phase (run_regression.sh) guards against this by treating a hash's
# PRESENCE in the pot as the false-negative signal, so this ambiguity cannot
# turn a real crack into a spurious framework failure. Do not extend this
# hex-detect heuristic to higher-stakes uses.
def _to_hex(pt: bytes) -> str:
    """Normalize a pot plaintext field to lowercase hex.

    A NetNTLMv1 pot already stores hex text (e.g. "41424344") for the 7 raw
    bytes; that is kept verbatim (lowercased). An NTLM ASCII plaintext (e.g.
    "hello") is not valid hex, so its raw bytes are hex-encoded. Both end up as
    a hex string so the two hash types compare consistently.
    """
    s = pt.decode("latin-1")
    if s and len(s) % 2 == 0:
        try:
            bytes.fromhex(s)
            return s.lower()
        except ValueError:
            pass
    return pt.hex()


def parse_pot(path: str) -> dict:
    """Return {hash_hex_lower: plaintext_hex}. Missing file => {}.

    The plaintext field is normalized to hex (see _to_hex): NetNTLMv1 hex text
    is kept verbatim while NTLM ASCII plaintext is hex-encoded, so both hash
    types land in the same hex representation.
    """
    result = {}
    if not os.path.exists(path):
        return result
    with open(path, "rb") as f:
        for raw in f.read().split(b"\n"):
            if not raw:
                continue
            if raw.startswith(b"$NT$"):
                raw = raw[4:]
            idx = raw.find(b":")
            if idx < 0:
                continue
            h = raw[:idx].decode("latin-1").lower()
            pt = raw[idx + 1:]
            result[h] = _to_hex(pt)
    return result


def diff_cracks(base: dict, cand: dict) -> dict:
    """Regressions = hashes cracked by base but not cand. Improvements = reverse."""
    base_keys = set(base)
    cand_keys = set(cand)
    return {
        "base_cracked": len(base_keys),
        "cand_cracked": len(cand_keys),
        "regressions": sorted(base_keys - cand_keys),
        "improvements": sorted(cand_keys - base_keys),
    }


def assert_in_pot(pot_path: str, expected_hash: str, expected_plaintext_hex: str) -> bool:
    """True iff the hash is present with the expected plaintext (hex compare)."""
    cracked = parse_pot(pot_path)
    return cracked.get(expected_hash.lower()) == expected_plaintext_hex.lower()


def main() -> int:
    p = argparse.ArgumentParser(description="Diff two pot files for crack regressions.")
    p.add_argument("--base-pot", required=True)
    p.add_argument("--cand-pot", required=True)
    p.add_argument("--out", required=True, help="Write crackdiff.json here")
    a = p.parse_args()
    d = diff_cracks(parse_pot(a.base_pot), parse_pot(a.cand_pot))
    with open(a.out, "w") as f:
        json.dump(d, f, indent=2, sort_keys=True)
    print(f"base={d['base_cracked']} cand={d['cand_cracked']} "
          f"regressions={len(d['regressions'])} improvements={len(d['improvements'])}")
    return 1 if d["regressions"] else 0


if __name__ == "__main__":
    sys.exit(main())
