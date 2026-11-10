#!/usr/bin/env python3
"""Revisit sweep: every room again at later points of the story.

The story tests follow the story forwards, so they never enter a room after
its chapter is over; rooms set themselves up by the story's progress, so a
revisit runs code no story test touches (a player's crash came from the
Excess Express after chapter 6). This boots every map of each area at the
story points after that area's chapter:

  - the start of every later chapter (the first is "right after this area's
    chapter ended"): the story table's STGn_00 rows (src/event.c's event_dt),
  - the post-game: its STG9_END row (Rogueport after the credits).

Each run is a new file set up as that row sets it up (story value, set-up
functions, partners: $TTYD_STORY_ROW with the $TTYD_MAP warp), on the map at
one of its entrances. An input script lets the room settle (cutscenes over,
Mario in control), walks a short pattern, talks to every NPC (talk_all) and
leaves by an exit (entrance k leaves by the next exit, so every entrance and
every exit is used once per story point). TTYD_GOD, TTYD_AUTO_BATTLE and
TTYD_AUTO_TEXT resolve battles and talks.

A run fails on a crash (non-zero exit, a fault, a panic), a hang (no frame:
the watchdog; or the run's time limit), a room that never gives Mario
control (STUCK), a Game Over, or the game going to a map it shouldn't (one
the room's scripts never name, or another than the exit's own). An exit
walk_to can't reach is a warning. Harness limits (states a warp makes that
play can't) are listed in HARNESS_LIMITS and reported apart.

usage: revisit_sweep.py ISO [--exe EXE] [--version G8ME01] [-j N] [--areas gor,rsh]
                        [--points ch7,post] [--maps rsh_01_a,...] [--entries first|all]
                        [--out DIR] [--resume] [--timeout S] [--no-slot]

Runs go through $GAME_SLOT when it is set (a wrapper for shared game
slots; --no-slot runs them directly), each with TTYD_FAST=1 and a fresh user
folder. Results: OUT/results.jsonl (one line a run; --resume skips those
done), OUT/summary.json, OUT/summary.md, OUT/logs/<run>.log for every run
that did not pass.
"""
import argparse
import collections
import concurrent.futures
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SLOT = os.environ.get("GAME_SLOT", "")  # a wrapper that waits for a free test-game slot; unset: run directly

VERSIONS = {"G8MJ01": 2, "G8ME01": 3, "G8MP01": 6}
VERSION_NAMES = {"VERSION_DEMO_US": 0, "VERSION_DEMO_JP": 1, "VERSION_JP": 2, "VERSION_US": 3,
                 "VERSION_DEMO_US2": 4, "VERSION_DEMO_EU": 5, "VERSION_EU": 6}

# Each area (map prefix) and the chapter whose story it belongs to: it is
# revisited from the start of the next chapter on. 0: the prologue's areas
# and those open all game (Rogueport, its sewers, the Pit, the parlour's games).
AREA_CHAPTER = {
    "gor": 0, "tik": 0, "aaa": 0, "jon": 0, "yuu": 0, "qiz": 0,
    "hei": 1, "nok": 1, "gon": 1,
    "win": 2, "mri": 2,
    "tou": 3,
    "usu": 4, "gra": 4, "jin": 4,
    "muj": 5, "dou": 5,
    "hom": 6, "eki": 6, "rsh": 6, "pik": 6,
    "bom": 7, "moo": 7, "aji": 7,
    "las": 8,
}
# Not revisited: interludes and maps play never comes back to (Bowser's
# side-scrolling levels, the credits, the opening, the digest movie, test
# and battle-test stages).
SKIP_AREAS = {"kpa", "end", "dmo", "dig", "sys", "stg", "tst", "bti"}
# Areas with channels Mario crosses on a boat (Pirate's Grotto): an entrance
# whose walk-in never ends is tried again on the boat ($TTYD_MARIO_SHIP)
BOAT_AREAS = {"dou"}

# Failures a warp's state causes that play can't reach: (run regex, failure
# kind or signature text, why). Reported apart; they do not fail a sweep.
HARNESS_LIMITS = [
    # (the paper-peel stall and auto-battle's loops are fixed in the shared
    # build 62683b835: both are judged again)
]

# Rooms whose own map change is part of the room (a cutscene that moves on, a
# ride): (map regex, the maps it may go to).
EXTRA_TARGETS = {
}


