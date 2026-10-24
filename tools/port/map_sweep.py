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
import subprocess
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def maps_of(root):
    """Disc maps the game can change to: those in its map table (src/data/mapdata.c;
    mapDataPtr returns NULL for the rest, e.g. stg_* test stages, and the game crashes too)."""
    table = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                         "src", "data", "mapdata.c")
    known = set(re.findall(r'^MapData \w+ = \{ "(\w+)"', open(table, encoding="utf-8").read(), re.M))
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


def first_bero(name):
    """The first entrance (bero) the map's source declares, so the warp enters
    as in play; without one Mario starts at the map's origin, which on some
    maps (eki_02) is over no floor."""
    import glob
    if name in ORIGIN_ONLY:
        return None
    for f in glob.glob(os.path.join(ROOT, "rels", "*", "src", name + ".c")):
        m = re.search(r'"(\w+)",\s*//\s*hitName', open(f, encoding="utf-8", errors="replace").read())
        if m:
            return m.group(1)
    return None


def audio_hash(path):
    """SHA-1 of a WAV dump's samples (not the header)."""
    try:
        with open(path, "rb") as f:
            return hashlib.sha1(f.read()[44:]).hexdigest()
    except OSError:
        return None


def run(exe, root, name, frames, timeout, disc=None, shots=None, script=None, audio=None, bero=None):
    env = dict(os.environ, TTYD_DATA_DIR=root, TTYD_MAP=name + ("," + bero if bero else ""),
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
    try:
        r = subprocess.run([exe] + ([disc] if disc else []), env=env, capture_output=True, text=True, errors="replace", timeout=timeout)
        out, code = r.stdout + r.stderr, r.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        code = "timeout"
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
    m = re.search(r"\*\*\* (?:crash|stall): (.*)\n((?:[ \t]+[0-9a-fA-F]{16} .*\n){0,3})", out)
    if m:
        frames = [re.sub(r"^\s*\S+\s+", "", l).split(" (")[0] for l in m.group(2).splitlines()]
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
    root = os.path.join(a.data, "files") if os.path.isdir(os.path.join(a.data, "files")) else a.data
    if not os.path.isdir(os.path.join(root, "m")):
        print(f"map_sweep: no extracted disc at {a.data}: skipped")
        return 77
    names = maps_of(root)
    if a.only:
        names = [n for n in names if n.startswith(tuple(a.only.split(",")))]
    exe = os.path.abspath(a.exe)
    if a.golden and not a.audio:
        import tempfile
        a.audio = tempfile.mkdtemp(prefix="map_sweep_audio_")
    levels = {}
    hashes = {}
    if a.audio:
        os.makedirs(a.audio, exist_ok=True)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    groups = collections.defaultdict(list)
    collected = collections.defaultdict(list)
    passed = 0
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        for name, sig, out in pool.map(lambda n: run(exe, root, n, a.frames, a.timeout, a.disc or None,
                                                      os.path.abspath(a.shots) if a.shots else None,
                                                      os.path.abspath(a.input) if a.input else None,
                                                      os.path.abspath(a.audio) if a.audio else None,
                                                      None if a.no_bero else first_bero(n)), names):
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
                    hashes[name] = audio_hash(os.path.join(os.path.abspath(a.audio), name + ".wav"))
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
    print(f"\nmap_sweep: {len(names)} maps, {passed} passed, {len(names) - passed} failed, {len(groups)} distinct causes")
    for sig, ns in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"{len(ns):4d}  {sig}\n      {' '.join(ns[:12])}{' ...' if len(ns) > 12 else ''}")
    if a.shots:
        sheets = os.path.join(os.path.dirname(os.path.abspath(a.shots)), os.path.basename(os.path.abspath(a.shots)) + "_sheet")
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "contact_sheet.py"),
                        a.shots, sheets])
    return 1 if groups or golden_failed else 0


if __name__ == "__main__":
    sys.exit(main())
