"""Paper Cut's hand-cut lettering for Super Luigi's signs, banners and posters, drawn to match the
game's own painted text (studied on the disc's map textures, decoded into scratch, never committed):

  tou_02 t_19   "WELCOME": heavy white block capitals cut from paper; stems about a third of the
                cap height, small square counters, corners chamfered, each letter a little off
                square, tilted and bobbing on the line
  yuu_02 t_17/18/20/21  the Pianta Parlor and casino signs: the same chunky cut capitals, set on an
                arc with each letter turned to it, faced in a red-to-yellow ramp with a grey edge
                and a darker drop below right
  tou_02 t_3/t_18/t_21  the Glitz Pit posters: short headlines in a heavy capital with a dark edge

No font file is used: every glyph is a few polygons on a 12-unit-high grid (cap height 12, stem 3),
unioned with the nonzero rule, so the counters come out of the bars' arrangement. Each letter is
jittered (vertices, tilt, size, baseline) from a seed, so a word reads hand-cut and stays the same
from run to run.

    from lettering import text_svg
    svg = text_svg("RACE DAY!", x, y, size, fill="#fff", edge="#1a0a04", edge_w=2.0)
"""
import math
import random

H = 12.0     # cap height in grid units
TRACK = 1.0  # space between letters, grid units


