#!/usr/bin/env python3
"""The full run's segments side by side: each from its own checkpoint
(tests/port/full_run/checkpoints/<region>/), N at a time, with
tools/port/full_run.py --only for each, on the headless backend by default.

The chained run (full_run.py alone) plays every segment from the save the one
before it wrote, so it can only go one at a time; this runs the segments
independently, so a whole playthrough's check takes about as long as its
longest segments instead of all of them together. Each segment's checkpoint
is what a chained run recorded (--record), so both check the same thing as
long as the checkpoints are current.

usage: full_run_parallel.py [--jobs N] [--from N] [--to N] [--out DIR] [full_run.py options...]
Exit 0 when every segment passed.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SEG_DIR = os.path.join(ROOT, "tests", "port", "full_run")
FULL_RUN = os.path.join(ROOT, "tools", "port", "full_run.py")


sys.path.insert(0, os.path.dirname(FULL_RUN))
import full_run  # noqa: E402  (its segment lists)


def segments(lo, hi, which):
    """[(set, index, name)]: the TTYD set's segments and/or the PM64 set's (full_run.py --pm64), lo..hi of each"""
    out = []
    for s in (["ttyd", "pm64"] if which == "both" else [which]):
        out += [(s, n, name) for n, name, _ in full_run.segments(s == "pm64") if lo <= n <= hi]
    # (the PM64 set's games are Aurora's, slower: started first, so the run ends with the short ones)
    return sorted(out, key=lambda x: x[0] != "pm64")


def run(st, n, name, a, rest):
    out = os.path.join(a.out, st, name)
    cmd = [sys.executable, FULL_RUN, "--only", str(n), "--out", out] + rest + (["--pm64"] if st == "pm64" else [])
    start = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    lines = [l for l in r.stdout.splitlines() if l.startswith(f"[full_run] {name}:")]
    return st, n, name, r.returncode, time.time() - start, lines[-1] if lines else (r.stdout + r.stderr).strip()[-300:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 3))
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=99)
    ap.add_argument("--out", default=os.path.join(ROOT, "build-full-run-parallel"))
    ap.add_argument("--set", dest="which", default="ttyd", choices=("ttyd", "pm64", "both"),
                    help="ttyd (default; headless), pm64 (full_run.py --pm64: Aurora off screen) or both at once")
    a, rest = ap.parse_known_args()
    if "--backend" not in rest:
        rest += ["--backend", "headless"]  # (the PM64 set's are Aurora's whatever this says)
    segs = segments(a.lo, a.hi, a.which)
    if not segs:
        sys.exit("full_run_parallel: no segments")
    os.makedirs(a.out, exist_ok=True)
    start = time.time()
    failed = 0
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for st, n, name, rc, secs, line in pool.map(lambda s: run(s[0], s[1], s[2], a, rest), segs):
            failed += rc != 0
            rows.append((st, name, rc, secs, line))
            print(f"{st} {name}: exit {rc}, {secs:.0f} s: {line}")
            sys.stdout.flush()
    wall = time.time() - start
    with open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8") as f:
        f.write(f"# Full run, side by side ({a.which}, {a.jobs} at a time)\n\n{len(segs)} segments, {failed} failed, "
                f"{wall:.0f} s wall, {sum(r[3] for r in rows):.0f} s of runs\n\n| set | segment | exit | time | result |\n"
                "|---|---|---|---|---|\n")
        for st, name, rc, secs, line in rows:
            f.write(f"| {st} | {name} | {rc} | {secs:.0f} s | {line.replace('|', '/')} |\n")
    print(f"[full_run_parallel] {len(segs)} segments, {a.jobs} at a time: {wall:.0f} s, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
