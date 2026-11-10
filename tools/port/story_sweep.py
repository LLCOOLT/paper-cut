#!/usr/bin/env python3
"""Play every story beat of the game and check it moves on without crashing.

The beats are the rows of the game's own story table (tests/port/story_beats.json,
from tools/port/story_beats.py). For each row the game starts a new file on
the row's map and entrance with the story just before it and everything the
table's set-up functions give up to it ($TTYD_EVENT, as the game's scenario
select does), and plays on by itself (port/include/port/story_test.h): the
text advanced, choices answered, battles fought by the versus computer with
no damage taken, and, if nothing plays by itself, the script that moves the
story started as the game would (the NPC talked to, the case or map object
triggered). A beat passes once the story moves past the row's start (or, for
rows of one story value played in a row, the next row's map is reached) and
the game runs on a few seconds more.

Each failure is put down to its cause: a crash (with its stack), an
OSPanic, a stuck story (the running scripts listed), a Game Over, a warp that
never arrived, or a run that timed out.

usage: story_sweep.py [--region us|jp|eu] [--exe EXE --disc ISO]
                      [--rows 0-120] [--chapter 0,1,...] [--text STG1_03,...]
                      [-j N] [--out DIR] [--resume] [--timeout S] [--no-slot]

Every run goes through the shared game slots when their script is found
($GAME_SLOT, else game_slot.sh beside the checkout; --no-slot to skip it): a
`bash game_slot.sh <command>` that waits for a free slot. Each has $TTYD_FAST, its own
user folder and the window never focused. Results: <out>/results.jsonl (one
line per beat, appended: --resume skips those done), <out>/summary.md and
summary.json, and each beat's log and last picture in <out>/beats/<row>/.
"""
import argparse
import concurrent.futures
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSIONS = {"us": "G8ME01", "jp": "G8MJ01", "eu": "G8MP01"}

# Exceptions to what story_beats.json derives, by the row's text id (event.c's
# STG..): extra environment for the run (a choice's answer, a script to start),
# "skip" with why, or "note" (shown in the summary). Keep it small: every entry
# says why the derived set-up isn't enough.
OVERRIDES = {
    # the epilogue: about 15,000 frames of scenes over 17 maps (gor_11 to aaa_00) on one story value
    "STG9_00#476": {"env": {"TTYD_STORY_WATCH": "900"}, "note": "epilogue chain, longer than the 180 s watch"},
}

# Settings profiles (--profile NAME, --matrix): settings.txt lines in each run's fresh user folder, and
# environment. Keys: port/settings/features/*.cpp and port/src/*/ (portSettingsDeclare).
PROFILES = {
    "default": {},
    "luigi": {"settings": {"playable_luigi": 1}},
    "peach": {"settings": {"playable_luigi": 2}},
    "bowser": {"settings": {"playable_luigi": 3}},
    "brothers": {"settings": {"brothers_mode": 1, "second_player": 1}},
    # (the picture's shape only changes drawing; headless draws nothing, so these run on Aurora only)
    "wide16x9": {"settings": {"widescreen": 1}, "aurora_only": True},
    "open_world": {"env": {"TTYD_OPEN_SEED": "PAPERCUT"}, "regions": ["us"]},
    "master_quest": {"settings": {"master_quest": 1}},
    "directors_cut": {"settings": {"directors_cut": 1}},
}
MATRIX = ["default", "luigi", "peach", "bowser", "brothers", "open_world", "master_quest", "directors_cut"]

# Failures a warp causes because it skips what earlier maps set up (as map_sweep.py's WARP_ARTIFACTS):
# (map regex, lowest story, highest story, text of the failure, why). Reported apart; never a hard failure.
WARP_ARTIFACTS = [
    # (none known: the Great Tree crashes first taken for one were a port bug, angleABf's NaN index)
]

# The order kicks are tried in: what the player does first
KICK_ORDER = {"talk": 0, "case": 1, "mobj": 2, "npc": 3, "item": 4, "exit": 5, "other": 6, "c": 7, "unknown": 8,
              "door": 9}


