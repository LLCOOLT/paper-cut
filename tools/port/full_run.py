#!/usr/bin/env python3
"""The full run: one save carried from a new file to the credits, segment by
segment, in fast mode, as a regression test (tests/port/full_run/README.md).

Each segment is an input script (tests/port/full_run/NN_name.txt, read by
port/src/common/input_pc.c) that starts from a save and ends by saving with the
game's own save (the script's `save` step) at a known point. Its first lines
may say where it starts and where it must end:

  #start map=gor_00 story=3 party=1 level=2 items=ITEM_HAMMER,...   (for --gen)
  #end story=7 map=gor_01
  #env TTYD_GOD=0                  (the test hooks, over DEFAULT_ENV below; 0: off)

The full run plays the segments in order, each one from the save the one
before it just wrote. A segment alone (--only N) starts from its checkpoint in
tests/port/full_run/checkpoints/<region>/: NN_name.sav, recorded from a full
run (--record), else NN_name.gen.sav, generated from its '#start' line
(--gen: the game warps to that map and story, set-up steps give the items,
partners, level and flags, and it saves). A checkpoint is one card slot's save,
the player's state only: no banner, icon or anything else from the disc.

Each segment is checked for: a clean exit at the script's end (no crash, no
stall: $TTYD_WATCHDOG; no wait or walk that ran out), its '#end' story and
map, and against tests/port/full_run/expected_<region>.json the frame count and
game-state fingerprint at its end (TTYD_FINGERPRINT): a fingerprint that
differs is DRIFT (the game played differently: look at it, or re-record).

usage: full_run.py [--region us|jp|eu] [--from N] [--to N] [--only N] [--record]
                   [--gen N|all] [--backend headless|aurora] [--exe EXE] [--out DIR]
  --backend  headless (default; the same frames, no pictures, about 25x faster) or
             aurora (a window off screen; `shot` steps write pictures)
  --record   after a deliberate change: write the expected values and the
             checkpoints from this run
  --gen      (re)generate segment N's start save (all: every segment's that has
             a '#start' line), then run nothing else
  --strict   DRIFT fails the run too
Exit 0 when every segment run passed.
"""
import argparse
import datetime
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SEG_DIR = os.path.join(ROOT, "tests", "port", "full_run")
# a wrapper that waits for a free test-game slot ($GAME_SLOT, else game_slot.sh beside the checkout); none: run directly
GAME_SLOT = os.environ.get("GAME_SLOT", os.path.join(os.path.dirname(ROOT), "game_slot.sh"))
REGIONS = {"us": ("G8ME01", "USA"), "jp": ("G8MJ01", "JAP"), "eu": ("G8MP01", "EUR")}

GCI_HEADER = 0x40
META = 0x2000
SLOT = 0x4000
CARD_FILE = "mariost_save_file"


PM64_DIR = os.path.join(SEG_DIR, "pm64")
PM64 = False  # --pm64: the chain with Paper Mario 64's content on (set by main)


def segments(pm64=None):
    """[(index, name, path)] of the segment scripts, in order: NN_name.txt; with --pm64 the same and, between
    them, tests/port/full_run/pm64/NNp_name.txt (NNp after NN_: its boss fights, joins and battles). The index
    is the place in that list. A TTYD segment may carry a letter after its NN (NNa_ .. NNo_, never p, the PM64
    set's): '_' sorts before the letters, so 59_ < 59a_ < 59b_ < 60_ (room for a chapter's later segments)."""
    pm64 = PM64 if pm64 is None else pm64
    paths = [p for p in glob.glob(os.path.join(SEG_DIR, "[0-9][0-9]*_*.txt"))
             if re.match(r"\d\d[a-o]?_", os.path.basename(p))]
    if pm64:
        paths += glob.glob(os.path.join(PM64_DIR, "[0-9][0-9]*_*.txt"))
    paths.sort(key=os.path.basename)
    return [(i, os.path.basename(p)[:-4], p) for i, p in enumerate(paths)]


