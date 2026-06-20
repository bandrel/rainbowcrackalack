#!/usr/bin/env python3
"""Render regression_summary.md and decide overall pass/fail.

Inputs:
  --roundtrip backend=path  (repeatable) per-backend roundtrip JSON
  --crackdiff path          differential JSON from crack_diff.py
  --out path                regression_summary.md
Exit code: 0 if overall pass, 1 otherwise.
"""
import argparse
import json
import sys


def _cross_backend_divergences(roundtrips: dict) -> list:
    """Paths whose 'cracked' result is not identical across all backends present."""
    paths = set()
    for res in roundtrips.values():
        paths.update(res)
    diverged = []
    for path in sorted(paths):
        results = [res.get(path, {}).get("cracked") for res in roundtrips.values()
                   if path in res]
        if len(set(results)) > 1:
            diverged.append(path)
    return diverged


def overall_pass(roundtrips: dict, crackdiff: dict) -> bool:
    for res in roundtrips.values():
        for path_result in res.values():
            if not path_result.get("cracked"):
                return False
    if _cross_backend_divergences(roundtrips):
        return False
    if crackdiff.get("regressions"):
        return False
    return True


def build_report(roundtrips: dict, crackdiff: dict) -> str:
    status = "PASS" if overall_pass(roundtrips, crackdiff) else "FAIL"
    lines = [f"# Crack Regression Summary — {status}", ""]

    lines.append("## Round-trip (self-constructed known-crackable hashes)")
    lines.append("")
    lines.append("| path | " + " | ".join(roundtrips) + " |")
    lines.append("|------|" + "|".join(["------"] * len(roundtrips)) + "|")
    all_paths = sorted({p for res in roundtrips.values() for p in res})
    for path in all_paths:
        cells = []
        for backend in roundtrips:
            r = roundtrips[backend].get(path)
            if r is None:
                cells.append("n/a")
            else:
                cells.append("PASS" if r.get("cracked") else "FAIL")
        lines.append(f"| {path} | " + " | ".join(cells) + " |")
    lines.append("")

    diverged = _cross_backend_divergences(roundtrips)
    if diverged:
        lines.append(f"**Cross-backend divergence:** {', '.join(diverged)}")
        lines.append("")

    lines.append("## Differential (BASE vs CANDIDATE, real tables)")
    lines.append("")
    lines.append(f"- BASE cracked:      {crackdiff.get('base_cracked', 0)}")
    lines.append(f"- CANDIDATE cracked: {crackdiff.get('cand_cracked', 0)}")
    regs = crackdiff.get("regressions", [])
    imps = crackdiff.get("improvements", [])
    lines.append(f"- Regressions (cracked by BASE, missed by CANDIDATE): {len(regs)}")
    for h in regs:
        lines.append(f"  - {h}")
    lines.append(f"- Improvements (informational): {len(imps)}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--roundtrip", action="append", default=[],
                   help="backend=path/to/roundtrip.json")
    p.add_argument("--crackdiff", default=None)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    roundtrips = {}
    for spec in a.roundtrip:
        backend, _, path = spec.partition("=")
        with open(path) as f:
            roundtrips[backend] = json.load(f)
    crackdiff = {}
    if a.crackdiff:
        with open(a.crackdiff) as f:
            crackdiff = json.load(f)

    md = build_report(roundtrips, crackdiff)
    with open(a.out, "w") as f:
        f.write(md + "\n")
    print(md)
    return 0 if overall_pass(roundtrips, crackdiff) else 1


if __name__ == "__main__":
    sys.exit(main())
