"""The Drama Slam's three painted sets (Super Luigi ch4): flat backdrops for the stage battle.

Run: python ch4_sets.py -> ../ch4_sets/*.svg, *.png, parts.json, preview.png
(The ballroom and the volcano are painted designs: see painted() below.)
ch4.md, New assets 4: "flat painted backdrops (a country road with a cardboard sun; a ballroom; a
volcano that 'erupts': a flame card that rises) ... as battle stage props (BattleStageObjectData
models), made in the stage-prop style of the game's stages (flat cards)".
The backdrops are scenery canvases, so they follow STYLE.md R10 (map textures): power-of-two sizes,
flat tones (3 to 5 per material), no outlines, a dark drop line along the lower edge of each shape,
opaque. The road is 512x256 at the game's 8 texels a unit (sl_rig's default): a flat 64 units wide and
32 tall, the width of the battle stage's back. The painted ballroom and volcano are the same flat at
cutout.py's density (2048x1024, 32 texels a unit), and so is the cardboard sun cut from its design; the
volcano's flame is a drawn prop card at 8. parts.json gives each card its texels_per_unit.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, poly_path, ellipse_pts, star_pts, stroke_svg, fill_svg, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch4_sets")
W, H = 512, 256
meta = {}


def note(name, w, h, pivot, desc, tpu=8):
    meta[name] = {"size": [w, h], "pivot": list(pivot), "texels_per_unit": tpu, "desc": desc}


def density():
    import cutout
    return cutout.DENSITY


def flat(p, pts, colour, drop=None, smooth=True, tension=0.5, seed=1, drop_w=2.5):
    d = smooth_path(pts, tension=tension, jitter=0.6, seed=seed) if smooth else poly_path(pts)
    if drop:   # R10: a dark drop line two to three texels deep under the shape
        p.raw(f'<path d="{d}" fill="{drop}" transform="translate(0 {drop_w})"/>')
    p.raw(fill_svg(d, colour))
    return d


def frame(p):
    """the flat's wooden battens, top and bottom, so it reads as stage scenery"""
    for y0, y1 in ((0, 8), (H - 8, H)):
        p.raw(f'<rect x="0" y="{y0}" width="{W}" height="{y1 - y0}" fill="#8a5a2e"/>')
        p.raw(f'<rect x="0" y="{y0 + (y1 - y0) - 2}" width="{W}" height="2" fill="#5e3a18"/>')
        for x in range(24, W, 96):
            p.raw(fill_svg(smooth_path(ellipse_pts(x, (y0 + y1) / 2, 1.6, 1.6, 6)), "#3e2410"))


def road():
    p = Part("set_road", W, H)
    rnd = random.Random(4)
    p.raw(f'<rect width="{W}" height="{H}" fill="#9cc8ec"/>')
    p.raw(f'<rect y="{H * 0.42}" width="{W}" height="{H}" fill="#b4d8f2"/>')   # paler near the horizon
    for cx, cy, s in ((90, 52, 1.0), (300, 38, 1.3), (440, 70, 0.8)):   # flat painted clouds
        pts = []
        for k in range(14):
            a = 2 * math.pi * k / 14
            r = (1.0 if k % 2 else 0.78) * s
            pts.append((cx + 36 * r * math.cos(a), cy + 13 * r * math.sin(a) * (0.7 if math.sin(a) > 0 else 1.2)))
        flat(p, pts, "#f6f8fa", drop="#c8d8e8", seed=int(cx))
    # far hills, near hills
    far = [(0, 150)] + [(x, 132 - 22 * math.sin(x / 70) - rnd.uniform(0, 6)) for x in range(0, W + 1, 32)] + [(W, 150), (W, H), (0, H)]
    flat(p, far, "#7cb45a", drop="#5e9440", seed=5)
    near = [(0, 176)] + [(x, 168 - 14 * math.sin(x / 55 + 1.6)) for x in range(0, W + 1, 32)] + [(W, H), (0, H)]
    flat(p, near, "#5ea042", drop="#447c2e", seed=6)
    # light patches on the hills (blobs, not crescents)
    for cx, cy in ((80, 160), (210, 150), (380, 145), (470, 190), (140, 210)):
        flat(p, ellipse_pts(cx, cy, 26, 6, 10), "#86c066" if cy < 170 else "#72b252", seed=cx)
    # the road, winding from the front edge to the horizon
    rd = [(196, H), (316, H), (284, 214), (300, 186), (276, 160), (262, 144), (256, 136), (250, 144), (254, 160),
          (262, 186), (226, 214)]
    flat(p, rd, "#d2a868", drop="#9c7440", tension=0.4, seed=7)
    for y in (230, 200, 172, 152):   # flat tyre-rut stripes
        p.raw(stroke_svg(f"M{256 - (H - y) * 0.0 - 10} {y} l-6 8", "#b88e52", 3))
    # a fence along the near hill and two round trees
    for x in range(20, 190, 28):
        p.raw(f'<rect x="{x}" y="{178 + (x % 3)}" width="5" height="22" fill="#8a5a2e"/>')
    p.raw(stroke_svg("M14 188 L196 186", "#8a5a2e", 4))
    for tx, ty, r in ((400, 150, 24), (446, 160, 18)):
        p.raw(f'<rect x="{tx - 3}" y="{ty}" width="7" height="{r + 6}" fill="#6e4422"/>')
        flat(p, ellipse_pts(tx, ty - r * 0.4, r, r * 0.9, 12, wobble=0.1, seed=tx), "#3e8a34", drop="#2a6424",
             seed=tx)
        flat(p, ellipse_pts(tx - r * 0.3, ty - r * 0.7, r * 0.45, r * 0.3, 10), "#5aa848", seed=tx + 1)
    frame(p)
    p.save(OUT)
    note("set_road", W, H, (256, 256), "act 1: a country road between green hills, under a blue sky; "
                                       "pivot at the bottom middle, on the stage floor at the back")


