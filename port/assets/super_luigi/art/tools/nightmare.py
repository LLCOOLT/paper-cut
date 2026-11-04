"""The Nightmare (c_sl_nightmare): Minister Crepe unfolded, chapter 6's last battle form (Super Luigi).

Run: python nightmare.py -> ../nightmare/*.svg, *.png, parts.json, preview.png
ch6.md, New assets 5: "a tall shadow (deep violet-black paper with gold edges), a round compass for a
face whose needle spins and stops, two long arms made of folded pages. Battle only." Animations to
serve: idle (needle spinning), Doom Foretold (needle stops and points), Hatesong (face opens, a howl),
A Thousand Tomorrows (page-slaps), Page Grab, arm lost / regrow, hurt, the crack and shatter.
A giant boss: 4 texels/unit (STYLE.md R2), mostly flat with up to three hard tones (R6), outlines
3 to 5 px (R4). About 56 units tall (Luigi is 24). Facing -x, feet (the hem) at y 0.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, poly_path, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "nightmare")
R = Rig("c_sl_nightmare", OUT, unit=4)

SHADOW = dict(fill="#3a2650", light="#4e3668", shadow="#28183a", line="#0e0614")
SHADOW_B = dict(fill="#2e1e42", light="#3e2c56", shadow="#1e1230", line="#0e0614")
GOLD = "#e2b444"
GOLD_D = "#a07a1c"
PAGE = dict(fill="#e8dcc0", light="#f6eedc", shadow="#bcaa86", line="#2a1a10")
PAGE_B = dict(fill="#cfc2a4", light="#ddd2b6", shadow="#a29070", line="#2a1a10")
INK = "#6a5a48"
FACE = dict(fill="#f2e6c4", light="#fff6dc", shadow="#cdb88a", line="#3a2606")
RIM = dict(fill="#e2b444", light="#f6d67a", shadow="#a07a1c", line="#3a2606")
VOID = "#120818"
LIGHT = dict(fill="#fff2b0", light="#ffffff", shadow="#f2c860", line="#6a4208")
CARD = dict(grow=1.3, inner=1.5, bias=(1.0, 1.3))   # 3 to 5 px lines at 4 texels/unit


def card(name, rect):
    return R.card(name, rect, **CARD)


def gold_edge(c, pts, w=2.4):
    """the paper's gilt edge: a gold band just inside the outline"""
    return f'<path d="{c.d(pts, 0.45, 0.6, 1)}" fill="none" stroke="{GOLD}" stroke-width="{w * 2}"/>'


def body(cracked=False):
    name = "body_cracked" if cracked else "body"
    c = card(name, (-16.0, -1.0, 16.0, 47.0))
    rnd = random.Random(2)
    hem = []
    for k in range(13):   # a ragged, torn-paper hem
        x = 14.0 - 28.0 * k / 12
        hem.append((x, 0.4 + (1.6 if k % 2 else 0.0) + rnd.uniform(-0.3, 0.3)))
    pts = [(-6.0, 44.0), (-3.0, 46.0), (3.0, 46.0), (6.0, 44.0), (8.0, 36.0), (10.0, 24.0), (14.0, 10.0),
           (15.0, 1.2)] + hem + [(-15.0, 1.2), (-13.6, 10.0), (-9.6, 24.0), (-7.6, 36.0)]
    det = [gold_edge(c, pts),
           # folds of the paper: long hard creases (flat tones at this scale)
           f'<path d="{c.d([(2.0, 40.0), (5.0, 22.0), (9.0, 3.0), (13.0, 3.0), (8.0, 24.0)], 0.3, 0.3, 3)}" '
           f'fill="{SHADOW["shadow"]}"/>',
           f'<path d="{c.d([(-3.0, 40.0), (-6.0, 20.0), (-11.0, 3.0), (-7.0, 3.0), (-3.0, 22.0)], 0.3, 0.3, 4)}" '
           f'fill="{SHADOW["light"]}"/>',
           stroke_svg(c.line([(0.0, 38.0), (0.6, 20.0), (-0.4, 2.0)]), SHADOW["shadow"], 2.5)]
    for x, y in ((-4.0, 30.0), (6.0, 14.0), (-8.0, 10.0), (3.0, 34.0)):   # faint gold flecks (old ink)
        det.append(fill_svg(c.ell(x, y, 0.35, 0.35, 6), GOLD_D))
    if cracked:   # light breaking through the paper
        for crack in ([(0.0, 44.0), (-1.4, 34.0), (1.2, 26.0), (-0.8, 16.0), (1.6, 6.0)],
                      [(-1.4, 34.0), (-6.0, 30.0)], [(1.2, 26.0), (7.0, 20.0)], [(-0.8, 16.0), (-7.0, 8.0)]):
            det.append(stroke_svg(c.line(crack), "#fff2b0", 5))
            det.append(stroke_svg(c.line(crack), "#ffffff", 2))
    c.shape(pts, SHADOW, det, seed=5, tension=0.4, jitter=0.6, lo=(4, 4), so=(5, 5), soft=0)
    return c.save((0.0, 0.0), ("cracked: lines of light breaking through (the shatter, stage 1)" if cracked else
                               "the tall shadow: violet-black paper with a gilt edge, creased folds, a torn hem") +
                  "; root card, pivot at the hem's middle (0, 0)")


