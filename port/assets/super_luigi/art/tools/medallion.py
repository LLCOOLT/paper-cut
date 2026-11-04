"""Luigi's chapter-card medallion for Super Luigi.

The chapter card (map sys_01, rels/sys/src/sys.c display_func) spins a gold coin over "Chapter N":
images 0 to 3 of e/<lang>/effect_sys.tpl, 160x160 CMPR, swapped every half turn (Mario, Peach,
Bowser, the Crystal Star emblem). This draws Luigi's face for image 0, Mario's, as a drop-in: the
same 160x160 round 1-bit coin, struck the same way, so it spins beside Peach's and Bowser's as
the same engraver's. Measured on the decoded original in scratch (nothing from the disc is in the
repo); every tone is flat, with no blur and no gradient:

  coin    a dark line (#634d00, 3 to 4 px), a bright rim (#ceb23c, 5 to 6 px), and the rim's inner bevel,
          2 px: dark (#635510) on the top and left, cream (#fff7c6) on the bottom and right. Then a
          flat olive field (#99831e).
  plate   the head stands on a raised plate round its silhouette: a dark line (3 px), a gold band
          (4 px) lit (#dcc066) on the top left and dark (#a08218) on the bottom right, then a
          cream bevel (#fff1bd, 2 to 3 px) hugging the head's line.
  head    a heavy dark line (#634d00, 4.2 px); cap, visor, hair and moustache in dark gold
          (#967300); skin in cream (#fff1bd); the badge a cream oval with the letter in the line.

The pose is Mario's: three-quarter profile facing left, the nose to the left, the visor pointing
left, the ear on the right, the head filling the field the same way. Luigi's are the taller cap
with the L on its front panel, the longer face and chin, and his own moustache.

Monochrome like the other three faces: Luigi's L is the badge's shape, not its green.

Run: python medallion.py [ORIGINAL.png COMPARE.png]
  -> ../titles/medallion_drawn/<dolphin name>.svg/.png and preview.png; with the decoded original
     (scratch only), also a side-by-side at 3x and 1x.
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, ellipse_pts, poly_path, stroke_svg, fill_svg  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "titles", "medallion_drawn")
# Dolphin's name for effect_sys.tpl image 0 (XXH64 of its CMPR data): the same on JP, US and EU
NAME = "tex1_160x160_93898cc57cbf557a_14"

LINE, GROOVE = "#634d00", "#635510"
RIM, RIM_LT = "#ceb23c", "#fff7c6"
FIELD = "#99831e"
BAND_LT, BAND_DK = "#dcc066", "#a08218"
GOLD = "#967300"
CREAM = "#fff1bd"

C = 80.0          # coin centre
HEAD_LINE = 4.2   # the head's dark line, centred on each part's edge
BEVEL = 5.0       # cream bevel's outer edge, px out from the parts' edges
BAND = 9.0        # gold band's outer edge
PLATE = 12.0      # the plate's dark line's outer edge


def circle(r, cx=C, cy=C):
    return (f"M{cx - r:.2f} {cy:.2f}A{r} {r} 0 1 0 {cx + r:.2f} {cy:.2f}"
            f"A{r} {r} 0 1 0 {cx - r:.2f} {cy:.2f}Z")


def sm(pts, seed, tension=0.85, jitter=0.3):
    return smooth_path(pts, tension=tension, jitter=jitter, seed=seed)


def oval(cx, cy, rx, ry, seed, rot=0.0, n=14):
    return smooth_path(ellipse_pts(cx, cy, rx, ry, n, rot=rot, wobble=0.02, seed=seed), tension=0.9,
                       jitter=0.15, seed=seed)


def split(gid, a, b):
    """a hard two-tone split across the top-right / bottom-left diagonal: a on the top left, b on
    the bottom right (the light comes from the top left)"""
    return (f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="40" y1="40" x2="120" y2="120">'
            f'<stop offset="0.49" stop-color="{a}"/><stop offset="0.51" stop-color="{b}"/></linearGradient>')


# ------------------------------------------------------------- Luigi's head --
# Texture coordinates. Mario's head spans about x 28-124, y 27-115; Luigi's cap rises higher and
# his chin drops lower, in the same three-quarter view.

CROWN = sm([(34, 64), (35, 48), (43, 35), (57, 26), (75, 22), (95, 23), (112, 32), (123, 47), (127, 61),
            (124, 74), (112, 78), (96, 73), (72, 66), (50, 63)], 11)
VISOR = sm([(24, 63), (31, 58), (48, 58), (70, 63), (92, 71), (104, 78), (96, 82), (74, 76), (52, 71),
            (34, 70), (25, 68)], 12)
BADGE = oval(47, 41, 8.4, 11.6, 19, rot=-8)
L_MARK = poly_path([(44.6, 33.2), (48.4, 32.8), (49.0, 44.6), (54.0, 44.0), (54.6, 48.0), (45.6, 49.0)])
FACE = sm([(36, 72), (60, 72), (86, 76), (106, 80), (119, 90), (121, 106), (116, 120), (104, 130),
           (88, 136), (74, 137), (63, 133), (55, 124), (46, 112), (36, 92)], 14)
BACK_HAIR = sm([(112, 104), (123, 102), (127, 112), (121, 121), (112, 121)], 13)
EAR = oval(110, 97, 8.2, 10.4, 17, rot=-10)
SIDEBURN = sm([(85, 74), (100, 78), (102, 90), (98, 100), (93, 97), (91, 88), (86, 82)], 20)
NOSE = sm([(52, 84), (46, 80), (36, 79), (28, 84), (25, 93), (28, 102), (36, 107), (48, 107), (56, 101),
           (58, 92)], 15)
STACHE = sm([(44, 104), (58, 101), (72, 102), (84, 104), (93, 103), (98, 99), (101, 104), (95, 112),
             (85, 115), (76, 113), (68, 118), (58, 117), (50, 115), (44, 111)], 16)
EYE = oval(66, 87, 3.4, 8.0, 18, rot=-4)

# back to front
PARTS = [(BACK_HAIR, GOLD), (FACE, CREAM), (EAR, CREAM), (SIDEBURN, GOLD), (NOSE, CREAM), (STACHE, GOLD),
         (EYE, GOLD), (CROWN, GOLD), (BADGE, CREAM), (VISOR, GOLD)]
SILHOUETTE = [CROWN, VISOR, FACE, BACK_HAIR, EAR, NOSE, STACHE]


# The head is drawn on its own grid and set into the field at SCALE, so it and its plate leave a
# ring of field showing as Mario's does. Line widths are given in texels and divided by SCALE.
SCALE, FROM, TO = 0.88, (77.0, 78.0), (77.0, 76.0)
PLACE = (f'<g transform="translate({TO[0] - FROM[0] * SCALE:.3f} {TO[1] - FROM[1] * SCALE:.3f}) '
         f'scale({SCALE})">')


def w(px):
    return round(px / SCALE, 3)


def grown(by, paint, dx=0.0, dy=0.0):
    """the head's silhouette grown by `by` texels: every part stroked round, so the union grows evenly"""
    t = f' transform="translate({w(dx)} {w(dy)})"' if dx or dy else ""
    return f"{PLACE}<g{t}>" + "".join(f'<path d="{d}" fill="{paint}" stroke="{paint}" stroke-width="{w(2 * by)}" '
                                      f'stroke-linejoin="round"/>' for d in SILHOUETTE) + "</g></g>"


