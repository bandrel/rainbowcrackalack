#!/usr/bin/env python3
"""Benchmark zstd compression of .rt rainbow tables.

Sweeps compression levels through crackalack_rt2zst, measuring ratio,
compression and decompression throughput, and peak RSS, then compares
end-to-end crackalack_lookup time across .rt / .rtc / .rt.zst.
"""
import argparse, json, os, re, shutil, subprocess, sys, time

MAXRSS_RE = re.compile(r"(\d+)\s+maximum resident set size")


def run_timed(cmd):
    """Run cmd under /usr/bin/time -l; return (seconds, peak_rss_bytes, stdout)."""
    wrapped = ["/usr/bin/time", "-l"] + cmd
    t0 = time.monotonic()
    p = subprocess.run(wrapped, capture_output=True, text=True)
    elapsed = time.monotonic() - t0
    m = MAXRSS_RE.search(p.stderr)
    rss = int(m.group(1)) if m else 0
    if p.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({p.returncode}):\n{p.stderr[-2000:]}")
    return elapsed, rss, p.stdout


def sweep_levels(rt2zst, table, levels, workdir):
    raw = os.path.getsize(table)
    rows = []
    for level, threads in levels:
        zst = os.path.join(workdir, f"L{level}T{threads}.rt.zst")
        back = os.path.join(workdir, f"L{level}T{threads}.rt")
        try:
            c_time, c_rss, _ = run_timed([rt2zst, "-l", str(level), "-T", str(threads), table, zst])
        except RuntimeError as e:
            rows.append({"level": level, "threads": threads, "error": str(e).splitlines()[0]})
            continue
        comp = os.path.getsize(zst)
        d_time, d_rss, _ = run_timed([rt2zst, "-d", zst, back])
        identical = subprocess.run(["cmp", "-s", table, back]).returncode == 0
        rows.append({
            "level": level, "threads": threads,
            "raw_bytes": raw, "comp_bytes": comp, "ratio": raw / comp,
            "compress_s": c_time, "compress_mbs": raw / c_time / 1e6, "compress_rss": c_rss,
            "decompress_s": d_time, "decompress_mbs": raw / d_time / 1e6, "decompress_rss": d_rss,
            "roundtrip_ok": identical,
        })
        os.remove(zst); os.remove(back)
    return rows


CRACKED_RE = re.compile(r"Of the \d+ hashes loaded, (\d+) were cracked")


def _clear_pot_files(workdir):
    """Remove any stale john/hashcat pot files from workdir so each lookup run
    starts from a clean state.  Without this, crackalack_lookup can hit its
    "All hashes have already been cracked!" early-exit path against a pot file
    left over from a previous format's run, making the lookup look instant and
    the RSS look tiny regardless of the format actually being read."""
    for name in ("rainbowcrackalack_jtr.pot", "rainbowcrackalack_hashcat.pot"):
        path = os.path.join(workdir, name)
        if os.path.exists(path):
            os.remove(path)


def compare_formats(bins, table, hashfile, workdir, level):
    results = {}
    for fmt in ("rt", "rtc", "zst"):
        d = os.path.join(workdir, "fmt_" + fmt)
        os.makedirs(d, exist_ok=True)
        base = os.path.basename(table)
        dst = os.path.join(d, base)
        shutil.copy(table, dst)
        if fmt == "rtc":
            subprocess.run([bins["rt2rtc"], dst, dst[:-3] + ".rtc"], check=True,
                           capture_output=True)
            os.remove(dst)
        elif fmt == "zst":
            subprocess.run([bins["rt2zst"], "-l", str(level), dst, dst + ".zst"],
                           check=True, capture_output=True)
            os.remove(dst)
        # crackalack_lookup writes its pot file(s) relative to its own cwd, which
        # is the cwd this script was invoked from (subprocess.run below does not
        # chdir), not `d` or `workdir` -- clear all three to be safe.
        _clear_pot_files(os.getcwd())
        _clear_pot_files(workdir)
        _clear_pot_files(d)
        elapsed, rss, out = run_timed([bins["lookup"], d, hashfile])
        m = CRACKED_RE.search(out)
        cracked = bool(m) and int(m.group(1)) > 0
        results[fmt] = {"lookup_s": elapsed, "lookup_rss": rss,
                        "cracked": cracked}
    return results


def render_markdown(table, levels_rows, fmt_results):
    lines = []
    lines.append("## Level sweep\n")
    lines.append("| level | threads | raw bytes | comp bytes | ratio | compress s | compress MB/s | compress RSS | decompress s | decompress MB/s | decompress RSS | roundtrip_ok |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in levels_rows:
        if "error" in r:
            lines.append(f"| {r['level']} | {r['threads']} | ERROR | {r['error']} | | | | | | | | |")
            continue
        ok = "**TRUE**" if r["roundtrip_ok"] else "**FALSE — INVALID ROW**"
        lines.append(
            f"| {r['level']} | {r['threads']} | {r['raw_bytes']} | {r['comp_bytes']} | "
            f"{r['ratio']:.3f} | {r['compress_s']:.3f} | {r['compress_mbs']:.2f} | {r['compress_rss']} | "
            f"{r['decompress_s']:.3f} | {r['decompress_mbs']:.2f} | {r['decompress_rss']} | {ok} |"
        )
    if fmt_results:
        lines.append("\n## Format comparison (crackalack_lookup end-to-end)\n")
        lines.append("| format | lookup s | lookup RSS | cracked |")
        lines.append("|---|---|---|---|")
        for fmt, r in fmt_results.items():
            lines.append(f"| {fmt} | {r['lookup_s']:.3f} | {r['lookup_rss']} | {r['cracked']} |")
        lines.append("\n`.rti2` has no in-tree writer (only `rti2_decompress`), so it is excluded from this comparison.")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--table", required=True)
    ap.add_argument("--levels", default="1,3,9,12,19,22")
    ap.add_argument("--hashfile", default=None)
    ap.add_argument("--out", default="docs/benchmarks")
    ap.add_argument("--tag", default="2026-07-27-zstd-rt")
    args = ap.parse_args()

    levels = [int(x) for x in args.levels.split(",")]
    level_list = [(l, 0) for l in levels] + [(19, os.cpu_count())]

    rt2zst = shutil.which("./crackalack_rt2zst") or os.path.abspath("crackalack_rt2zst")
    rt2rtc = shutil.which("./crackalack_rt2rtc") or os.path.abspath("crackalack_rt2rtc")
    lookup = shutil.which("./crackalack_lookup") or os.path.abspath("crackalack_lookup")
    bins = {"rt2zst": rt2zst, "rt2rtc": rt2rtc, "lookup": lookup}

    os.makedirs(args.out, exist_ok=True)
    workdir = os.path.join(args.out, f".{args.tag}.work")
    os.makedirs(workdir, exist_ok=True)

    rows = sweep_levels(rt2zst, args.table, level_list, workdir)

    fmt_results = None
    if args.hashfile:
        fmt_results = compare_formats(bins, args.table, args.hashfile, workdir, level=19)

    md = render_markdown(args.table, rows, fmt_results)

    json_path = os.path.join(args.out, f"{args.tag}.json")
    md_path = os.path.join(args.out, f"{args.tag}.md")
    with open(json_path, "w") as f:
        json.dump({"table": args.table, "levels": rows, "format_comparison": fmt_results}, f, indent=2)
    with open(md_path, "w") as f:
        f.write(md)

    print(md)
    shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