FACE_C = (0.0, 47.0)


def face(state):
    name = {"closed": "face", "open": "face_open", "cracked": "face_cracked"}[state]
    c = card(name, (-10.0, 37.0, 10.0, 57.0))
    fx, fy = FACE_C
    det = []
    for k in range(12):   # the compass dial: ticks, and N/E/S/W marks as small diamonds
        a = math.radians(90 - k * 30)
        r0, r1 = (6.0, 7.2) if k % 3 else (5.4, 7.2)
        det.append(stroke_svg(c.line([(fx + r0 * math.cos(a), fy + r0 * math.sin(a)),
                                      (fx + r1 * math.cos(a), fy + r1 * math.sin(a))]), INK, 2.2 if k % 3 else 3.4))
    det.append(stroke_svg(c.ell(fx, fy, 4.6, 4.6, 16), INK, 1.4))
    if state == "open":   # Hatesong: the dial splits, a dark howling mouth with gold teeth inside
        mouth = [(fx - 5.2, fy + 1.0), (fx, fy + 3.2), (fx + 5.2, fy + 1.0), (fx + 4.0, fy - 4.0), (fx, fy - 6.0),
                 (fx - 4.0, fy - 4.0)]
        det.append(f'<path d="{c.d(mouth, 0.5, 0.3, 7)}" fill="{VOID}"/>')
        for k in range(5):
            x = fx - 3.6 + k * 1.8
            det.append(fill_svg(poly_path(c.pts([(x - 0.6, fy + 1.6), (x + 0.6, fy + 1.6), (x, fy - 0.2)])), GOLD))
            det.append(fill_svg(poly_path(c.pts([(x - 0.5, fy - 4.6), (x + 0.5, fy - 4.6), (x, fy - 3.0)])), GOLD))
    if state == "cracked":
        for crack in ([(fx - 7.0, fy + 3.0), (fx - 2.0, fy + 1.0), (fx + 1.0, fy - 2.0), (fx + 6.4, fy - 4.0)],
                      [(fx - 2.0, fy + 1.0), (fx - 1.0, fy + 7.0)], [(fx + 1.0, fy - 2.0), (fx + 2.0, fy - 7.0)]):
            det.append(stroke_svg(c.line(crack), "#fff2b0", 5))
            det.append(stroke_svg(c.line(crack), "#ffffff", 2))
    c.shape(None, RIM, d=c.ell(fx, fy, 9.0, 9.0, 18), seed=8, lo=(3, 3), so=(4, 4), soft=0,
            hl=(0.36, 0.22, 0.26, 0.14))
    c.shape(None, FACE, det, d=c.ell(fx, fy, 7.6, 7.6, 18), seed=9, lo=(3, 3), so=(4, 4), soft=0,
            hl=(0.36, 0.22, 0.26, 0.12) if state == "closed" else None)
    return c.save(FACE_C, {"closed": "the face: a round compass in a gold rim, its dial ticked; the needle is "
                                     "its own card", "open": "Hatesong: the dial split open, a dark howling mouth "
                                     "with gold teeth", "cracked": "the face cracking with light (the shatter)"}[state] +
                  "; pivot at the dial's centre")


