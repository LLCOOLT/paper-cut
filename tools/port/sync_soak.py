#!/usr/bin/env python3
"""Sync what matters' soak test (docs/STATE_SYNC.md): rounds of two to four
headless games on this computer playing together, each in their own game, by
address on the IPv6 loopback or through a room on a local stand-in for the
relay (sync_test_relay.py, beside this file), with network trouble on purpose
($TTYD_NET_FAULTS, port/include/port/online_faults.h).

  sync_soak.py --exe build-pc-us-headless/ttyd-us.exe --iso orig/G8ME01/game.iso \\
      --out sync_soak --hours 2

Each round plays one kind of session (--kinds, each in turn):
  walk     two games: items, coins, flags and the story on both sides; the
           guest joins mid-game (the whole progress merged)
  battle   two games share a script battle (hei_03's Goombas), frame for frame
  battle3  the same with three games (player 3 a companion)
  watch    a player and a watcher who starts in another room and follows
  late     a player in the pause menu when the battle starts joins it under way
  relay    walk through a relay room
  relay4   battle with four games through a relay room (its peer places)
  listed   a listed relay room: two games join the list's first room
Every game gets a name ($TTYD_SYNC_NAME). A round passes when every game
logged no DRIFT (a shared battle's games differing), was linked, learned every
other player's name, and ended with the same progress hash as the others (the
story, the area's flags, the pouch, the coins, Mario's level and the partners:
statesync_pc.c's digest); a battle round also needs the host's shared battle
to have ended back in the field.

--faults: off, on (every round), or mix (every other round): drops,
duplicates, reordering and delays, with a fresh seed each round; walking
rounds by address also lose their connection twice. The games run with
TTYD_FAST=1 and a fresh user folder each. Results: one JSON line per round in
<out>/results.jsonl and a summary; a round's folder is kept only when it
failed (--keep-all keeps every one). --slot runs the soak through a shell
script that hands out a shared game slot (its arguments: the command to run).

Exit status: 0 every round passed, 1 a round failed, 2 the soak couldn't run."""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
AUTO = ["TTYD_AUTO_BATTLE=1", "TTYD_GOD=1", "TTYD_AUTO_TEXT=1"]
WALK_HOST = "wait 60;give 148;give 148;coins 150;flag 1000;story 20;join 2;wait 900"
WALK_GUEST = "wait 60;give 154;give 62;wait 500;give 240;coins 10;wait 400"
BATTLE_HOST = "wait 900;give 148;battle 4;wait 5300"
IDLE = "wait 6400"
COOP_HOST = "wait 1700;goto w_bero;stick -127 0 90;wait 1300"
COOP_GUEST = "wait 500;stick 127 0 240;stick -127 0 240;stick 0 127 240;wait 1900"