# --- the story table -------------------------------------------------------

def _preprocess(text, version):
    """src/event.c's VERSION #if blocks, kept or dropped for this version."""
    out, stack = [], []  # stack: (this branch on, any branch taken)
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"#if\s+VERSION\s*(>=|<=|==|<|>|!=)\s*(\w+)", s)
        if m:
            v = VERSION_NAMES[m.group(2)]
            on = {"<=": version <= v, ">=": version >= v, "==": version == v, "<": version < v,
                  ">": version > v, "!=": version != v}[m.group(1)]
            stack.append([on, on])
            continue
        if s.startswith("#if"):
            stack.append([True, True])  # (none in the tables; kept)
            continue
        if s.startswith("#else") and stack:
            stack[-1][0] = not stack[-1][1]
            continue
        if s.startswith("#endif") and stack:
            stack.pop()
            continue
        if all(on for on, _ in stack):
            out.append(line)
    return "\n".join(out)


def story_rows(version):
    """Every row of the story table in order: dicts of row (the TTYD_EVENT /
    TTYD_STORY_ROW index), stage, id, text id, name, map, story (the value a
    warp to it sets: the row before's id)."""
    t = _preprocess(open(os.path.join(ROOT, "src", "event.c"), encoding="utf-8").read(), version)
    rows, prev = [], 0
    for stage in range(10):
        m = re.search(r"EventData stage%d_evt\[\] = \{(.*?)\n\};" % stage, t, re.S)
        if not m:
            raise SystemExit(f"revisit_sweep: no stage{stage}_evt in src/event.c")
        for r in re.finditer(r'\{\s*(\d+),\s*\d+,\s*\d+,\s*(\d+),\s*"(\w+)",\s*"[^"]*"\s*(?:NAME_EN\("([^"]*)"\))?,'
                             r'\s*"(\w*)",\s*(?:"(\w*)"|NULL)', m.group(1)):
            rows.append({"row": len(rows), "stage": stage, "id": int(r.group(2)), "text": r.group(3),
                         "name": (r.group(4) or "").strip(), "map": r.group(5), "bero": r.group(6) or "",
                         "ship": r.group(1) == "1", "story": prev})
            prev = int(r.group(2))
    return rows


def story_points(version):
    """The points revisits start from: {name: row}, ch1..ch8 (each chapter's
    first row) and post (the post-game's last row)."""
    rows = story_rows(version)
    points = {}
    for n in range(1, 9):
        r = next((r for r in rows if r["text"] == f"STG{n}_00"), None)
        if r is None:
            raise SystemExit(f"revisit_sweep: no STG{n}_00 row")
        points[f"ch{n}"] = r
    points["post"] = next(r for r in rows if r["text"] == "STG9_END")
    return points


def points_for(area):
    """Later chapters' starts after the area's own, and the post-game."""
    c = AREA_CHAPTER[area]
    return [f"ch{n}" for n in range(c + 1, 9)] + ["post"]


# --- maps, entrances and exits --------------------------------------------

def map_source(name):
    """The map's source; a map's variants (rsh_01_a, _b, _c: the train by
    day, evening and night) share one (rsh_01.c)."""
    for n in (name, re.sub(r"_[a-z]$", "", name)):
        for f in (glob.glob(os.path.join(ROOT, "rels", "*", "src", n + ".c")) +
                  glob.glob(os.path.join(ROOT, "rels", "*", "src", "*", n + ".c"))):
            return f
    return None


def maps_of(files_dir):
    """The maps of the game's map table (src/data/mapdata.c) on the disc."""
    table = open(os.path.join(ROOT, "src", "data", "mapdata.c"), encoding="utf-8").read()
    known = sorted(set(re.findall(r'^MapData \w+ = \{\s*"(\w+)"', table, re.M)))
    if files_dir and os.path.isdir(os.path.join(files_dir, "m")):
        m = os.path.join(files_dir, "m")
        known = [n for n in known if os.path.exists(os.path.join(m, n, "d"))]
    return known


