#!/usr/bin/env python3
"""Cool Mode's glasses checked close up: each model's head as the game poses it,
with the glasses where port/src/common/fun_cool.inc puts them (on the head group,
as fun_pc.c draws them), in its common animations (standing, walking, talking,
hurt) from the front, and standing turned three-quarters and to the side.

  fun_cool_check.py DISC OUT.png [--models a,b,...] [--rows N] [--cell PX]

A row a model: its name, then the tiles. A tile with a red edge: the glasses'
group is hidden in that pose (none drawn); grey: no such animation. OUT.png is a
page of --rows models (OUT-1.png, ... when there are more).
"""
import argparse
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402
import fun_heads as F  # noqa: E402
import fun_cool as C  # noqa: E402

# (animation, turned by degrees, how far into the animation): front-on, mid-step and mid-word, hurt, then
# turned three-quarters each way and to the side
VIEWS = [("stand", 0, 0.0), ("walk", 0, 0.25), ("walk", 0, 0.5), ("talk", 0, 0.5), ("hurt", 0, 0.0),
         ("stand", 40, 0.0), ("stand", -40, 0.0), ("stand", 70, 0.0)]
WANT = {"walk": ("W_1", "W_2", "R_1"), "talk": ("T_1", "T_2", "S_2"), "hurt": ("D_1", "D_2", "H_1", "Y_1")}

# a 3x5 font for the names
_FONT = {
    "a": "010101111101101", "b": "110101110101110", "c": "011100100100011", "d": "110101101101110",
    "e": "111100110100111", "f": "111100110100100", "g": "011100101101011", "h": "101101111101101",
    "i": "111010010010111", "j": "001001001101010", "k": "101101110101101", "l": "100100100100111",
    "m": "101111111101101", "n": "110101101101101", "o": "010101101101010", "p": "110101110100100",
    "q": "010101101110011", "r": "110101110101101", "s": "011100010001110", "t": "111010010010010",
    "u": "101101101101111", "v": "101101101101010", "w": "101101111111101", "x": "101101010101101",
    "y": "101101010010010", "z": "111001010100111", "_": "000000000000111", "-": "000000111000000",
    "0": "111101101101111", "1": "010110010010111", "2": "110001010100111", "3": "110001010001110",
    "4": "101101111001001", "5": "111100110001110", "6": "011100111101111", "7": "111001010010010",
    "8": "111101111101111", "9": "111101111001110", "|": "010010010010010",
}


