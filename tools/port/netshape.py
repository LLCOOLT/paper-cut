#!/usr/bin/env python3
"""Network conditions for the online tests: one connection's frames held and let go as a real link would
(sync_test_relay.py's --net, relay_bridge.py's --net). TCP-like only: frames never get lost, duplicated or
out of order (the relay's WebSocket never does that either; a message's own drops, duplicates and reorders
are the game's $TTYD_NET_FAULTS, port/src/common/online_faults_pc.c, which knows which messages may go).

A spec is rules separated by commas (times in ms unless said):
  lat:A-B        one-way latency, A to B (lat:80 for a fixed one)
  jitter:J       up to J more per frame, at random (order kept: a frame never passes the one before it)
  loss:P         P percent of frames "lost" once: held for a retransmission (rto, default 3 x latency, at
                 least 200), and everything behind it with it (head-of-line blocking, as TCP)
  burst:E/L/P    every E ms a burst L ms long in which the loss is P percent (a Wi-Fi hiccup)
  rto:R          the retransmission's wait
  stall:S+D      S seconds after the connection's first frame, nothing for D seconds, then all of it at once
                 (several: stall:5+8/20+12)
  bw:K           K kilobytes a second at most (serialisation delay)
  seed:N         the random numbers' seed (with the connection's name: a combination reruns alike)

Shaper(spec, seed, name).delay(now, length) -> the frame's release time; the caller sends frames in order at
their release times. Counts: Shaper.stats().
"""
import random
import zlib


def parse(spec):
    """{rule: value} from a spec string ('' or None: no rules); raises ValueError on an unknown rule"""
    rules = {}
    for tok in (spec or "").split(","):
        tok = tok.strip()
        if not tok:
            continue
        if ":" not in tok:
            raise ValueError(f"net rule {tok!r}: rule:value")
        k, v = tok.split(":", 1)
        if k == "lat":
            lo, _, hi = v.partition("-")
            rules[k] = (float(lo), float(hi or lo))
        elif k in ("jitter", "loss", "rto", "bw"):
            rules[k] = float(v)
        elif k == "burst":
            e, l, p = v.split("/")
            rules[k] = (float(e), float(l), float(p))
        elif k == "stall":
            rules[k] = []
            for one in v.split("/"):
                s, d = one.split("+")
                rules[k].append((float(s) * 1000.0, float(d) * 1000.0))
        elif k == "seed":
            rules[k] = int(v)
        else:
            raise ValueError(f"net rule {k!r}: one of lat, jitter, loss, burst, rto, stall, bw, seed")
    return rules


class Shaper:
    def __init__(self, spec, seed=1, name=""):
        self.rules = parse(spec) if isinstance(spec, str) or spec is None else dict(spec)
        s = self.rules.get("seed", seed)
        self.rng = random.Random((int(s) * 1000003) ^ zlib.crc32(name.encode()))
        self.start = None
        self.last = 0.0
        self.frames = self.lost = self.stalled = 0
        self.held_ms = 0.0

    def active(self):
        return bool(self.rules)

    def delay(self, now_ms, length=0):
        """the release time (ms, the same clock as now_ms) of a frame put in now"""
        r = self.rules
        if self.start is None:
            self.start = now_ms
        self.frames += 1
        t = now_ms
        lo, hi = r.get("lat", (0.0, 0.0))
        t += lo if hi <= lo else self.rng.uniform(lo, hi)
        if r.get("jitter"):
            t += self.rng.uniform(0, r["jitter"])
        loss = r.get("loss", 0.0)
        if "burst" in r:
            every, length_ms, p = r["burst"]
            if every > 0 and (now_ms - self.start) % every < length_ms:
                loss = max(loss, p)
        if loss > 0 and self.rng.random() * 100.0 < loss:
            self.lost += 1
            t += r.get("rto", max(200.0, 3.0 * hi))
        if r.get("bw"):
            t += length / r["bw"]  # (K kilobytes a second: length / K ms)
        for s, d in r.get("stall", []):
            a, b = self.start + s, self.start + s + d
            if a <= t < b:
                self.stalled += 1
                t = b
        t = max(t, self.last)  # (in order: TCP never lets a frame pass another)
        self.held_ms += t - now_ms
        self.last = t
        return t

    def stats(self):
        n = max(self.frames, 1)
        return (f"frames {self.frames}, lost-and-resent {self.lost}, stalled {self.stalled}, "
                f"mean hold {self.held_ms / n:.0f} ms")


if __name__ == "__main__":
    import sys
    sh = Shaper(sys.argv[1] if len(sys.argv) > 1 else "lat:50-150,jitter:20,loss:5,stall:1+2", 7, "demo")
    out = [round(sh.delay(i * 16.7, 200) - i * 16.7) for i in range(300)]
    print("holds (ms):", out[:40], "...")
    print(sh.stats())
