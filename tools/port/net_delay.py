#!/usr/bin/env python3
"""Online co-op's input delay, from the games' delay logs ($TTYD_NET_DELAY_LOG=1; the online_netdelay_* scenarios).

  python tools/port/net_delay.py DIR [DIR ...]

Reads every log under each DIR (a test pass run's scenario folder holds both games'), matches each guest press to
the host's lines for the same controller frame, and prints the median (and range) of each step, in milliseconds:

  read->out     the guest's controller read until it went onto the wire (send cadence, a room's batching, the
                faults' delay when a test adds one)
  out->arrived  the network (and the relay) to the host
  arrived->applied  the host's queue: played as the seat's controller
  applied->state out  the host's game moving the character, and its word going out
  state out->in  the network back
  in->seen      the guest drawing it (the ghost's interpolation or buffer)
  total         press to seen, and in frames

The games run on one computer, so every line's time is the same clock (QueryPerformanceCounter / CLOCK_MONOTONIC).
With --mode local (phase 2: the guest moves its own character) the guest logs "seen" from its own simulation."""
import glob
import os
import re
import statistics
import sys

GUEST = re.compile(r"netdelay: guest (?:press )?pad (\d+) (read|out|state in|seen) (\d+)(?:: (\d+) ms, (\d+) frames)?")
HOST = re.compile(r"netdelay: host pad (\d+) from player (\d+) (arrived|applied|state out) (\d+)(?: as frame \d+ \(queued (\d+)\))?")


def read(dirs):
    g, h, queued = {}, {}, []
    for d in dirs:
        for path in glob.glob(os.path.join(d, "**", "*"), recursive=True):
            if not os.path.isfile(path) or os.path.getsize(path) > 200 << 20:
                continue
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if "netdelay:" not in text:
                continue
            key = os.path.dirname(path)
            for m in GUEST.finditer(text):
                e = g.setdefault((key, int(m.group(1))), {})
                e[m.group(2)] = int(m.group(3))
                if m.group(2) == "seen":
                    e["frames"] = int(m.group(5))
            for m in HOST.finditer(text):
                e = h.setdefault((int(m.group(1)),), {})
                e[m.group(3)] = int(m.group(4))
                if m.group(5) is not None:
                    queued.append(int(m.group(5)))
    return g, h, queued


def stat(xs):
    if not xs:
        return "-"
    return f"{statistics.median(xs):7.1f}  ({min(xs)}..{max(xs)}, n={len(xs)})"


def main():
    dirs = [a for a in sys.argv[1:] if not a.startswith("-")]
    if not dirs:
        sys.exit(__doc__)
    g, h, queued = read(dirs)
    steps = {k: [] for k in ("read->out", "out->arrived", "arrived->applied", "applied->state out",
                             "state out->in", "in->seen", "total", "frames")}
    for (_, f), e in sorted(g.items()):
        if "read" not in e or "seen" not in e:
            continue
        he = h.get((f,), {})
        steps["total"].append(e["seen"] - e["read"])
        steps["frames"].append(e.get("frames", 0))
        if "out" in e:
            steps["read->out"].append(e["out"] - e["read"])
        if "out" in e and "arrived" in he:
            steps["out->arrived"].append(he["arrived"] - e["out"])
        if "arrived" in he and "applied" in he:
            steps["arrived->applied"].append(he["applied"] - he["arrived"])
        if "applied" in he and "state out" in he:
            steps["applied->state out"].append(he["state out"] - he["applied"])
        if "state out" in he and "state in" in e:
            steps["state out->in"].append(e["state in"] - he["state out"])
        if "state in" in e:
            steps["in->seen"].append(e["seen"] - e["state in"])
    for k, xs in steps.items():
        print(f"{k:20} {stat(xs)}")
    if queued:
        print(f"{'host queue depth':20} {stat(queued)}")
    return 0 if steps["total"] else 1


if __name__ == "__main__":
    sys.exit(main())
