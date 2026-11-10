#!/usr/bin/env python3
"""Long scenes at a player's pace: each scene below played headless with the
test hooks reading every message slowly (TTYD_AUTO_TEXT_DELAY), and checked
to end (TTYD_STORY_UNTIL) rather than stall (TTYD_STORY_WATCH).

Why: the fast runs press A at once, so a long one-script scene is over in
seconds. A player reading at their own pace keeps its scripts running for
minutes, and some bugs only show then: in 0.4.0 a WAIT_MS still running when
its script passed 53 s (2^31 timer ticks; then every 106 s) never ended, and
Goombella's joining scene in the plaza (gor_01, christine_nakama) froze for a
player. Each scene runs at a few reading speeds, so its waits fall on the 53 s
mark at different places.

usage: slow_reader.py EXE ISO [--only NAME] [--play-as N,...] [--delays 180,240,...] [--jobs 3]
Exit 0 when every run ended; 1 when one stalled or failed; 77 with no disc.
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

GAME_SLOT = os.environ.get("GAME_SLOT", "")  # a wrapper that waits for a free test-game slot; unset: run directly

# name: (TTYD_MAP (map,bero), story at the start, story that ends it, partner, its script, what it is)
SCENES = {
    "goombella_joins": ("gor_01,s_bero", 6, 7, "0", "christine_nakama",
                        "gor_01: Goombella joins (the 0.4.0 freeze, holding the map)"),
}

# frames each message page is held: about 3 to 8 s a page, as people read
DELAYS = [180, 300, 480]


def run(a, name, delay, play_as):
    m, start, until, party, script, _ = SCENES[name]
    os.makedirs(a.out, exist_ok=True)
    out = tempfile.mkdtemp(prefix=f"{name}_{delay}_{play_as}_", dir=a.out)
    user = os.path.join(out, "user")
    os.makedirs(user)
    with open(os.path.join(user, "settings.txt"), "w") as f:
        f.write("skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("TTYD_")}
    env.update(TTYD_FAST="1", TTYD_USER_DIR=user, TTYD_MAP=m, TTYD_STORY=str(start), TTYD_PARTY=party,
               TTYD_STORY_UNTIL=str(until), TTYD_STORY_SETTLE="60", TTYD_AUTO_TEXT="1",
               TTYD_AUTO_TEXT_DELAY=str(delay), TTYD_STORY_WATCH="900",  # (the scene itself takes minutes at this pace)
               TTYD_WATCHDOG="120", TTYD_GOD="1", TTYD_AUTO_BATTLE="1", TTYD_EXIT_FRAMES="1000000",
               TTYD_SET_playable_luigi=str(play_as))
    cmd = [a.exe, a.iso]
    if os.path.exists(GAME_SLOT):
        cmd = ["bash", GAME_SLOT, "timeout", "1800"] + [c.replace(os.sep, "/") for c in cmd]
    with open(os.path.join(out, "log.txt"), "wb") as log:
        code = subprocess.call(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=os.path.dirname(a.exe))
    with open(os.path.join(out, "log.txt"), "rb") as f:
        text = f.read().decode("utf-8", errors="replace")
    ok = code == 0 and "[story] PASS" in text
    if ok:
        shutil.rmtree(out, ignore_errors=True)
    else:
        stuck = [l for l in text.splitlines() if l.startswith("[story]") and ("STUCK" in l or script in l)]
        print("\n".join(stuck[-6:]))
    print(f"[slow_reader] {name} delay {delay} play-as {play_as}: {'PASS' if ok else f'FAIL (exit {code}, {out})'}")
    sys.stdout.flush()
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("iso", nargs="?", default="")
    ap.add_argument("--only", default="")
    ap.add_argument("--play-as", default="0", help="Play as picks, comma separated (0 Mario .. 3 Bowser)")
    ap.add_argument("--delays", default=",".join(map(str, DELAYS)))
    ap.add_argument("--jobs", type=int, default=3, help="games at once")
    ap.add_argument("--out", default="build-slow-reader", help="each run's folder (kept when it fails)")
    a = ap.parse_args()
    if not a.iso or not os.path.exists(a.iso):
        print("slow_reader: no disc image; skipped")
        return 77
    a.exe = os.path.abspath(a.exe)
    a.out = os.path.abspath(a.out)
    jobs = [(name, d, pick) for name in SCENES if not a.only or name == a.only
            for pick in [int(p) for p in a.play_as.split(",")] for d in [int(x) for x in a.delays.split(",")]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        results = list(pool.map(lambda j: run(a, *j), jobs))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
