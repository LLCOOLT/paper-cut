#!/usr/bin/env python3
"""Boot every map of a disc headless and report every failure in one run.

Each map runs in its own process (TTYD_MAP warp, TTYD_FAST frames; see
port/src/common/trace_pc.c), several at once. A map passes when it reaches
the field and runs TTYD_EXIT_FRAMES frames. Failures are grouped by their
first crash frame / panic message so one root cause shows up once.

usage: map_sweep.py EXE DATA_DIR [-j N] [--frames N] [--only PREFIX,...] [--timeout S]
                    [--disc ISO --shots DIR]   (Aurora build: captures + contact sheets)
                    [--golden FILE [--update-golden]]   (headless: audio regression)

Fast runs are deterministic, so each map's audio is a fingerprint of how it
played: --golden compares them with FILE (tests/port/golden/) and fails on any
change; --update-golden records them after an intended change.
"""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def maps_of(root):
    """Disc maps the game can change to: those in its map table (src/data/mapdata.c;
    mapDataPtr returns NULL for the rest, e.g. stg_* test stages, and the game crashes too)."""
    table = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                         "src", "data", "mapdata.c")
    known = set(re.findall(r'^MapData \w+ = \{\s*"(\w+)"', open(table, encoding="utf-8").read(), re.M))
    m = os.path.join(root, "m")
    return sorted(n for n in os.listdir(m) if os.path.exists(os.path.join(m, n, "d")) and n in known)


def audio_level(path, seconds=5):
    """(rms, peak, clipped samples) of the last `seconds` of a 16-bit WAV dump."""
    import array
    import wave
    try:
        w = wave.open(path)
    except (OSError, EOFError, wave.Error):
        return None
    n = w.getnframes()
    keep = min(n, w.getframerate() * seconds)
    w.setpos(n - keep)
    s = array.array("h", w.readframes(keep))
    if not s:
        return 0, 0, 0
    rms = (sum(x * x for x in s) / len(s)) ** 0.5
    return int(rms), max(abs(x) for x in s), sum(1 for x in s if x in (32767, -32768))


# Maps entered only in a state a warp can't set up: they keep the origin warp.
ORIGIN_ONLY = {
    "dou_10",  # the boat ride: Mario arrives on the boat (mot_ship); on foot, a wave's
               # fail script stops a boat that isn't there (a NULL write, as on GameCube)
}

# Failures a warp causes because it skips state earlier maps set up: (run name
# regex, failure signature text, why). Reported apart; they do not fail a sweep.
WARP_ARTIFACTS = [
    (r"^mri_0[13]%(089|09\d|100)$", "evt_puni_pera_onoff",
     "the Punis join on earlier Great Tree maps, which set up Punidat; warped in, a Puni has no NPC (a NULL read, as on GameCube)"),
]


def warp_artifact(name, sig):
    for pattern, text, why in WARP_ARTIFACTS:
        if re.match(pattern, name) and text in sig:
            return why
    return None


def beros(name):
    """The entrances (beros) the map's source declares, in order."""
    import glob
    if name in ORIGIN_ONLY:
        return []
    for f in glob.glob(os.path.join(ROOT, "rels", "*", "src", name + ".c")):
        return re.findall(r'"(\w+)",\s*//\s*hitName', open(f, encoding="utf-8", errors="replace").read())
    return []


