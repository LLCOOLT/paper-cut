#!/usr/bin/env python3
"""Backend parity: the same scripted runs on the Aurora and headless builds,
compared frame by frame (TTYD_FINGERPRINT), so headless (the fast backend for
test runs) is known to play exactly as Aurora does.

Each case is a map warp (or none), an input script from tests/port/input and
extra environment. Both games run with TTYD_FAST=1 and a fresh user folder
(skip_intro=1, fullscreen=0), through the shared game slots when present.

usage: backend_parity.py [--region us] [--aurora EXE] [--headless EXE] [--iso ISO]
                         [--only NAME[,NAME]] [--out DIR] [--jobs N] [--reuse-aurora]
  --reuse-aurora  keep the Aurora runs already in --out (only headless changed)
Exit 0 when every case's fingerprints are identical.
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INPUT = os.path.join(ROOT, "tests", "port", "input")
GAME_SLOT = os.environ.get("GAME_SLOT", "")  # a wrapper that waits for a free test-game slot; unset: run directly
REGIONS = {"us": "G8ME01", "jp": "G8MJ01", "eu": "G8MP01"}

# name: (map or None, script, frames after the script, extra env)
CASES = {
    "walk": ("gor_01", "gor_01_walk.txt", 900, {}),
    "battle": ("hei_01,w_bero", "petal_battle.txt", 1500, {"TTYD_STORY": "25"}),
    "walk_maps": ("gor_01", "walk_maps.txt", 600, {}),
    "paper_fence": ("gor_02", "paper_fence.txt", 600,
                    {"TTYD_STORY": "9", "TTYD_AUTO_TEXT": "1", "TTYD_STORY_WATCH": "60",
                     "TTYD_STORY_KICK": "rel_gor__kurihakase_wall_remove", "TTYD_STORY_KICK_AFTER": "60"}),
    "paper_pedestal": ("tik_05", "paper_pedestal.txt", 600,
                       {"TTYD_STORY": "346", "TTYD_AUTO_TEXT": "1", "TTYD_STORY_WATCH": "60",
                        "TTYD_STORY_KICK": "rel_tik__mahojin_event6", "TTYD_STORY_KICK_AFTER": "60"}),
    "paper_ship": ("gor_00", "paper_ship.txt", 600,
                   {"TTYD_STORY": "2", "TTYD_PARTY": "0", "TTYD_AUTO_TEXT": "1", "TTYD_STORY_WATCH": "60"}),
    "auto_battle": ("hei_00", "auto_battle.txt", 600,
                    {"TTYD_AUTO_BATTLE": "1", "TTYD_GOD": "1", "TTYD_AUTO_TEXT": "1", "TTYD_STORY_WATCH": "120"}),
    "save_block": ("gor_01", "save_block.txt", 600, {}),
    "game_over": ("hei_01", "game_over.txt", 600, {}),
    "field": ("gor_01", "interact_field.txt", 600, {"TTYD_PARTY": "1,2,3,4,5,6,7"}),
}


def run(exe, backend, name, case, a, out):
    m, script, frames, extra = case
    d = os.path.join(out, f"{backend}_{name}")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "user"))
    with open(os.path.join(d, "user", "settings.txt"), "w") as f:
        f.write("skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("TTYD_")}
    env.update(TTYD_FAST="1", TTYD_USER_DIR=os.path.join(d, "user"), TTYD_NO_LAUNCHER="1",
               SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", TTYD_EXIT_FRAMES=str(frames),
               TTYD_INPUT=os.path.join(INPUT, script), TTYD_FINGERPRINT=os.path.join(d, "fingerprint.txt"),
               TTYD_CACHE_DIR=os.path.join(out, "cache"), TTYD_SHOT_DIR=os.path.join(d, "shots"))
    if m:
        env["TTYD_MAP"] = m
    env.update(extra)
    cmd = [exe, a.iso]
    if os.path.exists(GAME_SLOT) and not a.no_slot:
        cmd = ["bash", GAME_SLOT] + [c.replace("\\", "/") for c in cmd]
    start = time.time()
    with open(os.path.join(d, "log.txt"), "w") as log:
        r = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=a.timeout,
                           cwd=os.path.dirname(exe))
    return r.returncode, time.time() - start, os.path.join(d, "fingerprint.txt")


def compare(fa, fh):
    with open(fa) as f:
        la = f.read().splitlines()
    with open(fh) as f:
        lh = f.read().splitlines()
    for i, (x, y) in enumerate(zip(la, lh)):
        if x != y:
            return f"first difference at line {i + 1}: aurora '{x[:60]}' headless '{y[:60]}'"
    if len(la) != len(lh):
        return f"aurora {len(la)} frames, headless {len(lh)}"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--region", default="us", choices=sorted(REGIONS))
    ap.add_argument("--aurora", default="")
    ap.add_argument("--headless", default="")
    ap.add_argument("--iso", default="")
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=os.path.join(ROOT, "build-backend-parity"))
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--no-slot", action="store_true")
    ap.add_argument("--reuse-aurora", action="store_true")
    a = ap.parse_args()
    exe = f"ttyd-{a.region}.exe" if os.name == "nt" else f"ttyd-{a.region}"
    a.aurora = a.aurora or os.path.join(ROOT, f"build-pc-{a.region}-aurora", exe)
    a.headless = a.headless or os.path.join(ROOT, f"build-pc-{a.region}-headless", exe)
    if not a.iso:
        d = os.path.join(ROOT, "orig", REGIONS[a.region])
        isos = [f for f in os.listdir(d) if f.lower().endswith(".iso")] if os.path.isdir(d) else []
        a.iso = os.path.join(d, isos[0]) if isos else ""
    if not (os.path.exists(a.aurora) and os.path.exists(a.headless) and os.path.exists(a.iso)):
        sys.exit(f"backend_parity: need {a.aurora}, {a.headless} and the disc ({a.iso or 'orig/'})")
    names = a.only.split(",") if a.only else list(CASES)
    os.makedirs(a.out, exist_ok=True)
    jobs = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for n in names:
            kept = os.path.join(a.out, f"aurora_{n}", "fingerprint.txt")
            if a.reuse_aurora and os.path.exists(kept):
                done = concurrent.futures.Future()
                done.set_result((0, 0.0, kept))
            else:
                done = pool.submit(run, a.aurora, "aurora", n, CASES[n], a, a.out)
            jobs[n] = (done,
                       pool.submit(run, a.headless, "headless", n, CASES[n], a, a.out))
        failed = 0
        for n in names:
            (ra, ta, fa), (rh, th, fh) = jobs[n][0].result(), jobs[n][1].result()
            diff = compare(fa, fh) if os.path.exists(fa) and os.path.exists(fh) else "no fingerprint file"
            ok = diff is None and ra == rh
            failed += not ok
            frames = sum(1 for _ in open(fh)) if os.path.exists(fh) else 0
            print(f"{n}: {'SAME' if ok else 'DIFFERENT'} ({frames} frames; aurora exit {ra} {ta:.0f} s, "
                  f"headless exit {rh} {th:.1f} s){'' if ok else ': ' + (diff or 'exit codes differ')}")
            sys.stdout.flush()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