# kind: (map, story, frames, relay, battle, [(role, script, extra env, own map)...], extra)
KINDS = {
    "walk": dict(map="gor_01", frames=1600, games=[
        ("host", WALK_HOST, [], None), ("guest", WALK_GUEST, ["TTYD_SYNC_AT=1100"], None)]),
    "battle": dict(map="hei_03", story="11", frames=6600, battle=True, games=[
        ("host", "wait 200;give 148;battle 4;wait 6000", AUTO, None), ("guest", IDLE, AUTO, None)]),
    "battle3": dict(map="hei_03", story="11", frames=6600, battle=True, games=[
        ("host", "wait 200;give 148;battle 4;wait 6000", AUTO, None), ("guest", IDLE, AUTO, None),
        ("guest2", IDLE, AUTO, None)]),
    "watch": dict(map="hei_03", story="11", frames=6600, battle=True, games=[
        ("host", BATTLE_HOST, AUTO, None), ("guest", IDLE, AUTO, None),
        ("guest2", IDLE, AUTO + ["TTYD_SYNC_WATCH=1"], "hei_01")]),
    "late": dict(map="hei_03", story="11", frames=7000, battle=True, games=[
        ("host", "wait 900;give 148;battle 4;wait 5900", AUTO, None),
        ("guest", "wait 700;press start;wait 900;press b;wait 30;press b;wait 30;press start;wait 5300", AUTO,
         None)]),
    "relay": dict(map="gor_01", frames=1600, relay=True, games=[
        ("host", WALK_HOST, [], None), ("guest", WALK_GUEST, ["TTYD_SYNC_AT=1100"], None)]),
    "relay4": dict(map="hei_03", story="11", frames=6600, relay=True, battle=True, games=[
        ("host", BATTLE_HOST, AUTO, None), ("guest", IDLE, AUTO, None), ("guest2", IDLE, AUTO, None),
        ("guest3", IDLE, AUTO, None)]),
    "listed": dict(map="gor_01", frames=1600, relay=True, games=[
        ("host", WALK_HOST, ["TTYD_SYNC_LIST=Soak"], None),
        ("guest", WALK_GUEST, ["TTYD_SYNC_JOIN_ROOM=listed"], None),
        ("guest2", WALK_GUEST, ["TTYD_SYNC_JOIN_ROOM=listed"], None)]),
    # playing together: the guest's stick walks the partner in the host's game, then the host changes rooms
    # and the guest's game goes along (the guest starts elsewhere: it follows the host there first)
    "coop": dict(map="hei_03", story="11", frames=3200, coop=True, games=[
        ("host", COOP_HOST, AUTO, None), ("guest", COOP_GUEST, AUTO, "hei_01")]),
    # playing together: the host's battle, the guest as the partner (its turns chosen on its own screen)
    "coopbattle": dict(map="hei_03", story="11", frames=6600, coop=True, battle=True, games=[
        ("host", "wait 400;give 148;battle 4;wait 6000", AUTO, None), ("guest", IDLE, AUTO, None)]),
    # playing together, the guest asks for Mario's seat (Who's who), the host says yes: the guest's stick walks
    # Mario in the host's game, the host's own controller plays the partner
    "coopmario": dict(map="hei_03", story="11", frames=3000, coop=True, games=[
        ("host", "wait 2900", AUTO + ["TTYD_SYNC_SEAT_YES=1"], None),
        ("guest", "wait 1000;stick 127 0 300;stick -127 0 300;wait 1300", AUTO + ["TTYD_SYNC_SEAT_ASK_AT=700:1"],
         None)]),
    # the same with the guest a differently laid-out program (--exe2: a build configured with CFLAGS=-DTTYD_LAYOUT_PAD)
    "xplat": dict(map="hei_03", story="11", frames=6600, coop=True, battle=True, xplat=True, games=[
        ("host", "wait 400;give 148;battle 4;wait 6000", AUTO, None), ("guest", IDLE, AUTO, None)]),
}
FAULTS = "drop:3,dup:2,reorder:3,delay:0-30,quiet:3000"
FAULTS_WALK = "drop:5,dup:3,reorder:5,delay:0-40,disconnect_at:1250/1400"
CODE = "2TST-RXYM"
PROGRESS = re.compile(r"progress ([0-9a-f]{16}/[0-9a-f]{16})")
NAME = re.compile(r"peer: player (\d)'s name: \"")


