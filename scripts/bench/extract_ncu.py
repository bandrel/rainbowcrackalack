#!/usr/bin/env python3
"""Parse `ncu --csv --metrics ...` output into a friendly metrics dict, and
merge one (config, role) profile into profile.json.

Usage (merge):
  extract_ncu.py --merge <results_dir> --config <name> --role <base|cand> --csv <file>
"""
import argparse, csv, io, json, os, sys

# ncu raw metric name -> (friendly key, python type)
METRIC_MAP = {
    "sm__throughput.avg.pct_of_peak_sustained_elapsed": ("compute_sm_pct", float),
    "gpu__dram_throughput.avg.pct_of_peak_sustained_elapsed": ("dram_pct", float),
    "sm__warps_active.avg.pct_of_peak_sustained_active": ("achieved_occupancy_pct", float),
    "launch__registers_per_thread": ("registers_per_thread", int),
    "l1tex__data_bank_conflicts_pipe_lsu_mem_shared_op_ld.sum": ("shared_ld_bank_conflicts", int),
}

# The exact --metrics list profile_ncu.sh requests (kept next to the map).
NCU_METRICS = ",".join(METRIC_MAP.keys())


def parse_ncu_csv(text: str) -> dict:
    # ncu emits its CSV report to stdout AFTER the profiled app's own stdout
    # (the crackalack_gen banner + progress + ==PROF== lines).  Skip everything
    # before the real CSV header row so DictReader doesn't treat banner text as
    # the header.
    lines = text.splitlines()
    start = None
    for i, ln in enumerate(lines):
        if '"Metric Name"' in ln and '"Metric Value"' in ln:
            start = i
            break
    if start is None:
        return {}
    out = {}
    reader = csv.DictReader(io.StringIO("\n".join(lines[start:])))
    for row in reader:
        raw = (row.get("Metric Name") or "").strip()
        if raw in METRIC_MAP:
            key, typ = METRIC_MAP[raw]
            val = row.get("Metric Value", "").strip().replace(",", "")
            try:
                out[key] = typ(float(val)) if typ is int else typ(val)
            except ValueError:
                continue
    return out


def merge(results_dir, config, role, metrics):
    path = os.path.join(results_dir, "profile.json")
    data = {}
    if os.path.exists(path):
        with open(path) as f:
            data = json.load(f)
    data.setdefault(config, {})[role] = metrics
    with open(path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--merge", required=True)
    p.add_argument("--config", required=True)
    p.add_argument("--role", required=True, choices=["base", "cand"])
    p.add_argument("--csv", required=True)
    a = p.parse_args()
    with open(a.csv, errors="replace") as f:
        metrics = parse_ncu_csv(f.read())
    if not metrics:
        print(f"{a.config}/{a.role}: no metrics parsed (ncu failed?) — not merging",
              file=sys.stderr)
        return 1
    merge(a.merge, a.config, a.role, metrics)
    print(f"{a.config}/{a.role}: {metrics}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