def needle():
    c = card("needle", (-2.0, -7.0, 2.0, 7.0))
    north = [(0.0, 6.4), (0.9, 0.0), (-0.9, 0.0)]
    south = [(0.0, -6.0), (0.8, 0.0), (-0.8, 0.0)]
    c.shape(north, dict(fill="#d8303a", light="#f06a6a", shadow="#9a1824", line="#2a0608"), seed=10, tension=0.05,
            lo=(1, 1), so=(1.5, 1.5), soft=0)
    c.shape(south, dict(fill="#3a3448", light="#524a62", shadow="#24202e", line="#0e0614"), seed=11, tension=0.05,
            lo=(1, 1), so=(1.5, 1.5), soft=0)
    c.shape(None, RIM, d=c.ell(0.0, 0.0, 0.9, 0.9, 8), seed=12, lo=(1, 1), so=(1, 1), soft=0)
    return c.save((0.0, 0.0), "the compass needle (red north); spin it about its pivot, the dial's centre; stop it "
                              "pointing at the target for Doom Foretold")


def arm(front):
    name = "arm_front" if front else "arm_back"
    t = PAGE if front else PAGE_B
    c = card(name, (-30.0, 10.0, 2.0, 42.0))
    # an arm of folded pages: an accordion of leaves from the shoulder (0, 38) out to a flat page-hand
    pts_top, pts_bot = [], []
    n = 7
    for k in range(n + 1):
        t_ = k / n
        x = -t_ * 22.0
        y = 38.0 - t_ * 14.0
        w = 2.4 - 0.8 * t_
        z = 0.9 if k % 2 else -0.9   # the zigzag of the folds
        pts_top.append((x + z * 0.4, y + w + z))
        pts_bot.append((x - z * 0.4, y - w + z))
    hand = [(-21.0, 27.5), (-25.0, 30.5), (-29.0, 28.0), (-29.4, 22.0), (-27.0, 15.0), (-23.0, 13.0),
            (-21.0, 16.0), (-20.4, 21.0)]
    pts = pts_top + hand + list(reversed(pts_bot))
    det = []
    for k in range(1, n):   # fold creases and a few lines of writing on each leaf
        x, y = -k / n * 22.0, 38.0 - k / n * 14.0
        det.append(stroke_svg(c.line([(x + 0.3, y + 2.6), (x - 0.3, y - 2.6)]), t["shadow"], 2.5))
    for k in range(5):
        y = 26.0 - k * 2.4
        det.append(stroke_svg(c.line([(-27.6, y), (-22.4, y + 0.3)]), INK, 1.4))
    det.append(gold_edge(c, pts, 1.4))
    c.shape(pts, t, det, seed=13 if front else 14, tension=0.25, jitter=0.4, lo=(3, 3), so=(4, 4), soft=0)
    return c.save((0.0, 38.0), ("the front arm" if front else "the back arm, a tone darker") +
                  ": an accordion of folded pages ending in a flat page-hand with writing on it; "
                  "pivot at the shoulder (0, 38)")


def stub():
    c = card("arm_stub", (-8.0, 32.0, 2.0, 42.0))
    pts = [(1.0, 40.4), (-3.0, 39.6), (-5.6, 38.4), (-6.4, 36.6), (-5.0, 35.6), (-6.6, 34.6), (-3.0, 35.2),
           (1.0, 35.6)]
    c.shape(pts, PAGE, [gold_edge(c, pts, 1.4)], seed=15, tension=0.3, jitter=0.4, lo=(2, 2), so=(3, 3), soft=0)
    return c.save((0.0, 38.0), "arm lost: a torn stump of pages at the shoulder; swap for the arm until it regrows")


