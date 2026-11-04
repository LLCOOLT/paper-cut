"""Check cards against STYLE.md's measurable rules (R1 size, R3 alpha, R4 outline, R5 fills,
R6b highlight: present on round parts, area 7-25 % of the part, centroid y < 0.45,
white overlay 0.2-0.35).

usage: check_style.py CARD.png [...]
Prints per card: size and whether both sides are multiples of 8; alpha levels and
partial texels; outline width (median, 10th-90th percentile, per side) and colour;
and the base fill (most common light colour) with its saturation/value.
The same measurements were run on the game's textures to write STYLE.md.
"""
import colorsys
import statistics
import sys
from collections import Counter

from PIL import Image


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def outline(im):
    w, h = im.size
    px = im.load()
    op = lambda x, y: 0 <= x < w and 0 <= y < h and px[x, y][3] > 127
    sides = {"L": [], "R": [], "T": [], "B": []}
    cols = []
    for y in range(h):
        for x in range(w):
            if not op(x, y):
                continue
            for name, dx, dy in (("L", 1, 0), ("R", -1, 0), ("T", 0, 1), ("B", 0, -1)):
                if op(x - dx, y - dy):
                    continue
                run, i = [], 0
                while op(x + dx * i, y + dy * i) and i < 20:
                    run.append(px[x + dx * i, y + dy * i]); i += 1
                if len(run) < 8 or lum(run[-1]) - lum(run[0]) <= 40:
                    continue
                thr = (lum(run[0]) + lum(run[-1])) / 2
                n = 0
                while n < len(run) and lum(run[n]) < thr:
                    n += 1
                if 0 < n < 12:
                    sides[name].append(n); cols.append(run[0][:3])
    allw = sorted(v for s in sides.values() for v in s)
    if not allw:
        return None
    med = lambda s: statistics.median(s) if s else None
    c = sorted(cols, key=lum)[len(cols) // 2]
    return {"median": med(allw), "p10_p90": (allw[len(allw) // 10], allw[len(allw) * 9 // 10]),
            "sides": {k: med(v) for k, v in sides.items()}, "colour": "#%02x%02x%02x" % c,
            "lum_p90": round(sorted(lum(c) for c in cols)[len(cols) * 9 // 10])}


def highlight(im):
    """R6b: on the largest part, texels of the base's hue lighter than its median by > 12 lum
    (5 px inside the edge). Returns area % of the part, centroid (bbox fractions) and the
    white-overlay strength, or None if there is none."""
    w, h = im.size
    px = im.load()
    seen, best = set(), []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] > 127 and (x, y) not in seen:
                st, comp = [(x, y)], []
                seen.add((x, y))
                while st:
                    a, b = st.pop(); comp.append((a, b))
                    for q in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                        if 0 <= q[0] < w and 0 <= q[1] < h and q not in seen and px[q][3] > 127:
                            seen.add(q); st.append(q)
                if len(comp) > len(best):
                    best = comp
    s = set(best)
    inner = [q for q in best if all((q[0] + dx, q[1] + dy) in s for dx in (-5, 0, 5) for dy in (-5, 0, 5))]
    if len(inner) < 150:
        return None
    base = [statistics.median(px[q][k] for q in inner) for k in range(3)]
    bh, bs, _ = colorsys.rgb_to_hsv(*(v / 255 for v in base))
    hl = []
    for q in inner:
        c = px[q]
        ch, cs, _ = colorsys.rgb_to_hsv(*(v / 255 for v in c[:3]))
        dh = min(abs(ch - bh), 1 - abs(ch - bh)) * 360
        if lum(c) > lum(base) + 12 and not (bs > 0.3 and cs < 0.15) and (bs < 0.2 or dh < 18):
            hl.append(q)
    if len(hl) < 10:
        return None
    xs = [x for x, _ in best]; ys = [y for _, y in best]
    x0, y0, W, H = min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    hc = [statistics.mean(px[q][k] for q in hl) for k in range(3)]
    alpha = statistics.mean((hc[k] - base[k]) / max(1, 255 - base[k]) for k in range(3))
    return {"area_pct": round(100 * len(hl) / len(best), 1),
            "centroid": (round((statistics.mean(x for x, _ in hl) - x0) / W, 2),
                         round((statistics.mean(y for _, y in hl) - y0) / H, 2)),
            "white_alpha": round(alpha, 2)}


def main(paths):
    for p in paths:
        im = Image.open(p).convert("RGBA")
        w, h = im.size
        data = list(im.getdata()) if not hasattr(im, "get_flattened_data") else list(im.get_flattened_data())
        alphas = {a for *_, a in data}
        partial = sum(1 for *_, a in data if 0 < a < 255)
        fills = Counter(px[:3] for px in data if px[3] == 255 and lum(px) > 80)
        base = fills.most_common(1)[0][0] if fills else (0, 0, 0)
        hh, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in base))
        print(f"{p}\n  R1 size {w}x{h} x8={'yes' if w % 8 == 0 and h % 8 == 0 else 'NO'}"
              f" | R3 alpha levels {len(alphas)} partial {partial}"
              f"\n  R4 outline {outline(im)}"
              f"\n  R5 base fill #{'%02x%02x%02x' % base} s{s:.2f} v{v:.2f}"
              f"\n  R6b highlight {highlight(im)}")


if __name__ == "__main__":
    main(sys.argv[1:])