def play(kind: str, folder: Path, exe: Path, iso: Path, port: int, faults: str | None, seed: int,
         slowness: float, exe2: Path | None = None) -> dict:
    k = KINDS[kind]
    folder.mkdir(parents=True, exist_ok=True)
    relay = None
    if k.get("relay"):
        rport = port + 100
        relay = subprocess.Popen([sys.executable, str(HERE / "sync_test_relay.py"), str(rport), "--log",
                                  str(folder / "relay.txt"), "--seconds", str(int(k["frames"] / 60 * slowness) + 90)])
        time.sleep(1.0)
    procs, logs = [], []
    for i, (role, script, extra, own_map) in enumerate(k["games"]):
        user = folder / role
        user.mkdir(parents=True, exist_ok=True)
        (user / "settings.txt").write_text(
            "skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\nupdate_check=0\n",
            encoding="utf-8")
        (user / "input.txt").write_text(script.replace(";", "\n") + "\nend\n", encoding="utf-8")
        env = {key: v for key, v in os.environ.items() if not key.startswith("TTYD_")}
        env.update({"TTYD_FAST": "1", "TTYD_USER_DIR": str(user), "TTYD_MAP": own_map or k["map"],
                    "TTYD_EXIT_FRAMES": str(k["frames"]), "TTYD_WARP_LIMIT": "120", "TTYD_NO_LAUNCHER": "1",
                    "TTYD_INPUT": str(user / "input.txt"), "TTYD_ONLINE_MODE": "sync", "TTYD_SYNC_PACE": "300",
                    "TTYD_SYNC_LOG": "60", "TTYD_SYNC_NAME": "Soak %s" % role.capitalize(),
                    "TTYD_WIRE_CHECK": "1"})
        if k.get("story"):
            env["TTYD_STORY"] = k["story"]
        if role == "host" and not k.get("coop"):
            env["TTYD_SYNC_PICK"] = "-1"  # (the layers alone, each player free: the adventure together is a room's default)
        if relay is not None:
            env["TTYD_SYNC_TEST_RELAY"] = "127.0.0.1:%d" % (port + 100)
            env["TTYD_SYNC_HOST_ROOM" if role == "host" else "TTYD_SYNC_JOIN_ROOM"] = CODE
        elif role == "host":
            env["TTYD_SYNC_HOST"] = str(port)
        else:
            env["TTYD_SYNC_JOIN"] = "[::1]:%d" % port
        if faults:
            env["TTYD_NET_FAULTS"] = faults
            env["TTYD_NET_FAULTS_SEED"] = str(seed + i)
        for kv in extra:
            key, v = kv.split("=", 1)
            env[key] = v
        log = open(user / "log.txt", "w", encoding="utf-8", errors="replace")
        logs.append(log)
        game = exe2 if (k.get("xplat") and role != "host" and exe2 is not None) else exe
        procs.append(subprocess.Popen([str(game), str(iso)], env=env, stdout=log, stderr=subprocess.STDOUT,
                                      cwd=str(game.parent)))
        time.sleep(0.3)
    deadline = time.time() + k["frames"] / 60 * slowness + 60
    while any(p.poll() is None for p in procs) and time.time() < deadline:
        time.sleep(1)
    timed_out = any(p.poll() is None for p in procs)
    for p in procs:
        if p.poll() is None:
            p.kill()
            p.wait()
    for log in logs:
        log.close()
    if relay is not None:
        relay.kill()
        relay.wait()
    return check(kind, folder, timed_out)


