#!/usr/bin/env python3
"""Monkey runs: every map gets a minute or so of seeded random input, to shake
out crashes no scripted run would hit.

For each map of the game's map table, at its own chapter's story value and at
the post-game's, K seeds of N game-seconds each: the TTYD_MAP warp in by the
map's first entrance (with the partners the story has by then), then the
input script's `random FRAMES SEED` (port/src/common/input_pc.c: walking,
jumps, the hammer, partner abilities, the menus and back out). Fast headless
runs are deterministic, so a failing seed replays exactly.

A run fails on a crash, a stall (no frame for 30 s: the watchdog's stack), an
OSPanic (the game's asserts), a fatal exit, or a softlock: in the field, Mario
without control for --softlock seconds (TTYD_CONTROL_WATCH, port/src/aurora/
story_test_aurora.c). Each run goes through the shared game slots
($GAME_SLOT, when set) with its own fresh user dir.

usage:
  monkey.py --exe EXE [--iso ISO] [--out DIR] [-j N] [--seeds K] [--seed-base B]
            [--seconds S] [--only PREFIX,...] [--stories own,post] [--god off|on|both]
            [--resume] [--confirm] [--shrink]
  monkey.py --exe EXE --replay KEY [--log FILE]   one run again (KEY as in the summary)

Results go to OUT/results.jsonl (one line per run; --resume skips the runs
already there), OUT/summary.json and OUT/summary.md, failing runs' logs to
OUT/logs/. KEY is MAP%STORY#SEED (with a trailing g when TTYD_GOD was on).
"""
import argparse
import collections
import concurrent.futures
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_sweep  # noqa: E402  (maps_of, first_bero, signature, warp artifacts)

ROOT = map_sweep.ROOT
POST_GAME = 405  # STORY_STG9_02_RETURN_TO_ROGUEPORT (include/evt/evt_story.h)
GAME_SLOT = os.environ.get("GAME_SLOT", "")  # unset: run directly
VERSIONS = {"G8MJ01": "jp", "G8ME01": "us", "G8MP01": "eu"}

# partner id (1 Goombella .. 7 Ms. Mowz) and the story value they join at (include/evt/evt_story.h)
PARTNERS = [(1, 7), (2, 33), (5, 87), (4, 137), (6, 206), (3, 228), (7, POST_GAME)]

# Failures random input causes in a state the warp set up that play can't reach:
# (key regex, text in the signature or the run's output, why). Reported apart; they don't fail a run.
MONKEY_ARTIFACTS = [
    (r".", "evt_img_wait_animend",
     "headless: a paper-peel scene (evt_img_*) only animates in the draw callback, so its wait never ends"),
]


def story_steps():
    """[(value, maps named in its comment)] from include/evt/evt_story.h."""
    out = []
    line = re.compile(r"^\s*STORY_\w+\s*=\s*(-?\d+),\s*//\s*(.*)$")
    for l in open(os.path.join(ROOT, "include", "evt", "evt_story.h"), encoding="utf-8"):
        m = line.match(l)
        if m:
            out.append((int(m.group(1)), re.findall(r"\((\w+?_\w+)\)", m.group(2))))
    return out