def set_suffix():
    """the PM64 set's own expected values and checkpoints: expected_<region>_pm64.json, checkpoints/<region>_pm64"""
    return "_pm64" if PM64 else ""


def checkpoint(region, name, script):
    """The start save of segment name (its script): the set's recorded one, else its generated one; the PM64
    set's TTYD segments fall back to the TTYD set's, and a PM64 segment NNp_ (between NN_ and the next) to
    the next TTYD segment's (it starts where NN_ ends). None: none."""
    own = os.path.join(SEG_DIR, "checkpoints", region + set_suffix())
    ttyd = os.path.join(SEG_DIR, "checkpoints", region)
    cks = [os.path.join(d, name + x) for x in (".sav", ".gen.sav") for d in dict.fromkeys([own, ttyd])]
    if PM64 and os.path.dirname(os.path.abspath(script)) == os.path.abspath(PM64_DIR):
        after = [s for s in segments(False) if os.path.basename(s[2]) > os.path.basename(script)]
        if after:
            cks.append(os.path.join(ttyd, after[0][1] + ".sav"))
    return next((ck for ck in cks if os.path.exists(ck)), None)


def card_path(user, region):
    code = REGIONS[region][0][:4]
    return os.path.join(user, REGIONS[region][1], "Card A", f"01-{code}-{CARD_FILE}.gci")


def slot_of(gci_bytes, n=0):
    """Card slot n's save (its GameCube image) out of a .gci of the game's card file."""
    at = GCI_HEADER + META + SLOT * n
    return gci_bytes[at:at + SLOT]


def empty_slot(region):
    """An empty card slot as the game makes one (cardmgr.c's make_data): the
    'empty' flag, the version and size, the checksums."""
    s = bytearray(SLOT)
    struct.pack_into(">H", s, 0, 1)
    s[0x3FF0:0x3FF3] = b"010"
    struct.pack_into(">I", s, 0x3FF4, 0x2260 if region == "us" else 0x2140)  # PORT_CARD_GC_DATA_SIZE
    struct.pack_into(">II", s, 0x3FF8, 1, 0xFFFFFFFE)
    return bytes(s)