def beros(name):
    """[(hitName, nextMapName or None)] of every BeroEntry in the map's source,
    in order, each name once (a map with several tables by story lists the
    targets of all)."""
    f = map_source(name)
    if f is None:
        return []
    t = open(f, encoding="utf-8", errors="replace").read()
    out = collections.OrderedDict()
    for m in re.finditer(r'"(\w+)"\)*,\s*//\s*hitName(.*?)//\s*nextMapName', t, re.S):
        nxt = re.search(r'"(\w+)"\s*,\s*//\s*nextMapName', m.group(0))
        out.setdefault(m.group(1), set())
        if nxt:
            out[m.group(1)].add(nxt.group(1))
    return [(h, sorted(v)) for h, v in out.items()]


def bero_links(name):
    """[(hitName, nextMapName, nextBeroName)] of the map's BeroEntries."""
    f = map_source(name)
    if f is None:
        return []
    t = open(f, encoding="utf-8", errors="replace").read()
    return re.findall(r'"(\w+)"\)*,\s*//\s*hitName.*?"(\w+)",\s*//\s*nextMapName\s*'
                      r'\(*(?:\(char\*\))?"(\w*)"\)*,\s*//\s*nextBeroName', t, re.S)


def boat_entrances(rows, names):
    """(map, bero) pairs Mario comes in by on the boat: the story table's boat
    rows (motion 1: Pirate's Grotto's channels), and the far ends of their
    beros, and theirs in turn."""
    boat = {(r["map"], r["bero"]) for r in rows if r["ship"] and r["bero"]}
    links = {n: bero_links(n) for n in {m for m, _ in boat} | {n for n in names if n.startswith("dou_")}}
    grew = True
    while grew:
        grew = False
        for m, ls in links.items():
            for hit, nm, nb in ls:
                for a, b in (((m, hit), (nm, nb)), ((nm, nb), (m, hit))):
                    if a in boat and b not in boat and b[1]:
                        boat.add(b)
                        grew = True
    return boat


def named_maps(name):
    """Every map name the map's source (and its area's common source) names:
    where its scripts may send Mario."""
    names = set()
    f = map_source(name)
    files = [f] if f else []
    if f:
        src = f[:f.index(os.sep + "src" + os.sep) + 5] if os.sep + "src" + os.sep in f else os.path.dirname(f)
        area = os.path.basename(os.path.dirname(src))
        common = os.path.join(src, area + ".c")
        if os.path.exists(common):
            files.append(common)
        files += glob.glob(os.path.join(src, "event", "*.c"))
    for g in files:
        names |= set(re.findall(r'"([a-z]{3}_\d\d(?:_\w)?)"', open(g, encoding="utf-8", errors="replace").read()))
    return names


# --- one run ---------------------------------------------------------------

SCRIPT = """# revisit_sweep: settle, walk, talk, leave
press_until_control a 2400
hold up 15
hold right 15
hold down 15
hold left 15
press_until_control a 1200
talk_all 2400
press_until_control a 2400
{exit}
end
"""


def plan(version, files_dir, areas, points_filter, maps_filter, entries):
    points = story_points(VERSIONS[version])
    rows = story_rows(VERSIONS[version])
    boat = boat_entrances(rows, maps_of(files_dir))
    items = []
    for name in maps_of(files_dir):
        area = name.split("_")[0]
        if area in SKIP_AREAS or area not in AREA_CHAPTER:
            continue
        if areas and area not in areas:
            continue
        if maps_filter and name not in maps_filter:
            continue
        bs = beros(name)
        hits = [b for b, _ in bs]
        ins = hits if entries == "all" else hits[:1]
        if not ins:
            ins = [None]  # no entrance: the map's origin
        # the forward visit: the story table's first row in this area (the
        # story as play first brings Mario here), to tell a revisit's failure
        # from one the room has anyway
        home = next((r["row"] for r in rows if r["map"] == name), None)
        if home is None:
            home = next((r["row"] for r in rows if r["map"].startswith(area + "_")), points["ch1"]["row"])
        for p in points_for(area):
            if points_filter and p not in points_filter:
                continue
            for k, b in enumerate(ins):
                out = hits[(k + 1) % len(hits)] if hits else None
                key = f"{name}%{p}@{b or 'origin'}"
                items.append({"key": key, "map": name, "point": p, "row": points[p]["row"],
                              "story": points[p]["story"], "entry": b, "exit": out, "home_row": home, "ship": (name, b) in boat,
                              "exit_to": dict(bs).get(out, []) if out else []})
    return items