def own_story(name, steps):
    """The map's own point of the story: the middle of the story steps set on it,
    else of those in its area; the post-game for an area the story never visits."""
    vals = sorted(v for v, maps in steps if name in maps)
    if not vals:
        area = name.split("_")[0] + "_"
        vals = sorted(v for v, maps in steps if any(m.startswith(area) for m in maps))
    return vals[len(vals) // 2] if vals else POST_GAME


def party_for(story, seed):
    """TTYD_PARTY: the partners joined by then, a different one out per seed."""
    have = [p for p, at in PARTNERS if story >= at] or [1]
    k = seed % len(have)
    return ",".join(str(p) for p in have[k:] + have[:k])


def parse_key(key):
    m = re.match(r"^(\w+)%(\d+)#(\d+)(g?)$", key)
    if not m:
        raise SystemExit(f"monkey: bad key {key!r} (MAP%STORY#SEED[g])")
    return m.group(1), int(m.group(2)), int(m.group(3)), m.group(4) == "g"


def key_of(name, story, seed, god):
    return f"{name}%{story:03d}#{seed}{'g' if god else ''}"


SCRIPT = """# monkey.py: {key}
give 148
give 132
give 240
give 9
give 6
coins 300
sp 300
random {frames} {seed}
end
"""


def bash_exe():
    # Git Bash before PATH's: on Windows that may be the WSL launcher (WindowsApps/bash.exe)
    for c in ("C:/Program Files/Git/bin/bash.exe", shutil.which("bash")):
        if c and os.path.exists(c):
            return c
    raise SystemExit("monkey: no bash for the game slots")


def run(a, key, extra_env=None, keep_log=None):
    """One run: (key, signature or None, output, seconds)."""
    name, story, seed, god = parse_key(key)
    work = tempfile.mkdtemp(prefix="ttyd-monkey-")
    try:
        user = os.path.join(work, "user")
        os.makedirs(user)
        with open(os.path.join(user, "settings.txt"), "w", encoding="utf-8") as f:
            f.write("skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\n")
        script = os.path.join(work, "monkey.txt")
        with open(script, "w", encoding="utf-8") as f:
            f.write(SCRIPT.format(key=key, frames=a.seconds * 60, seed=seed))
        bero = map_sweep.first_bero(name)
        env = dict(os.environ, TTYD_MAP=name + ("," + bero if bero else ""), TTYD_STORY=str(story),
                   TTYD_PARTY=party_for(story, seed), TTYD_FAST="1", TTYD_TRACE="1", TTYD_WATCHDOG="30",
                   TTYD_CONTROL_WATCH=str(a.softlock), TTYD_INPUT=script, TTYD_USER_DIR=user,
                   TTYD_WARP_LIMIT="120", TTYD_NO_LAUNCHER="1",
                   # Aurora (re-checking a headless failure): off-screen, no focus, the tests' own
                   # shader cache (kept between runs: never the user's %APPDATA%)
                   TTYD_WINDOW_POS="-4000,0", SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0",
                   TTYD_CACHE_DIR=os.environ.get("TTYD_CACHE_DIR",
                                                 os.path.join(tempfile.gettempdir(), "ttyd-monkey-cache")))
        env.pop("TTYD_GOD", None)
        if god:
            env["TTYD_GOD"] = "1"
        if a.data:
            env["TTYD_DATA_DIR"] = a.data
        env.update(extra_env or {})
        # through the shared game slots where there are any (this PC), else at once (-j bounds it)
        slot = [GAME_SLOT] if os.path.exists(GAME_SLOT) else ["-c", 'exec "$@"', "_"]
        cmd = [bash_exe()] + slot + ["timeout", "-k", "10", str(a.timeout), a.exe] + ([a.iso] if a.iso else [])
        t0 = time.time()
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace", cwd=work)
        out, code = r.stdout + r.stderr, r.returncode
        if code == 124:
            code = "timeout"
        dt = time.time() - t0
        if keep_log:
            with open(keep_log, "w", encoding="utf-8") as f:
                f.write(out)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return key, verdict(out, code), out, dt


def verdict(out, code):
    """None for a pass, else a short signature shared by runs with the same cause."""
    m = re.search(r"^\[story\] NO CONTROL: .*?\(story \d+, map '(\w*)'", out, re.M)
    if m:
        # keyed on the map alone: which scripts run at the moment it's noticed varies (details())
        return f"softlock: {m.group(1)}"
    if code == 0 and "[input] end" in out:
        return None
    if code == "timeout":
        return "timeout (frames still running)"
    return map_sweep.signature(out, code)


def details(out):
    """A softlock's running scripts (names, not ids or offsets); a crash's or stall's first stack lines."""
    held = re.findall(r"^\[story\]   #\d+ id \d+ at (\S+?)(?:\+0x[0-9a-f]+)?(?: |$)", out, re.M)
    if held:
        return ", ".join(sorted(set(held)))
    m = re.search(r"\*\*\* (?:crash|stall): .*\n((?:[ \t]+[0-9a-fA-F]{16} .*\n){1,6})", out)
    return m.group(1).strip() if m else ""


def artifact(key, sig, out=""):
    why = map_sweep.warp_artifact(key.split("#")[0], sig)
    if why:
        return why
    if sig.startswith("not reached"):
        return "the warp didn't reach the map at this story value (the map's own scripts move Mario on)"
    for pattern, text, why in MONKEY_ARTIFACTS:
        if re.match(pattern, key) and (text in sig or (sig.startswith("softlock") and text in out)):
            return why
    return None


def stats(out):
    """What the run did: battles fought, Game Overs, maps visited."""
    seqs = re.findall(r"^\[trace\] frame \d+: sequence (\d+), map '(\w*)'", out, re.M)
    return {"battles": sum(1 for s, _ in seqs if s == "4"), "game_overs": sum(1 for s, _ in seqs if s == "5"),
            "maps": len({m for s, m in seqs if s == "2" and m})}


def items(a):
    data = a.data
    if data and os.path.isdir(os.path.join(data, "m")):
        names = map_sweep.maps_of(data)
    else:  # the map table's maps on the disc image (as maps_of on an extracted disc)
        import gcdisc
        table = open(os.path.join(ROOT, "src", "data", "mapdata.c"), encoding="utf-8").read()
        known = set(re.findall(r'^MapData \w+ = \{\s*"(\w+)"', table, re.M))
        on_disc = {p.split("/")[1] for p in gcdisc.Disc(a.iso).files if p.startswith("m/") and p.endswith("/d")}
        names = sorted(known & on_disc)
    if a.only:
        prefixes = [p for p in a.only.split(",") if p]
        unknown = [p for p in prefixes if not any(n.startswith(p) for n in names)]
        if unknown:  # (a misspelt prefix selects nothing: never a monkey run of no maps that passes)
            raise SystemExit(f"monkey: --only {','.join(unknown)} matches no map")
        names = [n for n in names if n.startswith(tuple(prefixes))]
    steps = story_steps()
    gods = {"off": [False], "on": [True], "both": [False, True]}[a.god]
    out = []
    for n in names:
        stories = []
        if "own" in a.stories:
            stories.append(own_story(n, steps))
        if "post" in a.stories and POST_GAME not in stories:
            stories.append(POST_GAME)
        for v in stories:
            for s in range(a.seed_base, a.seed_base + a.seeds):
                for g in gods:
                    out.append(key_of(n, v, s, g))
    return out


def load_results(path):
    res = {}
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if line:
                r = json.loads(line)
                res[r["key"]] = r
    return res


def summarize(a, results, keys, elapsed):
    rs = [results[k] for k in keys if k in results]
    fails = [r for r in rs if r["sig"] and not r.get("artifact")]
    arts = [r for r in rs if r["sig"] and r.get("artifact")]
    groups = collections.defaultdict(list)
    for r in fails:
        groups[r["sig"]].append(r)
    by_god = {}
    for g in (False, True):
        sub = [r for r in rs if r["key"].endswith("g") == g]
        if sub:
            by_god["on" if g else "off"] = {
                "runs": len(sub), "failed": sum(1 for r in sub if r["sig"] and not r.get("artifact")),
                "battles": sum(r.get("battles", 0) for r in sub), "game_overs": sum(r.get("game_overs", 0) for r in sub),
                "causes": len({r["sig"] for r in sub if r["sig"] and not r.get("artifact")})}
    summary = {
        "exe": a.exe, "seconds": a.seconds, "seeds": [a.seed_base, a.seed_base + a.seeds - 1], "god": a.god,
        "stories": a.stories, "runs": len(rs), "planned": len(keys), "passed": len(rs) - len(fails) - len(arts),
        "failed": len(fails), "artifacts": len(arts), "wall_seconds": round(elapsed),
        "run_seconds": round(sum(r.get("dt", 0) for r in rs)), "by_god": by_god,
        "causes": [{"signature": s, "runs": [r["key"] for r in g], "confirmed": [r["key"] for r in g if r.get("confirmed")],
                    "shrunk": next((r["shrunk"] for r in g if r.get("shrunk")), None),
                    "detail": g[0].get("detail", "")}
                   for s, g in sorted(groups.items(), key=lambda kv: -len(kv[1]))],
        "artifact_runs": [{"key": r["key"], "signature": r["sig"], "why": r["artifact"]} for r in arts],
    }
    with open(os.path.join(a.out, "summary.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(summary, f, indent=1)
    md = [f"# Monkey runs", "",
          f"- exe: `{a.exe}`", f"- {len(rs)} of {len(keys)} runs, {a.seconds} s of random input each; seeds "
          f"{a.seed_base}..{a.seed_base + a.seeds - 1}; stories: {a.stories}; TTYD_GOD: {a.god}",
          f"- passed {summary['passed']}, failed {len(fails)} ({len(groups)} causes), harness artifacts {len(arts)}",
          f"- wall time {elapsed / 60:.1f} min", ""]
    for g, s in by_god.items():
        md.append(f"- TTYD_GOD {g}: {s['runs']} runs, {s['failed']} failed ({s['causes']} causes), "
                  f"{s['battles']} battles, {s['game_overs']} Game Overs")
    md += ["", "## Failures", ""]
    if not groups:
        md.append("None.")
    for c in summary["causes"]:
        md.append(f"### {c['signature']}")
        md.append("")
        if c["detail"]:
            md += ["```", c["detail"], "```", ""]
        md.append(f"{len(c['runs'])} runs: " + " ".join(f"`{k}`" for k in c["runs"][:20]) +
                  (" ..." if len(c["runs"]) > 20 else ""))
        if c["confirmed"]:
            md.append(f"\nreplayed: {', '.join(c['confirmed'][:5])}")
        if c["shrunk"]:
            md.append(f"\nshrunk: {c['shrunk']}")
        md.append(f"\nreplay: `monkey.py --exe EXE --replay {c['runs'][0]}`\n")
    if arts:
        md += ["## Harness artifacts", ""]
        for r in arts:
            md.append(f"- `{r['key']}`: {r['sig']} ({r['artifact']})")
    with open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md) + "\n")
    return summary


def actions_before_failure(out):
    """The random actions the run had drawn when it failed: [(index, name)]."""
    return [(int(i), n) for i, n in re.findall(r"^\[random\] #(\d+) frame \d+: ([^,]+),", out, re.M)]


def shrink(a, key, sig, budget):
    """ddmin over the random actions: the fewest actions (the rest played as no
    input, TTYD_RANDOM_SKIP) that still fail with the same signature."""
    _, s0, out, _ = run(a, key, {"TTYD_RANDOM_LOG": "1"})
    if s0 != sig:
        return None
    acts = actions_before_failure(out)
    if not acts:
        return None
    n = acts[-1][0] + 1  # the actions after the failure's never matter
    keep = list(range(n))
    runs = 0

    def skip_of(kept):  # TTYD_RANDOM_SKIP for all but `kept`, as ranges
        out, i, ks = [], 0, set(kept)
        while i < n:
            if i in ks:
                i += 1
                continue
            j = i
            while j + 1 < n and j + 1 not in ks:
                j += 1
            out.append(str(i) if i == j else f"{i}-{j}")
            i = j + 1
        return ",".join(out + [f"{n}-999999"])

    def fails(kept):
        nonlocal runs
        runs += 1
        _, s, _, _ = run(a, key, {"TTYD_RANDOM_SKIP": skip_of(kept)})
        return s == sig

    if fails([]):
        keep = []  # the warp's state alone, no input at all
    else:
        chunks = 2
        while len(keep) >= 2 and runs < budget:
            size = max(1, len(keep) // chunks)
            reduced = False
            for i in range(0, len(keep), size):
                trial = keep[:i] + keep[i + size:]
                if runs >= budget:
                    break
                if fails(trial):
                    keep, chunks, reduced = trial, max(chunks - 1, 2), True
                    break
            if not reduced:
                if size == 1:
                    break
                chunks = min(len(keep), chunks * 2)
    names = dict(acts)
    desc = ", ".join(f"#{i} {names.get(i, '?')}" for i in keep) or "none: the warp's state alone"
    return f"{len(keep)} of {n} actions ({desc}); TTYD_RANDOM_SKIP={skip_of(keep)} ({runs} runs)"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="the headless build (or any build: Aurora is slower)")
    ap.add_argument("--iso", default="", help="the disc image (default orig/<version>/*.iso by the exe's name)")
    ap.add_argument("--data", default="", help="the extracted disc's files/ (map list; default orig/<version>/files)")
    ap.add_argument("--out", default="", help="results folder (default local-docs/monkey/<version>)")
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--seed-base", type=int, default=1)
    ap.add_argument("--seconds", type=int, default=60, help="game-seconds of random input per run")
    ap.add_argument("--only", default="", help="map prefixes, comma-separated")
    ap.add_argument("--stories", default="own,post", help="own (the map's chapter), post (the post-game), or both")
    ap.add_argument("--god", default="on", choices=["off", "on", "both"], help="TTYD_GOD: no damage, no Game Over")
    ap.add_argument("--softlock", type=int, default=40, help="seconds without control in the field = a softlock")
    ap.add_argument("--timeout", type=int, default=900, help="real seconds per run (once its slot is free)")
    ap.add_argument("--resume", action="store_true", help="skip runs already in results.jsonl")
    ap.add_argument("--confirm", action="store_true", help="replay each failing cause's first run")
    ap.add_argument("--shrink", type=int, default=0, metavar="RUNS",
                    help="shrink each confirmed cause's first run with at most RUNS replays")
    ap.add_argument("--replay", default="", help="one run (KEY) again, its log to --log")
    ap.add_argument("--log", default="", help="--replay: where its output goes (default stdout's tail)")
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)  # (each run's working folder is its own)
    a.iso = os.path.abspath(a.iso) if a.iso else ""
    a.data = os.path.abspath(a.data) if a.data else ""
    # (by name: "both" or a misspelt one used to choose no story at all, and no runs passed as a clean sweep)
    stories = {s.strip() for s in a.stories.split(",") if s.strip()}
    if "both" in stories:
        stories = (stories - {"both"}) | {"own", "post"}
    if not stories or stories - {"own", "post"}:
        print(f"monkey: --stories {a.stories!r}: own, post or both")
        return 2
    a.stories = ",".join(sorted(stories))
    m = re.search(r"ttyd-(jp|us|eu)", os.path.basename(a.exe))
    version = {v: k for k, v in VERSIONS.items()}.get(m.group(1) if m else "us", "G8ME01")
    if not a.iso:
        import glob
        isos = glob.glob(os.path.join(ROOT, "orig", version, "*.iso"))
        a.iso = isos[0] if isos else ""
    if not a.data:
        d = os.path.join(ROOT, "orig", version, "files")
        a.data = d if os.path.isdir(os.path.join(d, "m")) else ""  # (orig/ may hold only rel/)
    if not a.iso and not a.data:
        print(f"monkey: no disc for {version} (--iso or --data)")
        return 77
    if a.replay:
        key, sig, out, dt = run(a, a.replay, {"TTYD_RANDOM_LOG": "1"}, a.log or None)
        print("\n".join(out.splitlines()[-40:]) if not a.log else f"log: {a.log}")
        print(f"\n{key}: {sig or 'pass'} ({dt:.1f} s)")
        return 1 if sig else 0
    a.out = os.path.abspath(a.out or os.path.join(ROOT, "local-docs", "monkey", VERSIONS[version]))
    os.makedirs(os.path.join(a.out, "logs"), exist_ok=True)
    res_path = os.path.join(a.out, "results.jsonl")
    if not a.resume and os.path.exists(res_path):
        os.replace(res_path, res_path + ".old")
    results = load_results(res_path)
    keys = items(a)
    if not keys:  # (0 of 0 runs is not a pass)
        print("monkey: no runs planned (no maps on the disc?)")
        return 2
    todo = [k for k in keys if k not in results]
    print(f"monkey: {len(keys)} runs ({len(todo)} to go), {a.seconds} s each, -j {a.j}, results in {a.out}", flush=True)
    t0 = time.time()
    done = 0
    with open(res_path, "a", encoding="utf-8", newline="\n") as rf, \
            concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        futs = [pool.submit(run, a, k) for k in todo]
        for fut in concurrent.futures.as_completed(futs):
            key, sig, out, dt = fut.result()
            done += 1
            r = {"key": key, "sig": sig, "dt": round(dt, 1), **stats(out)}
            if sig:
                r["detail"] = details(out)
            if sig:
                r["artifact"] = artifact(key, sig, out)
                with open(os.path.join(a.out, "logs", key.replace("%", "_").replace("#", "_") + ".log"), "w",
                          encoding="utf-8") as f:
                    f.write(out)
                print(f"{'ARTIFACT' if r['artifact'] else 'FAIL'} {key}: {sig}", flush=True)
            results[key] = r
            rf.write(json.dumps(r) + "\n")
            rf.flush()
            if done % 50 == 0:
                el = time.time() - t0
                print(f"monkey: {done}/{len(todo)} in {el / 60:.1f} min", flush=True)
    if a.confirm or a.shrink:
        # each failing map%story once with no input at all: failing the same then is the warp's
        # state (what map_sweep.py --story covers), not random play's
        spots = {}
        for k in keys:
            r = results.get(k)
            if r and r["sig"] and not r.get("artifact") and "no_input" not in r:
                spots.setdefault((k.split("#")[0], r["sig"]), []).append(k)
        with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
            checks = {spot: pool.submit(run, a, ks[0], {"TTYD_RANDOM_SKIP": "0-999999"}) for spot, ks in spots.items()}
            for (spot, sig), fut in checks.items():
                s3 = fut.result()[1]
                for k in spots[(spot, sig)]:
                    results[k]["no_input"] = s3
                    if s3 == sig:
                        results[k]["artifact"] = ("fails the same with no input: the warp's state (story value, "
                                                  "entrance or flags play would have set), not random play")
                print(f"no input {spot}: {'the same failure: a warp artifact' if s3 == sig else s3 or 'pass'}",
                      flush=True)
        firsts = {}
        for k in keys:
            r = results.get(k)
            if r and r["sig"] and not r.get("artifact") and r["sig"] not in firsts:
                firsts[r["sig"]] = k
        for sig, k in firsts.items():
            _, s2, _, _ = run(a, k)
            results[k]["confirmed"] = s2 == sig
            print(f"replay {k}: {'same failure' if s2 == sig else 'now: ' + str(s2)}", flush=True)
            if s2 == sig and a.shrink:
                results[k]["shrunk"] = shrink(a, k, sig, a.shrink)
                print(f"  shrunk: {results[k]['shrunk']}", flush=True)
        with open(res_path, "w", encoding="utf-8", newline="\n") as rf:
            for r in results.values():
                rf.write(json.dumps(r) + "\n")
    s = summarize(a, results, keys, time.time() - t0)
    print(f"\nmonkey: {s['runs']} runs, {s['passed']} passed, {s['failed']} failed ({len(s['causes'])} causes), "
          f"{s['artifacts']} harness artifacts; {os.path.join(a.out, 'summary.md')}")
    for c in s["causes"]:
        print(f"{len(c['runs']):4d}  {c['signature']}\n      {' '.join(c['runs'][:8])}")
    if s["failed"]:
        return 1
    if not s["passed"]:  # (every run a harness artifact, the warp never arriving one: nothing shows the game played)
        print("monkey: no run passed")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
