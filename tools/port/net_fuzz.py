#!/usr/bin/env python3
"""Online play's fuzzing (docs/TESTING.md, "Online fuzzing"): rounds of two to four headless games
playing together (sync_soak.py's kinds: walking, a shared battle, three and four players, playing
together), with hostile messages on purpose, two ways:
  in-process  one or more games hand mutated and made-up messages to every handler as they play
              ($TTYD_NET_FUZZ, port/src/common/statesync_fuzz_pc.inc: the corpus is what the others
              really sent; layers m r d t f s p by default, h k w now and then)
  hostile     the local stand-in relay (sync_test_relay.py --hostile) mutates, replays, duplicates and
              reorders the real traffic between the games, and adds junk words and too-big messages
Each round picks a kind, a way (or both), seeds, a rate and layers; the fuzzing stops a while before the
end, and the games must then still be playing together.

  net_fuzz.py --exe build-pc-us-headless/ttyd-us.exe --iso orig/G8ME01/game.iso --out local-docs/fuzz \\
      --hours 2 [--parallel 2] [--asan]

A round fails on: a crash (the crash handler's "*** crash", an AddressSanitizer report), a game that
ran past its time (a hang), a game that never linked, or one no longer linked after the fuzzing stopped
(the session must come through: reconnects are fine, "connection lost" for good is not). Each failed
round's folder is kept with its seeds, logs, the relay's log and the in-process fuzzer's last mutant
(fuzz_last.bin: $TTYD_NET_FUZZ_REPLAY=<it> hands that one message on again). Results: <out>/results.jsonl.
--asan: the games are the sanitizer build (docs/TESTING.md, "Sanitizer builds"; its DLL is looked for beside clang).
Exit status: 0 every round passed, 1 a round failed, 2 the fuzzing couldn't run."""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import shutil
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sync_soak  # noqa: E402

KINDS = ["walk", "battle", "battle3", "relay4", "coop", "coopbattle", "relay", "watch", "late"]
LAYERS_EXTRA = "hkw"
GRACE = 12  # seconds the games play on after the fuzzing stopped, before they all end
FPS = {}  # kind -> game frames a wall-clock second, its fastest game's (measured as it goes; 120 until then)
CRASH = ("*** crash", "*** stall","ERROR: AddressSanitizer", "AddressSanitizer:", "Assertion failed", "assert(")


def asan_path() -> str | None:
    clang = shutil.which("clang") or r"C:\Program Files\LLVM\bin\clang.exe"
    root = Path(clang).resolve().parent.parent / "lib" / "clang"
    for d in sorted(root.glob("*/lib/windows"), reverse=True):
        if (d / "clang_rt.asan_dynamic-x86_64.dll").is_file():
            return str(d)
    return None


def round_plan(rng: random.Random, n: int, forced_way: str | None) -> dict:
    kind = KINDS[n % len(KINDS)]
    way = forced_way or rng.choice(["inproc", "inproc", "hostile", "both"])
    k = sync_soak.KINDS[kind]
    roles = [g[0] for g in k["games"]]
    relay = bool(k.get("relay")) or way in ("hostile", "both") or rng.randrange(3) == 0
    layers = "mrdtfsp" if relay else "mrdsp"
    if rng.randrange(3) == 0:
        layers += rng.choice(LAYERS_EXTRA)
    frames = max(k["frames"], 4000) + rng.choice([0, 1800, 3600, 7200])
    # (mostly the host: a guest takes the host's word for leaving and removals, which end its session)
    victims = [r for r in roles if rng.randrange(4 if r != "host" else 3) < (2 if r == "host" else 1)] or ["host"]
    return {"kind": kind, "way": way, "relay": relay, "layers": layers, "victims": victims,
            "rate": rng.choice([1, 2, 4, 8, 16]), "from": rng.randrange(0, 400),
            "to": frames - 1500 - 1000, "frames": frames, "hostile": "%d:%d" % (rng.randrange(1 << 30), rng.choice([2, 5, 10, 20])),
            "seed": rng.randrange(1 << 30)}