def kill_tree(proc):
    """the run and what it started (the slot wrapper's game too, not only the bash in front)"""
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
        return
    import signal
    try:  # (started in a session of its own: its group is the run and its children; never ours)
        group = os.getpgid(proc.pid)
        if group == os.getpgrp():
            raise OSError("in the sweep's own process group")
        os.killpg(group, signal.SIGKILL)
    except OSError:
        proc.kill()


def last_scripts(out):
    """The last list of running scripts a TTYD_TRACE=2 run printed."""
    blocks = re.findall(r"^\[trace\] frame \d+: events\n((?:  #.*\n)*)", out, re.M)
    return blocks[-1] if blocks else ""


def run_one(it, a):
    r, out = run_game(it, a)
    scripts = ""
    if r.get("kind") in ("stuck", "battle stall", "hang", "game over"):
        # once more with the running scripts listed, to see what it waits on
        r2, out2 = run_game(it, a, {"TTYD_TRACE": "2", "TTYD_TRACE_EVERY": "600"})
        scripts = last_scripts(out2)
        r["scripts"] = [re.sub(r"^\s*#\d+ id \d+ (op \d+ )?at ", "", l) for l in scripts.splitlines()][:24]
        r["again"] = r2["result"]
        out += "\n\n===== again, TTYD_TRACE=2 =====\n" + out2
        if (r["kind"] == "stuck" and not it.get("ship") and "evt_bero_mario_enter" in scripts
                and it["map"].split("_")[0] in BOAT_AREAS):
            # walking in never ends: a channel Mario comes in by on the boat
            s, sout = run_game(dict(it, ship=True), a)
            out += "\n\n===== again, on the boat =====\n" + sout
            if s["result"] == "pass":
                s["warn"] = s.get("warn", []) + ["in on the boat"]
                return s, out
    if r["result"] == "fail" and a.forward:
        # the same room and entrance on the forward visit: a failure there too
        # is the room's (or the harness's), not the revisit's
        f, fout = run_game(dict(it, row=it["home_row"]), a)
        r["forward"] = f["result"] if f["result"] == "pass" else f"{f.get('kind')}: {f.get('sig')}"
        out += f"\n\n===== the forward visit (row {it['home_row']}) =====\n" + fout
        if f["result"] != "pass" and f.get("kind") == r.get("kind"):
            r["result"] = "forward"
    why = harness_limit(it["key"], r.get("kind"), (r.get("sig") or "") + "\n" + scripts)
    if why and r["result"] in ("fail", "forward"):
        r["result"], r["harness"] = "harness", why
    return r, out


def run_game(it, a, extra=None):
    user = tempfile.mkdtemp(prefix="revisit-")
    with open(os.path.join(user, "settings.txt"), "w", encoding="utf-8") as f:
        f.write("skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\n")
    script = os.path.join(user, "revisit.txt")
    ex = f"walk_to {it['exit']} 1500\nwait 120" if it["exit"] else "wait 60"
    with open(script, "w", encoding="utf-8") as f:
        f.write(SCRIPT.format(exit=ex))
    env = dict(os.environ, TTYD_MAP=it["map"] + ("," + it["entry"] if it["entry"] else ""),
               TTYD_STORY_ROW=str(it["row"]), TTYD_FAST="1", TTYD_TRACE="1", TTYD_INPUT=script,
               TTYD_GOD="1", TTYD_GOD_POWER="8", TTYD_AUTO_BATTLE="1", TTYD_AUTO_TEXT="1", TTYD_STORY_WATCH=str(a.watch),
               TTYD_WATCHDOG="30", TTYD_WARP_LIMIT="60", TTYD_USER_DIR=user, TTYD_CACHE_DIR=user,
               TTYD_WINDOW_POS="-4000,0", TTYD_ISO=a.iso)
    if it.get("ship"):
        env["TTYD_MARIO_SHIP"] = "1"  # in by water: on the boat, as play arrives there
    env.pop("TTYD_STORY", None)
    env.pop("TTYD_EVENT", None)
    env.update(extra or {})
    cmd = [a.exe, a.iso]
    if a.slot:
        cmd = ["bash", SLOT] + cmd
    t0 = time.time()
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            start_new_session=os.name != "nt")
    try:
        raw, _ = proc.communicate(timeout=a.timeout + (1200 if a.slot else 0))
        code = proc.returncode
    except subprocess.TimeoutExpired:
        kill_tree(proc)
        raw, _ = proc.communicate()
        code = "timeout"
    out = raw.decode("utf-8", errors="replace").replace("\r\n", "\n")
    shutil.rmtree(user, ignore_errors=True)
    r = judge(it, out, code)
    r["secs"] = round(time.time() - t0, 1)
    return r, out


