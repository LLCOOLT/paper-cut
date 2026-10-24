#!/usr/bin/env python3
"""Load every save in a folder of .gci files and play each a little.

Each save gets a fresh card folder (<user>/<region>/Card A/, as the port's
card reads it); the game loads it through its own file menu and plays about
20 s, pressing A through what the save starts in (tests/port/input/
save_sweep.txt). Reports, per save, the map and story value it loaded into,
and groups failures (crash, stall, not loaded) by cause, as map_sweep does.
The saves can be anyone's: speedrun practice saves cover the whole story
with the real state of a played game (tools/port/gcmemcard.py takes them out
of raw card images).

usage: save_sweep.py EXE GCI_DIR [--disc ISO] [-j N] [--logs DIR] [--shots DIR] [--only SUBSTRING]
"""
import argparse
import collections
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from map_sweep import signature  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REGION = {"G8MJ": "JAP", "G8ME": "USA"}


def run(exe, disc, gci, rel, work, shots, timeout):
    code = open(gci, "rb").read(4).decode("ascii", "replace")
    user = os.path.join(work, re.sub(r"[^\w]+", "_", rel))
    card = os.path.join(user, REGION.get(code, "USA"), "Card A")
    os.makedirs(card, exist_ok=True)
    shutil.copy(gci, os.path.join(card, f"01-{code}-mariost_save_file.gci"))
    env = dict(os.environ, TTYD_FAST="1", TTYD_TRACE="1", TTYD_WATCHDOG="30", TTYD_USER_DIR=user,
               TTYD_CACHE_DIR=os.path.join(user, "_cache"), TTYD_WINDOW_POS="-4000,0",
               TTYD_INPUT=os.path.join(ROOT, "tests", "port", "input", "save_sweep.txt"))
    env.pop("TTYD_MAP", None)
    # the script's captures: --shots, else the run's own folder (never the cwd)
    env["TTYD_SHOT_DIR"] = os.path.join(shots, re.sub(r"[^\w]+", "_", rel)) if shots else user
    try:
        r = subprocess.run([exe] + ([disc] if disc else []), env=env, capture_output=True, text=True,
                           errors="replace", timeout=timeout)
        out, rc = r.stdout + r.stderr, r.returncode
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        rc = "timeout"
    where = re.findall(r"sequence 2, map '(\w*)', area '\w*', story (\d+)", out)
    loaded = f"{where[0][0]} story {where[0][1]}" if where else "-"
    if rc == 0 and "[input] end" in out:
        return rel, loaded, None, out
    return rel, loaded, signature(out, rc), out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("gci_dir")
    ap.add_argument("--disc", default="")
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--logs", default="")
    ap.add_argument("--shots", default="")
    ap.add_argument("--only", default="")
    ap.add_argument("--timeout", type=int, default=300)
    a = ap.parse_args()
    saves = []
    for root, _, files in os.walk(a.gci_dir):
        for f in sorted(files):
            if f.endswith(".gci"):
                rel = os.path.relpath(os.path.join(root, f), a.gci_dir).replace(os.sep, "/")
                # the practice collection's "Empty" save has no file 1 to load:
                # the menu offers a new game instead (not something to sweep)
                if a.only in rel and not any(part.startswith("Empty") for part in rel.split("/")):
                    saves.append((os.path.join(root, f), rel[:-4]))
    work = tempfile.mkdtemp(prefix="save_sweep_")
    groups = collections.defaultdict(list)
    passed = 0
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        for rel, loaded, sig, out in pool.map(
                lambda s: run(os.path.abspath(a.exe), a.disc, s[0], s[1], work,
                              os.path.abspath(a.shots) if a.shots else None, a.timeout), sorted(saves)):
            print(f"{'ok  ' if sig is None else 'FAIL'} {rel:60s} {loaded}" + (f"   {sig}" if sig else ""), flush=True)
            if sig is None:
                passed += 1
                continue
            groups[sig].append(rel)
            if a.logs:
                os.makedirs(a.logs, exist_ok=True)
                open(os.path.join(a.logs, re.sub(r"[^\w]+", "_", rel) + ".log"), "w", encoding="utf-8").write(out)
    shutil.rmtree(work, ignore_errors=True)
    print(f"\nsave_sweep: {len(saves)} saves, {passed} passed, {len(saves) - passed} failed, {len(groups)} distinct causes")
    for sig, ns in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"{len(ns):4d}  {sig}\n      {' '.join(ns[:8])}{' ...' if len(ns) > 8 else ''}")
    return 1 if groups else 0


if __name__ == "__main__":
    sys.exit(main())
