"""The three lenses, their sockets and their light beams (Super Luigi ch5).

Run: python lenses.py -> ../lenses/*.svg, *.png, parts.json, preview.png
ch5.md, New assets 5: "crescent, circle and teardrop discs of glass in stone rims (silver, gold,
blue), and the three sockets (stone stands) round the dais. Each lens's light beam (a tall
translucent cylinder of colour)". The Moon Lens is the crescent (silver), the Sun Lens the circle
(gold), the Tear Lens the teardrop (blue).
Lenses and sockets: outlined props at 8 texels/unit, lit from the top left, 1-bit alpha. The beams
are effect textures (STYLE.md R10: effects use IA4), so they keep a soft alpha ramp.
"""
import io
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, fill_svg, smooth_path, ellipse_pts, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "lenses")
R = Rig("sl_lenses", OUT)

STONE = dict(fill="#c8c2b4", light="#e2ddd2", shadow="#9a9384", line="#2e2a24")
STONE_DARK = "#7e776a"
RIMS = {"moon": dict(fill="#c4c8d4", light="#eceef4", shadow="#8a90a2", line="#1e2230"),
        "sun": dict(fill="#e6b43c", light="#f8da7c", shadow="#ae7e1c", line="#4a3006"),
        "tear": dict(fill="#4a7ad0", light="#86aeee", shadow="#2c52a0", line="#0c1a48")}
GLASS = {"moon": ("#dfe8f4", "#ffffff", "#aebcd2"), "sun": ("#fbe6a2", "#fffbe6", "#e8c46a"),
         "tear": ("#b6d6f4", "#eef7ff", "#80aee0")}
BEAM = {"moon": (226, 232, 246), "sun": (255, 214, 110), "tear": (120, 176, 255)}


def outline_pts(kind, cx, cy, r):
    """the lens's shape in model units: a crescent, a circle or a teardrop"""
    if kind == "sun":
        return ellipse_pts(cx, cy, r, r, 20)
    if kind == "tear":
        pts = []
        for i in range(24):
            a = math.pi / 2 + 2 * math.pi * i / 24
            k = 1.0 + 0.75 * max(0.0, math.sin(a)) ** 3   # drawn out to a point at the top
            pts.append((cx + r * 0.82 * math.cos(a) * (1 - 0.55 * max(0.0, math.sin(a)) ** 2), cy + r * k * math.sin(a) - r * 0.25))
        return pts
    # the crescent: an outer arc and an inner arc, opening to the upper right
    pts = []
    for i in range(15):
        a = math.radians(55 + 250 * i / 14)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    for i in range(12):
        a = math.radians(305 - 250 * i / 11)
        pts.append((cx + r * 0.62 + r * 0.5 * math.cos(a), cy + r * 0.2 + r * 0.5 * math.sin(a)))
    return pts


def lens(kind):
    c = R.card(f"lens_{kind}", (-3.5, -3.5, 3.5, 5.5))
    rim = RIMS[kind]
    g, gl, gs = GLASS[kind]
    pts = outline_pts(kind, 0.0, 0.0, 3.0)
    d = c.d(pts, 0.5, 0.2, 5)
    # the glass fills the shape; the metal rim is a band round its edge, inside the line
    det = [f'<path d="{c.d([(x * 0.55 - 0.6, y * 0.5 + 0.7) for x, y in pts], 0.5, 0.2, 4)}" fill="{gl}"/>',
           stroke_svg(c.line([(0.3, -1.4), (1.2, -0.5)]), gs, 2.5),
           f'<path d="{d}" fill="none" stroke="{rim["fill"]}" stroke-width="13"/>',
           f'<path d="{d}" fill="none" stroke="{rim["light"]}" stroke-width="3" transform="translate(2.5 2.5)"/>',
           fill_svg(c.ell(-1.0, 1.0, 0.32, 0.32, 8), "#ffffff")]
    c.shape(None, dict(fill=g, light=None, shadow=gs, line=rim["line"]), det, d=d, seed=5, lo=(3, 3), so=(5, 5),
            soft=1.5)
    return c.save((0.0, -3.0), f"the {dict(moon='Moon', sun='Sun', tear='Tear')[kind]} Lens: a "
                               f"{dict(moon='crescent', sun='round', tear='teardrop')[kind]} of glass in a "
                               f"{dict(moon='silver', sun='gold', tear='blue')[kind]} rim; pivot at its foot, set in the socket")