def text(img, x, y, s, col, k=2):
    for ch in s.lower():
        f = _FONT.get(ch)
        if f:
            for i, b in enumerate(f):
                if b == "1":
                    for yy in range(k):
                        for xx in range(k):
                            py, px = y + (i // 3) * k + yy, x + (i % 3) * k + xx
                            if 0 <= py < len(img) and 0 <= px < len(img[0]):
                                img[py][px] = col
        x += 4 * k


def load_table(path):
    out = {}
    num = r"\{(-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f\}"
    for line in open(path, encoding="utf-8"):
        m = re.match(r'\{"([^"]+)", "([^"]*)", ' + ", ".join([num] * 4), line)
        if m:
            v = [float(x) for x in m.groups()[2:]]
            out[m.group(1)] = (m.group(2), v[0:3], v[3:6], v[6:9], v[9:12])
    return out


def _rot_y(deg):
    t = math.radians(deg)
    c, s = math.cos(t), math.sin(t)
    return [[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]]


def _mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def posed(pose, anim, yaw, share=0.0):
    P = F.Posed(pose, anim, share)
    if yaw:
        R = _rot_y(yaw)
        P.mats = [None if m is None else _mul(R, m) for m in P.mats]
        P.cards = {g: [F._apply(R, p) for p in pts] for g, pts in P.cards.items()}
    return P


def anim_for(pose, kind):
    names = [a.name for a in pose.anims if getattr(a, "model_offset", None) is None]
    if kind == "stand":
        return F.stand_anim(pose, None)
    for suf in WANT[kind]:
        for n in names:
            if n == suf or n.endswith("_" + suf):
                return n
    return None


def glasses_at(P, row):
    """(middle, ax, ay) in the view's world, or None (the group hidden), as fun_pc.c draws them"""
    gname, c, ax, ay, cb = row
    gi = next((i for i, g in enumerate(P.groups) if g.name == gname), None)
    if gi is None or P.mats[gi] is None or gi not in P.drawn:
        return None
    m = P.mats[gi]
    wc, wb = F._apply(m, c), F._apply(m, cb)
    lin = [[m[r][k] for k in range(3)] for r in range(3)]
    wax = [sum(lin[r][k] * ax[k] for k in range(3)) for r in range(3)]
    way = [sum(lin[r][k] * ay[k] for k in range(3)) for r in range(3)]
    if wb[2] > wc[2]:  # the one nearer the camera (in front: larger z)
        wc = wb
    return wc, wax, way


def tile(P, images, row, heads, shades, cell):
    """cell x cell RGB rows: the head, the glasses on it; and a state ('ok', 'hidden')"""
    g = glasses_at(P, row)
    hc = [c for h in heads for c in P.subtree(h) if c in P.cards]
    pts = [p for c in hc for p in P.cards[c]] or [p for pts in P.cards.values() for p in pts]
    lo, hi = F._bbox(pts)
    if g is not None:
        wc, wax, way = g
        half = math.hypot(wax[0], wax[1])
        lo = [min(lo[0], wc[0] - half), min(lo[1], wc[1] - half)]
        hi = [max(hi[0], wc[0] + half), max(hi[1], wc[1] + half)]
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    span = max(hi[0] - lo[0], hi[1] - lo[1]) * 1.15 + 1.0
    if g is not None and span > 7 * half:  # a big model (a dragon): about the glasses
        cx, cy, span = wc[0], wc[1], 7 * half
    box = (cx - span / 2, cy - span / 2, cx + span / 2, cy + span / 2)
    keep = [c for c, pts in P.cards.items()
            if max(p[0] for p in pts) > box[0] and min(p[0] for p in pts) < box[2]
            and max(p[1] for p in pts) > box[1] and min(p[1] for p in pts) < box[3]]

    class _Only:
        groups, cards, mats, geo, pos, tex = P.groups, {c: P.cards[c] for c in keep}, P.mats, P.geo, P.pos, P.tex

        @staticmethod
        def subtree(_g):
            return keep
    out = [[(236, 236, 240)] * cell for _ in range(cell)]
    pic = C.head_picture(_Only, [(P.root, None)], images) if keep else None
    k = cell / span  # pixels a unit
    if pic is not None:
        pimg, _zb, x0, y1, w, h = pic
        for y in range(cell):
            for x in range(cell):
                wx, wy = box[0] + (x + 0.5) / k, box[3] - (y + 0.5) / k
                px, py = int((wx - x0) * C.PX), int((y1 - wy) * C.PX)
                if 0 <= px < w and 0 <= py < h and pimg[py][px] is not None:
                    out[y][x] = pimg[py][px]
    if g is None:
        return out, "hidden"
    wc, wax, way = g
    det = wax[0] * way[1] - wax[1] * way[0]
    if abs(det) < 1e-6:
        return out, "edge"
    sw, sh, srows = shades
    for y in range(cell):
        for x in range(cell):
            dx, dy = box[0] + (x + 0.5) / k - wc[0], box[3] - (y + 0.5) / k - wc[1]
            s = (dx * way[1] - dy * way[0]) / det
            t = (wax[0] * dy - wax[1] * dx) / det
            if -1 <= s < 1 and -1 < t <= 1:
                p = srows[min(sh - 1, int((1 - t) / 2 * sh))][min(sw - 1, int((s + 1) / 2 * sw))]
                if p[3] >= 128:
                    out[y][x] = (p[0], p[1], p[2])
    return out, "ok"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("out")
    ap.add_argument("--models")
    ap.add_argument("--table", default=os.path.join(REPO, "port", "src", "common", "fun_cool.inc"))
    ap.add_argument("--rows", type=int, default=12)
    ap.add_argument("--cell", type=int, default=120)
    a = ap.parse_args(argv)
    src = F.open_disc(a.disc)
    table = load_table(a.table)
    models = a.models.split(",") if a.models else sorted(table)
    shades = C.shades_image(src)
    cell, label = a.cell, 120
    pages = [models[i:i + a.rows] for i in range(0, len(models), a.rows)]
    for pi, page in enumerate(pages):
        W, H = label + len(VIEWS) * (cell + 3), len(page) * (cell + 3)
        img = [[(255, 255, 255)] * W for _ in range(H)]
        for ri, model in enumerate(page):
            y0 = ri * (cell + 3)
            text(img, 2, y0 + 4, model, (0, 0, 0))
            row = table.get(model)
            try:
                pose = T.PoseFile.parse(src.read("a/" + model))
                images = [(w, h, rows) for w, h, rows, _s, _t in T.tpl_images(src.read("a/" + pose.texture_name + "-"))]
            except Exception as e:  # noqa: BLE001
                text(img, 2, y0 + 20, "err", (200, 0, 0))
                print(model, "ERR", e)
                continue
            if row is None:
                text(img, 2, y0 + 20, "none", (200, 0, 0))
                continue
            P0 = F.Posed(pose, F.stand_anim(pose, None))
            heads = [h for h, _p in F.pick(P0, F.override(model))] or [P0.root]
            for vi, (kind, yaw, share) in enumerate(VIEWS):
                x0 = label + vi * (cell + 3)
                an = anim_for(pose, kind)
                if an is None:
                    for y in range(cell):
                        for x in range(cell):
                            img[y0 + y][x0 + x] = (200, 200, 200)
                    continue
                P = posed(pose, an, yaw, share)
                t, state = tile(P, images, row, heads, shades, cell)
                for y in range(cell):
                    img[y0 + y][x0 + x0 * 0:x0 + cell] = t[y]
                if state != "ok":
                    col = (220, 40, 40) if state == "hidden" else (240, 160, 0)
                    for x in range(cell):
                        img[y0][x0 + x] = img[y0 + cell - 1][x0 + x] = col
                    for y in range(cell):
                        img[y0 + y][x0] = img[y0 + y][x0 + cell - 1] = col
                text(img, x0 + 2, y0 + cell - 9, "%s%s%s" % (kind[0], "" if not yaw else str(yaw), "" if not share else ("%.2f" % share)[1:]), (90, 90, 200), 1)
        out = a.out if len(pages) == 1 else "%s-%d.png" % (a.out[:-4], pi + 1)
        T.write_png(out, [[tuple(p[:3]) + (255,) for p in r] for r in img])
        print(out, " ".join(page))


if __name__ == "__main__":
    main()