def check_round(plan: dict):
    def check(kind: str, folder: Path, timed_out: bool) -> dict:
        k = sync_soak.KINDS[kind]
        roles = [g[0] for g in k["games"]]
        problems, texts, stop = [], {}, 0
        for role in roles:
            texts[role] = (folder / role / "log.txt").read_text(encoding="utf-8", errors="replace")
            m = re.search(r"\[sync\] f(\d+) fuzz: stopped", texts[role])
            if m:
                stop = max(stop, int(m.group(1)))
        relay_log = folder / "relay.txt"
        relay_text = relay_log.read_text(errors="replace") if relay_log.is_file() else ""
        hostile_stopped = "hostile: stopped" in relay_text
        # (a game that stopped reading what the relay sent it: sync_test_relay.py closes it after a while, so
        # the round may come through, but a game must never stop reading: its stand-in pipe wedged so on a 1 MB
        # message, and its guest never got back)
        for l in relay_text.splitlines():
            if "stalled:" in l:
                problems.append("relay: " + l.split("stalled:", 1)[1].strip()[:100])
                break
        # (a guest fuzzed as from the host takes its word for leaving or a removal: that session ends there)
        ended_by_word = plan["way"] != "hostile" and any(
            r in plan["victims"] and any(w in texts[r] for w in ("co-op: the host left the room",
                                                                 "co-op: the host removed this player",
                                                                 "refused: The host")) for r in roles)
        for role in roles:
            text = texts[role]
            for c in CRASH:
                if c in text:
                    at = text.index(c)
                    problems.append("%s: %s" % (role, text[at:at + 160].replace(chr(10), " | ")))
                    break
            lines = [l for l in text.splitlines() if l.startswith("[sync]")]
            if ended_by_word:
                continue  # (a mutant from the host told a guest it left or removed it: the host's word, no bug)
            if not any("net: session with player" in l for l in lines):
                problems.append("%s: never linked" % role)
                continue
            # at the end (every game stopped at the same moment, a while after the fuzzing stopped: stop_when):
            # playing together again, by its last status lines
            status = ["peer linked" in l for l in lines if re.match(r"\[sync\] f\d+ status: (host|guest)", l)]
            if not status:
                problems.append("%s: no status lines" % role)
            elif not any(status[-3:]) and not (plan["way"] != "hostile" and set(plan["layers"]) & set(LAYERS_EXTRA)):
                # (hellos, the key's rounds and raw bytes restart sessions and take places as a new game's would:
                # those rounds count crashes and hangs alone)
                lost = [l for l in lines if "connection lost" in l or "connection to player" in l]
                problems.append("%s: not linked at the end, %d s after the fuzzing stopped (%s)" % (
                    role, GRACE, lost[-1][:100] if lost else "never lost"))
        # (a mutant took the script's battle away, its room's enemy defeated or gone: the script stops that game,
        # and the others lose it; crashes and sanitizer reports still count)
        if any("battle: no NPC on this map" in t for t in texts.values()):
            problems = [p for p in problems if "not linked at the end" not in p and "no status" not in p]
        if timed_out:
            problems.append("ran too long: killed (a hang?)")
        return {"kind": kind, "ok": not problems, "problems": problems}
    return check