def socket(kind):
    c = R.card(f"socket_{kind}", (-5.5, -1.0, 5.5, 13.0))
    # a carved stone stand: a base, a waisted post and a cap whose front holds the lens's shape
    base = [(-5.0, 0.0), (5.0, 0.0), (4.6, 1.8), (-4.6, 1.8)]
    post = [(-3.0, 1.6), (3.0, 1.6), (2.2, 4.0), (2.2, 6.0), (3.2, 7.4), (-3.2, 7.4), (-2.2, 6.0), (-2.2, 4.0)]
    cap = [(-4.8, 7.2), (4.8, 7.2), (4.6, 12.2), (-4.6, 12.2)]
    c.shape(post, STONE, [stroke_svg(c.line([(-1.4, 2.4), (-1.4, 6.4)]), STONE["light"], 2)], seed=6, tension=0.3)
    c.shape(base, STONE, seed=7, tension=0.2, lo=(3, 3), so=(4, 4))
    hole = outline_pts(kind, 0.0, 9.6, 2.0)
    det = [f'<path d="{c.d(hole, 0.5, 0.2, 8)}" fill="#3a352e"/>',
           f'<path d="{c.d([(x + 0.3, y - 0.3) for x, y in hole], 0.5, 0.2, 9)}" fill="{STONE_DARK}"/>',
           stroke_svg(c.line([(-4.4, 8.0), (4.4, 8.0)]), STONE["shadow"], 2)]
    for s in (-1, 1):   # little Luff swirls carved on the cap's corners
        det.append(stroke_svg(c.ell(s * 3.6, 10.8, 0.45, 0.45, 8), STONE["shadow"], 1.6))
    c.shape(cap, STONE, det, hl=(0.34, 0.18, 0.26, 0.12), seed=10, tension=0.15)
    return c.save((0.0, 0.0), f"the {kind} socket: a stone stand whose cap holds an empty "
                              f"{dict(moon='crescent', sun='circle', tear='teardrop')[kind]}; the lens's pivot "
                              "goes at (0, 6.6), its shape over the recess; pivot at the base's middle")


def beam(kind):
    """an effect texture: the beam's colour with a bright core and an alpha ramp to soft edges, fading
    toward the top; map it round a tall cylinder (or a camera-facing card)"""
    w, h = 64, 256
    im = Image.new("RGBA", (w, h))
    px = im.load()
    r, g, b = BEAM[kind]
    for y in range(h):
        fade = 0.35 + 0.65 * (y / (h - 1)) ** 0.7       # brighter at the bottom, where it leaves the lens
        for x in range(w):
            t = abs((x + 0.5) - w / 2) / (w / 2)
            core = max(0.0, 1 - t / 0.25)
            a = (1 - t ** 2) * fade
            cr = int(r + (255 - r) * core * 0.8)
            cg = int(g + (255 - g) * core * 0.8)
            cb = int(b + (255 - b) * core * 0.8)
            px[x, y] = (cr, cg, cb, int(255 * min(1.0, a * 0.85 + core * 0.15)))
    name = f"beam_{kind}"
    im.save(os.path.join(OUT, name + ".png"))
    R.meta[name] = {"size": [w, h], "pivot": [32, 256], "model_xy": None,
                    "desc": f"the {kind} beam: an effect texture (soft alpha, like the game's IA4 effects); "
                            "map it round a tall cylinder from the lens to the ceiling, additive or alpha-blended; "
                            "its bottom is the bright end"}


def preview():
    W, H = 720, 330
    c = Image.new("RGBA", (W, H), (70, 66, 76, 255))
    d = ImageDraw.Draw(c)
    for i, kind in enumerate(("moon", "sun", "tear")):
        x = 120 + i * 220
        bm = Image.open(os.path.join(OUT, f"beam_{kind}.png")).convert("RGBA")
        c.alpha_composite(bm, (x - 32, 300 - 61 - 256 + 30))
        for n, ox, y_model in ((f"socket_{kind}", 0, 0.0), (f"lens_{kind}", 0, 6.6)):
            m = R.meta[n]
            im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
            c.alpha_composite(im, (round(x - m["pivot"][0]), round(300 - y_model * 8 - m["pivot"][1])))
        im = Image.open(os.path.join(OUT, f"lens_{kind}.png")).convert("RGBA")
        c.alpha_composite(im.resize((im.width * 2, im.height * 2), Image.NEAREST), (x + 40, 120))
        d.text((x - 60, 308), f"socket_{kind} + lens_{kind} + beam_{kind}", fill=(255, 255, 255, 255))
    d.text((8, 6), "The three lenses, 1:1 at 8 texels/unit (lens x2 beside each); beams are effect textures",
           fill=(255, 255, 255, 255))
    return c


def build():
    clean(OUT)
    for kind in ("moon", "sun", "tear"):
        lens(kind)
        socket(kind)
        beam(kind)
    R.write_json({"lens_in_socket": "the lens's pivot sits at the socket's (0, 6.6)"})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
