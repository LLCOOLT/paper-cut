"""The art manifesto's measurable rules (art/MANIFESTO.md), as a checker for one character's cards.

  check_manifesto.py PNG_OR_FOLDER ...          each folder or the PNGs together are one character
  check_manifesto.py --disc A_DIR MODEL ...     a disc model's cards (A_DIR is files/a of a disc)
  check_manifesto.py --holdout A_DIR            every held-out disc model: the rules' own test

Nothing is written, and it reads the disc only to measure it. Exit 1 when a checked character fails.

The bounds come from the 234 training models on the US disc; the 73 held-out models (a quarter of the
models, chosen by the CRC of their names) played no part in them. They hold for one character's large
cards together, not for each card: the game's own cards are not uniform.
"""
import glob
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image

LARGE_AREA = 2500  # opaque texels: a part where style reads; small cards are eyes, mouths and the like


def depth_map(op, cap=24):
    d = np.zeros(op.shape, dtype=np.int32)
    cur = op.copy()
    for k in range(1, cap + 1):
        d[cur] = k
        pad = np.pad(cur, 1, constant_values=False)
        cur = pad[1:-1, 1:-1] & pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:]
        if not cur.any():
            break
    return d


def box(a, r):
    h, w = a.shape
    i = np.zeros((h + 1, w + 1), dtype=np.float64)
    i[1:, 1:] = a.cumsum(0).cumsum(1)
    y0, y1 = np.clip(np.arange(h) - r, 0, h), np.clip(np.arange(h) + r + 1, 0, h)
    x0, x1 = np.clip(np.arange(w) - r, 0, w), np.clip(np.arange(w) + r + 1, 0, w)
    s = i[y1][:, x1] - i[y0][:, x1] - i[y1][:, x0] + i[y0][:, x0]
    return s / ((y1 - y0)[:, None] * (x1 - x0)[None, :])


def side_runs(op, dark):
    """from each clear-to-opaque entry, the length of the dark run going inward, by side"""
    out = {"L": [], "R": [], "T": [], "B": []}

    def scan(lo, ld, key):
        n = len(lo)
        for x in range(n):
            if lo[x] and (x == 0 or not lo[x - 1]):
                r = 0
                while x + r < n and lo[x + r] and ld[x + r] and r < 24:
                    r += 1
                if 0 < r < 24:
                    out[key].append(r)

    h, w = op.shape
    for y in range(h):
        scan(op[y], dark[y], "L")
        scan(op[y][::-1], dark[y][::-1], "R")
    for x in range(w):
        scan(op[:, x], dark[:, x], "T")
        scan(op[:, x][::-1], dark[:, x][::-1], "B")
    return out


def block_colours(rgba):
    """the median count of distinct colours in a 4x4 block: at most 4 once it has been through CMPR"""
    h, w = rgba.shape[:2]
    counts = []
    op = rgba[..., 3] >= 128
    for y in range(0, h - 3, 4):
        for x in range(0, w - 3, 4):
            if op[y:y + 4, x:x + 4].all():
                b = rgba[y:y + 4, x:x + 4, :3].reshape(-1, 3)
                counts.append(len(np.unique(b, axis=0)))
    return float(np.percentile(counts, 95)) if counts else 0.0


def measure(rgba):
    h, w = rgba.shape[:2]
    a = rgba[..., 3].astype(np.int32)
    op = a >= 128
    area = int(op.sum())
    r = {"w": w, "h": h, "area": area, "partial": float(((a > 8) & (a < 247)).sum()) / (w * h)}
    if area < 300 or area == w * h or min(w, h) < 24:
        return r
    rgb = rgba[..., :3].astype(np.float64)
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    d = depth_map(op)
    band = op & (d <= 2)
    deep = op & (d >= 7)
    if deep.sum() < 60:
        deep = op & (d >= 4)
    if deep.sum() < 40 or not band.any():
        return r
    edge, fill = float(np.median(lum[band])), float(np.median(lum[deep]))
    r["contrast"] = fill - edge
    e = lum[band]
    lc = np.median(rgb[band][e <= np.percentile(e, 40)], axis=0)
    r["line_rgb"] = lc.tolist()
    r["line_value"] = float(lc.max() / 255)
    if fill - edge > 25:
        rs = side_runs(op, lum < (edge + fill) / 2)
        every = sum(rs.values(), [])
        if len(every) >= 16:
            r["line_med"] = float(np.median(every))
            if all(rs[k] for k in "LRTB"):
                r["line_asym"] = float(np.median(rs["R"]) + np.median(rs["B"]) - np.median(rs["L"]) - np.median(rs["T"]))
    inner = op & (d > 5)
    if inner.sum() < 60:
        inner = deep
    q = rgba[..., :3][inner] >> 3
    _, counts = np.unique(q.reshape(-1, 3), axis=0, return_counts=True)
    r["colours"] = int((np.cumsum(np.sort(counts)[::-1]) < 0.9 * counts.sum()).sum() + 1)
    lf = np.where(op, lum, fill)
    r["hf"] = float(np.std((lf - box(lf, 2))[inner]))
    med = float(np.median(lum[inner]))
    r["flat12"] = float((np.abs(lum[inner] - med) <= 12).mean())
    r["blocks"] = block_colours(rgba)
    return r


# name, what it says, a character's value from its large cards' measures, lowest, highest
def med(rows, k):
    v = [c[k] for c in rows if k in c]
    return float(np.median(v)) if v else None


def spread_line(rows):
    v = [c["line_rgb"] for c in rows if "line_rgb" in c and c.get("contrast", 0) > 25]
    return float(np.mean(np.std(np.array(v), axis=0))) if len(v) >= 2 else None