def load_beats():
    path = os.path.join(ROOT, "tests", "port", "story_beats.json")
    return json.load(open(path, encoding="utf-8"))


def plan(beats, i):
    """The run's environment for beat i: (env, why-notes)."""
    b = beats[i]
    nxt = beats[i + 1] if i + 1 < len(beats) else None
    env = {"TTYD_EVENT": str(b["index"]), "TTYD_STORY_UNTIL": str(b["prev"] + 1)}
    if b["prev"] == b["id"]:
        # a scene that leaves the story as it was: over once the next scene's map is reached (a chain of
        # scenes on one story value), or Mario has had control back a while
        if nxt is not None and nxt["id"] == b["id"] and nxt["map"] != b["map"]:
            env["TTYD_STORY_UNTIL_MAP"] = nxt["map"]
        env["TTYD_STORY_UNTIL_CONTROL"] = "300"
    kicks = []
    for s in b["sets"]:
        if s["map"] != b["map"] or s["value"] <= b["prev"]:
            continue
        for w in s.get("walk") or []:
            if "walk:" + w not in kicks:
                kicks.append("walk:" + w)  # a script on the way waits for Mario somewhere
        auto = any(t["kind"] in ("init", "enter") for t in s["triggers"])
        if auto:
            continue
        for t in sorted(s["triggers"], key=lambda t: KICK_ORDER.get(t["kind"], 9)):
            if t["kind"] == "exit" and t["detail"] and not t["detail"].startswith("evt_"):
                k = "walk:" + t["detail"]  # leaving by that exit
            elif t["kind"] == "door":
                if not t["detail"]:
                    continue
                k = "case:" + t["detail"]  # the door opened from outside (its scripts need the door's own set-up)
            else:
                k = ("talk:" + t["symbol"]) if t["kind"] == "talk" else t["symbol"]  # (a talk only as a talk)
            if k not in kicks:
                kicks.append(k)
    # walks that each test one axis (a scene waits for x, the one before it for z): one walk to where both hold,
    # tried first
    xs = [w[5:].split(":")[0] for w in kicks if w.startswith("walk:") and ":" in w[5:] and w[5:].split(":")[0] != "_"]
    zs = [w[5:].split(":")[1] for w in kicks if w.startswith("walk:") and ":" in w[5:] and w[5:].split(":")[1] != "_"]
    if xs and zs and f"walk:{xs[0]}:{zs[0]}" not in kicks:
        kicks.insert(0, f"walk:{xs[0]}:{zs[0]}")
    if kicks:
        env["TTYD_STORY_KICK"] = ",".join(kicks[:8])
    env["TTYD_STORY_WATCH"] = "900" if b["battle"] else "180"
    if b["battle"]:
        env["TTYD_GOD_POWER"] = "30"  # (a boss whose barrier comes back (Grodus) falls in the windows it leaves)
    # by the row's text id, or "<text>#<row>" for one of the rows sharing it
    o = OVERRIDES.get(f"{b['text']}#{b['index']}", OVERRIDES.get(b["text"], {}))
    env.update(o.get("env", {}))
    return env, o


def slot_script():
    """The shared game-slot script ($GAME_SLOT, else game_slot.sh beside the checkout), which caps how
    many games the machine's test runs have open at once; None: none."""
    s = os.environ.get("GAME_SLOT", os.path.join(os.path.dirname(ROOT), "game_slot.sh")).replace(os.sep, "/")
    return s if os.path.exists(s) else None


def kill_tree(p):
    """the process and what it started (the slot script's timeout and game, not only the bash in front)"""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
        p.kill()
        return
    import signal
    try:  # (started in a session of its own: its group is the run and its children; never ours)
        group = os.getpgid(p.pid)
        if group == os.getpgrp():
            raise OSError("in the sweep's own process group")
        os.killpg(group, signal.SIGKILL)
    except OSError:
        p.kill()