def coop_problems(kind: str, role: str, lines: list[str]) -> list[str]:
    """Playing together: what each game's log must show."""
    p = []
    has = lambda text: any(text in l for l in lines)
    if role == "host":
        if not has("co-op: player 2 plays seat 2"):
            p.append("host: the guest never took seat 2")
        if kind == "coopmario":
            if not has("co-op: the seats changed: 1:2 2:1"):
                p.append("host: the guest never took Mario's seat")
            spots = {l.split("co-op: Mario at ")[1].split(")")[0] for l in lines if "co-op: Mario at " in l}
            if len(spots) < 3:
                p.append("host: Mario never moved by the guest's controller (%d places)" % len(spots))
            if not has("co-op: seat 2 is this game's own controller"):
                p.append("host: its own controller never played the partner")
        if kind == "coop":
            spots = {l.split("co-op: the partner at ")[1].split(")")[0] for l in lines if "co-op: the partner at " in l}
            if len(spots) < 3:
                p.append("host: the partner never moved by the guest's controller (%d places)" % len(spots))
            if not any("co-op: seat 2 is player 2's controller (stick 127" in l or
                       "co-op: seat 2 is player 2's controller (stick -127" in l for l in lines):
                p.append("host: the guest's stick never reached seat 2")
        if k_battle(kind) and not has("player 2's choice for unit"):
            p.append("host: the guest never chose the partner's move")
        if kind == "xplat" and not has("layout: the group at code"):
            p.append("host: no layout line")
    else:
        if not has("co-op: the host's whole progress taken"):
            p.append("guest: the host's progress never taken")
        if kind == "coopmario" and not has("seats now 1:2 2:1"):
            p.append("guest: never learned it plays Mario")
        if kind == "coop":
            if not has("co-op: the host went to hei_02: along") and not has("warped to hei_02"):
                p.append("guest: never followed the host to hei_02")
            if not any("ghost: player 1 at" in l and "partner" in l and "hei_02" not in l for l in lines):
                p.append("guest: never drew the host's partner")
        if k_battle(kind) and not has("choice for unit"):
            p.append("guest: never sent the partner's choice")
        if kind == "xplat" and not has("layout: table place"):
            p.append("guest: no layout line")
    return p


def k_battle(kind: str) -> bool:
    return bool(KINDS[kind].get("battle"))


