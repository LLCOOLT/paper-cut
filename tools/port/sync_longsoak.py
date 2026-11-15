#!/usr/bin/env python3
"""Online play's long soak (docs/TESTING.md, "Online soak"): one long session of two to four headless games
playing together through a room on the local stand-in relay (sync_test_relay.py), each walking about,
changing rooms (hei_03 and hei_02, through their doors), opening and closing the Start menu, the host
meeting the rooms' enemies (field battles), and the guests leaving the room and joining it again on a schedule, all under mild
network faults ($TTYD_NET_FAULTS: drops, duplicates, reordering, delays; the relay's quiet drops too with
--relay-faults). sync_soak.py plays many short rounds; this plays one session for hours of game time.

  sync_longsoak.py --exe build-pc-us-headless/ttyd-us.exe --iso orig/G8ME01/game.iso --out local-docs/soak \\
      --players 4 --game-hours 2

While it runs it samples each game every --every seconds: its memory (the working set and private bytes),
its log's size, and from its "[sync] status:" lines (every 600 frames) its frame, whether it's linked,
its sent and got counts, resends, gaps, reconnects and unacknowledged messages. At the end it says, per game:
memory at the start (after 20 status lines of warm-up) and the end, and its growth per
game-hour; messages sent per game-minute, first quarter against the last (a rate that grows is a leak of
work); the share of status lines linked; unacked at its highest; errors in the log (crashes, sanitizer
reports, DRIFT, WIRE, bad messages, refusals). Results: <out>/samples.jsonl, <out>/summary.json and the
games' logs. Fails (exit 1) on a crash, a hang (a game past its time), memory growing more than
--max-mb-per-hour, the message rate growing past 1.5x, a game linked less than 80% of the time (the
guests' own planned absences aside), or unacked messages that keep growing.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CODE = "2TST-RXYM"
STATUS = re.compile(r"status: (\w+) seq (-?\d+) map (\S+) .*?peer (linked|not linked) .*?\| me (\d) with (\S+) \| "
                     r"sent (\d+) got (\d+) resent (\d+) dupes (\d+) gaps (\d+) reconnects (\d+) unacked (\d+)")
ERRORS = {"crash": "*** crash", "asan": "AddressSanitizer", "drift": "DRIFT", "wire": "WIRE:",
          "bad": "bad message", "refused": "refused:", "too big": "message too big", "fuzz": "fuzz:"}
FAULTS = "drop:1,dup:1,reorder:1,delay:0-25"
# the games' own test exits (port/src/aurora/story_test_aurora.c, port/src/common/input_pc.c)
EXIT_CODES = {6: " (a test step failed: a battle the auto battler couldn't end, \"[story] STUCK\" in its log, or a "
                 "scripted step that couldn't run)"}


class Mem(ctypes.Structure):
    _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


def memory_mb(pid: int) -> tuple[float, float]:
    """(working set, private bytes) in MB; (0, 0) when it can't be read"""
    if os.name != "nt":
        try:
            with open("/proc/%d/status" % pid) as f:
                rss = [l for l in f if l.startswith("VmRSS")]
            return (int(rss[0].split()[1]) / 1024.0, 0.0) if rss else (0.0, 0.0)
        except OSError:
            return (0.0, 0.0)
    k32 = ctypes.windll.kernel32
    h = k32.OpenProcess(0x1000 | 0x0010, False, pid)  # (query limited information, VM read)
    if not h:
        return (0.0, 0.0)
    try:
        m = Mem()
        m.cb = ctypes.sizeof(Mem)
        if not ctypes.windll.psapi.GetProcessMemoryInfo(h, ctypes.byref(m), m.cb):
            return (0.0, 0.0)
        return (m.WorkingSetSize / 1048576.0, m.PrivateUsage / 1048576.0)
    finally:
        k32.CloseHandle(h)