def gci_from_slot(slot, region):
    """The game's card file holding only this save, as file 1, the other files
    empty: a directory entry and a header as the game writes them, with no
    banner or icon (the port's own saves have none either), then the slots
    (four, and their four backups). The save warp ($TTYD_LOAD_SAVE) reads it,
    and the game's own saves write to it."""
    code = REGIONS[region][0]
    entry = bytearray(GCI_HEADER)
    entry[0:6] = code.encode()
    entry[7] = 0x02  # banner format
    entry[8:8 + len(CARD_FILE)] = CARD_FILE.encode()
    struct.pack_into(">IHHBBHHH", entry, 0x2C, 0x40, 0x0001, 0x0002, 0x04, 0, 5, (META + SLOT * 8) // 0x2000, 0)
    meta = bytearray(META)
    meta[0:11] = b"Paper Mario"
    struct.pack_into(">H", meta, 0x1E40, 1)  # rumble on
    total = sum(meta[:0x1E46])
    struct.pack_into(">II", meta, 0x1FF8, total, total ^ 0xFFFFFFFF)
    empty = empty_slot(region)
    slots = [slot if slot is not None else empty] + [empty] * 3
    return bytes(entry) + bytes(meta) + b"".join(slots) * 2


def last_lines(path, n=1):
    """The last n lines of a (possibly big) text file."""
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            f.seek(max(0, size - 4096 * n))
            return f.read().decode("ascii", "replace").splitlines()[-n:]
    except OSError:
        return []


def failure(out, code):
    """Why a game run failed, in a few words; None if it didn't."""
    for pat, what in ((r"^OSPanic (.*)$", "panic"), (r"\*\*\* (crash|stall): (.*)$", None),
                      (r"^\[input\] FAIL: (.*)$", "wait ran out"),
                      (r"^(TTYD_INPUT: .*)$", None), (r"^\[warp\] (.*(?:Cannot|not a|no valid).*)$", "warp")):
        m = re.search(pat, out, re.M)
        if m:
            text = " ".join(g for g in m.groups() if g)
            return (what + ": " if what else "") + text[:200]
    if code == 124:
        return "timed out"
    if code != 0:
        return f"exit {code}"
    if "[input] end" not in out:
        return "the script did not reach its end"
    return None


def directives(script):
    """A segment's '#start', '#end', '#env' and '#setting' lines (settings.txt keys, an Enhancement a player
    can turn on: fast_travel=1): {key: value} each (later lines add to earlier ones);
    '#must REGEX' and '#must_not REGEX' lines: what its log must (not) say (a join, a partner's move), each a
    list; '#gap WHY': in a chained run it starts from its own checkpoint, not the save before it (the chain
    doesn't reach it yet: WHY is reported)."""
    out = {"start": {}, "end": {}, "env": {}, "setting": {}, "must": [], "must_not": [], "gap": ""}
    with open(script, encoding="utf-8") as f:
        for line in f:
            g = line.strip().split(None, 1)
            if g and g[0] in ("#must", "#must_not", "#must_pm64", "#must_not_pm64", "#gap") and len(g) == 2:
                if g[0] == "#gap":
                    out["gap"] = g[1].strip()
                elif not g[0].endswith("_pm64"):
                    out[g[0][1:]].append(g[1].strip())
                elif PM64:  # (#must_pm64: checked in the PM64 set's run of a TTYD segment only)
                    out[g[0][1:-5]].append(g[1].strip())
                continue
            m = re.match(r"#(start|end|env|setting)\s+(.*)$", line.strip())
            if m:
                for kv in m.group(2).split():
                    k, _, v = kv.partition("=")
                    out[m.group(1)][k] = v
    return out


# The test hooks every segment plays with (port/include/port/story_test.h): fights
# played and won, messages read. A segment's '#env NAME=VALUE ...' line adds to
# them or, with NAME=0, switches one off for that segment.
DEFAULT_ENV = {"TTYD_AUTO_BATTLE": "1", "TTYD_GOD": "1", "TTYD_AUTO_TEXT": "1", "TTYD_STORY_WATCH": "120"}


def segment_env(script):
    """The hooks' variables a segment plays with: the defaults, then its '#env' line."""
    env = dict(DEFAULT_ENV)
    env.update(directives(script)["env"])
    return env


def game_env(a, out_dir, script):
    """A fresh user folder and the game's environment for one run."""
    shutil.rmtree(out_dir, ignore_errors=True)
    if os.path.exists(out_dir):  # (a file held open: another game still writing there, its log mixed into ours)
        raise SystemExit(f"full_run: {out_dir} could not be cleared: another run (an orphaned game?) holds it")
    user = os.path.join(out_dir, "user")
    shots = os.path.join(out_dir, "shots")
    os.makedirs(user)
    os.makedirs(shots)
    with open(os.path.join(user, "settings.txt"), "w") as f:
        f.write("skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\n")
        if PM64:  # Paper Mario 64's partners, each joined by beating their boss, and the bosses in Rogueport
            f.write("pm64_partners=1\npm64_join=1\npm64_met_all=0\npm64_bosses=1\npm64_story_bosses=1\n")
        for k, v in directives(script)["setting"].items():  # '#setting KEY=VALUE ...': a setting on (fast travel)
            f.write(f"{k}={v}\n")
    # the script as the game reads it: Japanese names (walk_to npc 黒カロン) in
    # Shift-JIS, as the game's own strings are; the files here are UTF-8
    with open(script, encoding="utf-8") as f:
        text = f.read()
    game_script = os.path.join(out_dir, "script.txt")
    with open(game_script, "wb") as f:
        f.write(text.encode("cp932", errors="replace"))
    env = dict(os.environ)
    for k in [k for k in env if k.startswith("TTYD_")]:
        del env[k]
    env.update(TTYD_FAST="1", TTYD_USER_DIR=user, TTYD_INPUT=game_script, TTYD_WATCHDOG=str(a.watchdog), TTYD_TRACE="1",
               TTYD_FINGERPRINT=os.path.join(out_dir, "fingerprint.txt"))
    if a.backend == "aurora":
        env.update(TTYD_SHOT_DIR=shots, TTYD_CACHE_DIR=a.cache, TTYD_NO_LAUNCHER="1", TTYD_WINDOW_POS="-5000,-5000",
                   SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", TTYD_OFFSCREEN="1")
    if PM64:  # the player's own ROM, read where it is (docs/PM64_LINK.md), never copied
        env.update(TTYD_PM64_ROM=a.pm64_rom, TTYD_PM64_LOG="1")
        if a.backend == "aurora":
            env.update(TTYD_SHOT_UI="1")  # (each capture also with the ImGui layer: a join's notice is drawn there)
    return env, user, shots


def play(a, env, out_dir):
    """Run the game once (through a shared game slot when there are any); (exit code, log text, seconds)."""
    cmd = [a.exe, a.iso]  # both backends read the disc image
    slot = os.path.exists(GAME_SLOT) and not a.no_slot
    if slot:
        # through the shared slots, with coreutils' timeout (it ends the game itself, not only bash)
        cmd = ["bash", GAME_SLOT, "timeout", str(a.timeout)] + [c.replace(os.sep, "/") for c in cmd]
    t0 = time.time()
    with open(os.path.join(out_dir, "log.txt"), "w", encoding="utf-8", errors="replace") as log:
        p = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=os.path.dirname(a.exe))
        try:
            code = p.wait(timeout=None if slot else a.timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            code = 124
    secs = time.time() - t0
    # the log in UTF-8: the game prints its own names (NPCs) in Shift-JIS
    with open(os.path.join(out_dir, "log.txt"), "rb") as f:
        lines = f.read().split(b"\n")
    text = []
    for line in lines:
        try:
            text.append(line.decode("utf-8"))
        except UnicodeDecodeError:
            text.append(line.decode("cp932", errors="replace"))
    text = "\n".join(text)
    with open(os.path.join(out_dir, "log.txt"), "w", encoding="utf-8") as f:
        f.write(text)
    return code, text, secs


def saved_slot(out, user, region):
    """The save the run's `save` step wrote: (slot bytes, (slot, map, story)), or (None, None)."""
    saves = re.findall(r"^\[input\] save: slot (\d+), map '(\w*)', story (\d+)", out, re.M)
    card = card_path(user, region)
    if not saves or not os.path.exists(card):
        return None, None
    n = int(saves[-1][0])
    with open(card, "rb") as f:
        return slot_of(f.read(), n), (n, saves[-1][1], int(saves[-1][2]))


def run_segment(a, name, script, start_slot, out_dir):
    """Play one segment from start_slot (a save's slot bytes; None: no card, a new file); its result dict."""
    env, user, shots = game_env(a, out_dir, script)
    if start_slot is not None:
        # the card holds the start save as file 1; the save warp loads it at
        # the title (as file 4), and the segment's own save writes there
        card = card_path(user, a.region)
        os.makedirs(os.path.dirname(card))
        with open(card, "wb") as f:
            f.write(gci_from_slot(start_slot, a.region))
        start = os.path.join(out_dir, "start.gci")
        shutil.copy(card, start)
        env.update(TTYD_LOAD_SAVE=start)
    seg_env = segment_env(script)
    res_env = dict(seg_env)
    seg_env.update(dict(kv.split("=", 1) for kv in a.env))
    for k, v in seg_env.items():
        if v == "":
            env.pop(k, None)  # a hook unset for this segment ("0" is passed: some hooks read 0 as a value)
        else:
            env[k] = v
    code, out, secs = play(a, env, out_dir)
    res = {"segment": name, "seconds": round(secs, 1), "exit": code, "env": res_env}
    fp = last_lines(os.path.join(out_dir, "fingerprint.txt"), 1)
    if fp and len(fp[0].split()) >= 2:
        res["frames"], res["fingerprint"] = int(fp[0].split()[0]), fp[0].split()[1]
    res["shots"] = sorted(os.path.relpath(p, a.out).replace(os.sep, "/")
                          for p in glob.glob(os.path.join(shots, "*.png")))
    walks = len(re.findall(r"^\[input\] frame \d+: walk_to .* not reached", out, re.M))
    if walks:
        res["walks_short"] = walks  # walks that ran out of frames: not failures, the end checks decide
    why = failure(out, code)
    d = directives(script)
    for pat in d["must"]:
        if why is None and not re.search(pat, out, re.M):
            why = f"the log lacks: {pat}"
    for pat in d["must_not"]:
        m = re.search(pat, out, re.M) if why is None else None
        if m:
            why = f"the log has: {m.group(0)[:160]}"
    slot, where = saved_slot(out, user, a.region)
    if where:
        res["map"], res["story"] = where[1], where[2]
    if why is None and slot is None:
        why = "no save at the end (a segment ends with `save`)"
    if why is None:
        with open(os.path.join(out_dir, "end.sav"), "wb") as f:
            f.write(slot)
        want = directives(script)["end"]
        for k in ("story", "map"):
            if k in want and str(res.get(k)) != want[k]:
                why = f"ended at {k} {res.get(k)}, not {want[k]}"
                break
    # the fingerprint file grows a line a frame: keep its tail only
    tail = last_lines(os.path.join(out_dir, "fingerprint.txt"), 200)
    with open(os.path.join(out_dir, "fingerprint.txt"), "w") as f:
        f.write("\n".join(tail) + "\n")
    res["status"] = "FAIL" if why else "PASS"
    if why:
        res["why"] = why
    return res


def enum_names(header, prefix):
    """{NAME: value} of an enum's members in a header ('NAME = 12,' or 'NAME,' counting on)."""
    out, n = {}, 0
    with open(os.path.join(ROOT, header), encoding="utf-8") as f:
        text = f.read()
    for m in re.finditer(r"^\s*(" + prefix + r"\w+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,", text, re.M):
        n = int(m.group(2), 0) if m.group(2) else n
        out[m.group(1)] = n
        n += 1
    return out


def generate(a, name, script):
    """A start save made from the segment's '#start' line: the game warps to the
    map at that story value (TTYD_MAP, TTYD_STORY, TTYD_PARTY), set-up steps
    give the rest (items, level, flags, ...), and the game saves. Returns the
    save's slot bytes, or exits."""
    st = directives(script)["start"]
    if "map" not in st or "story" not in st:
        sys.exit(f"full_run: {name} has no '#start map=... story=...' line to generate its start save from")
    items = enum_names("include/mario/mario_pouch.h", "ITEM_")
    flags = enum_names("include/evt/evt_story.h", "GSWF_")
    gsws = enum_names("include/evt/evt_story.h", "GSW_")

    def num(v, table):
        return table[v] if v in table else int(v, 0)

    bundles = {}
    with open(os.path.join(SEG_DIR, "flag_bundles.txt"), encoding="utf-8") as f:
        for line in f:
            words = line.split("#")[0].split()
            if words and words[0].startswith("@"):
                bundles[words[0]] = words[1:]

    def expand(entries):
        """The entries with each @bundle (tests/port/full_run/flag_bundles.txt) replaced by its own."""
        out = []
        for e in entries:
            if e.startswith("@"):
                if e not in bundles:
                    sys.exit(f"full_run: {name}: no flag bundle {e} in flag_bundles.txt")
                out += expand(bundles[e])
            elif e:
                out.append(e)
        return out

    steps = ["wait 30"]
    warp = {}  # the save warp's own set-up (port/src/common/trace_pc.c): worn badges, the partners' rank
    for k, v in st.items():
        if k in ("map", "story", "party"):
            continue
        if k == "badges":
            warp["TTYD_BADGES"] = ",".join(str(num(i, items)) for i in v.split(",") if i)
            continue
        if k == "rank":
            warp["TTYD_PARTY_RANK"] = v
            continue
        if k == "items":
            steps += [f"give {num(i, items)}" for i in v.split(",") if i]
        elif k in ("flags", "gsw"):
            for e in expand(v.split(",")):
                if ":" in e:  # a saved byte, NAME:value
                    i, _, val = e.partition(":")
                    steps.append(f"gsw {num(i, gsws)} {int(val, 0)}")
                else:
                    steps.append(f"flag {num(e, flags)}")
        elif k in ("level", "coins", "sp", "maxsp", "stars", "audience"):
            steps.append(f"{k} {v}")
        elif k == "hp":
            steps.append(f"maxhp {v}")
        elif k == "fp":
            steps.append(f"maxfp {v}")
        else:
            sys.exit(f"full_run: {name}: unknown '#start' key {k}")
    steps += ["wait 60", "save", "end"]
    out_dir = os.path.join(a.out, name + ".gen")
    gen_script = os.path.join(a.out, name + ".gen.txt")
    with open(gen_script, "w") as f:
        f.write("# generated from the #start line of " + os.path.basename(script) + "\n" + "\n".join(steps) + "\n")
    env, user, _ = game_env(a, out_dir, gen_script)
    env.update(TTYD_MAP=st["map"], TTYD_STORY=st["story"], TTYD_PARTY=st.get("party", "0"), **warp)
    card = card_path(user, a.region)
    os.makedirs(os.path.dirname(card))
    with open(card, "wb") as f:
        f.write(gci_from_slot(None, a.region))
    code, out, secs = play(a, env, out_dir)
    slot, where = saved_slot(out, user, a.region)
    why = failure(out, code)
    if why or slot is None:
        sys.exit(f"full_run: {name}: generating its start save failed: {why or 'no save'} ({out_dir})")
    print(f"[full_run] {name}: start save generated (map {where[1]}, story {where[2]}, {secs:.0f} s)", flush=True)
    return slot


def check(res, exp):
    """Compare a passing segment with its expected values: DRIFT on the frames or fingerprint."""
    if res["status"] != "PASS":
        return
    if not exp:
        res["note"] = "no expected values yet (--record)"
        return
    for k in ("story", "map"):
        if res.get(k) != exp.get(k):
            res["status"], res["why"] = "FAIL", f"{k} {res.get(k)} (expected {exp.get(k)})"
            return
    if res.get("fingerprint") != exp.get("fingerprint") or res.get("frames") != exp.get("frames"):
        res["status"] = "DRIFT"
        res["why"] = (f"frame {res.get('frames')} fingerprint {res.get('fingerprint')} "
                      f"(expected frame {exp.get('frames')} {exp.get('fingerprint')})")
        if "env" in exp and exp["env"] != res.get("env"):
            res["why"] += f"; recorded with the hooks {exp['env']}, played with {res.get('env')}"


def write_summary(a, results, started):
    with open(os.path.join(a.out, "summary.json"), "w") as f:
        json.dump(results, f, indent=1)
    total = sum(r["seconds"] for r in results)
    lines = [f"# Full run ({a.region.upper()}{', PM64 on' if PM64 else ''}, {a.backend}), {started:%Y-%m-%d %H:%M}", "",
             f"{sum(r['status'] == 'PASS' for r in results)} of {len(results)} segments passed, "
             f"{total / 60:.1f} min in all.", "",
             "| segment | status | story | map | frames | time | why |", "|---|---|---|---|---|---|---|"]
    for r in results:
        lines.append(f"| {r['segment']} | {r['status']} | {r.get('story', '-')} | {r.get('map', '-')} | "
                     f"{r.get('frames', '-')} | {r['seconds']:.0f} s | {r.get('why', r.get('note', ''))} |")
    for r in results:
        if r["shots"]:
            lines += ["", f"## {r['segment']}", ""] + [f"![{os.path.basename(s)}]({s})" for s in r["shots"][-4:]]
    with open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--region", default="us", choices=sorted(REGIONS))
    ap.add_argument("--backend", default="headless", choices=("aurora", "headless"),
                    help="headless (default): no window, about 25x faster; aurora: with the screenshots")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=99)
    ap.add_argument("--only", type=int, default=None)
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--gen", default="", help="generate segment N's start save (or 'all'), then stop")
    ap.add_argument("--out", default="")
    ap.add_argument("--exe", default="", help="a prebuilt game to run (default $FULLRUN_EXE_HEADLESS / $FULLRUN_EXE_AURORA, else this worktree's build-pc-<region>-<backend>)")
    ap.add_argument("--iso", default="", help="the disc image")
    ap.add_argument("--cache", default="")
    ap.add_argument("--timeout", type=int, default=3600, help="seconds per segment")
    ap.add_argument("--watchdog", type=int, default=60)
    ap.add_argument("--no-slot", action="store_true", help="don't wait for a shared game slot ($GAME_SLOT)")
    ap.add_argument("--script", default="", help="play this script as segment --only's (trying a change)")
    ap.add_argument("--start", default="", help="the first segment's start save (.sav slot, or a .gci's file 1)")
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE for the game (more tracing, ...)")
    ap.add_argument("--pm64", action="store_true",
                    help="the chain with Paper Mario 64's content on: the TTYD segments and tests/port/full_run/pm64/'s "
                         "between them, the ROM from $PAPERCUT_PM64_ROM (--pm64-rom), on the Aurora build off screen "
                         "(the PM64 bosses are Aurora's)")
    ap.add_argument("--pm64-rom", default=os.environ.get("PAPERCUT_PM64_ROM", ""))
    ap.add_argument("--list", action="store_true", help="list the segments (index, name, gap) and stop")
    a = ap.parse_args()
    global PM64
    PM64 = a.pm64
    if a.list:
        for idx, name, script in segments():
            d = directives(script)
            print(f"{idx:3d} {name}" + (f"  (gap: {d['gap']})" if d["gap"] else ""))
        return
    if PM64:
        if not os.path.isfile(a.pm64_rom):
            sys.exit("full_run: --pm64 needs the player's Paper Mario 64 ROM ($PAPERCUT_PM64_ROM or --pm64-rom)")
        a.backend = "aurora"  # (the headless build has no PM64 bosses: port/src/aurora/pm64_*)
    if a.only is not None:
        a.lo = a.hi = a.only
    ver = REGIONS[a.region][0]
    build = os.path.join(ROOT, f"build-pc-{a.region}-{a.backend}")
    exe = f"ttyd-{a.region}" + (".exe" if os.name == "nt" else "")
    a.exe = os.path.abspath(a.exe or os.environ.get("FULLRUN_EXE_" + a.backend.upper(), "") or os.path.join(build, exe))
    a.iso = a.iso or (glob.glob(os.path.join(ROOT, "orig", ver, "*.iso")) + [""])[0]
    a.out = os.path.abspath(a.out or os.path.join(ROOT, "build-full-run", a.region + "-" + a.backend))
    a.cache = a.cache or os.path.join(a.out, "_cache")
    if not os.path.exists(a.exe) or not os.path.exists(a.iso):
        sys.exit(f"full_run: no game ({a.exe}) or disc image ({a.iso})")
    os.makedirs(a.out, exist_ok=True)
    ckpt_dir = os.path.join(SEG_DIR, "checkpoints", a.region + set_suffix())
    ttyd_ckpt_dir = os.path.join(SEG_DIR, "checkpoints", a.region)
    if a.gen:
        for idx, name, script in segments():
            if (a.gen == "all" and idx > 0 and directives(script)["start"]) or a.gen == str(idx) or \
                    (a.gen == "gaps" and directives(script)["gap"]):  # (gaps: the segments the chain doesn't reach)
                slot = generate(a, name, script)
                os.makedirs(ckpt_dir, exist_ok=True)
                with open(os.path.join(ckpt_dir, f"{name}.gen.sav"), "wb") as f:
                    f.write(slot)
        return
    exp_path = os.path.join(SEG_DIR, f"expected_{a.region}{set_suffix()}.json")
    expected = {}
    if os.path.exists(exp_path):
        with open(exp_path) as f:
            expected = json.load(f)
    started = datetime.datetime.now()
    results = []
    prev = None
    if a.start:
        with open(a.start, "rb") as f:
            data = f.read()
        prev = slot_of(data) if a.start.endswith(".gci") else data
    ok = True
    for idx, name, script in segments():
        if idx < a.lo or idx > a.hi:
            continue
        if a.script:
            script = os.path.abspath(a.script)
        start = None
        gap = directives(script)["gap"]
        if idx > 0:
            if prev is not None and not gap:
                start = prev  # the chain: the save the segment before just wrote
            else:
                if gap and prev is not None:
                    print(f"[full_run] {name}: a gap in the chain ({gap}): from its checkpoint", flush=True)
                ck = checkpoint(a.region, name, script)
                if ck is None:
                    sys.exit(f"full_run: {name} has no checkpoint: run from an earlier segment, or --gen {idx}")
                with open(ck, "rb") as f:
                    start = f.read()
                print(f"[full_run] {name}: from {os.path.relpath(ck, ROOT)}", flush=True)
        print(f"[full_run] {name} ...", flush=True)
        res = run_segment(a, name, script, start, os.path.join(a.out, name))
        check(res, expected.get(name))
        print(f"[full_run] {name}: {res['status']} story {res.get('story', '-')} map {res.get('map', '-')} "
              f"frame {res.get('frames', '-')} {res['seconds']:.0f} s {res.get('why', '')}", flush=True)
        results.append(res)
        write_summary(a, results, started)
        if res["status"] == "FAIL":
            ok = False
            break
        if res["status"] == "DRIFT" and a.strict:
            ok = False
        # a PM64 side trip the chain doesn't reach (pm64/9Np_*, a #gap from its own start) ends somewhere else
        # (Rogueport): the chain goes on from the save before it, so a TTYD 9N_ after a 9Np_ starts where it should
        side_trip = PM64 and gap and os.path.dirname(os.path.abspath(script)) == os.path.abspath(PM64_DIR)
        if not side_trip:
            with open(os.path.join(a.out, name, "end.sav"), "rb") as f:
                prev = f.read()
        if a.record:
            expected[name] = {k: res[k] for k in ("story", "map", "frames", "fingerprint", "env") if k in res}
            nxt = [s for s in segments() if s[0] > idx and not (
                PM64 and directives(s[2])["gap"] and os.path.dirname(os.path.abspath(s[2])) == os.path.abspath(PM64_DIR))]
            if nxt and not directives(nxt[0][2])["gap"] and not side_trip:  # the next one's checkpoint: this end save
                os.makedirs(ckpt_dir, exist_ok=True)
                with open(os.path.join(ckpt_dir, f"{nxt[0][1]}.sav"), "wb") as f:
                    f.write(prev)
    if a.record:
        with open(exp_path, "w") as f:
            json.dump(expected, f, indent=1, sort_keys=True)
            f.write("\n")
    print(f"[full_run] summary: {os.path.join(a.out, 'summary.md')}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