def _bar(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def _p(*pts):
    return list(pts)


def _o(w):
    """an O / 0 of width w: four bars round a counter, chamfered outside"""
    c = 2.2
    return [_p((c, 0), (w - c, 0), (w, c), (w, 3), (0, 3), (0, c)),
            _p((0, 9), (w, 9), (w, H - c), (w - c, H), (c, H), (0, H - c)),
            _bar(0, 2, 3, 10), _bar(w - 3, 2, w, 10)]


def _six():
    return [_p((2, 0), (8, 0), (8, 3), (3.2, 3), (3.2, 9), (5.6, 9), (5.6, 7.6), (3, 7.6), (3, 4.8), (6.8, 4.8),
               (8.8, 6.8), (8.8, 10), (6.8, 12), (2, 12), (0, 10), (0, 2))]


def _flip(polys, w):
    return [[(w - x, H - y) for x, y in p] for p in polys]


_P = [_bar(0, 0, 3, 12), _bar(0, 0, 6.4, 3), _bar(0, 5.6, 6.4, 8.4),
      _p((5.4, 0), (7, 0), (9, 2), (9, 6.4), (7, 8.4), (5.4, 8.4))]

GLYPHS = {
    "A": (10, [_p((3.4, 0), (6.6, 0), (10, 12), (7, 12), (5, 4.6), (3, 12), (0, 12)), _bar(2.6, 7.4, 7.4, 9.6)]),
    "B": (9, [_bar(0, 0, 3, 12), _bar(0, 0, 6.6, 3), _bar(0, 4.6, 6.6, 7.2), _bar(0, 9.4, 6.8, 12),
              _p((5.6, 0), (7.2, 0), (8.6, 1.4), (8.6, 4.4), (7.2, 5.9), (5.6, 5.9)),
              _p((5.6, 5.9), (7.4, 5.9), (9, 7.4), (9, 10.6), (7.6, 12), (5.6, 12))]),
    "C": (8.8, [_p((2, 0), (8.6, 0), (8.6, 3.2), (3.2, 3.2), (3.2, 8.8), (8.8, 8.8), (8.8, 12), (2, 12), (0, 10),
                   (0, 2))]),
    "D": (9, [_bar(0, 0, 3, 12), _bar(0, 0, 6, 3), _bar(0, 9, 6, 12),
              _p((4.8, 0), (6.4, 0), (9, 2.6), (9, 9.4), (6.4, 12), (4.8, 12), (6, 9), (6, 3))]),
    "E": (8.2, [_bar(0, 0, 3, 12), _bar(0, 0, 8, 3), _bar(0, 4.6, 7, 7.2), _bar(0, 9, 8.2, 12)]),
    "F": (8, [_bar(0, 0, 3, 12), _bar(0, 0, 8, 3), _bar(0, 4.8, 7, 7.4)]),
    "G": (9.2, [_p((2, 0), (8.6, 0), (8.6, 3.2), (3.2, 3.2), (3.2, 8.8), (6.2, 8.8), (6.2, 7.8), (4.8, 7.8),
                   (4.8, 5.2), (9.2, 5.2), (9.2, 12), (2, 12), (0, 10), (0, 2))]),
    "H": (9, [_bar(0, 0, 3, 12), _bar(6, 0, 9, 12), _bar(0, 4.6, 9, 7.4)]),
    "I": (3.2, [_bar(0, 0, 3.2, 12)]),
    "J": (8, [_p((5, 0), (8, 0), (8, 10), (6, 12), (1.6, 12), (0, 10.4), (0, 7.6), (3, 7.6), (3, 9), (5, 9))]),
    "K": (9.4, [_bar(0, 0, 3, 12), _p((5.8, 0), (9.2, 0), (5.4, 6), (9.4, 12), (5.8, 12), (2.6, 7.4), (2.6, 4.6))]),
    "L": (7.6, [_bar(0, 0, 3, 12), _bar(0, 9, 7.6, 12)]),
    "M": (11.6, [_bar(0, 0, 3, 12), _bar(8.6, 0, 11.6, 12), _p((0, 0), (3.2, 0), (5.8, 4.6), (8.4, 0), (11.6, 0),
                                                                (5.8, 8.6))]),
    "N": (9.6, [_bar(0, 0, 3, 12), _bar(6.6, 0, 9.6, 12), _p((0, 0), (3.2, 0), (9.6, 12), (6.4, 12))]),
    "O": (9.6, _o(9.6)),
    "P": (9, _P),
    "Q": (10.2, _o(9.6) + [_p((5.6, 8.4), (8.4, 8.4), (10.4, 12.8), (7.6, 12.8))]),
    "R": (9.4, _P + [_p((4.4, 7.4), (7.4, 7.4), (9.4, 12), (6.2, 12))]),
    "S": (8.6, [_p((2, 0), (8.4, 0), (8.4, 3), (3.2, 3), (3.2, 4.4), (6.8, 4.4), (8.6, 6.2), (8.6, 10), (6.6, 12),
                   (0, 12), (0, 9), (5.4, 9), (5.4, 7.6), (1.8, 7.6), (0, 5.8), (0, 2))]),
    "T": (9, [_bar(0, 0, 9, 3), _bar(3, 0, 6, 12)]),
    "U": (9.4, [_p((0, 0), (3, 0), (3, 9), (6.4, 9), (6.4, 0), (9.4, 0), (9.4, 10), (7.4, 12), (2, 12), (0, 10))]),
    "V": (10, [_p((0, 0), (3.2, 0), (5, 7), (6.8, 0), (10, 0), (6.6, 12), (3.4, 12))]),
    "W": (13, [_p((0, 0), (3, 0), (4.2, 6.6), (5.4, 2), (7.6, 2), (8.8, 6.6), (10, 0), (13, 0), (10.6, 12),
                  (7.8, 12), (6.5, 7), (5.2, 12), (2.4, 12))]),
    "X": (10, [_p((0, 0), (3.4, 0), (5, 3.4), (6.6, 0), (10, 0), (6.8, 6), (10, 12), (6.6, 12), (5, 8.6),
                  (3.4, 12), (0, 12), (3.2, 6))]),
    "Y": (10, [_p((0, 0), (3.4, 0), (5, 3.6), (6.6, 0), (10, 0), (6.6, 7), (6.6, 12), (3.4, 12), (3.4, 7))]),
    "Z": (9, [_p((0, 0), (9, 0), (9, 3), (4.4, 9), (9, 9), (9, 12), (0, 12), (0, 9), (4.6, 3), (0, 3))]),
    "0": (8.8, _o(8.8)),
    "1": (5.6, [_p((0, 1.6), (3, 0), (5.6, 0), (5.6, 12), (2.6, 12), (2.6, 4), (0, 4.4))]),
    "2": (8.6, [_p((0, 2), (2, 0), (6.6, 0), (8.6, 2), (8.6, 5.4), (4.6, 9), (8.6, 9), (8.6, 12), (0, 12), (0, 9.4),
                   (5.4, 4.6), (5.4, 3), (3, 3), (3, 4), (0, 4))]),
    "3": (8.4, [_p((0, 0), (6.6, 0), (8.4, 1.8), (8.4, 4.6), (7.2, 6), (8.4, 7.4), (8.4, 10.2), (6.6, 12), (0, 12),
                   (0, 9), (5.4, 9), (5.4, 7.4), (2, 7.4), (2, 4.6), (5.4, 4.6), (5.4, 3), (0, 3))]),
    "4": (9.2, [_p((2.6, 0), (5.6, 0), (3.4, 5.6), (9.2, 5.6), (9.2, 8.4), (0, 8.4), (0, 6)),
                _bar(5.6, 3, 8.6, 12)]),
    "5": (8.6, [_p((0.4, 0), (8.4, 0), (8.4, 3), (3.2, 3), (3.2, 4.4), (6.6, 4.4), (8.6, 6.4), (8.6, 10), (6.6, 12),
                   (0, 12), (0, 9), (5.4, 9), (5.4, 7.4), (0.4, 7.4))]),
    "6": (8.8, _six()),
    "7": (8.4, [_p((0, 0), (8.4, 0), (8.4, 2.8), (5, 12), (1.8, 12), (5, 3), (0, 3))]),
    "8": (8.8, [_p((1.6, 0), (7.2, 0), (8.6, 1.4), (8.6, 2.8), (0.2, 2.8), (0.2, 1.4)), _bar(0.2, 1, 3, 6),
                _bar(5.8, 1, 8.6, 6), _bar(0.4, 4.6, 8.4, 7.2), _bar(0, 6, 3, 11), _bar(5.8, 6, 8.8, 11),
                _p((0, 9.2), (8.8, 9.2), (8.8, 10.6), (7.4, 12), (1.4, 12), (0, 10.6))]),
    "9": (8.8, _flip(_six(), 8.8)),
    "!": (3.6, [_p((0, 0), (3.6, 0), (3, 8), (0.6, 8)), _bar(0.4, 9.2, 3.2, 12)]),
    "#": (10, [_p((2.6, 0), (5, 0), (4, 12), (1.6, 12)), _p((6.6, 0), (9, 0), (8, 12), (5.6, 12)),
               _bar(0, 3, 10, 5.2), _bar(0, 7, 10, 9.2)]),
    "&": (10.4, [_bar(1.4, 0, 6.6, 2.6), _bar(1.2, 0, 3.8, 5.2), _p((4.4, 0), (5.8, 0), (7, 1.2), (7, 3.6), (5, 5.6),
                                                                      (4.4, 4.6)),
                 _p((1.4, 3.6), (4.4, 3.6), (10.4, 12), (7.2, 12)),
                 _p((2.6, 5.4), (4.4, 7.2), (3, 8.8), (3.4, 9.4), (5.8, 9.4), (7.4, 7.6), (10.2, 7.6), (8, 10.8),
                    (6.6, 12), (2, 12), (0, 10), (0, 8))]),
    "♥": (11, [_p((5.5, 3), (7, 0.8), (9, 0), (10.6, 1), (11, 3.2), (10, 6.4), (5.5, 12), (1, 6.4), (0, 3.2),
                       (0.4, 1), (2, 0), (4, 0.8))]),
    ",": (3, [_p((0.4, 9), (3, 9), (3, 11), (1.2, 13.6), (0, 13.2), (0.8, 12), (0.4, 12))]),
    ".": (3, [_bar(0.2, 9.2, 3, 12)]),
    "/": (6.6, [_p((4, 0), (6.6, 0), (2.6, 12), (0, 12))]),
    "'": (3, [_p((0.4, 0), (2.8, 0), (2.4, 4), (0.8, 4))]),
    "-": (5.4, [_bar(0, 4.8, 5.4, 7.2)]),
    " ": (4.4, []),
}


def _cw(p):
    a = sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))
    return p if a > 0 else list(reversed(p))