def script(role: str, frames: int, rng: random.Random, rejoin_every: int) -> str:
    """a role's input: walking, rooms, menus, (the host) battles, (the guests) leaving and coming back"""
    out, t, room, next_leave = [], 0, "hei_03", rejoin_every + rng.randrange(rejoin_every // 2 + 1)
    out.append("wait 600")
    t += 600
    while t < frames - 1200 and len(out) < 2600:  # (a press is two steps: under the 4095 a script holds)
        r = rng.random()
        if role != "host" and rejoin_every and t >= next_leave:
            away = rng.choice([300, 900, 1800])
            out += ["online leave", "wait %d" % away, "online join %s" % CODE, "wait 600"]
            t += away + 600
            next_leave = t + rejoin_every + rng.randrange(rejoin_every // 2 + 1)
        elif r < 0.45:
            n = rng.choice([30, 60, 120, 240])
            out.append("stick %d %d %d" % (rng.choice([-127, -90, 0, 90, 127]), rng.choice([-127, -90, 0, 90, 127]), n))
            t += n
        elif r < 0.58:
            out += ["press start", "wait 40", "press right", "wait 30", "press b", "wait 30", "press b", "wait 30"]
            t += 130
        elif r < 0.72:
            # through a door: hei_03's west to hei_02, hei_02's east back
            out += ["goto %s" % ("w_bero" if room == "hei_03" else "e_bero"),
                    "stick %d 0 90" % (-127 if room == "hei_03" else 127), "wait 300"]
            room = "hei_02" if room == "hei_03" else "hei_03"
            t += 390
        elif r < 0.78:
            # (a while in the room: its enemies come for Mario, a field battle; a script's battle or fight
            # ends the game when the room has no enemy left, so none here)
            n = rng.choice([300, 600, 900])
            out += ["stick %d 0 %d" % (rng.choice([-127, 127]), n // 3), "wait %d" % (n - n // 3)]
            t += n
        elif r < 0.82:
            out += ["press a", "wait 60", "press b", "wait 30"]
            t += 90
        else:
            n = rng.choice([60, 200, 600])
            out.append("wait %d" % n)
            t += n
    if t < frames - 1200:
        # (the steps a script holds used up short of the run (2 game-hours: about 55% of it): from the top
        # again, $TTYD_EXIT_FRAMES then ends it. A last long wait stood every game still for the rest, and the
        # last quarter's message rate measured idle games: seed 101 "grew" from 77 to 188 a status)
        return "\n".join(out) + "\nloop\n"
    out.append("wait %d" % max(60, frames - t))
    return "\n".join(out) + "\nend\n"


def last_status(path: Path, at: int) -> tuple[list[dict], int]:
    """the status lines after byte `at` of a log, parsed; where it ends"""
    try:
        with open(path, "rb") as f:
            f.seek(at)
            data = f.read()
    except OSError:
        return [], at
    keep = data.rfind(b"\n") + 1
    out = []
    for line in data[:keep].decode("utf-8", errors="replace").splitlines():
        m = STATUS.search(line)
        if m:
            out.append({"role": m.group(1), "seq": int(m.group(2)), "map": m.group(3),
                        "linked": m.group(4) == "linked", "with": m.group(6), "sent": int(m.group(7)),
                        "got": int(m.group(8)), "resent": int(m.group(9)), "gaps": int(m.group(11)),
                        "reconnects": int(m.group(12)), "unacked": int(m.group(13))})
    return out, at + keep


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--out", default="sync_longsoak")
    ap.add_argument("--players", type=int, default=2, choices=[2, 3, 4])
    ap.add_argument("--game-hours", type=float, default=1.0)
    ap.add_argument("--rejoin-minutes", type=float, default=12, help="a guest leaves and comes back about this often "
                                                                       "(game minutes; 0 never)")
    ap.add_argument("--faults", default=FAULTS, help="$TTYD_NET_FAULTS for every game ('' none)")
    ap.add_argument("--relay-faults", action="store_true", help="the relay's quiet drops too, now and then")
    ap.add_argument("--every", type=float, default=30.0, help="seconds between samples")
    ap.add_argument("--seed", type=int, default=int(time.time()) & 0xFFFFFF)
    ap.add_argument("--port", type=int, default=48300)
    ap.add_argument("--max-hours", type=float, default=0, help="wall-clock limit (default: 3x the game time + 20 min)")
    ap.add_argument("--max-mb-per-hour", type=float, default=24.0)
    a = ap.parse_args()
    exe, iso = Path(a.exe).resolve(), Path(a.iso).resolve()
    if not exe.is_file() or not iso.is_file():
        print("the exe or the disc image isn't there")
        return 2
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    frames = int(a.game_hours * 3600 * 60)
    rng = random.Random(a.seed)
    run = exe.with_name("%s-longsoak-%d%s" % (exe.stem, a.port, exe.suffix))
    run.write_bytes(exe.read_bytes())
    roles = ["host", "guest", "guest2", "guest3"][:a.players]
    wall = a.max_hours * 3600 if a.max_hours else a.game_hours * 3600 * 3 + 1200
    relay_args = [sys.executable, str(HERE / "sync_test_relay.py"), str(a.port), "--log", str(out / "relay.txt"),
                  "--seconds", str(int(wall) + 120)]
    if a.relay_faults:
        relay_args += ["--quiet"] + ["%s:%d:3" % (r if r in ("host", "guest") else "peer%d" % (roles.index(r) - 2),
                                                  600 + 900 * i) for i, r in enumerate(roles)]
    relay = subprocess.Popen(relay_args)
    time.sleep(1.0)
    procs, logs = {}, {}
    rejoin = int(a.rejoin_minutes * 3600) if a.rejoin_minutes else 0
    for i, role in enumerate(roles):
        user = out / role
        user.mkdir(parents=True, exist_ok=True)
        (user / "settings.txt").write_text(
            "skip_intro=1\nfullscreen=0\ndiscord_status=0\nsettings_hint_shown=1\nupdate_check=0\n", encoding="utf-8")
        (user / "input.txt").write_text(script(role, frames, random.Random(a.seed + i), rejoin), encoding="utf-8")
        env = {k: v for k, v in os.environ.items() if not k.startswith("TTYD_")}
        env.update({"TTYD_FAST": "1", "TTYD_USER_DIR": str(user), "TTYD_MAP": "hei_03", "TTYD_STORY": "11",
                    "TTYD_EXIT_FRAMES": str(frames), "TTYD_WARP_LIMIT": "100000", "TTYD_NO_LAUNCHER": "1",
                    "TTYD_INPUT": str(user / "input.txt"), "TTYD_ONLINE_MODE": "sync", "TTYD_SYNC_LOG": "600",
                    "TTYD_SYNC_NAME": "Soak %s" % role.capitalize(), "TTYD_AUTO_BATTLE": "1", "TTYD_GOD": "1",
                    "TTYD_AUTO_TEXT": "1", "TTYD_SYNC_TEST_RELAY": "127.0.0.1:%d" % a.port})
        env["TTYD_SYNC_HOST_ROOM" if role == "host" else "TTYD_SYNC_JOIN_ROOM"] = CODE
        if role == "host":
            env["TTYD_SYNC_PICK"] = "-1"
        if a.faults:
            env["TTYD_NET_FAULTS"] = a.faults
            env["TTYD_NET_FAULTS_SEED"] = str(a.seed + i)
        logs[role] = open(user / "log.txt", "w", encoding="utf-8", errors="replace")
        procs[role] = subprocess.Popen([str(run), str(iso)], env=env, stdout=logs[role], stderr=subprocess.STDOUT,
                                       cwd=str(run.parent))
        time.sleep(0.5)
    start = time.time()
    at = {r: 0 for r in roles}
    status = {r: [] for r in roles}
    samples = open(out / "samples.jsonl", "w", encoding="utf-8")
    first_mem, last_mem = {}, {}
    print("long soak: %d players, %.2f game-hours (%d frames), seed %d, faults %s" % (
        a.players, a.game_hours, frames, a.seed, a.faults or "none"), flush=True)
    try:
        while (procs["host"].poll() is None and any(p.poll() is None for p in procs.values())
               and time.time() - start < wall):
            time.sleep(a.every)
            now = time.time() - start
            for role, p in procs.items():
                new, at[role] = last_status(out / role / "log.txt", at[role])
                status[role] += new
                ws, priv = memory_mb(p.pid) if p.poll() is None else (0.0, 0.0)
                s = {"t": round(now), "role": role, "alive": p.poll() is None, "ws_mb": round(ws, 1),
                     "private_mb": round(priv, 1), "log_kb": (out / role / "log.txt").stat().st_size // 1024,
                     "statuses": len(status[role])}
                if status[role]:
                    s.update({k: status[role][-1][k] for k in ("sent", "got", "resent", "gaps", "reconnects",
                                                               "unacked", "linked", "map")})
                samples.write(json.dumps(s) + "\n")
                if p.poll() is None and priv > 0:
                    if len(status[role]) >= 20 and role not in first_mem:  # (warmed up: 20 status lines in)
                        first_mem[role] = (len(status[role]), priv)
                    last_mem[role] = (len(status[role]), priv)
            samples.flush()
            print("%6.0f s: " % now + "  ".join(
                "%s %s %s" % (r, "%d st" % len(status[r]), "%.0f MB" % last_mem.get(r, (0, 0))[1]) for r in roles),
                flush=True)
    finally:
        timed_out = [r for r, p in procs.items() if p.poll() is None] if time.time() - start >= wall else []
        # (the games the soak stops itself, the host's game over: their kill's exit code 1 is no failure of theirs)
        stopped = [r for r, p in procs.items() if p.poll() is None and r not in timed_out]
        for p in procs.values():
            if p.poll() is None:
                p.kill()
                p.wait()
        for f in logs.values():
            f.close()
        relay.kill()
        relay.wait()
        samples.close()
        try:
            run.unlink()
        except OSError:
            pass
    summary, problems = {"seed": a.seed, "players": a.players, "frames": frames, "wall_s": round(time.time() - start),
                         "games": {}}, []
    for role in roles:
        new, _ = last_status(out / role / "log.txt", at[role])
        st = status[role] + new
        text = (out / role / "log.txt").read_text(encoding="utf-8", errors="replace")
        errs = {k: text.count(v) for k, v in ERRORS.items() if v in text}
        g = {"statuses": len(st), "errors": errs, "exit": procs[role].returncode}
        if st:
            q = max(1, len(st) // 4)
            # messages sent per status line (600 frames) in the first and the last quarter
            # (counts reset with a new session, a guest's rejoin: the status after it counts from 0. Measured only
            # from the quarter's last reset, a guest's quarter was the few statuses after its last rejoin, a
            # burst or a lull: seed 202's guest "grew" from 38 to 92 with 87 and 85 a status in all)
            def rate(part):
                steps = [b["sent"] - a["sent"] if b["sent"] >= a["sent"] else b["sent"] for a, b in zip(part, part[1:])]
                return sum(steps) / len(steps) if steps else 0.0
            g["rate_first"] = round(rate(st[:q]), 1)
            g["rate_last"] = round(rate(st[-q:]), 1)
            g["linked_share"] = round(sum(1 for s in st if s["linked"]) / len(st), 3)
            g["unacked_max"] = max(s["unacked"] for s in st)
            g["unacked_last_quarter_min"] = min(s["unacked"] for s in st[-q:])
            g["reconnects"] = st[-1]["reconnects"]
        if role in first_mem and role in last_mem and last_mem[role][0] > first_mem[role][0]:
            hours = (last_mem[role][0] - first_mem[role][0]) * 600 / 216000.0
            g["mem_first_mb"] = round(first_mem[role][1], 1)
            g["mem_last_mb"] = round(last_mem[role][1], 1)
            g["mb_per_game_hour"] = round((last_mem[role][1] - first_mem[role][1]) / max(hours, 1e-6), 2)
            if hours > 0.25 and g["mb_per_game_hour"] > a.max_mb_per_hour:
                problems.append("%s: memory grows %.1f MB per game-hour" % (role, g["mb_per_game_hour"]))
        if "crash" in errs or "asan" in errs:
            problems.append("%s: crashed" % role)
        elif procs[role].returncode not in (0, None) and role not in timed_out and role not in stopped:
            problems.append("%s: ended with exit code %s%s" % (role, procs[role].returncode,
                                                                EXIT_CODES.get(procs[role].returncode, "")))
        if role in stopped:
            g["stopped"] = "by the soak, the host's game over"
        if role in timed_out:
            problems.append("%s: still running at the wall-clock limit (a hang?)" % role)
        if g.get("rate_first") and g.get("rate_last", 0) > 1.5 * g["rate_first"] + 20:
            problems.append("%s: messages a status grew from %.0f to %.0f" % (role, g["rate_first"], g["rate_last"]))
        # (a guest away by plan is not linked a while: 80% is the bar)
        if g.get("linked_share", 0) < 0.8:
            problems.append("%s: linked only %.0f%% of the time" % (role, 100 * g.get("linked_share", 0)))
        if g.get("unacked_last_quarter_min", 0) > 64:
            problems.append("%s: never fewer than %d unacknowledged in the last quarter" % (
                role, g["unacked_last_quarter_min"]))
        if errs.get("drift"):
            problems.append("%s: %d DRIFT lines" % (role, errs["drift"]))
        summary["games"][role] = g
    summary["problems"] = problems
    (out / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(json.dumps(summary, indent=1))
    print("long soak: %s" % ("ok" if not problems else "FAILED: " + "; ".join(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
