#!/usr/bin/env python3
"""Output-equivalence check for the benchmark harness.

sha256 a generated artifact and record base-vs-candidate match into
equivalence.json. The driver generates the same small seeded table from each
build (same args => identical bytes iff chain math is unchanged) and calls
record_equivalence with the two shas.

Usage (record):
  check_equivalence.py --results <dir> --check <name> --base-sha <hex> --cand-sha <hex>
  check_equivalence.py --results <dir> --check <name> --base-file <path> --cand-file <path>
"""
import argparse, hashlib, json, os, sys


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def record_equivalence(results_dir: str, check: str, base_sha: str, cand_sha: str) -> bool:
    path = os.path.join(results_dir, "equivalence.json")
    data = {}
    if os.path.exists(path):
        with open(path) as f:
            data = json.load(f)
    match = base_sha == cand_sha
    data[check] = {"base_sha": base_sha, "cand_sha": cand_sha, "match": match}
    with open(path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
    return match


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--results", required=True)
    p.add_argument("--check", required=True)
    p.add_argument("--base-sha"); p.add_argument("--cand-sha")
    p.add_argument("--base-file"); p.add_argument("--cand-file")
    a = p.parse_args()
    base = a.base_sha or (sha256_file(a.base_file) if a.base_file else None)
    cand = a.cand_sha or (sha256_file(a.cand_file) if a.cand_file else None)
    if base is None or cand is None:
        print("need --base/cand-sha or --base/cand-file", file=sys.stderr)
        return 2
    match = record_equivalence(a.results, a.check, base, cand)
    print(f"{a.check}: {'MATCH' if match else 'MISMATCH'} ({base[:12]} vs {cand[:12]})")
    return 0 if match else 1


if __name__ == "__main__":
    sys.exit(main())