def classify(out, code):
    """(result, cause): pass, or the failure's kind and a short key."""
    if re.search(r"^\[story\] PASS", out, re.M) and code == 0:
        return "pass", ""
    m = re.search(r"^OSPanic (.*)$", out, re.M)
    if m:
        return "panic", re.sub(r"0x[0-9A-Fa-f]+", "0x?", m.group(1))[:160]
    m = re.search(r"\*\*\* (crash|stall): (.*)\n((?:[ \t]+[0-9a-fA-F]{16} .*\n)*)", out)
    if m:
        frames = [re.sub(r"^\s*\S+\s+", "", l).split(" (")[0] for l in m.group(3).splitlines()][:4]
        return m.group(1), (" < ".join(frames) or m.group(2))[:200]
    m = re.search(r"^\[story\] STUCK: (.*)$", out, re.M)
    if m:
        return "stuck", m.group(1)[:200]
    m = re.search(r"^\[story\] FAIL: (.*)$", out, re.M)
    if m:
        return "gameover" if "Game Over" in m.group(1) else "fail", m.group(1)[:200]
    m = re.search(r"^\[event\] FAIL: (.*)$", out, re.M)
    if m:
        return "warp", m.group(1)[:200]
    if code == "timeout" or code == 124:
        return "timeout", ""
    return "exit", f"exit {code}"


def last_lines(out, pattern, n=3):
    return [l for l in out.splitlines() if re.search(pattern, l)][-n:]