# name, what it says, value, lowest, highest, hard. The bounds are the training models' 2.5th to 97.5th
# percentiles. A hard rule must hold; one soft rule may miss, as the game's own characters do.
RULES = [
    ("alpha", "cards are 1-bit: no partial alpha (cloth may have some)", lambda m: med(m, "partial"), 0.0, 0.02, True),
    ("cmpr grain", "95th percentile of colours in a 4x4 block", lambda m: med(m, "blocks"), 0.0, 4.0, True),
    ("line width", "median outline run, texels at 8 texels a unit", lambda m: med(m, "line_med"), 3.0, 6.0, False),
    ("even line", "right+bottom minus left+top: no side heavier", lambda m: med(m, "line_asym"), -3.0, 3.0, False),
    ("line dark", "the outline's value", lambda m: med(m, "line_value"), 0.0, 0.52, False),
    ("one line colour", "spread of the line's colour across cards", spread_line, 0.0, 23.0, False),
    ("few colours", "5-bit colours holding 90% of the inside", lambda m: med(m, "colours"), 0.0, 50.0, False),
    ("detail", "high-frequency detail of the inside", lambda m: med(m, "hf"), 0.0, 32.5, False),
]


def check(rows):
    large = [c for c in rows if c["area"] >= LARGE_AREA and "hf" in c]
    if not large:
        large = sorted((c for c in rows if "hf" in c), key=lambda c: -c["area"])[:4]
    res = []
    for name, _, fn, lo, hi, hard in RULES:
        v = fn(large) if large else None
        res.append((name, v, v is None or lo <= v <= hi, hard))
    return large, res


def tpl_cards(path):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "..", "tools", "port"))
    import tpl2png as T
    data = open(path, "rb").read()
    magic, count, table = struct.unpack_from(">III", data, 0)
    if magic != 0x0020AF30:
        return
    for n in range(count):
        io, po = struct.unpack_from(">II", data, table + 8 * n)
        h, w, fmt, off = struct.unpack_from(">HHII", data, io)
        if fmt in T.BLOCKS and not po:
            yield np.array(T.decode(data, w, h, fmt, off), dtype=np.uint8).reshape(h, w, 4)


def held_out(name):
    return zlib.crc32(name.encode()) % 4 == 0


def report(name, rows, verbose=True):
    large, res = check(rows)
    bad = [n for n, _, ok, _ in res if not ok]
    failed = any(not ok and hard for _, _, ok, hard in res) or len(bad) > 1
    if verbose:
        vals = " ".join(f"{n.split()[0]}={'-' if v is None else round(v, 2)}{'' if ok else '!'}" for n, v, ok, _ in res)
        print(f"{'FAIL' if failed else 'pass'} {name:26s} parts={len(large):3d} {vals}")
    return not failed, bad


def at_game_density(path):
    """a card as RGBA, measured at the game's 8 texels a unit: cards cut denser (cutout.py's DENSITY, parts.json's
    texels_per_unit) are taken down first, so line widths and sizes compare with the disc's"""
    import json
    im = Image.open(path).convert("RGBA")
    meta = os.path.join(os.path.dirname(path), "parts.json")
    tpu = 8
    if os.path.exists(meta):
        d = json.load(open(meta))
        card = d.get("cards", {}).get(os.path.splitext(os.path.basename(path))[0], {})
        tpu = card.get("texels_per_unit", d.get("texels_per_unit", 8))
    if tpu != 8:
        k = 8.0 / tpu
        im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.BOX)
        a = np.array(im)
        a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
        a = a[: a.shape[0] // 4 * 4, : a.shape[1] // 4 * 4]
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "..", "tools", "port"))
        from ink import cmpr_round_trip  # as it would have been, cut at 8 and compressed
        return cmpr_round_trip(a) if a.shape[0] >= 4 and a.shape[1] >= 4 else a
    return np.array(im)


def main(argv):
    if argv[:1] == ["--holdout"] or argv[:1] == ["--train"]:
        adir = argv[1]
        names = sorted(os.path.basename(p)[:-1] for p in glob.glob(os.path.join(adir, "c_*-")))
        want = argv[0] == "--holdout"
        total, passed, fails = 0, 0, {}
        for n in names:
            if held_out(n) != want:
                continue
            rows = [measure(c) for c in tpl_cards(os.path.join(adir, n + "-"))]
            if not any("hf" in c for c in rows):
                continue
            ok, bad = report(n, rows, verbose=False)
            total += 1
            passed += ok
            for b in bad:
                fails[b] = fails.get(b, 0) + 1  # (soft misses of passing models too)
            if not ok:
                print("FAIL", n, bad)
        print(f"{passed}/{total} models pass; failures by rule: {fails}")
        return 0
    failed = 0
    if argv[:1] == ["--disc"]:
        for n in argv[2:]:
            ok, _ = report(n, [measure(c) for c in tpl_cards(os.path.join(argv[1], n + "-"))])
            failed += not ok
    else:
        for p in argv:
            files = sorted(glob.glob(os.path.join(p, "**", "*.png"), recursive=True)) if os.path.isdir(p) else [p]
            files = [f for f in files if os.path.basename(f) != "preview.png"]
            rows = []
            for f in files:
                m = measure(at_game_density(f))
                if m is not None:  # the CMPR grain is the card's own, at the density it was cut (and compressed) at
                    full = measure(np.array(Image.open(f).convert("RGBA")))
                    if full is not None and "blocks" in full:
                        m["blocks"] = full["blocks"]
                rows.append(m)
            ok, _ = report(os.path.basename(p.rstrip("/\\")), rows)
            failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