# The ballroom and the volcano are the style model's painted backdrops (wave 3, ART_PLAN rows 6 and 7), not drawn
# here: designs/sets/. The ballroom is ballroom_seed1.png (SDXL + ttydstyle, "painted stage backdrop") with its
# painted chandelier taken out by no_chandelier() below (chapter 4's chandelier is a prop of its own that drops;
# Qwen's edit for it tripped the GPU's 80 C stop three times); the volcano is volcano_quiet_s1.png, Qwen's edit of volcano_seed1.png with the plume and smoke taken out
# (the flame card erupts from the crater). Each is cropped to its band, flattened a little (median 3, 48 colours),
# kept opaque (R10), given the road's battens and put through the game's CMPR.
DESIGNS = os.path.join(os.path.dirname(__file__), "..", "designs", "sets")


def no_chandelier(im):
    """ballroom_seed1 without its chandelier: the wall behind it is horizontal bands (the ceiling, the cornice, the
    valance), so each row of the hole runs from the colour at its left side to the colour at its right; the rows are
    then bent down into the cornice's U (the ceiling's rim, lowest in the middle) and softened into each other."""
    import numpy as np
    a = np.asarray(im).astype(float)
    h = a.shape[0]
    poly = [(556, 0), (1018, 0), (1018, 205), (972, 245), (836, 272), (792, 328), (738, 328), (694, 272), (566, 245),
            (556, 205)]
    m = Image.new("L", im.size)
    ImageDraw.Draw(m).polygon(poly, fill=255)
    hole = np.asarray(m) > 0
    fill = a.copy()
    for y in range(h):
        xs = np.where(hole[y])[0]
        if not len(xs):
            continue
        x0, x1 = xs[0] - 1, xs[-1] + 1
        left, right = a[y, max(0, x0 - 5):x0 + 1].mean(0), a[y, x1:x1 + 6].mean(0)
        t = (np.arange(x0 + 1, x1) - x0) / (x1 - x0)
        fill[y, x0 + 1:x1] = left[None] * (1 - t[:, None]) + right[None] * t[:, None]
    res = fill.copy()
    x0, x1, sag = 556, 1018, 22
    for x in range(x0, x1 + 1):
        dy = int(round(sag * math.sin(math.pi * (x - x0) / (x1 - x0))))
        ys = np.where(hole[:, x])[0]
        res[ys, x] = fill[np.maximum(0, ys - dy), x]
    k = 9
    pad = np.pad(res, ((k // 2, k // 2), (0, 0), (0, 0)), mode="edge")
    soft = sum(pad[i:i + h] for i in range(k)) / k
    res[hole] = soft[hole]
    return Image.fromarray(np.clip(res + 0.5, 0, 255).astype(np.uint8))


def painted(name, src, box, desc, repair=None):
    import numpy as np
    from PIL import ImageFilter
    from ink import cmpr_round_trip
    k = density()   # the road's 512x256 flat, k times the texels
    im = Image.open(os.path.join(DESIGNS, src)).convert("RGB")
    if repair:
        im = repair(im)
    im = im.crop(box).resize((W * k, H * k), Image.LANCZOS)
    im = im.filter(ImageFilter.MedianFilter(MEDIAN)).quantize(48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    im = im.convert("RGBA")
    d = ImageDraw.Draw(im)
    for y0, y1 in ((0, 8), (H - 8, H)):   # the flat's wooden battens, as frame() draws the road's
        d.rectangle((0, y0 * k, W * k - 1, y1 * k - 1), fill="#8a5a2e")
        d.rectangle((0, (y1 - 2) * k, W * k - 1, y1 * k - 1), fill="#5e3a18")
        for x in range(24, W, 96):
            cx, cy, r = x * k, (y0 + y1) / 2 * k, 1.6 * k
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill="#3e2410")
    img = cmpr_round_trip(np.asarray(im))
    img[..., 3] = 255
    Image.fromarray(img, "RGBA").save(os.path.join(OUT, name + ".png"))
    note(name, W * k, H * k, (256 * k, 256 * k), desc, tpu=8 * k)


def ballroom():
    painted("set_ballroom", "ballroom_seed1.png", BALLROOM_BOX,
            "act 2: a painted ballroom, gold walls, red drapes across starry windows, a checkered floor, from the "
            "style model; pivot at the bottom middle", repair=no_chandelier)


def volcano():
    painted("set_volcano", "volcano_quiet_s1.png", VOLCANO_BOX,
            "act 3: a painted volcano on a sunset sky over lava, from the style model; the flame card rises out "
            "of its crater at %d, %d" % CRATER_PX)


BALLROOM_BOX = (0, 0, 1536, 768)
VOLCANO_BOX = (0, 200, 1024, 790)   # the letterboxed band is rows 157 to 855; 590 rows keep it near 2:1
CRATER_PX = (238, 75)   # the crater's middle, (475, 372) in the design, in the road's 512x256 (x density in the PNG)
MEDIAN = 3   # the painted sets' flattening, in texels


def sun():
    """act 1's cardboard sun, cut from the style model's design (designs/props/sun_seed1.png: SDXL + ttydstyle, "a
    cardboard sun on a wooden stick, a stage prop, a round smiling face with a ring of pointed rays"; ART_PLAN row 9:
    the recipe's sun had no face): the background flooded off, the line warmed, cut by cutout.py, its stick's foot on
    the card's pivot, (64, 188) of 128x192 as the drawn one was"""
    import numpy as np
    import cutout
    from ink import cmpr_round_trip
    fig = cutout.warm_line(cutout.figure(Image.open(os.path.join(DESIGNS, "..", "props", "sun_seed1.png")), 30),
                           "#5a2e04")
    rays = [(475 + 250 * math.cos(2 * math.pi * i / 48), 392 + 262 * math.sin(2 * math.pi * i / 48)) for i in range(48)]
    stick = [(458, 600), (494, 600), (494, 1020), (458, 1020)]
    m = Image.new("L", fig.size)
    d = ImageDraw.Draw(m)
    d.polygon(rays, fill=255)
    d.polygon(stick, fill=255)
    poly_img = np.asarray(m) > 0
    fig = fig.copy()
    fa = np.asarray(fig).copy()
    fa[~poly_img, 3] = 0
    c = fa[..., :3].astype(int)
    grey = (c.max(-1) - c.min(-1) < 45) & (c.mean(-1) > 110)   # the wall seen between the rays, and the paper's white edge
    fa[grey, 3] = 0
    fig = Image.fromarray(fa, "RGBA")
    k = cutout.DENSITY   # 4.8 design pixels a game texel (8 a unit), k texels each
    img, piv = cutout.cut(fig, {"poly": [(0, 0), (fig.size[0], 0), fig.size, (0, fig.size[1])], "pivot": (476, 1018)},
                          4.8 / k, "#5a2e04", 0.35)
    cw, ch, pvx, pvy = 128 * k, 192 * k, 64 * k, 188 * k
    out = np.zeros((ch, cw, 4), np.uint8)
    ox, oy = int(round(pvx - piv[0])), int(round(pvy - piv[1]))
    h, w = img.shape[:2]
    ys, xs = slice(max(0, oy), min(ch, oy + h)), slice(max(0, ox), min(cw, ox + w))
    out[ys, xs] = img[ys.start - oy:ys.stop - oy, xs.start - ox:xs.stop - ox]
    Image.fromarray(cmpr_round_trip(out), "RGBA").save(os.path.join(OUT, "cardboard_sun.png"))
    note("cardboard_sun", cw, ch, (pvx, pvy), "act 1's cardboard sun on a stick, smiling, from the style model; a "
                                              "stagehand raises it behind the hills; pivot at the stick's foot", tpu=8 * k)


def flame(frame):
    name = f"volcano_flame_{frame}"
    p = Part(name, 128, 192, grow=1.6, inner=1.8, bias=(1.4, 1.6))
    rnd = random.Random(30 + frame)
    tips = [(24, 70), (44, 20), (64, 6 if frame == 0 else 14), (86, 26 if frame == 0 else 12), (104, 64)]
    pts = [(34, 188)]
    for i, (tx, ty) in enumerate(tips):
        pts.append((tx + rnd.uniform(-3, 3), ty))
        if i < len(tips) - 1:
            nx, ny = tips[i + 1]
            pts.append(((tx + nx) / 2, max(ty, ny) + 22 + rnd.uniform(-4, 4)))
    pts += [(94, 188)]
    core = [(44, 188), (50, 110), (64, 70 if frame == 0 else 80), (78, 112), (84, 188)]
    p.shape(smooth_path(pts, tension=0.5, jitter=0.6, seed=31 + frame), "#ff8a24", "#5a1604", "#ffc040", "#e04a1a",
            (6, 6), (7, 7), soft=2, details=[fill_svg(smooth_path(core, tension=0.5), "#ffe27a")])
    p.save(OUT)
    note(name, 128, 192, (64, 188), f"the eruption's flame card, frame {frame}: it rises out of the crater "
                                     "(slide it up from behind the cone) and flickers between the two frames")


def _at8(n):
    """a card at the game's 8 texels a unit, as the preview lays them out"""
    im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
    k = meta[n]["texels_per_unit"] / 8
    return im if k == 1 else im.resize((round(im.width / k), round(im.height / k)), Image.LANCZOS)


def preview():
    c = Image.new("RGBA", (1060, 860), (60, 50, 56, 255))
    d = ImageDraw.Draw(c)
    for i, n in enumerate(("set_road", "set_ballroom", "set_volcano")):
        im = _at8(n)
        x, y = 10 + (i % 2) * 530, 24 + (i // 2) * 290
        c.alpha_composite(im, (x, y))
        d.text((x, y - 16), n, fill=(255, 255, 255, 255))
        if n == "set_road":
            s = _at8("cardboard_sun")
            c.alpha_composite(s, (x + 330, y + 256 - 188 - 40))
        if n == "set_volcano":
            f = Image.open(os.path.join(OUT, "volcano_flame_0.png")).convert("RGBA")
            tmp = Image.new("RGBA", im.size, (0, 0, 0, 0))
            tmp.alpha_composite(f, (CRATER_PX[0] - 64, CRATER_PX[1] - 150))
            c.alpha_composite(tmp, (x, y))
    x = 560
    for n in ("cardboard_sun", "volcano_flame_0", "volcano_flame_1"):
        im = _at8(n)
        c.alpha_composite(im, (x, 640))
        d.text((x, 836), n, fill=(255, 255, 255, 255))
        x += 150
    d.text((10, 840), "The Drama Slam's sets at 8 texels a unit. Previews: the sun raised behind the hills; "
                      "the flame risen from the crater", fill=(255, 255, 255, 255))
    return c


def build():
    clean(OUT)
    road()
    ballroom()
    volcano()
    sun()
    flame(0)
    flame(1)
    import json
    with open(os.path.join(OUT, "parts.json"), "w", encoding="utf-8") as fh:
        json.dump({"for": "the Drama Slam's battle stage props", "pivot_note": "pixels from the PNG's top-left",
                   "cards": meta}, fh, indent=1)
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(meta))
