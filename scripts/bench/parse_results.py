#!/usr/bin/env python3
"""Parse benchmark trial logs into results.json and summary.md.

Inputs: a results directory containing per-trial files
    trial_NN_<branch>.log    (lookup stdout/stderr)
    trial_NN_<branch>.time   (/usr/bin/time -v output)

Outputs (written into the same directory):
    results.json   per-trial array with parsed wall, RSS, cracks, exit
    summary.md     human-readable comparison
"""
import argparse
import glob
import json
import os
import re
import statistics
import sys

WALL_RE = re.compile(r"Elapsed \(wall clock\) time .*: ([\d:.]+)")
RSS_RE = re.compile(r"Maximum resident set size \(kbytes\): (\d+)")
EXIT_RE = re.compile(r"Exit status: (\d+)")
CRACK_RE = re.compile(r"^HASH CRACKED")
TRIAL_RE = re.compile(r"trial_(\d+)_(\w+)\.time$")


def _parse_wall(s: str) -> float:
    """Convert "h:mm:ss" or "m:ss[.frac]" to seconds."""
    parts = s.split(":")
    parts = [float(p) for p in parts]
    if len(parts) == 3:
        return parts[0] * 3600 + parts[1] * 60 + parts[2]
    if len(parts) == 2:
        return parts[0] * 60 + parts[1]
    raise ValueError(f"unrecognized time format: {s}")


def parse_time_file(path: str) -> dict:
    with open(path) as f:
        text = f.read()
    wall_m = WALL_RE.search(text)
    rss_m = RSS_RE.search(text)
    exit_m = EXIT_RE.search(text)
    if not wall_m:
        raise ValueError(f"no wall-clock line in {path}")
    if not rss_m:
        raise ValueError(f"no RSS line in {path}")
    return {
        "wall_seconds": _parse_wall(wall_m.group(1)),
        "peak_rss_kb": int(rss_m.group(1)),
        "exit_status": int(exit_m.group(1)) if exit_m else -1,
    }


def count_cracks(log_path: str) -> int:
    if not os.path.exists(log_path):
        return 0
    n = 0
    with open(log_path, errors="replace") as f:
        for line in f:
            if CRACK_RE.search(line):
                n += 1
    return n


def collect_trials(results_dir: str) -> list:
    trials = []
    for time_path in sorted(glob.glob(os.path.join(results_dir, "trial_*.time"))):
        m = TRIAL_RE.search(os.path.basename(time_path))
        if not m:
            continue
        trial_num = int(m.group(1))
        branch = m.group(2)
        log_path = time_path.replace(".time", ".log")
        parsed = parse_time_file(time_path)
        parsed["trial"] = trial_num
        parsed["branch"] = branch
        parsed["cracked"] = count_cracks(log_path)
        trials.append(parsed)
    return trials


def summarize(trials: list) -> dict:
    """Return per-branch median wall/RSS, speedup, divergence flag."""
    summary = {"divergence": False}
    by_branch = {}
    for t in trials:
        by_branch.setdefault(t["branch"], []).append(t)

    crack_sets = set()
    for branch, branch_trials in by_branch.items():
        ok = [t for t in branch_trials if t["exit_status"] == 0]
        if len(ok) < 2:
            summary[branch] = {"status": "INSUFFICIENT_DATA",
                               "successful_trials": len(ok),
                               "total_trials": len(branch_trials)}
            continue
        walls = [t["wall_seconds"] for t in ok]
        rsses = [t["peak_rss_kb"] for t in ok]
        cracks = [t["cracked"] for t in ok]
        summary[branch] = {
            "status": "OK",
            "wall_median": statistics.median(walls),
            "rss_median_kb": statistics.median(rsses),
            "cracked_median": statistics.median(cracks),
            "successful_trials": len(ok),
            "total_trials": len(branch_trials),
        }
        crack_sets.add(tuple(sorted(cracks)))

    if len(crack_sets) > 1:
        summary["divergence"] = True

    # Compute speedup using whatever two branches are present (if exactly two with OK status)
    branch_keys = [k for k in sorted(by_branch.keys())]
    if len(branch_keys) == 2:
        branch_a = summary.get(branch_keys[0], {})
        branch_b = summary.get(branch_keys[1], {})
        if branch_a.get("status") == "OK" and branch_b.get("status") == "OK" and branch_b["wall_median"] > 0:
            summary["speedup"] = branch_a["wall_median"] / branch_b["wall_median"]
    return summary