def advance(ch):
    return GLYPHS[ch.upper() if ch.upper() in GLYPHS else ch][0] + TRACK


def width(text, size, spacing=0.0):
    """the set width of `text` at cap height `size` (px), before jitter; spacing: extra grid units
    between letters"""
    return (sum(advance(ch) + spacing for ch in text) - TRACK - spacing) * size / H


def layout(text, x, y, size, anchor="middle", seed=0, jitter=0.3, tilt=4.0, bob=0.05, grow=0.04, arc=None,
           squash=1.0, spacing=0.0):
    """Polygons (px) for `text` with cap height `size`, baseline y. anchor: start/middle/end at x.
    arc: (cx, cy) the centre of a circle through (x, y); the letters follow it, each turned to it.
    squash: horizontal scale of the letters; spacing: extra grid units between them. Returns a list of
    polygons."""
    rnd = random.Random(seed)
    k = size / H
    total = width(text, size, spacing) * squash
    x0 = x - (total / 2 if anchor == "middle" else total if anchor == "end" else 0)
    out = []
    pen = 0.0
    for ch in text:
        key = ch.upper() if ch.upper() in GLYPHS else ch
        w, polys = GLYPHS.get(key, GLYPHS[" "])
        if key != ch:   # a lower-case letter: the capital, smaller, on the same baseline
            k2 = k * 0.72
        else:
            k2 = k
        s = k2 * (1 + rnd.uniform(-grow, grow))
        rot = math.radians(rnd.uniform(-tilt, tilt))
        dy = rnd.uniform(-bob, bob) * size
        gw = w * s * squash
        lx = pen + gw / 2   # the letter's centre along the line
        if arc:
            cx, cy = arc
            r = math.hypot(x - cx, y - cy)
            a0 = math.atan2(y - cy, x - cx)
            a = a0 + (x0 - x + lx) / r
            bx, by = cx + r * math.cos(a), cy + r * math.sin(a)
            turn = a - a0
        else:
            bx, by, turn = x0 + lx, y, 0.0
        c, sn = math.cos(rot + turn), math.sin(rot + turn)
        for poly in polys:
            q = []
            for px, py in poly:
                px += rnd.uniform(-jitter, jitter)
                py += rnd.uniform(-jitter, jitter)
                lx2, ly2 = (px - w / 2) * s * squash, (py - H) * s + dy   # from the letter's foot centre
                q.append((bx + lx2 * c - ly2 * sn, by + lx2 * sn + ly2 * c))
            out.append(_cw(q))
        pen += (w + TRACK + spacing) * k2 * squash
    return out