def run_round(n: int, plan: dict, a, out: Path, run: Path, iso: Path, env_more: dict) -> dict:
    folder = out / ("round-%04d-%s-%s" % (n, plan["kind"], plan["way"]))
    if folder.exists():
        shutil.rmtree(folder)
    more = {}
    k = sync_soak.KINDS[plan["kind"]]
    for g in k["games"]:
        role = g[0]
        e = dict(env_more)
        if plan["way"] in ("inproc", "both") and role in plan["victims"]:
            e.update({"TTYD_NET_FUZZ": "1", "TTYD_NET_FUZZ_SEED": str(plan["seed"] + len(role)),
                      "TTYD_NET_FUZZ_RATE": str(plan["rate"]), "TTYD_NET_FUZZ_FROM": str(plan["from"]),
                      "TTYD_NET_FUZZ_TO": str(plan["to"]), "TTYD_NET_FUZZ_LAYERS": plan["layers"],
                      "TTYD_NET_FUZZ_LAST": str(folder / role / "fuzz_last.bin")})
        more[role] = e
    # (the relay's fuzzing stops about when the games' would: their pace measured in the rounds so far)
    fps = FPS.get(plan["kind"], 120.0)
    seconds = (plan["to"] + 1000) / fps
    # (from 1000 game frames on: the keys agreed first, as the games' own fuzzing waits for the first link)
    relay_args = (["--hostile", "%s::%.0f:%.1f" % (plan["hostile"], seconds, 1500 / fps)]
                  if plan["way"] in ("hostile", "both") else [])
    t0 = time.time()
    def stop_when(folder):
        """every fuzzing game's fuzzing stopped (and the relay's) GRACE seconds ago"""
        now = time.time()
        if not hasattr(stop_when, "since"):
            done = all("fuzz: stopped" in (folder / v / "log.txt").read_text(errors="replace")
                       for v in more if more[v].get("TTYD_NET_FUZZ"))
            if relay_args:
                rl = folder / "relay.txt"
                done = done and rl.is_file() and "hostile: stopped" in rl.read_text(errors="replace")
            if done:
                stop_when.since = now
            return False
        return now - stop_when.since >= GRACE
    r = sync_soak.play(plan["kind"], folder, run, iso, a.port_base + (n % 40) * 2, None, plan["seed"], a.slowness,
                       more_env=more, relay_args=relay_args, relay=plan["relay"], check_round=check_round(plan),
                       frames=plan["frames"] * 4, stop_when=stop_when,  # (the wall-clock limit: a hang past it)
                       own_end=plan["frames"] * 40)  # (stop_when ends them all at once, never a game alone)
    r.update({"round": n, "plan": plan, "seconds": round(time.time() - t0, 1),
              "relay_fuzz_s": round(seconds) if relay_args else 0})
    # (the frames the games really reached: they play on past the plan's until stop_when. Measured as the plan's
    # frames over the round's time, the pace came out ever lower and the relay's fuzzing ever longer, until the
    # games reached their own last frame first, one leaving before the other: "not linked at the end", no bug.
    # The fastest game's: it reaches its last frame first, and a slow one's count (a late joiner's, a restart's)
    # made the pace a seventh of the games' and the relay's fuzzing outlast them)
    reached = []
    for role in more:
        lg = folder / role / "log.txt"
        m = re.findall(r"\[sync\] f(\d+) ", lg.read_text(errors="replace")) if lg.is_file() else []
        if m:
            reached.append(max(int(x) for x in m))  # (a restart's lines count from 0 again)
    if reached and r["seconds"] > 5:
        now_fps = max(60.0, max(reached) / r["seconds"])
        FPS[plan["kind"]] = 0.7 * fps + 0.3 * now_fps if plan["kind"] in FPS else now_fps
    (folder / "plan.json").write_text(json.dumps(plan, indent=1), encoding="utf-8")
    if r["ok"] and not a.keep_all:
        shutil.rmtree(folder, ignore_errors=True)
    return r


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--out", default="net_fuzz")
    ap.add_argument("--hours", type=float, default=0)
    ap.add_argument("--rounds", type=int, default=0)
    ap.add_argument("--parallel", type=int, default=1, help="rounds at once (each is 2-4 games)")
    ap.add_argument("--max-games", type=int, default=4, help="games running at once at most, over every round")
    ap.add_argument("--way", choices=["inproc", "hostile", "both"], default=None)
    ap.add_argument("--kinds", default=",".join(KINDS))
    ap.add_argument("--seed", type=int, default=int(time.time()) & 0xFFFFFF)
    ap.add_argument("--port-base", type=int, default=48100)
    ap.add_argument("--slowness", type=float, default=3.0)
    ap.add_argument("--asan", action="store_true")
    ap.add_argument("--keep-all", action="store_true")
    a = ap.parse_args()
    KINDS[:] = [x for x in a.kinds.split(",") if x in sync_soak.KINDS]
    exe, iso = Path(a.exe).resolve(), Path(a.iso).resolve()
    if not exe.is_file() or not iso.is_file() or not KINDS:
        print("the exe, the disc image or the kinds aren't there")
        return 2
    # (a game whose frame never ends: its stack printed and the game out, "*** stall", instead of a round that
    # ran past its time with no word of where)
    env_more = {"TTYD_WATCHDOG": "60"}
    if a.asan:
        d = asan_path()
        if d is None:
            print("no AddressSanitizer DLL found")
            return 2
        os.environ["PATH"] = d + os.pathsep + os.environ.get("PATH", "")
        env_more["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=0:handle_segv=0:windows_hook_rtl_allocators=0"
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    run = exe.with_name("%s-fuzz-%d%s" % (exe.stem, a.port_base, exe.suffix))
    shutil.copy2(exe, run)
    rng = random.Random(a.seed)
    end = time.time() + a.hours * 3600 if a.hours else None
    limit = a.rounds or (0 if a.hours else len(KINDS))
    results, lock = [], threading.Lock()
    games = threading.Condition(lock)
    running, waiting = [0], [0]
    counter = [0]
    print("net fuzz: seed %d, kinds %s, %d at once%s" % (a.seed, ",".join(KINDS), a.parallel,
                                                         ", AddressSanitizer" if a.asan else ""), flush=True)

    def worker():
        while True:
            with lock:
                while waiting[0]:
                    games.wait()  # (one waits for room for its games: first come, first served)
                n = counter[0]
                if (limit and n >= limit) or (end is not None and time.time() >= end):
                    return
                counter[0] += 1
                plan = round_plan(rng, n, a.way)
                need = len(sync_soak.KINDS[plan["kind"]]["games"])
                waiting[0] += 1
                while running[0] and running[0] + need > a.max_games:
                    games.wait()
                waiting[0] -= 1
                games.notify_all()
                running[0] += need
            try:
                r = run_round(n, plan, a, out, run, iso, env_more)
            finally:
                with lock:
                    running[0] -= need
                    games.notify_all()
            with lock:
                results.append(r)
                with open(out / "results.jsonl", "a", encoding="utf-8") as f:
                    f.write(json.dumps(r) + "\n")
                print("round %d %-10s %-7s %s (%.0f s)" % (n, plan["kind"], plan["way"],
                                                         "ok" if r["ok"] else "FAILED: " + "; ".join(r["problems"]),
                                                         r["seconds"]), flush=True)

    threads = [threading.Thread(target=worker) for _ in range(max(1, a.parallel))]
    try:
        for t in threads:
            t.start()
            time.sleep(2)
        for t in threads:
            t.join()
    finally:
        try:
            run.unlink()
        except OSError:
            pass
    failed = [r for r in results if not r["ok"]]
    print("net fuzz: %d rounds, %d failed, %.1f game-hours" % (
        len(results), len(failed), sum(r["seconds"] for r in results) / 3600))
    return 2 if not results else 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