def _fmt_secs(s: float) -> str:
    if s >= 3600:
        return f"{int(s//3600)}h{int((s%3600)//60):02d}m{s%60:04.1f}s"
    if s >= 60:
        return f"{int(s//60)}m{s%60:04.1f}s"
    return f"{s:.2f}s"


def _fmt_kb(kb: int) -> str:
    return f"{kb/1024/1024:.2f} GB"


def write_summary_md(trials: list, summary: dict, meta: dict, out_path: str) -> None:
    lines = []
    base_ref = meta.get("base_ref", "base")
    cand_ref = meta.get("cand_ref", "cand")
    lines.append(f"# Benchmark: {base_ref} (base) vs {cand_ref} (candidate)")
    lines.append(
        f"Host: {meta.get('host','?')} | GPU: {meta.get('gpu','?')} | "
        f"Subset: {meta.get('parts','?')} parts | Hashes: {meta.get('hash_count','?')}"
    )
    lines.append("")
    if summary.get("divergence"):
        lines.append("> **DIVERGENCE DETECTED** — branches reported different crack counts. "
                     "Perf number below is suspect; investigate before trusting it.")
        lines.append("")
    lines.append("## Wall-clock (median of successful trials)")
    lines.append("")
    lines.append("| Branch     | Wall-clock | Peak RSS | Cracked | Status |")
    lines.append("|------------|-----------:|---------:|--------:|:-------|")
    # Iterate over branches present in summary, skipping special keys
    for branch in sorted(k for k in summary.keys() if k not in ("divergence", "speedup")):
        b = summary.get(branch, {})
        if b.get("status") == "OK":
            lines.append(
                f"| {branch:10} | {_fmt_secs(b['wall_median']):>10} | "
                f"{_fmt_kb(b['rss_median_kb']):>8} | {int(b['cracked_median']):>7} | "
                f"OK ({b['successful_trials']}/{b['total_trials']}) |"
            )
        else:
            lines.append(
                f"| {branch:10} |          - |        - |       - | "
                f"{b.get('status','MISSING')} ({b.get('successful_trials',0)}/"
                f"{b.get('total_trials',0)}) |"
            )
    if "speedup" in summary:
        lines.append(f"| **speedup**| **{summary['speedup']:.2f}x** | | | |")
    lines.append("")

    lines.append("## Per-trial detail")
    lines.append("")
    lines.append("| Trial | Branch    |   Wall |  RSS | Cracked | Exit |")
    lines.append("|------:|-----------|-------:|-----:|--------:|-----:|")
    for t in sorted(trials, key=lambda x: (x["branch"], x["trial"])):
        lines.append(
            f"| {t['trial']:>5} | {t['branch']:<9} | {_fmt_secs(t['wall_seconds']):>6} | "
            f"{_fmt_kb(t['peak_rss_kb']):>4} | {t['cracked']:>7} | {t['exit_status']:>4} |"
        )
    lines.append("")

    lines.append("## Provenance")
    for k in ("blurbdust_sha", "feature_sha", "base_ref", "base_sha", "cand_ref", "cand_sha",
              "host", "gpu", "parts", "hash_count", "hash_seed", "table_source", "started_at"):
        if k in meta:
            lines.append(f"- {k}: `{meta[k]}`")
    lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("results_dir", help="Directory containing trial_*.log/.time")
    p.add_argument("--meta", default="{}", help="JSON string with metadata")
    args = p.parse_args()

    trials = collect_trials(args.results_dir)
    if not trials:
        print(f"No trials found in {args.results_dir}", file=sys.stderr)
        return 1

    summary = summarize(trials)
    try:
        meta = json.loads(args.meta)
    except json.JSONDecodeError as e:
        print(f"Error parsing --meta JSON: {e}", file=sys.stderr)
        return 1

    json_path = os.path.join(args.results_dir, "results.json")
    md_path = os.path.join(args.results_dir, "summary.md")
    with open(json_path, "w") as f:
        json.dump({"meta": meta, "summary": summary, "trials": trials},
                  f, indent=2, sort_keys=True)
    write_summary_md(trials, summary, meta, md_path)
    print(f"Wrote {json_path} and {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
