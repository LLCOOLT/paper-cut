"""Luigi's drawn faces and tab: the stand-ins (fallbacks) for what the game makes from the disc.

Luigi's faces in the game's Mario icons (ICON_1A3, 19E, 1A1), level-up's BP panel and the pause
menu's first tab are made on the player's machine from the disc's own images
(../recipes/luigi_icons.recipe and luigi_tabs.recipe, RECIPES.md): nothing from the disc is in the
repo. This script draws, in the paper style, without the disc:
  drawn/luigi_hud, luigi_head, luigi_emblem   the three icons' fallbacks
  drawn/ui/levelup                           level-up's BP panel's
  drawn/ui/tab_us                            the "Luigi" tab's (luigi_tab_jp.py draws drawn/ui/tab_jp)
Each face is Luigi's head drawn in the shape of c_luigi's head as it stands in the field, laid out
as the recipe lays out the disc's head: our own outlines and colours, nothing from the disc.

Run: python luigi_icons.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image, ImageFilter  # noqa
from papercut import Part, harden, stroke_svg  # noqa
from icons import Fig, render, heart, sticker  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "icons", "luigi")

CAP = dict(fill="#39a639", light="#6cc34e", shadow="#2a8030", line="#182810")     # c_luigi's cap
SKIN = dict(fill="#f0bc88", light="#f8d4a8", shadow="#d89c68", line="#5a2c10")
HAIR = dict(fill="#5a3418", light="#74482a", shadow="#42240e", line="#1e0e04")
MOUSTACHE = dict(fill="#3e2210", light="#583418", shadow="#2a1608", line="#140802")
DISC = dict(fill="#f2eeee", light="#faf8f8", shadow="#d8d2d4", line="#182810")
EYE = "#1e140c"
# the emblem: everything in the coin's golds, the line a dark gold-brown
GOLD = dict(fill="#e0b020", light="#f2cc48", shadow="#b88a10", line="#5a3606")
GOLD_IN = dict(fill="#e8bc30", light="#f2cc48", shadow="#c49618", line="#6a4008")


# ------------------------------------------------------------ Luigi's head --
# Drawn in the shape of c_luigi's head as it stands in the field (pose Z_1, facing left), in a
# 96x104 box at the disc's 8 texels a unit: each part where his model puts that card, the same
# layout luigi_icons.recipe builds from the disc. Our own outlines and colours; nothing from the disc.
HEAD_W, HEAD_H = 96, 104
NAPE = [(72.9, 85.9), (74.0, 91.3), (68.6, 92.9), (64.6, 95.9), (59.5, 94.8), (56.2, 90.8), (56.1, 85.9),
        (57.1, 81.6), (60.1, 78.2), (64.6, 76.2), (71.5, 74.0), (70.9, 82.3)]
UNDER_BRIM = [(49.9, 38.8), (40.4, 45.9), (32.2, 46.0), (24.0, 46.0), (17.5, 44.9), (6.1, 38.8), (16.4, 32.1),
              (28.1, 31.1), (38.1, 33.1)]
FACE = [(70.9, 68.3), (69.7, 76.0), (66.2, 82.9), (61.9, 90.0), (58.5, 99.0), (52.0, 101.5), (42.0, 100.0),
        (36.3, 97.5), (29.1, 91.8), (23.3, 85.0), (20.0, 76.8), (20.2, 68.3), (21.2, 60.2), (23.1, 51.6),
        (25.0, 39.2), (33.7, 30.1), (46.2, 33.1), (56.0, 38.1), (64.6, 43.1), (74.1, 48.1), (75.9, 58.7)]
SIDEBURN = [(58, 58), (66, 55), (71, 62), (68, 73), (60, 72)]
EYES = [(28.6, 50.5, 3.0, 6.8), (43.2, 52.5, 3.0, 6.8)]
TACHE_PTS = [(53.9, 78.8), (50.4, 84.0), (45.6, 86.9), (41.1, 87.9), (37.1, 86.9), (33.6, 84.8), (31.5, 82.9),
             (24.7, 85.9), (16.1, 84.8), (12.2, 78.8), (13.5, 72.0), (28.0, 74.1), (36.0, 74.1), (38.6, 73.1),
             (49.2, 68.1), (53.9, 72.5)]
NOSE = [(35.8, 63.0), (34.8, 71.4), (27.8, 76.0), (20.3, 77.0), (13.1, 75.6), (7.1, 70.6), (4.1, 63.0), (6.7, 55.1),
        (12.8, 50.0), (20.3, 49.2), (27.8, 50.0), (34.0, 55.1)]
CAP_SHAPE = [(76.8, 30.5), (89.9, 42.7), (91.0, 58.6), (66.9, 50.6), (58.6, 49.8), (52.3, 47.8), (47.3, 46.0),
             (42.6, 43.9), (37.2, 41.6), (27.1, 38.7), (7.1, 30.5), (23.1, 21.0), (25.6, 11.1), (33.1, 4.0),
             (42.7, 1.1), (52.3, 2.0), (60.0, 7.0), (66.0, 11.7), (70.9, 17.0), (74.0, 23.5)]
CAP_DISC = (39.4, 20.6, 10.0, 9.6)
L_MARK = [(34.4, 15.2), (38.2, 15.2), (38.2, 22.6), (43.6, 22.6), (43.6, 26.2), (34.4, 26.2)]
EAR = [(91.8, 70.4), (90.0, 76.3), (85.9, 81.0), (79.8, 83.9), (72.0, 83.8), (68.1, 77.2), (68.0, 70.4),
       (70.1, 64.8), (73.0, 58.7), (79.8, 56.2), (86.9, 58.1), (91.9, 63.4)]
NECK_CUT = [(0, 116), (0, 97), (30, 97), (36, 98), (42, 100), (50, 101), (56, 99), (60, 96), (62, 92), (96, 92),
            (96, 116)]

# the head's colours: his own, and each gold face's (flat fills, one line)
TONES = dict(cap=CAP, skin=SKIN, hair=HAIR, tache=MOUSTACHE, disc=DISC, ell=CAP["fill"], eye=EYE, shade=True)


def gold_tones(fill, line, dark=None, light=None):
    """flat tones for a gold face: skin and the L's disc in `light` (else `fill`), cap and hair in
    `dark` (else `fill`), the moustache in `dark`, every line, the eyes and the L in `line`"""
    t = lambda c: dict(fill=c, light=None, shadow=None, line=line)
    lt, dk = t(light or fill), t(dark or fill)
    return dict(cap=dk, skin=lt, hair=dk, tache=dk, disc=lt, ell=line, eye=line, shade=False)


def head_image(tones, k=4, right=False):
    """Luigi's head (HEAD_W x HEAD_H, facing left) drawn at k x, the neck cut off under the chin;
    right: its L mirrored, for a face that is flipped to look right"""
    from papercut import smooth_path, ellipse_pts, fill_svg, poly_path
    from PIL import ImageDraw
    p = Part("head", HEAD_W * k, HEAD_H * k)
    lw = 3.0 * k
    q = lambda pts: [(x * k, y * k) for x, y in pts]

    def shape(pts, tone, details=(), seed=1, d=None, line=lw):
        sh = tones["shade"]
        p.shape(d or smooth_path(q(pts), tension=0.5, jitter=0.1, seed=seed), tone["fill"], tone["line"],
                tone.get("light") if sh else None, tone.get("shadow") if sh else None, (2 * k, 2 * k), (2 * k, 2 * k),
                details=details, soft=k * 0.6 if sh else 0, grow=line * 0.5, inner=line * 0.55,
                bias=(line * 0.15, line * 0.2))

    def oval(cx, cy, rx, ry, n=14):
        return smooth_path(ellipse_pts(cx * k, cy * k, rx * k, ry * k, n), tension=0.5)
    dark = dict(fill=tones["eye"], light=None, shadow=None, line=tones["eye"])
    shape(UNDER_BRIM, dark, seed=40)
    shape(NAPE, tones["hair"], seed=41)
    shape(FACE, tones["skin"], [fill_svg(smooth_path(q(SIDEBURN), tension=0.5), tones["hair"]["fill"])], seed=42)
    for cx, cy, rx, ry in EYES:
        p.raw(fill_svg(oval(cx, cy, rx, ry, 10), tones["eye"]))
    shape(TACHE_PTS, tones["tache"], seed=43)
    shape(NOSE, tones["skin"], seed=44)
    shape(CAP_SHAPE, tones["cap"], seed=45)
    shape(None, tones["disc"], [fill_svg(poly_path(q([(2 * CAP_DISC[0] - x, y) for x, y in L_MARK] if right else L_MARK)), tones["ell"])], d=oval(*CAP_DISC), line=lw * 0.6)
    shape(EAR, tones["skin"], [stroke_svg("M%.1f %.1f Q%.1f %.1f %.1f %.1f" % (86 * k, 64 * k, 76 * k, 64 * k, 80 * k,
                                                                         77 * k), tones["skin"]["line"], 1.6 * k)],
          seed=46, line=lw * 0.8)
    im = render(p)
    cut = Image.new("L", im.size, 0)
    ImageDraw.Draw(cut).polygon(q(NECK_CUT), fill=255)
    im.paste((0, 0, 0, 0), (0, 0), cut)
    return im


def grow_line(im, r, colour):
    """a border r px wide round the opaque shape, in the colour (as a recipe's `grow`)"""
    a = im.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
    big = a.filter(ImageFilter.MaxFilter(2 * r + 1))
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(Image.new("RGBA", im.size, colour), (0, 0), big)
    out.alpha_composite(im)
    return out


def pad(im, l, t, r, b):
    out = Image.new("RGBA", (im.width + l + r, im.height + t + b), (0, 0, 0, 0))
    out.paste(im, (l, t))
    return out


def shrink(im, w, h):
    """down to w x h, box-filtered, 1-bit alpha"""
    return harden(im.resize((w, h), Image.BOX))


def posterise(im, stops):
    """each opaque texel to the colour of the first stop its lightness (0..255) is under:
    [(limit, colour), ...] (the recipes' stepped `ramp`)"""
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            l = 0.299 * r + 0.587 * g + 0.114 * b
            for lim, c in stops:
                if l <= lim:
                    px[x, y] = tuple(int(c[i:i + 2], 16) for i in (1, 3, 5)) + (255,)
                    break
    return im


def hex4(c):
    return tuple(int(c[i:i + 2], 16) for i in (1, 3, 5)) + (255,)


def coin(f):
    """the emblem's gold coin: a dark rim, a lighter face"""
    f.oval(50, 50, 48, 48, GOLD, hl=(0.36, 0.24, 0.3, 0.16))


# ------------------------------------------------------- the menu textures --
# Named as Dolphin names a replacement (tex1_WxH_<XXH64 of the CMPR data>_14), for the "*#luigi"
# texture set (luigi_faces_aurora.c).
UI = os.path.join(OUT, "ui")
DRAWN_UI = os.path.join(OUT, "drawn", "ui")
LEVELUP = "tex1_128x128_4d465d3c205b172e_14"   # battle_common.tpl image 51 (JP 38): level-up's BP panel
TILE = dict(fill="#d4aa3c", light="#e8c45a", shadow="#a8822a", line="#4a3006")


def tab(p):
    """the pause menu's first tab, as Mario's (win.tpl image 1): a red board with a darker ragged
    edge, "Luigi" in chunky pale-pink cut letters with a deeper pink drop below right"""
    from papercut import smooth_path
    board = smooth_path([(3, 4), (48, 2), (93, 4), (94, 20), (93, 36), (48, 38), (3, 36), (2, 20)], tension=0.3,
                        jitter=0.6, seed=51)
    p.shape(board, "#cc4a36", "#a01a04", "#d8604a", "#b03a26", (3, 3), (3, 3), soft=2.0, grow=1.6, inner=1.6,
            bias=(0.6, 0.8))
    # glyphs on a 12-unit grid (x-height from 4, the g's tail to 15.5): L u i g i
    bars = {
        "L": (7, [(0, 0, 3, 12), (0, 9, 7, 12)]),
        "u": (8, [(0, 4, 3, 12), (5, 4, 8, 12), (0, 9.2, 8, 12)]),
        "i": (3, [(0, 4.4, 3, 12), (0, 0.4, 3, 3)]),
        "g": (8, [(0, 4, 8, 6.6), (0, 4, 3, 11), (5, 4, 8, 15.5), (0, 8.6, 8, 11), (0, 13, 8, 15.5)]),
    }
    s, x0, y0 = 2.05, 13.0, 7.5
    word = "Luigi"
    import random
    rnd = random.Random(7)
    layers = []
    x = x0
    for ch in word:
        w, rects = bars[ch]
        tilt, bob = rnd.uniform(-0.06, 0.06), rnd.uniform(-0.6, 0.6)
        polys = []
        for (a, b, c, d) in rects:
            pts = [(a, b), (c, b), (c, d), (a, d)]
            pts = [(px + rnd.uniform(-0.25, 0.25), py + rnd.uniform(-0.25, 0.25)) for px, py in pts]
            polys.append([(x + (px + (py - 6) * tilt) * s, y0 + bob + py * s) for px, py in pts])
        layers.append(polys)
        x += (w + 1.4) * s
    path = "".join("M" + " L".join("%.1f %.1f" % pt for pt in poly) + " Z" for polys in layers for poly in polys)
    shift = lambda dx, dy: "".join("M" + " L".join("%.1f %.1f" % (a + dx, b + dy) for a, b in poly) + " Z"
                                   for polys in layers for poly in polys)
    p.raw(f'<path d="{shift(1.6, 1.8)}" fill="#a82a1c" fill-rule="nonzero"/>')
    p.raw(f'<path d="{path}" fill="#b83a2c" stroke="#b83a2c" stroke-width="1.8" stroke-linejoin="round"/>')
    p.raw(f'<path d="{path}" fill="#f4dde0" fill-rule="nonzero"/>')


def levelup(p):
    """level-up's BP panel, as Mario's (battle_common.tpl image 51): a gold tile with a bevelled
    dark edge and on it a gold coin (Luigi's head goes on it in make_faces)"""
    from papercut import smooth_path
    tile = smooth_path([(6, 6), (64, 4), (122, 6), (124, 64), (122, 122), (64, 124), (6, 122), (4, 64)],
                       tension=0.25, jitter=0.4, seed=61)
    p.shape(tile, TILE["fill"], TILE["line"], TILE["light"], TILE["shadow"], (6, 6), (7, 7), soft=3.0, grow=3.0,
            inner=3.0, bias=(1.2, 1.6))
    coin(Fig(p, 16.5, 17.5, 0.94, line=4.0))


def make_ui():
    """the drawn tab into drawn/ui/, the fallback of its recipe"""
    os.makedirs(DRAWN_UI, exist_ok=True)
    p = Part("tab_us", 96, 40)
    tab(p)
    im = harden(render(p))
    with open(os.path.join(DRAWN_UI, "tab_us.svg"), "w", encoding="utf-8") as fh:
        fh.write(p.svg())
    im.save(os.path.join(DRAWN_UI, "tab_us.png"))
    return ["tab_us"]


# ------------------------------------------------------------ the faces --
# The fallbacks of luigi_icons.recipe's faces, laid out as the recipe lays out the disc's head: the
# same box, flip, tilt, scale and place, and the same frames round it. K: the head's drawing scale.
K = 4


def flipped(im):
    return im.transpose(Image.FLIP_LEFT_RIGHT)


def face_hud():
    """ICON_1A3, 52x52: the head facing right, tilted 15 degrees forward, the HP heart behind it,
    the pair one sticker"""
    h = grow_line(head_image(TONES, K, right=True), 2 * K, SKIN["line"])
    h = flipped(pad(h, 14 * K, 12 * K, 14 * K, 12 * K))
    h = h.rotate(-15, resample=Image.BICUBIC)
    h = shrink(h, 36, 37)
    h = posterise_dark(h)
    h = grow_line(h, 1, "#2a1408")
    p = Part("luigi_hud", 56, 56)
    heart(Fig(p, 23, 6, 0.22, line=2.6))
    art = harden(render(p)).crop((0, 0, 52, 52))
    art.alpha_composite(h, (0, 5))
    return sticker(art, 52)


def posterise_dark(im):
    """the soft edge of the line taken down to the line's dark brown, as the HUD recipe does"""
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a >= 128 and max(r, g, b) < 80:
                px[x, y] = (0x2a, 0x14, 0x08, 255)
    return im


def face_menu():
    """ICON_19E, 40x40: the head facing right, filling the square, a heavy line"""
    h = grow_line(pad(head_image(TONES, K, right=True), 3 * K, 3 * K, 3 * K, 3 * K), 3 * K, SKIN["line"])
    out = Image.new("RGBA", (40, 40), (0, 0, 0, 0))
    out.alpha_composite(shrink(flipped(h), 36, 39), (2, 0))
    return out


def gold_head(fill, line, dark=None, light=None, right=True):
    h = head_image(gold_tones(fill, line, dark, light), K, right)
    return grow_line(pad(h, 2 * K, 2 * K, 2 * K, 2 * K), 2 * K, line)


def face_emblem():
    """ICON_1A1, 40x40: a gold coin, the head on it in the coin's line, posterised to its tones"""
    p = Part("luigi_emblem", 40, 40)
    coin(Fig(p, 0, 0, 0.4, line=2.4))
    out = harden(render(p))
    h = shrink(flipped(gold_head("#e7b200", "#421000")), 31, 33)
    out.alpha_composite(posterise(h, [(115, "#421000"), (149, "#b07c00"), (999, "#e7b200")]), (4, 4))
    return out


def face_levelup():
    """level-up's BP panel, 128x128: the tile and coin, the head in one brown and its line, a darker
    gold shadow under it"""
    p = Part("levelup", 128, 128)
    levelup(p)
    out = harden(render(p))
    h = shrink(flipped(gold_head("#b26a00", "#521800")), 70, 76)
    h = posterise(h, [(63, "#521800"), (90, "#8c4c00"), (999, "#b26a00")])
    shadow = Image.new("RGBA", h.size, (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", h.size, hex4("#8c6900")), (0, 0), h.getchannel("A"))
    out.alpha_composite(shadow, (32, 29))
    out.alpha_composite(h, (29, 26))
    return out


def make_faces():
    os.makedirs(DRAWN_UI, exist_ok=True)
    done = []
    for path, fn in ((os.path.join(OUT, "drawn", "luigi_hud.png"), face_hud),
                     (os.path.join(OUT, "drawn", "luigi_head.png"), face_menu),
                     (os.path.join(OUT, "drawn", "luigi_emblem.png"), face_emblem),
                     (os.path.join(DRAWN_UI, "levelup.png"), face_levelup)):
        im = harden(fn())
        im.save(path)
        done.append(os.path.basename(path))
    return done


if __name__ == "__main__":
    print(", ".join(make_ui() + make_faces()))