def judge(it, out, code):
    """The run's result: pass, fail (kind, signature) or warn."""
    r = {k: it[k] for k in ("key", "map", "point", "row", "story", "entry", "exit")}
    r["code"] = code
    maps = re.findall(r"^\[trace\] frame \d+: sequence (\d+), map '(\w*)'", out, re.M)
    r["maps"] = []
    for seq, m in maps:
        if m and (not r["maps"] or r["maps"][-1] != m):
            r["maps"].append(m)
    # the story's changes once in the room (not the warp's own)
    start = out.find("[input] frame")
    r["story_changes"] = re.findall(r"^\[story\] frame \d+: (-?\d+ -> \d+)", out[start:] if start >= 0 else "", re.M)
    r["talks"] = len(re.findall(r"^\[input\] talk \d+:", out, re.M))
    warns = []
    kind = sig = None
    early = False
    if code == "timeout":
        kind, sig = "hang", "the run's time limit"
    # a sanitized build (--exe): AddressSanitizer's report, or UBSan's
    m = re.search(r"^==\d+==ERROR: AddressSanitizer: (\S+)(?:.*\n)*?\s+#\d+ \S+ in (\S+)", out, re.M)
    if m:
        kind, sig = "sanitizer", f"asan {m.group(1)} in {m.group(2)}"
    m = re.search(r"^(\S+?):(\d+):\d+: runtime error: (.*)$", out, re.M)
    if m and kind is None:
        kind, sig = "sanitizer", f"ubsan {os.path.basename(m.group(1))}:{m.group(2)}: {m.group(3)[:100]}"
    m = re.search(r"^OSPanic (.*)$", out, re.M)
    if m and kind != "sanitizer":
        kind, sig = "panic", re.sub(r"0x[0-9A-Fa-f]+", "0x?", m.group(1))[:160]
    m = re.search(r"\*\*\* (crash|stall): (.*)\n((?:[ \t]+[0-9a-fA-F]{16} .*\n)*)", out)
    if m and kind not in ("panic", "sanitizer"):
        frames = [re.sub(r"^\s*\S+\s+", "", l).split(" (")[0] for l in m.group(3).splitlines()]
        while frames and re.match(r"(__psynch_cvwait|_pthread_cond_wait|pthread_cond_wait|__futex|syscall|"
                                  r"NtWaitFor|WaitFor|RtlSleep|SleepConditionVariable)", frames[0]):
            frames = frames[1:]
        kind = "crash" if m.group(1) == "crash" else "hang"
        sig = " < ".join(frames[:3]) or m.group(2)
    if kind is None and re.search(r"^\[warp\] FAIL", out, re.M):
        kind, sig = "not reached", re.search(r"^\[warp\] FAIL: (.*)$", out, re.M).group(1)
        sig = re.sub(r"\d+s, frame \d+", "", sig)
    if kind is None and re.search(r"^\[event\] FAIL", out, re.M):
        kind, sig = "harness", "no such story row"
    if kind is None and "[warp] frame" in out and not re.search(r"^\[event\] row \d+ ", out, re.M):
        kind, sig = "harness", "the story row was never set up"
    if kind is None and re.search(r"^\[story\] STUCK", out, re.M):
        kind, sig = "stuck", "the story watch: " + re.search(r"^\[story\] STUCK: (.*?);", out, re.M).group(1)
    if kind is None:
        m = re.search(r"^\[input\] FAIL: (.*)$", out, re.M)
        left = m and re.match(r"walk_to \S+: no such hit object on map '(\w+)'", m.group(1))
        if left and left.group(1) != it["map"]:
            # the short walks took Mario out a door before the walk to the
            # exit: the room was left early (judged below by where it went)
            early = True
        elif m:
            what = m.group(1)
            # a battle that never ends is told apart: auto-battle's own loops
            # (HARNESS_LIMITS) or a battle that hangs
            kind = "battle stall" if "(sequence 4," in what else "stuck"
            sig = "no control: " + re.sub(r"\d+ frames", "N frames", re.sub(r"press_until_control \S+ \d+", "press_until_control", what))
    if kind is None and "sequence 5," in out:
        kind, sig = "game over", "a Game Over"
    if kind is None and code == 6:
        # auto-battle ran away after its command cap: a battle that never ends
        kind, sig = "battle stall", "auto-battle's command cap (exit 6)"
    # (left early, the failed walk_to ends the run with exit 5, input_pc.c's portTestExit(5): any other code is
    # the next room's crash)
    if kind is None and code not in (0, "timeout") and not (early and code == 5):
        kind, sig = "exit", f"exit {code}"
    # the maps it went to
    allowed = named_maps(it["map"]) | {it["map"]}
    for pat, extra in EXTRA_TARGETS.items():
        if re.match(pat, it["map"]):
            allowed |= set(extra)
    went = [m for m in r["maps"][r["maps"].index(it["map"]) + 1:]] if it["map"] in r["maps"] else []
    walk = re.search(r"^\[input\] frame \d+: walk_to: reached \(map (\w+)\)", out, re.M)
    if walk:
        r["left_to"] = walk.group(1)
        if it["exit"] and it["exit_to"] and walk.group(1) not in it["exit_to"] and walk.group(1) not in allowed:
            if kind is None:
                kind, sig = "wrong map", f"{it['exit']} led to {walk.group(1)}, not {'/'.join(it['exit_to'])}"
    elif it["exit"] and re.search(r"walk_to \S+: not reached", out):
        warns.append("exit not reached")
    # only the room's own departure: the maps after it are the next room's doing
    stray = [m for m in went[:1] if m not in allowed and m not in it["exit_to"] and m != r.get("left_to")]
    if stray and kind is None:
        kind, sig = "wrong map", "went to " + ",".join(stray)
    if went and not walk and kind is None:
        warns.append("left early to " + ",".join(went))
    if "talk_all: the map changed" in out:
        warns.append("a talk changed the map")
    if re.search(r"^\[input\] talk \d+: still running", out, re.M) or "still running after" in out:
        warns.append("a talk ran out of time")
    if r["story_changes"]:
        warns.append("story moved " + ",".join(r["story_changes"][:4]))
    if kind is None and early and went:
        r["result"] = "pass"
    elif kind is None and code == 0 and "[input] frame" in out and re.search(r"^\[input\] frame \d+: end", out, re.M):
        r["result"] = "pass"
    elif kind is None:
        kind, sig = "exit", f"exit {code} without the script's end"
    if kind is not None:
        why = harness_limit(it["key"], kind, sig)
        if why:
            r["result"], r["harness"] = "harness", why
        else:
            r["result"] = "fail"
        r["kind"], r["sig"] = kind, sig
    r["warn"] = warns
    return r