def shard(i):
    c = card(f"light_shard_{i}", (-3.0, -3.0, 3.0, 3.0))
    rnd = random.Random(20 + i)
    pts = [(rnd.uniform(-2.6, -1.4), rnd.uniform(0.8, 2.4)), (rnd.uniform(0.6, 2.4), rnd.uniform(1.2, 2.6)),
           (rnd.uniform(1.4, 2.6), rnd.uniform(-2.4, -0.6)), (rnd.uniform(-1.6, 0.0), rnd.uniform(-2.6, -1.6))]
    c.shape(pts, LIGHT, seed=21 + i, tension=0.05, jitter=0.2, lo=(2, 2), so=(2, 2), soft=0)
    return c.save((0.0, 0.0), "a shard of the shattered Nightmare, glowing; throw them out and fade them")


ORDER = ["arm_back", "body", "arm_front", "face", "needle"]
REST = {"arm_back": (3.0, 38.0), "arm_front": (-3.0, 37.0), "face": FACE_C, "needle": FACE_C, "body": (0.0, 0.0)}


def preview():
    for n, xy in REST.items():
        R.meta[n]["model_xy"] = list(xy)
    for n in ("face_open", "face_cracked"):
        R.meta[n]["model_xy"] = list(FACE_C)
    R.meta["body_cracked"]["model_xy"] = [0.0, 0.0]
    R.meta["arm_stub"]["model_xy"] = [-3.0, 37.0]
    Wd, Hd = 1000, 300
    cv = Image.new("RGBA", (Wd, Hd), (200, 192, 200, 255))
    d = ImageDraw.Draw(cv)
    poses = [("idle", {}, ORDER), ("Hatesong", {"face": "face_open"}, ["arm_back", "body", "arm_front", "face"]),
             ("arm lost", {"arm_front": "arm_stub"}, ORDER), ("cracking", {"face": "face_cracked", "body": "body_cracked"},
                                                           ["arm_back", "body", "arm_front", "face"])]
    for i, (label, ov, order) in enumerate(poses):
        im = Image.new("RGBA", (180, 250), (0, 0, 0, 0))
        for name in order:
            file = ov.get(name, name)
            card_im = Image.open(os.path.join(OUT, file + ".png")).convert("RGBA")
            px, py = R.meta[file]["pivot"]
            mx, my = R.meta[name]["model_xy"]
            im.alpha_composite(card_im, (round(130 + mx * 4 - px), round(240 - my * 4 - py)))
        cv.alpha_composite(im, (10 + i * 200, 30))
        d.text((20 + i * 200, 12), label, fill=(30, 20, 40, 255))
    d.rectangle([0, 270, Wd, 272], fill=(120, 110, 120, 255))
    d.rectangle([900, 270 - 96, 903, 270], fill=(60, 140, 60, 255))
    d.text((908, 180), "Luigi (24 units)", fill=(40, 90, 40, 255))
    x = 820
    for i in range(3):
        im = Image.open(os.path.join(OUT, f"light_shard_{i}.png")).convert("RGBA")
        cv.alpha_composite(im, (x + i * 28, 40))
    d.text((820, 70), "light shards", fill=(30, 20, 40, 255))
    d.text((10, 282), "The Nightmare (c_sl_nightmare), 1:1 at 4 texels/unit", fill=(30, 20, 40, 255))
    return cv


def build():
    clean(OUT)
    body()
    body(cracked=True)
    for s in ("closed", "open", "cracked"):
        face(s)
    needle()
    arm(True)
    arm(False)
    stub()
    for i in range(3):
        shard(i)
    pv = preview()
    R.write_json({"draw_order": ORDER, "rest_note": "model_xy is each card's pivot at rest"})
    pv.convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
