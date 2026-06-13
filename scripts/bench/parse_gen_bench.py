#!/usr/bin/env python3
"""Parse `crackalack_gen ... -bench` output into a chains/sec rate.

Also a CLI to merge one (config, role, rate) into gen.json in a results dir:
    parse_gen_bench.py --merge <results_dir> --config <name> --role <base|cand> --log <logfile>
"""
import argparse, json, os, re, sys

RATE_RE = re.compile(r"Rate:\s*([\d.]+)\s*/s")


def parse_rate(text: str) -> float:
    m = RATE_RE.search(text)
    if not m:
        raise ValueError("no 'Rate: N/s' line found")
    return float(m.group(1))


def merge(results_dir: str, config: str, role: str, rate: float) -> None:
    path = os.path.join(results_dir, "gen.json")
    data = {}
    if os.path.exists(path):
        with open(path) as f:
            data = json.load(f)
    data.setdefault(config, {})[role] = {"chains_per_s": rate}
    with open(path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--merge", required=True, help="results dir")
    p.add_argument("--config", required=True)
    p.add_argument("--role", required=True, choices=["base", "cand"])
    p.add_argument("--log", required=True)
    a = p.parse_args()
    rate = parse_rate(open(a.log, errors="replace").read())
    merge(a.merge, a.config, a.role, rate)
    print(f"{a.config}/{a.role}: {rate}/s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