def path_d(polys):
    return "".join("M" + "L".join("%.2f %.2f" % pt for pt in p) + "Z" for p in polys)


def text_svg(text, x, y, size, fill, edge=None, edge_w=0.0, shadow=None, shadow_off=(1.0, 1.2), anchor="middle",
             seed=0, extra="", **kw):
    """The word as SVG: an optional drop (shadow, offset), an edge (a stroke under the face, so only
    its outer half shows) and the face. fill may be a url(#gradient)."""
    d = path_d(layout(text, x, y, size, anchor=anchor, seed=seed, **kw))
    out = []
    j = 'stroke-linejoin="miter" stroke-miterlimit="3"'
    if shadow:
        sx, sy = shadow_off
        sw = f' stroke="{shadow}" stroke-width="{2 * edge_w:.2f}" {j}' if edge_w else ""
        out.append(f'<path d="{d}" fill="{shadow}"{sw} transform="translate({sx} {sy})" {extra}/>')
    if edge and edge_w:
        out.append(f'<path d="{d}" fill="{edge}" stroke="{edge}" stroke-width="{2 * edge_w:.2f}" {j} {extra}/>')
    out.append(f'<path d="{d}" fill="{fill}" {extra}/>')
    return "".join(out)


def outline_svg(text, x, y, size, colour, width=1.0, anchor="middle", seed=0, **kw):
    """the word as a thin outline only (a pencil guide)"""
    d = path_d(layout(text, x, y, size, anchor=anchor, seed=seed, **kw))
    return f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{width}" stroke-linejoin="round"/>'


if __name__ == "__main__":   # a specimen sheet, for checking the glyphs
    import io
    import sys
    import resvg_py
    from PIL import Image
    lines = ["ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789 !#&♥,./'-", "WELCOME ♥ CIRCUIT BREAK"]
    body = "".join(text_svg(t, 20, 70 + i * 80, 48, "#f4f0e6", "#3a1a08", 3, "#1a0a04", (2, 3), "start", seed=i)
                   for i, t in enumerate(lines))
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="900" height="360"><rect width="900" height="360" fill="#5a6a9a"/>{body}</svg>'
    im = Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg))))
    im.save(sys.argv[1] if len(sys.argv) > 1 else "lettering_specimen.png")