def run_beat(a, beats, i, lock):
    b = beats[i]
    env, o = plan(beats, i)
    if o.get("skip"):
        return {"row": b["index"], "text": b["text"], "name": b["name"], "id": b["id"], "result": "skip",
                "cause": o["skip"], "seconds": 0}
    d = os.path.join(a.out, "beats", f"{b['index']:03d}_{b['text']}")
    shutil.rmtree(d, ignore_errors=True)
    user = os.path.join(d, "user")
    os.makedirs(user)
    prof = PROFILES[a.profile]
    with open(os.path.join(user, "settings.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\n")
        for k, v in prof.get("settings", {}).items():
            f.write(f"{k}={v}\n")
    cache = os.path.join(a.out, "cache")
    os.makedirs(cache, exist_ok=True)
    full = dict(os.environ, TTYD_FAST="1", TTYD_TRACE="1", TTYD_WATCHDOG="60", TTYD_NO_LAUNCHER="1",
                SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", TTYD_WINDOW_POS="-5000,-5000", TTYD_USER_DIR=user,
                TTYD_CACHE_DIR=cache, TTYD_SHOT_DIR=d, TTYD_AUTO_BATTLE="1", TTYD_GOD="1", TTYD_GOD_POWER="8",
                TTYD_AUTO_TEXT="2", TTYD_WARP_LIMIT="90")
    full.update(prof.get("env", {}))  # the profile's, then the beat's own (OVERRIDES', a boss's power)
    full.update(env)
    for k in ("TTYD_MAP", "TTYD_INPUT", "TTYD_STORY", "TTYD_LOAD_SAVE"):
        full.pop(k, None)
    cmd = [a.exe, a.disc]
    if a.timeout:
        cmd = ["timeout", str(a.timeout)] + cmd if a.slot else cmd
    if a.slot:
        cmd = ["bash", a.slot] + [c.replace("\\", "/") for c in cmd]
    t0 = time.time()
    log = os.path.join(d, "log.txt")  # written as it runs, so a long beat can be watched
    with open(log, "wb") as f:
        p = subprocess.Popen(cmd, env=full, stdout=f, stderr=subprocess.STDOUT, start_new_session=os.name != "nt")
        try:
            code = p.wait(timeout=(a.timeout + 3600) if a.slot else a.timeout)  # (a slot may be waited for)
        except subprocess.TimeoutExpired:
            kill_tree(p)
            p.wait()
            code = "timeout"
    secs = time.time() - t0
    out = open(log, encoding="utf-8", errors="replace").read()
    m = re.search(r"\[event\] frame \d+: on ", out)
    result, cause = classify(out, code)
    shutil.rmtree(user, ignore_errors=True)
    stories = [int(x) for x in re.findall(r"^\[story\] frame \d+: -?\d+ -> (\d+)", out, re.M)]
    rec = {"row": b["index"], "text": b["text"], "name": b["name"], "id": b["id"], "prev": b["prev"],
           "map": b["map"], "bero": b["bero"], "kind": b["kind"], "result": result, "cause": cause,
           "seconds": round(secs, 1), "arrived": bool(m), "story_max": max(stories) if stories else None,
           "kicked": last_lines(out, r"^\[story\] frame \d+: kick", 3),
           "line": (last_lines(out, r"^\[story\] (PASS|FAIL|STUCK)", 1) or last_lines(out, r"\S", 1) or [""])[0][:240],
           "shot": os.path.relpath(os.path.join(d, "story.png"), a.out) if os.path.exists(os.path.join(d, "story.png")) else None,
           "env": env, "profile": a.profile}
    if o.get("note"):
        rec["note"] = o["note"]
    for pattern, lo, hi, text, why in WARP_ARTIFACTS:
        if result in ("crash", "panic", "stall") and re.match(pattern, b["map"]) and lo <= b["prev"] <= hi and \
                text in cause:
            rec["warp_artifact"] = why
    if result in ("crash", "panic", "stall") and re.search(r"kick \S+: on its own", out):
        # a script started without the set-up its caller gives it (a user function's, a table's): the
        # crash may be the harness's; the triage checks it in play
        rec["after_plain_kick"] = True
    with lock:
        with open(os.path.join(a.out, "results.jsonl"), "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        print(f"{rec['result']:8s} {b['index']:3d} {b['text']:14s} {b['map']:9s} {secs:6.1f}s  {cause}", flush=True)
    return rec


def rows_wanted(a, beats):
    out = list(range(len(beats)))
    if a.rows:
        keep = set()
        for part in a.rows.split(","):
            lo, _, hi = part.partition("-")
            keep |= set(range(int(lo), int(hi or lo) + 1))
        out = [i for i in out if i in keep]
    if a.chapter:
        ch = {int(c) for c in a.chapter.split(",")}
        out = [i for i in out if beats[i]["stage"] in ch]
    if a.text:
        t = set(a.text.split(","))
        unknown = t - {b["text"] for b in beats}
        if unknown:  # (a misspelt text id selects nothing: never a sweep of no beats that passes)
            raise SystemExit(f"story_sweep: no such text id: {', '.join(sorted(unknown))}")
        out = [i for i in out if beats[i]["text"] in t]
    return out


def summary(a, beats):
    recs = {}
    path = os.path.join(a.out, "results.jsonl")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            r = json.loads(line)
            recs[r["row"]] = r  # the last run of each row
    rs = [recs[k] for k in sorted(recs)]
    passed = sum(1 for r in rs if r["result"] == "pass")
    secs = sum(r["seconds"] for r in rs)
    by = {}
    for r in rs:
        if r["result"] != "pass":
            by.setdefault((r["result"], r["cause"]), []).append(r)
    lines = [f"# Story sweep ({a.region}, profile {a.profile})", "",
             f"{len(rs)} beats run, {passed} passed ({100.0 * passed / max(1, len(rs)):.1f}%), "
             f"{len(rs) - passed} not; {secs / 60:.0f} min of runs in all.", ""]
    for (res, cause), group in sorted(by.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        lines.append(f"- **{res}** ({len(group)}): {cause}")
        for r in group[:20]:
            lines.append(f"  - row {r['row']} {r['text']} \"{r['name']}\" {r['map']} {r.get('bero') or ''}: "
                         f"{r['line']}")
    open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
    json.dump({"region": a.region, "beats": len(rs), "passed": passed, "seconds": round(secs),
               "failures": [r for r in rs if r["result"] != "pass"]},
              open(os.path.join(a.out, "summary.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("\n".join(lines[:4]))
    return passed, len(rs)


# results that are the game's (or the port's) fault whatever the row: never a harness limit
HARD = ("crash", "panic", "stall")  # (a Game Over with no damage taken is a challenge the harness failed)


def verdict(a, want=None):
    """The exit code: with --baseline, 1 on a hard failure or a row it lists as passing that didn't (or
    wasn't run); without, 1 unless every row passed; 2 when there are no results at all."""
    recs = {}
    path = os.path.join(a.out, "results.jsonl")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            r = json.loads(line)
            recs[r["row"]] = r
    if not recs:  # (no beat run: nothing passed)
        print(f"story_sweep: no results in {a.out}")
        return 2
    if not a.baseline:
        return 0 if all(r["result"] == "pass" for r in recs.values()) else 1
    if a.update_baseline:
        keep = {}
        if os.path.exists(a.baseline):
            keep = {int(k): v for k, v in json.load(open(a.baseline, encoding="utf-8"))["rows"].items()}
        for k, r in recs.items():
            keep[k] = r["result"] if r["result"] != "pass" else "pass"
        with open(a.baseline, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"about": "tools/port/story_sweep.py --update-baseline: each row's result when last "
                                "recorded; the sweep fails when a row that passed doesn't",
                       "rows": {str(k): keep[k] for k in sorted(keep)}}, f, indent=0)
            f.write("\n")
        print(f"story_sweep: wrote {a.baseline}")
        return 0
    if not os.path.exists(a.baseline):  # (a missing baseline would let every row that no longer passes through)
        print(f"story_sweep: no baseline {a.baseline} (--update-baseline writes it)")
        return 2
    base = {int(k): v for k, v in json.load(open(a.baseline, encoding="utf-8"))["rows"].items()}
    if not base:
        print(f"story_sweep: the baseline {a.baseline} lists no rows")
        return 2
    bad = [r for r in recs.values() if r["result"] in HARD and not r.get("after_plain_kick") and not r.get("warp_artifact")]
    bad += [r for r in recs.values() if r["result"] != "pass" and base.get(r["row"]) == "pass" and r not in bad]
    for r in sorted(bad, key=lambda r: r["row"]):
        print(f"story_sweep: FAIL row {r['row']} {r['text']} ({r['map']}): {r['result']} {r['cause']}")
    # (a row the baseline passes that this sweep wanted but has no result for: never a pass)
    missing = [i for i in (want or []) if i not in recs and base.get(i) == "pass"]
    for i in missing:
        print(f"story_sweep: FAIL row {i}: no result (the baseline has it passing)")
    return 1 if bad or missing else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", default="us", choices=sorted(VERSIONS))
    ap.add_argument("--backend", default="headless", choices=["headless", "aurora"],
                    help="the build to run (default headless: no window, much faster; frame for frame as Aurora)")
    ap.add_argument("--profile", default="default", choices=sorted(PROFILES),
                    help="settings for every run (PROFILES): Play as, Brothers mode, the randomizer, Master Quest...")
    ap.add_argument("--matrix", action="store_true",
                    help="the beats under every profile of MATRIX in turn, each in <out>/<profile>")
    ap.add_argument("--exe", default="")
    ap.add_argument("--disc", default="")
    ap.add_argument("--rows", default="", help="rows of the story table: 0-120,300")
    ap.add_argument("--chapter", default="", help="stages: 0 the prologue .. 8, 9 the ending")
    ap.add_argument("--text", default="", help="rows by their text id: STG1_03,STG8_16")
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--out", default="")
    ap.add_argument("--resume", action="store_true", help="skip the beats results.jsonl already has")
    ap.add_argument("--retry-failed", action="store_true", help="with --resume: run the failed ones again")
    ap.add_argument("--timeout", type=int, default=900, help="seconds per beat (wall)")
    ap.add_argument("--no-slot", action="store_true", help="run without the shared game slots")
    ap.add_argument("--summary", action="store_true", help="only write the summary of what's in --out")
    ap.add_argument("--baseline", default="",
                    help="JSON of the rows known to pass (tests/port/story_baseline_<region>.json): fail only on "
                         "a crash, a panic or a stall, or on a "
                         "listed row that no longer passes")
    ap.add_argument("--update-baseline", action="store_true", help="write --baseline from this run's results")
    ap.add_argument("--build", action="store_true",
                    help="configure (once) and build the region's Aurora build first, as local_ci.py does (CI)")
    a = ap.parse_args()
    v = VERSIONS[a.region]
    if a.build and not a.exe:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import local_ci
        d = local_ci.build_dir(a.region, a.backend)
        if not os.path.exists(os.path.join(d, "CMakeCache.txt")):
            subprocess.run(["cmake", "-S", ROOT, "-B", d] + local_ci.configure_args(v, a.backend), env=local_ci.ENV,
                           check=True)
        ninja = shutil.which("ninja", path=local_ci.ENV.get("PATH")) or "ninja"
        a.exe = os.path.join(d, f"ttyd-{a.region}.exe")
        if subprocess.run([ninja, "-C", d, "-j8", f"ttyd-{a.region}.exe"], env=local_ci.ENV).returncode != 0:
            print("story_sweep: the build failed")
            return 2
    if not a.exe:
        if a.backend == "aurora":
            a.exe = os.path.join(ROOT, f"build-pc-{a.region}-aurora", f"ttyd-{a.region}.exe")
        else:
            a.exe = next((e for e in (os.path.join(ROOT, f"build-pc-{v}-headless", f"ttyd-{a.region}.exe"),
                                      os.path.join(ROOT, f"build-pc-{a.region}-headless", f"ttyd-{a.region}.exe"),
                                      os.path.join(ROOT, f"build-pc-{a.region}", f"ttyd-{a.region}.exe"))
                          if os.path.exists(e)), os.path.join(ROOT, f"build-pc-{v}-headless", f"ttyd-{a.region}.exe"))
    a.exe = os.path.abspath(a.exe)
    if not a.disc:
        isos = glob.glob(os.path.join(ROOT, "orig", v, "*.iso"))
        a.disc = isos[0] if isos else ""
    a.out = os.path.abspath(a.out or os.path.join(ROOT, "build-story", a.region))
    os.makedirs(a.out, exist_ok=True)
    data = load_beats()
    beats = data["beats"]
    if a.summary:
        summary(a, beats)
        return verdict(a)
    if not os.path.exists(a.exe) or not os.path.exists(a.disc):
        print(f"story_sweep: no {a.exe if not os.path.exists(a.exe) else a.disc}: skipped")
        return 77
    a.slot = None if a.no_slot else slot_script()
    if not a.matrix:
        return run_profile(a, beats)
    # the settings matrix: every profile in turn, each in a folder of its own
    base, worst = a.out, 0
    for name in MATRIX:
        prof = PROFILES[name]
        if a.region not in prof.get("regions", [a.region]) or (prof.get("aurora_only") and a.backend != "aurora"):
            print(f"story_sweep: profile {name}: not for {a.region}/{a.backend}, skipped")
            continue
        a.profile, a.out = name, os.path.join(base, name)
        os.makedirs(a.out, exist_ok=True)
        print(f"\n=== profile {name}", flush=True)
        worst = max(worst, run_profile(a, beats))
    return worst


def run_profile(a, beats):
    """The wanted beats under a.profile, into a.out; the exit code."""
    want = rows_wanted(a, beats)
    if not want:  # (the filters chose no beat: not a sweep that passed)
        print("story_sweep: --rows/--chapter/--text select no beat")
        return 2
    rows = [beats[i]["index"] for i in want]
    if a.resume and os.path.exists(os.path.join(a.out, "results.jsonl")):
        done = {}
        for line in open(os.path.join(a.out, "results.jsonl"), encoding="utf-8"):
            r = json.loads(line)
            done[r["row"]] = r["result"]
        want = [i for i in want if i not in done or (a.retry_failed and done[i] != "pass")]
    elif not a.resume:
        open(os.path.join(a.out, "results.jsonl"), "w").close()
    print(f"story_sweep: {len(want)} beats on {a.region} ({os.path.basename(a.exe)}, profile {a.profile}), {a.j} at once"
          f"{' through the game slots' if a.slot else ''}", flush=True)
    lock = threading.Lock()
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        list(pool.map(lambda i: run_beat(a, beats, i, lock), want))
    print(f"\nstory_sweep: {len(want)} beats in {(time.time() - t0) / 60:.1f} min")
    summary(a, beats)
    return verdict(a, rows)


if __name__ == "__main__":
    sys.exit(main())
