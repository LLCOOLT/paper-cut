#!/usr/bin/env python3
"""Measures the voice clips in a headless audio capture (TTYD_AUDIO_WAV): each sound's start, length,
peak and loudness, and its pitch (the median of a frame-by-frame autocorrelation), for the play-as
voices (docs/PLAY_AS.md, Voices). Tests can't listen; this gives the numbers to check against.

  voice_wav.py clips  AUDIO.wav NAMES.txt [--grid FRAMES]   one sound every FRAMES game frames (60 a second),
                                                             named in order by NAMES.txt: a table per sound
  voice_wav.py events AUDIO.wav                              every sound found (silence-separated): a table
  voice_wav.py compare A.wav B.wav                           the sounds of two captures side by side: length
                                                             ratio, pitch ratio in semitones, level difference

Only the standard library: reads 16-bit PCM WAV (mono or stereo, any rate).
"""
import argparse
import array
import math
import sys
import wave


def load(path):
    w = wave.open(path, "rb")
    ch, rate, width, n = w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()
    if width != 2:
        sys.exit(f"{path}: {8 * width}-bit samples, only 16-bit read")
    a = array.array("h")
    a.frombytes(w.readframes(n))
    if sys.byteorder == "big":
        a.byteswap()
    if ch > 1:  # the channels' mean
        mono = [sum(a[i:i + ch]) / ch for i in range(0, len(a) - ch + 1, ch)]
    else:
        mono = [float(x) for x in a]
    return mono, rate


def db(x):
    return 20 * math.log10(x / 32768.0) if x > 0 else -120.0


def envelope(x, rate, ms=10):
    """RMS per window of ms milliseconds."""
    n = max(1, int(rate * ms / 1000))
    out = []
    for i in range(0, len(x) - n + 1, n):
        s = 0.0
        for v in x[i:i + n]:
            s += v * v
        out.append(math.sqrt(s / n))
    return out, n


def pitch(x, rate, lo=70.0, hi=900.0):
    """Median F0 (Hz) over 40 ms frames whose autocorrelation peak is clear; 0 if none."""
    d = max(1, int(rate // 8000))  # (at about 8 kHz: a box average, then every d-th sample)
    if d > 1:
        x = [sum(x[i:i + d]) / d for i in range(0, len(x) - d + 1, d)]
        rate = rate / d
    n = int(rate * 0.04)
    hop = n // 2
    lags = range(int(rate / hi), int(rate / lo) + 1)
    f0s = []
    for i in range(0, len(x) - n, hop):
        f = x[i:i + n]
        e = sum(v * v for v in f)
        if e <= 0 or math.sqrt(e / n) < 300:
            continue
        best, bl = 0.0, 0
        for L in lags:
            c = 0.0
            for k in range(0, n - L):
                c += f[k] * f[k + L]
            c = c / e
            if c > best:
                best, bl = c, L
        if best > 0.45 and bl:
            f0s.append(rate / bl)
    if not f0s:
        return 0.0
    f0s.sort()
    return f0s[len(f0s) // 2]


def measure(x, rate, start, end, floor_db=-50.0):
    """The sound inside [start, end): its own start and length above floor_db, peak and RMS (dBFS), F0."""
    seg = x[start:end]
    env, w = envelope(seg, rate)
    on = [i for i, v in enumerate(env) if db(v) > floor_db]
    if not on:
        return None
    a, b = on[0] * w, (on[-1] + 1) * w
    s = seg[a:b]
    peak = max(abs(v) for v in s)
    rms = math.sqrt(sum(v * v for v in s) / len(s))
    return {"start": (start + a) / rate, "len": (b - a) / rate, "peak": db(peak), "rms": db(rms),
            "f0": pitch(s, rate)}


def events(x, rate, floor_db=-50.0, gap_ms=250):
    env, w = envelope(x, rate)
    gap = int(gap_ms / 10)
    out, cur, quiet = [], None, 0
    for i, v in enumerate(env):
        if db(v) > floor_db:
            if cur is None:
                cur = i
            quiet = 0
        elif cur is not None:
            quiet += 1
            if quiet > gap:
                out.append((cur * w, (i - quiet + 1) * w))
                cur, quiet = None, 0
    if cur is not None:
        out.append((cur * w, len(env) * w))
    return [measure(x, rate, a, b, floor_db) for a, b in out]


def row(name, m):
    if m is None:
        return f"{name:34s}  (silent)"
    return (f"{name:34s} {m['start']:8.2f}s {m['len']:6.2f}s  peak {m['peak']:6.1f}  rms {m['rms']:6.1f} dBFS"
            f"  f0 {m['f0']:6.1f} Hz")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("clips")
    c.add_argument("wav")
    c.add_argument("names")
    c.add_argument("--grid", type=int, default=200, help="game frames between the sounds")
    c.add_argument("--fps", type=float, default=60.0)
    e = sub.add_parser("events")
    e.add_argument("wav")
    p = sub.add_parser("compare")
    p.add_argument("a")
    p.add_argument("b")
    a = ap.parse_args()
    if a.cmd == "clips":
        x, rate = load(a.wav)
        names = [n.strip() for n in open(a.names) if n.strip()]
        ev = [m for m in events(x, rate) if m]
        if not ev:
            sys.exit("no sound in the capture")
        t0 = ev[0]["start"]
        step = a.grid / a.fps
        for k, n in enumerate(names):
            s = int((t0 + k * step - 0.1) * rate)
            print(row(n, measure(x, rate, max(0, s), min(len(x), s + int(step * rate)))))
    elif a.cmd == "events":
        x, rate = load(a.wav)
        for k, m in enumerate(events(x, rate)):
            print(row(f"#{k}", m))
    else:
        xa, ra = load(a.a)
        xb, rb = load(a.b)
        ea, eb = [m for m in events(xa, ra) if m], [m for m in events(xb, rb) if m]
        print(f"{len(ea)} sounds in {a.a}, {len(eb)} in {a.b}")
        for k, (ma, mb) in enumerate(zip(ea, eb)):
            st = 12 * math.log2(mb["f0"] / ma["f0"]) if ma["f0"] and mb["f0"] else float("nan")
            print(f"#{k}: len {ma['len']:.2f}s -> {mb['len']:.2f}s (x{mb['len'] / ma['len']:.3f}), "
                  f"pitch {st:+.2f} st, rms {mb['rms'] - ma['rms']:+.1f} dB, peak {mb['peak'] - ma['peak']:+.1f} dB")


if __name__ == "__main__":
    main()