def battle_tables():
    """(area, entries, maps) of every area's battle table (btlsetup_<area>_tbl);
    maps: where it exists when another REL registers it (tou's: tou2, for its
    arena tou_03), else empty (the area's maps)."""
    import glob
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, "rels", "**", "*.c"), recursive=True)):
        t = open(f, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
        for m in re.finditer(r"NpcSetupBattleInfo btlsetup_(\w+)_tbl\[\] = \{(.*?)\n\};", t, re.S):
            body = m.group(2)
            n = len(re.findall(r"// name\b", body)) - len(re.findall(r"^\s*NULL, // name", body, re.M))
            rel = os.path.relpath(f, os.path.join(ROOT, "rels")).replace(os.sep, "/").split("/")[0]
            maps = []
            if rel != m.group(1):
                for g in glob.glob(os.path.join(ROOT, "rels", rel, "src", "*.c")):
                    src = open(g, encoding="utf-8", errors="replace").read()
                    maps += re.findall(r'relSetEvtAddr\("(\w+)", \w', src)
            out.append((m.group(1), n, maps))
    return out


STORY_MAX = 420  # GSW(0) runs to about 405; larger compared values are timers


def story_points(name):
    """The story values (GSW(0)) the map's scripts tell apart: each value they
    compare GSW(0) with (IF_*, CASE_* in a SWITCH on it) and the one below,
    plus 0; [0] for a map without its own source."""
    import glob
    vals = set()
    for f in glob.glob(os.path.join(ROOT, "rels", "*", "src", name + ".c")):
        t = open(f, encoding="utf-8", errors="replace").read()
        vals |= {int(v) for v in re.findall(r"(?:IF_[A-Z_]+|CASE_[A-Z_]+)\(\s*GSW\(0\)\s*,\s*(-?\d+)", t)}
        for block in re.findall(r"SWITCH\(GSW\(0\)\)(.*?)END_SWITCH", t, re.S):
            vals |= {int(v) for v in re.findall(r"CASE_[A-Z_]+\((-?\d+)", block)}
            vals |= {int(v) for pair in re.findall(r"CASE_BETWEEN\((-?\d+),\s*(-?\d+)\)", block) for v in pair}
    points = {0}
    for v in vals:
        if 0 <= v <= STORY_MAX:
            points |= {max(0, v - 1), v}
    return sorted(points)


def has_npcs(name):
    """The map's source sets up NPCs (a battle needs one to fight through)."""
    import glob
    for f in glob.glob(os.path.join(ROOT, "rels", "*", "src", name + ".c")):
        return "NpcSetupInfo" in open(f, encoding="utf-8", errors="replace").read()
    return False


def first_bero(name):
    """The first entrance, so the warp enters as in play; without one Mario
    starts at the map's origin, which on some maps (eki_02) is over no floor."""
    b = beros(name)
    return b[0] if b else None


def audio_hash(path):
    """SHA-1 of a WAV dump's samples (not the header)."""
    try:
        with open(path, "rb") as f:
            return hashlib.sha1(f.read()[44:]).hexdigest()
    except OSError:
        return None


def run(exe, root, item, frames, timeout, disc=None, shots=None, script=None, audio=None):
    # name: the map, "map@bero" (--every-bero) or "area#N" (--battles); extra: more env
    name, map_name, bero = item[:3]
    env = dict(os.environ, TTYD_DATA_DIR=root, TTYD_MAP=map_name + ("," + bero if bero else ""),
               **(item[3] if len(item) > 3 else {}),
               TTYD_FAST="1", TTYD_TRACE="1",
               TTYD_EXIT_FRAMES=str(frames), TTYD_WATCHDOG="30")  # a hang prints the stuck stack
    if script:  # an input script (port/src/common/input_pc.c) drives the map and ends the run
        env["TTYD_INPUT"] = script
    if audio:  # the mixed output as a WAV (headless: libs/musyx hw_pc.c)
        env["TTYD_AUDIO_WAV"] = os.path.join(audio, name + ".wav")
    if shots:  # Aurora: capture the map's last frame, window off-screen
        cache = os.path.join(shots, "_cache", name)  # its own pipeline cache: a bad one can't spread
        os.makedirs(cache, exist_ok=True)
        env.update(TTYD_SHOT_DIR=shots, TTYD_WINDOW_POS="-4000,0", TTYD_CACHE_DIR=cache,
                   TTYD_USER_DIR=cache)  # its own card image too: parallel runs on one file can block
        user = None
    else:  # its own (empty) memory card: a run that saves never writes the user's own
        user = tempfile.mkdtemp(prefix="ttyd-sweep-card-")
        env["TTYD_USER_DIR"] = user
    try:
        r = subprocess.run([exe] + ([disc] if disc else []), env=env, capture_output=True, text=True, errors="replace", timeout=timeout)
        out, code = r.stdout + r.stderr, r.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        code = "timeout"
    if user:
        shutil.rmtree(user, ignore_errors=True)
    if audio:
        level = audio_level(env["TTYD_AUDIO_WAV"])
        if level is not None:
            out += f"\n[audio] rms {level[0]} peak {level[1]} clipped {level[2]}\n"
    if code == 0 and ("[warp] ok" in out or "[input] end" in out):
        return name, None, out
    return name, signature(out, code), out


def signature(out, code):
    """A short key naming the failure, shared by maps with the same cause."""
    m = re.search(r"^OSPanic (.*)$", out, re.M)
    if m:
        return "panic: " + re.sub(r"0x[0-9A-Fa-f]+", "0x?", m.group(1))[:160]
    m = re.search(r"\*\*\* (?:crash|stall): (.*)\n((?:[ \t]+[0-9a-fA-F]{16} .*\n)*)", out)
    if m:
        frames = [re.sub(r"^\s*\S+\s+", "", l).split(" (")[0] for l in m.group(2).splitlines()]
        # POSIX: the stack is printed from the signal handler; key on the frames below it
        tramp = [i for i, f in enumerate(frames) if f.split("+")[0] in ("_sigtramp", "__restore_rt")]
        frames = frames[tramp[0] + 1:] if tramp else frames
        # the thread waiting for its turn (baton) is not the cause: skip the wait
        while frames and re.match(r"(__psynch_cvwait|_pthread_cond_wait|pthread_cond_wait|__futex|syscall)", frames[0]):
            frames = frames[1:]
        frames = frames[:3]
        return ("stall: " if "*** stall" in m.group(0) else "crash: ") + (" < ".join(frames) or m.group(1))
    m = re.search(r"^\[warp\] FAIL: (.*)$", out, re.M)
    if m:
        return "not reached: " + re.sub(r", frame \d+", "", re.sub(r"^\S+ ", "", m.group(1)))
    return f"exit {code}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("data")
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--only", default="")
    ap.add_argument("--no-bero", action="store_true", help="warp to each map's origin instead of its first entrance")
    ap.add_argument("--every-bero", action="store_true", help="one run per entrance of each map (map@bero)")
    ap.add_argument("--party", type=int, default=0,
                    help="the partner on every run ($TTYD_PARTY: 1 Goombella .. 7 Ms. Mowz; default Goombella)")
    ap.add_argument("--story", action="store_true",
                    help="each map at each story value its scripts compare GSW(0) with, and the one below "
                         "(map%%NNN; --input default story_sweep.txt)")
    ap.add_argument("--battles", action="store_true",
                    help="one battle per entry of every area's battle table (area#N; --input default battle_sweep.txt)")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--logs", default="", help="directory for each failing map's output")
    ap.add_argument("--disc", default="", help="disc image to pass to the exe (the Aurora build)")
    ap.add_argument("--shots", default="", help="Aurora: save each map's last frame here, then contact sheets")
    ap.add_argument("--input", default="", help="input script run on each map (shots named <map>__<shot>)")
    ap.add_argument("--audio", default="", help="dump each map's audio here (headless) and report silent or clipping maps")
    ap.add_argument("--golden", default="", help="JSON of each map's audio hash: fail on any difference")
    ap.add_argument("--update-golden", action="store_true", help="write the --golden file from this run")
    ap.add_argument("--collect", default="", help=r"regex: report matching output lines of every run, e.g. '\[msg\] missing'")
    a = ap.parse_args()
    if a.party:
        os.environ["TTYD_PARTY"] = str(a.party)  # every run inherits it (trace_pc.c's warp)
    root = os.path.join(a.data, "files") if os.path.isdir(os.path.join(a.data, "files")) else a.data
    if not os.path.isdir(os.path.join(root, "m")):
        print(f"map_sweep: no extracted disc at {a.data}: skipped")
        return 77
    names = maps_of(root)
    if a.only:
        names = [n for n in names if n.startswith(tuple(a.only.split(",")))]
    if a.story:
        # every map at every point of the story its scripts tell apart
        items = [(f"{n}%{v:03d}", n, first_bero(n), {"TTYD_STORY": str(v)}) for n in names for v in story_points(n)]
        if not a.input:
            a.input = os.path.join(ROOT, "tests", "port", "input", "story_sweep.txt")
    elif a.battles:
        # every entry of every area's battle table, fought from the area's
        # first map with NPCs (input_pc.c "battle N" via $TTYD_BATTLE_SETUP)
        items = []
        for area, count, rel_maps in battle_tables():
            area_maps = rel_maps or [n for n in names if n.startswith(area + "_")]
            # NPCs set up in the map's source, else (spawned by scripts, e.g. jon) the first map
            on = next((n for n in area_maps if has_npcs(n)), area_maps[0] if area_maps else None)
            if on is None:
                print(f"map_sweep: no map with NPCs for area {area}: its {count} battles skipped")
                continue
            items += [(f"{area}#{i:02d}", on, first_bero(on), {"TTYD_BATTLE_SETUP": str(i)}) for i in range(count)]
        if not a.input:
            a.input = os.path.join(ROOT, "tests", "port", "input", "battle_sweep.txt")
    elif a.every_bero:
        items = [(f"{n}@{b}", n, b) for n in names for b in beros(n)]
        items += [(n, n, None) for n in names if not beros(n)]
    else:
        items = [(n, n, None if a.no_bero else first_bero(n)) for n in names]
    exe = os.path.abspath(a.exe)
    tmp_audio = a.golden and not a.audio  # only the hashes are wanted: drop each dump once hashed
    if tmp_audio:
        import tempfile
        import atexit, shutil
        a.audio = tempfile.mkdtemp(prefix="map_sweep_audio_")
        atexit.register(shutil.rmtree, a.audio, True)
    levels = {}
    hashes = {}
    if a.audio:
        os.makedirs(a.audio, exist_ok=True)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    groups = collections.defaultdict(list)
    collected = collections.defaultdict(list)
    artifacts = collections.defaultdict(list)
    passed = 0
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        for name, sig, out in pool.map(lambda it: run(exe, root, it, a.frames, a.timeout, a.disc or None,
                                                       os.path.abspath(a.shots) if a.shots else None,
                                                       os.path.abspath(a.input) if a.input else None,
                                                       os.path.abspath(a.audio) if a.audio else None), items):
            m = re.search(r"\[audio\] rms (\d+) peak (\d+) clipped (\d+)", out)
            if m:
                levels[name] = tuple(int(g) for g in m.groups())
            if a.collect:
                for line in out.splitlines():
                    if re.search(a.collect, line) and line not in collected[name]:
                        collected[name].append(line)
            if sig is None:
                passed += 1
                if a.audio:
                    wav = os.path.join(os.path.abspath(a.audio), name + ".wav")
                    hashes[name] = audio_hash(wav)
                    if tmp_audio and os.path.exists(wav):
                        os.remove(wav)
                continue
            why = warp_artifact(name, sig)
            if why is not None:
                artifacts[why].append(name)
                continue
            groups[sig].append(name)
            if a.logs:
                os.makedirs(a.logs, exist_ok=True)
                open(os.path.join(a.logs, name + ".log"), "w", encoding="utf-8").write(out)
            print(f"FAIL {name}: {sig}", flush=True)
    if levels:
        silent = sorted(n for n, (rms, _, _) in levels.items() if rms < 50)
        clipping = sorted(n for n, (_, _, c) in levels.items() if c > 0)
        print(f"\naudio: {len(levels)} maps; {len(silent)} silent at the end, {len(clipping)} clipping")
        if silent:
            print("  silent: " + " ".join(silent))
        for n in clipping:
            print(f"  clipping: {n} ({levels[n][2]} samples)")
    golden_failed = False
    if a.golden and a.update_golden:
        old = {}
        if os.path.exists(a.golden):
            old = json.load(open(a.golden, encoding="utf-8"))
        old.update(hashes)  # with --only, the other maps keep their hashes
        os.makedirs(os.path.dirname(os.path.abspath(a.golden)), exist_ok=True)
        with open(a.golden, "w", encoding="utf-8", newline="\n") as f:
            json.dump(dict(sorted(old.items())), f, indent=0)
            f.write("\n")
        print(f"\ngolden: wrote {len(hashes)} maps' audio hashes to {a.golden}")
    elif a.golden:
        if not os.path.exists(a.golden):
            print(f"\ngolden: {a.golden} missing; record it with --update-golden")
        else:
            want = json.load(open(a.golden, encoding="utf-8"))
            changed = sorted(n for n in hashes if n in want and want[n] != hashes[n])
            new = sorted(n for n in hashes if n not in want)
            print(f"\ngolden: {len(hashes)} maps compared, {len(changed)} changed, {len(new)} not in {a.golden}")
            if changed:
                print("  changed: " + " ".join(changed))
                golden_failed = True
    if collected:
        print(f"\ncollected ({a.collect}):")
        for name in sorted(collected):
            for line in collected[name]:
                print(f"  {name}: {line}")
    unit = ("runs (map%story)" if a.story else "battles (area#entry)" if a.battles
            else "runs (map@entrance)" if a.every_bero else "maps")
    known = sum(len(ns) for ns in artifacts.values())
    print(f"\nmap_sweep: {len(items)} {unit}, {passed} passed, {len(items) - passed - known} failed, "
          f"{known} known warp artifacts, {len(groups)} distinct causes")
    for why, ns in artifacts.items():
        print(f"known warp artifact ({len(ns)}): {why}\n      {' '.join(ns)}")
    for sig, ns in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"{len(ns):4d}  {sig}\n      {' '.join(ns[:12])}{' ...' if len(ns) > 12 else ''}")
    if a.shots:
        sheets = os.path.join(os.path.dirname(os.path.abspath(a.shots)), os.path.basename(os.path.abspath(a.shots)) + "_sheet")
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "contact_sheet.py"),
                        a.shots, sheets])
    return 1 if groups or golden_failed else 0


if __name__ == "__main__":
    sys.exit(main())