def check(kind: str, folder: Path, timed_out: bool) -> dict:
    k = KINDS[kind]
    games = [g[0] for g in k["games"]]
    problems, hashes, drift = [], {}, 0
    for role in games:
        lines = [l for l in (folder / role / "log.txt").read_text(encoding="utf-8", errors="replace").splitlines()
                 if l.startswith("[sync]")]
        d = sum(1 for l in lines if "DRIFT" in l)
        drift += d
        if d:
            problems.append("%s: %d DRIFT lines" % (role, d))
        if not any("net: session with player" in l for l in lines):
            problems.append("%s: never linked" % role)
        found = [m.group(1) for l in lines for m in [PROGRESS.search(l)] if m]
        hashes[role] = found[-1] if found else None
        named = {m.group(1) for l in lines for m in [NAME.search(l)] if m}
        if len(named) < len(games) - 1:
            problems.append("%s: learned %d other players' names of %d" % (role, len(named), len(games) - 1))
        if k.get("battle") and role == "host" and not any("the shared battle ends (back in the field)" in l
                                                          for l in lines):
            problems.append("host: the shared battle never ended back in the field")
        if "*** crash" in (folder / role / "log.txt").read_text(encoding="utf-8", errors="replace"):
            problems.append("%s: crashed" % role)
        wire = sum(1 for l in lines if "WIRE:" in l)
        if wire:
            problems.append("%s: %d messages carried an address of its program (WIRE)" % (role, wire))
        if k.get("coop"):
            problems += coop_problems(kind, role, lines)
    if len(set(hashes.values())) != 1 or None in hashes.values() or \
            next(iter(hashes.values())) == "0000000000000000/0000000000000000":
        problems.append("progress differs: " + ", ".join("%s %s" % kv for kv in hashes.items()))
    if timed_out:
        problems.append("ran too long: killed")
    return {"kind": kind, "ok": not problems, "drift": drift, "progress": hashes, "problems": problems}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="the headless build's ttyd-*.exe")
    ap.add_argument("--iso", required=True)
    ap.add_argument("--out", default="sync_soak")
    ap.add_argument("--kinds", default=",".join(k for k in KINDS if not KINDS[k].get("xplat")))
    ap.add_argument("--rounds", type=int, default=0, help="rounds to play (0: one of each kind, or --hours)")
    ap.add_argument("--hours", type=float, default=0, help="wall-clock time to run")
    ap.add_argument("--faults", default="mix", choices=["mix", "on", "off"])
    ap.add_argument("--seed", type=int, default=int(time.time()) & 0xFFFFFF)
    ap.add_argument("--port-base", type=int, default=47750)
    ap.add_argument("--slowness", type=float, default=3.0, help="wall-clock allowance per game second")
    ap.add_argument("--slot", help="a script that runs its arguments in a shared game slot")
    ap.add_argument("--keep-all", action="store_true")
    ap.add_argument("--exe2", help="xplat rounds: the guest's program, laid out differently (configured with CFLAGS=-DTTYD_LAYOUT_PAD)")
    a = ap.parse_args()
    if a.slot and os.environ.get("SYNC_SOAK_IN_SLOT") != "1":
        env = dict(os.environ, SYNC_SOAK_IN_SLOT="1")
        bash = shutil.which("bash") or "bash"
        return subprocess.run([bash, a.slot, sys.executable] + sys.argv, env=env).returncode
    kinds = [k.strip() for k in a.kinds.split(",") if k.strip()]
    if not kinds:
        print("no kinds to play (--kinds is empty)")
        return 2
    for k in kinds:
        if k not in KINDS:
            print("no such kind: %s (%s)" % (k, ", ".join(KINDS)))
            return 2
    exe, iso = Path(a.exe).resolve(), Path(a.iso).resolve()
    if not exe.is_file() or not iso.is_file():
        print("the exe or the disc image isn't there")
        return 2
    out = Path(a.out).resolve()  # (the games run in the program's folder)
    out.mkdir(parents=True, exist_ok=True)
    # a copy of the program (a build may link the original meanwhile), beside it for its data
    run = exe.with_name("%s-soak-%d%s" % (exe.stem, a.port_base, exe.suffix))
    shutil.copy2(exe, run)
    run2 = None
    if a.exe2:
        exe2 = Path(a.exe2).resolve()
        run2 = exe2.with_name("%s-soak-%d%s" % (exe2.stem, a.port_base, exe2.suffix))
        shutil.copy2(exe2, run2)
    elif any(KINDS[k].get("xplat") for k in kinds):
        print("xplat rounds need --exe2 (a build configured with CFLAGS=-DTTYD_LAYOUT_PAD)")
        return 2
    rng = random.Random(a.seed)
    rounds = a.rounds or (0 if a.hours else len(kinds))
    end = time.time() + a.hours * 3600 if a.hours else None
    results = []
    print("sync soak: seed %d, kinds %s, faults %s" % (a.seed, ",".join(kinds), a.faults), flush=True)
    try:
        n = 0
        while (end is None and n < rounds) or (end is not None and time.time() < end and (not a.rounds or n < rounds)):
            kind = kinds[n % len(kinds)]
            faulty = a.faults == "on" or (a.faults == "mix" and n % 2 == 1)
            faults = None
            if faulty:
                faults = FAULTS_WALK if kind == "walk" else FAULTS
            folder = out / ("round-%03d-%s" % (n, kind))
            if folder.exists():
                shutil.rmtree(folder)
            seed = rng.randrange(1 << 30)
            r = play(kind, folder, run, iso, a.port_base + n % 20, faults, seed, a.slowness, run2)
            r.update({"round": n, "faults": faults or "", "seed": seed})
            results.append(r)
            with open(out / "results.jsonl", "a", encoding="utf-8") as f:
                f.write(json.dumps(r) + "\n")
            print("round %d %-8s %s%s" % (n, kind, "ok" if r["ok"] else "FAILED: " + "; ".join(r["problems"]),
                                           " (faults)" if faults else ""), flush=True)
            if r["ok"] and not a.keep_all:
                shutil.rmtree(folder, ignore_errors=True)
            n += 1
    finally:
        for r_ in (run, run2):
            try:
                if r_ is not None:
                    r_.unlink()
            except OSError:
                pass
    failed = [r for r in results if not r["ok"]]
    print("sync soak: %d rounds, %d failed, %d DRIFT lines" % (len(results), len(failed),
                                                                 sum(r["drift"] for r in results)))
    if not results:  # (no round played, e.g. --hours too short: nothing passed)
        print("sync soak: no round played")
        return 2
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