def harness_limit(key, kind, sig):
    for pattern, text, why in HARNESS_LIMITS:
        if re.search(pattern, key) and (text == kind or text in (sig or "")):
            return why
    return None


# --- the sweep -------------------------------------------------------------

def summarize(results, out_dir, a, elapsed):
    res = list(results.values())
    by = collections.Counter(r["result"] for r in res)
    visits = {(r["map"], r["point"]) for r in res}
    bad_visits = {(r["map"], r["point"]) for r in res if r["result"] == "fail"}
    groups = collections.defaultdict(list)
    for r in res:
        if r["result"] == "fail":
            groups[f"{r['kind']}: {r['sig']}"].append(r["key"])
    forward = collections.defaultdict(list)
    for r in res:
        if r["result"] == "forward":
            forward[f"{r['kind']}: {r['sig']}"].append(r["key"])
    harness = collections.defaultdict(list)
    for r in res:
        if r["result"] == "harness":
            harness[r["harness"]].append(r["key"])
    warns = collections.Counter(w.split(" to ")[0].split(" moved")[0] for r in res for w in r.get("warn", []))
    summary = {"version": a.version, "exe": a.exe, "runs": len(res), "visits": len(visits),
               "visits_failed": len(bad_visits), "results": dict(by), "elapsed_s": round(elapsed),
               "pass_rate": round(by.get("pass", 0) / max(1, len(res) - by.get("harness", 0) - by.get("forward", 0)), 4),
               "failures": {k: v for k, v in sorted(groups.items(), key=lambda kv: -len(kv[1]))},
               "harness_limits": dict(harness), "forward_too": dict(forward), "warnings": dict(warns)}
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(summary, f, indent=1)
    md = [f"# Revisit sweep: {a.version}", "",
          f"- exe: `{os.path.basename(a.exe)}`",
          f"- {len(res)} runs, {len(visits)} (room, story point) visits",
          f"- pass {by.get('pass', 0)}, fail {by.get('fail', 0)}, harness limits {by.get('harness', 0)}"
          f", failing forward too {by.get('forward', 0)} (pass rate {summary['pass_rate'] * 100:.1f}% of the runs that count)",
          f"- visits with a failure: {len(bad_visits)}",
          f"- time: {elapsed / 60:.1f} min", "", "## Failures by cause", ""]
    for k, v in summary["failures"].items():
        md.append(f"- **{len(v)}** {k}  \n  {' '.join(v[:20])}{' ...' if len(v) > 20 else ''}")
    if not groups:
        md.append("none")
    md += ["", "## Failing on the forward visit too (not the revisit's)", ""]
    for k, v in sorted(forward.items(), key=lambda kv: -len(kv[1])):
        md.append(f"- **{len(v)}** {k}  \n  {' '.join(v[:20])}{' ...' if len(v) > 20 else ''}")
    if not forward:
        md.append("none")
    md += ["", "## Harness limits", ""]
    for k, v in harness.items():
        md.append(f"- **{len(v)}** {k}  \n  {' '.join(v[:20])}{' ...' if len(v) > 20 else ''}")
    if not harness:
        md.append("none")
    md += ["", "## Warnings", ""] + [f"- {n} {w}" for w, n in warns.most_common()]
    md += ["", "## Per area", "", "| area | runs | pass | fail | forward too | harness |", "|---|---|---|---|---|---|"]
    per = collections.defaultdict(collections.Counter)
    for r in res:
        per[r["map"].split("_")[0]][r["result"]] += 1
    for area in sorted(per):
        c = per[area]
        md.append(f"| {area} | {sum(c.values())} | {c['pass']} | {c['fail']} | {c['forward']} | {c['harness']} |")
    with open(os.path.join(out_dir, "summary.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md) + "\n")
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("iso", help="the disc image (the exe's argv[1])")
    ap.add_argument("--exe", default="", help="the game (default the headless build-pc-<us|jp|eu>/ttyd-<us|jp|eu>); a sanitized build works too")
    ap.add_argument("--version", default="G8ME01", choices=sorted(VERSIONS))
    ap.add_argument("-j", type=int, default=4)
    ap.add_argument("--areas", default="", help="only these areas (map prefixes), comma-separated")
    ap.add_argument("--points", default="", help="only these story points (ch1..ch8, post)")
    ap.add_argument("--maps", default="", help="only these maps")
    ap.add_argument("--entries", default="all", choices=("first", "all"), help="every entrance, or the first only")
    ap.add_argument("--out", default="", help="results folder (default local-docs/revisit_<version>)")
    ap.add_argument("--resume", action="store_true", help="skip the runs results.jsonl already has")
    ap.add_argument("--retry-failed", action="store_true", help="with --resume: run the failures again")
    ap.add_argument("--timeout", type=int, default=420, help="seconds a run may take")
    ap.add_argument("--watch", type=int, default=900, help="TTYD_STORY_WATCH seconds (game time): a STUCK is a hang")
    ap.add_argument("--no-slot", dest="slot", action="store_false", help="run directly, not through $GAME_SLOT")
    ap.add_argument("--list", action="store_true", help="print the runs and the story points, run nothing")
    ap.add_argument("--no-forward", dest="forward", action="store_false",
                    help="no second run of a failure on the room's forward visit")
    ap.add_argument("--keep-logs", action="store_true", help="save every run's log, not only the failures'")
    a = ap.parse_args()
    a.iso = os.path.abspath(a.iso)
    if not a.exe:
        short = {"G8MJ01": "jp", "G8ME01": "us", "G8MP01": "eu"}[a.version]
        ext = ".exe" if os.name == "nt" else ""
        cands = [os.path.join(ROOT, d, f"ttyd-{short}{ext}")
                 for d in (f"build-pc-{short}", f"build-pc-{short}-headless", f"build-pc-{a.version}-headless")]
        a.exe = next((c for c in cands if os.path.exists(c)), cands[0])
    a.exe = os.path.abspath(a.exe)
    # the exe must have the TTYD_STORY_ROW hook (port/src/common/trace_pc.c),
    # or every run silently plays at story 0 (a shared build without it did)
    if not a.list and os.path.exists(a.exe) and b"TTYD_STORY_ROW" not in open(a.exe, "rb").read():
        print(f"revisit_sweep: {a.exe} has no TTYD_STORY_ROW hook (build this branch's headless target)", file=sys.stderr)
        return 2
    if a.slot and not os.path.exists(SLOT):
        a.slot = False
    files_dir = os.path.join(ROOT, "orig", a.version, "files")
    # (a misspelt filter selects nothing, and a sweep of no runs passed: each name must be one there is)
    known = {"areas": set(AREA_CHAPTER), "points": {f"ch{n}" for n in range(1, 9)} | {"post"},
             "maps": set(maps_of(files_dir))}
    for what, names in known.items():
        unknown = set(filter(None, getattr(a, what).split(","))) - names
        if unknown:
            print(f"revisit_sweep: no such {what[:-1]}: {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2
    items = plan(a.version, files_dir, set(filter(None, a.areas.split(","))),
                 set(filter(None, a.points.split(","))), set(filter(None, a.maps.split(","))), a.entries)
    if a.list:
        for p, r in story_points(VERSIONS[a.version]).items():
            print(f"{p}: row {r['row']} {r['text']} '{r['name']}', story {r['story']}")
        for it in items:
            print(it["key"], "->", it["exit"])
        print(f"{len(items)} runs, {len({(i['map'], i['point']) for i in items})} visits")
        return 0
    if not items:  # (the filters together chose nothing, e.g. a map and an area of another: 0 runs is no pass)
        print("revisit_sweep: no runs for --areas/--points/--maps", file=sys.stderr)
        return 2
    out_dir = a.out or os.path.join(ROOT, "local-docs", f"revisit_{a.version}")
    os.makedirs(os.path.join(out_dir, "logs"), exist_ok=True)
    jsonl = os.path.join(out_dir, "results.jsonl")
    results = {}
    if a.resume and os.path.exists(jsonl):
        for line in open(jsonl, encoding="utf-8"):
            try:
                r = json.loads(line)
            except ValueError:
                continue
            results[r["key"]] = r
    elif os.path.exists(jsonl):
        os.remove(jsonl)
    wanted = {it["key"] for it in items}
    todo = [it for it in items if it["key"] not in results
            or (a.retry_failed and results[it["key"]]["result"] != "pass")]
    results = {k: v for k, v in results.items() if k in wanted}
    print(f"revisit_sweep: {len(items)} runs ({len(todo)} to do), {a.version}, {os.path.basename(a.exe)}, -j{a.j}",
          flush=True)
    lock = threading.Lock()
    t0 = time.time()
    done = 0
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        futs = [pool.submit(run_one, it, a) for it in todo]
        for fu in concurrent.futures.as_completed(futs):
            r, out = fu.result()
            with lock:
                done += 1
                results[r["key"]] = r
                with open(jsonl, "a", encoding="utf-8", newline="\n") as f:
                    f.write(json.dumps(r) + "\n")
                if r["result"] != "pass" or a.keep_logs:
                    with open(os.path.join(out_dir, "logs", r["key"].replace("%", "_").replace("@", "_") + ".log"),
                              "w", encoding="utf-8", newline="\n") as f:
                        f.write(out)
                if r["result"] != "pass":
                    print(f"{r['result'].upper()} {r['key']}: {r.get('kind')}: {r.get('sig')}", flush=True)
                if done % 50 == 0:
                    print(f"  {done}/{len(todo)} in {(time.time() - t0) / 60:.1f} min", flush=True)
    s = summarize(results, out_dir, a, time.time() - t0)
    print(f"\nrevisit_sweep: {s['runs']} runs, {s['visits']} visits; {s['results']}; pass rate "
          f"{s['pass_rate'] * 100:.1f}%; {len(s['failures'])} causes; {out_dir}/summary.md")
    if s["failures"]:
        return 1
    if not s["results"].get("pass"):  # (every run a harness limit or failing forward too: nothing shows a room played)
        print("revisit_sweep: no run passed")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