def coin(p):
    p.defs.append(split("bevel", GROOVE, RIM_LT))
    p.defs.append(split("band", BAND_LT, BAND_DK))
    p.raw(fill_svg(circle(81.0), LINE))
    p.raw(fill_svg(circle(76.9), RIM))
    p.raw(fill_svg(circle(71.4), "url(#bevel)"))
    p.raw(fill_svg(circle(69.4), FIELD))
    # the raised plate: dark line, gold band, cream bevel (a touch wider on the top left)
    p.raw(grown(PLATE, LINE, 0.4, 0.4))
    p.raw(grown(BAND, "url(#band)"))
    p.raw(grown(BEVEL - 0.3, CREAM, 0.4, 0.4))
    p.raw(grown(BEVEL - 0.3, CREAM, -0.4, -0.4))


def head(p):
    s = [PLACE]
    for d, fill in PARTS:
        s.append(f'<path d="{d}" fill="{fill}" stroke="{LINE}" stroke-width="{w(HEAD_LINE)}" '
                 f'stroke-linejoin="round"/>')
        if d is BADGE:
            s.append(fill_svg(L_MARK, LINE))
        if d is EAR:
            s.append(stroke_svg("M113 91 Q106 91 106.5 97 Q107 103 113.5 102", LINE, w(2.0)))
        if d is STACHE:
            s.append(stroke_svg("M69 109 Q71 113 68 117", LINE, w(1.8)))  # the dip between its lobes
    p.raw("".join(s) + "</g>")


def build():
    os.makedirs(OUT, exist_ok=True)
    p = Part(NAME, 160, 160)
    coin(p)
    head(p)
    im = p.render()
    # the coin's round cut (1-bit, transparent corners black): the circle that best fits the
    # original's alpha, centre 80.1 and radius 80.2 (it differs from it on 148 edge texels of 25600)
    px = im.load()
    for y in range(160):
        for x in range(160):
            if (x + 0.5 - 80.1) ** 2 + (y + 0.5 - 80.1) ** 2 > 80.2 ** 2:
                px[x, y] = (0, 0, 0, 0)
            elif px[x, y][3] < 255:
                px[x, y] = (*px[x, y][:3], 255)
    with open(os.path.join(OUT, NAME + ".svg"), "w", encoding="utf-8") as f:
        f.write(p.svg())
    im.save(os.path.join(OUT, NAME + ".png"))
    return im


def preview(im, original=None, others=()):
    """the new medallion at 3x and 1x; given the decoded original (scratch only), beside it, and
    given the other three faces, the whole spin at 1x with Luigi in Mario's place"""
    tiles = [("Luigi (new)", im)] + ([("Mario (original)", original)] if original else [])
    W = 20 + len(tiles) * 680
    H = 540 + (200 if others else 0)
    c = Image.new("RGBA", (W, H), (88, 84, 104, 255))
    d = ImageDraw.Draw(c)
    for i, (label, t) in enumerate(tiles):
        x = 20 + i * 680
        c.alpha_composite(t.resize((480, 480), Image.NEAREST), (x, 40))
        c.alpha_composite(t, (x + 490, 360))
        d.text((x, 14), label, fill=(240, 236, 220, 255))
    if others:
        d.text((20, 550), "the spin at 1x: Luigi, then the game's other three faces", fill=(240, 236, 220, 255))
        for i, t in enumerate([im, *others]):
            c.alpha_composite(t, (20 + i * 180, 570))
    return c


if __name__ == "__main__":
    img = build()
    preview(img).convert("RGB").save(os.path.join(OUT, "preview.png"))
    if len(sys.argv) > 2:
        orig = Image.open(sys.argv[1]).convert("RGBA")
        base = sys.argv[1].rsplit("_", 1)[0]
        others = [Image.open(f"{base}_{i}.png").convert("RGBA") for i in (1, 2, 3)
                  if os.path.exists(f"{base}_{i}.png")]
        preview(img, orig, others).convert("RGB").save(sys.argv[2])
