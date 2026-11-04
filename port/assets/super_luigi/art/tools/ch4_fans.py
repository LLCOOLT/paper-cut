"""Chapter 4's optional props (Super Luigi): the fans' paper leaves and their "WE ♥ THE GRASS"
banner, Dazzlina's tiara of sequins, and the Baron's tiny top hat.

Run: python ch4_fans.py -> ../ch4_fans/*.svg, *.png, parts.json, preview.png
ch4.md, New assets 5-6 (optional): "Dazzlina's sparkle: the Amazy Dayzee at 1.2x with a tiara of
sequins (texture or a small prop)"; "Paper leaves for the fans (a held prop) and the 'WE ♥ THE
GRASS' banner (a texture)"; Petunia Primrose's "pink petals (texture, optional)"; and Hayzee's battle poses: "the Baron's costume (a tiny top hat: a prop,
optional)". 8 texels/unit, outlined, lit from the top left. The banner's letters are lettering.py's
hand-cut capitals, painted.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, Rig, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, clean  # noqa
import lettering as lt  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch4_fans")
R = Rig("sl_ch4_fans", OUT)

LEAF = dict(fill="#6cbc3e", light="#9cd86a", shadow="#4a9226", line="#163a0c")
LEAF_Y = dict(fill="#a6cc44", light="#c8e47a", shadow="#7ea42a", line="#283a08")
STICK = dict(fill="#c8a06a", light="#e2c08a", shadow="#9a7444", line="#3a240a")
PAPER = dict(fill="#f2ead2", light="#fcf6e6", shadow="#d2c49e", line="#3e3218")
TAPE = "#d8e0e4"
INK_G = "#3a9a2a"
INK_G_DARK = "#1e5a14"
INK_R = "#e0304a"
SILVER = dict(fill="#d4d8e2", light="#f2f4f8", shadow="#a2a8b8", line="#262a38")
SEQUINS = ("#f25a9a", "#4ac8e8", "#f8d23a", "#9a6ae0", "#5ad87a")
TOPHAT = dict(fill="#3a3440", light="#5a5262", shadow="#24202a", line="#0c0a10")
HATBAND = dict(fill="#8a3ab0", light="#ac62d0", shadow="#62228a", line="#220a30")


# ------------------------------------------------------------------ fans --
def stick(c, top):
    c.shape([(-0.32, 0.2), (0.32, 0.2), (0.3, top), (-0.3, top)], STICK, seed=1, tension=0.1, lo=(1, 1), so=(1.5, 1.5),
            soft=0.6, grow=1.2, inner=1.4, bias=(1.0, 1.2))


def paper_leaf():
    c = R.card("paper_leaf", (-2.5, 0.0, 2.5, 8.0))
    stick(c, 3.4)
    # a broad paper leaf, pointed, with a midrib and three pairs of veins drawn in
    pts = [(0.0, 2.6), (-1.4, 3.4), (-2.2, 4.8), (-2.0, 6.2), (-1.0, 7.2), (0.2, 7.8), (1.2, 7.0), (2.1, 5.6),
           (2.0, 4.2), (1.2, 3.2)]
    det = [stroke_svg(c.curve((0.0, 3.0), (-0.1, 5.2), (0.2, 7.4)), LEAF["shadow"], 2)]
    for y in (4.2, 5.2, 6.2):
        det.append(stroke_svg(c.curve((0.0, y - 0.4), (-0.7, y), (-1.4, y + 0.2)), LEAF["shadow"], 1.4))
        det.append(stroke_svg(c.curve((0.05, y - 0.4), (0.7, y), (1.4, y + 0.2)), LEAF["shadow"], 1.4))
    c.shape(pts, LEAF, det, hl=(0.38, 0.26, 0.24, 0.16), seed=2, tension=0.5)
    return c.save((0.0, 1.0), "a fan's paper leaf on a stick, held up and waved; pivot at the grip (0, 1)")


def paper_grass():
    c = R.card("paper_grass", (-2.5, 0.0, 2.5, 8.0))
    stick(c, 3.0)
    for i, (tx, ty, tone) in enumerate(((-1.8, 7.0, LEAF_Y), (1.9, 7.2, LEAF), (0.1, 7.8, LEAF))):
        bx = 0.1 * (i - 1)
        pts = [(bx - 0.9, 2.6), (bx + (tx - bx) * 0.5 - 0.5, 5.0), (tx, ty), (bx + (tx - bx) * 0.5 + 0.5, 5.0),
               (bx + 0.9, 2.6)]
        c.shape(pts, tone, [stroke_svg(c.curve((bx, 2.8), ((bx + tx) / 2, 5.0), (tx * 0.9, ty - 0.6)),
                                       tone["shadow"], 1.4)], seed=3 + i, tension=0.4, lo=(2, 2), so=(2, 2), soft=1)
    return c.save((0.0, 1.0), "a fan's paper grass, three blades on a stick (the Grass's own fans); pivot at the grip")


def banner():
    c = R.card("banner_grass", (-18.0, -4.5, 18.0, 4.5))
    # a long strip of paper, cut by hand (a ragged edge), taped up at the corners
    top = [(-17.0, 3.6), (-8.0, 3.9), (0.0, 3.7), (8.0, 4.0), (17.0, 3.6)]
    bot = [(17.2, -3.4), (8.0, -3.8), (0.0, -3.6), (-8.0, -3.9), (-17.2, -3.4)]
    pts = top + [(17.4, 0.2)] + bot + [(-17.4, 0.0)]
    U = c.U
    cx, cy = c.u(0.0, -2.0)
    text, sp = "WE ♥ THE GRASS", 0.6
    size = min(4.4, 31.0 / lt.width(text, 1.0, sp)) * U
    left = cx - lt.width(text, size, sp) / 2
    step = lambda t: lt.width(t, size, sp) + (lt.TRACK + sp) * size / lt.H   # the pen after `t` and its gap
    kw = dict(shadow=INK_G_DARK, shadow_off=(1.4, 1.6), anchor="start", jitter=0.4, tilt=6, bob=0.06, spacing=sp)
    det = [lt.text_svg("WE", left, cy, size, INK_G, seed=11, **kw),
           lt.text_svg("THE GRASS", left + step("WE ♥ "), cy, size, INK_G, seed=13, **kw),
           # the heart painted red, a little too big, as fans would
           lt.text_svg("♥", left + step("WE ") - 0.06 * size, cy + 0.08 * size, size * 1.12, INK_R,
                       shadow="#8a1426", shadow_off=(1.4, 1.6), anchor="start", seed=12, jitter=0.3, tilt=8)]
    det += [fill_svg(c.ell(x, y, 0.5, 0.3, 8), INK_G) for x, y in ((-13.6, -2.8), (9.4, 2.6), (14.2, -2.9))]   # drips, spots
    c.shape(pts, PAPER, det, hl=(0.36, 0.26, 0.24, 0.16), hl_alpha=0.6, seed=10, tension=0.15, jitter=0.5,
            lo=(5, 5), so=(6, 6))
    for x, y, r in ((-16.6, 3.2, 35), (16.6, 3.2, -35)):   # tape at the top corners
        tape = c.pts([(x - 1.4, y - 0.5), (x + 1.4, y - 0.5), (x + 1.4, y + 0.5), (x - 1.4, y + 0.5)])
        px, py = c.u(x, y)
        c.p.raw(f'<path d="M{"L".join("%.1f %.1f" % q for q in tape)}Z" fill="{TAPE}" fill-opacity="0.9" '
                f'stroke="#7a8288" stroke-width="1.2" transform="rotate({r} {px:.1f} {py:.1f})"/>')
    return c.save((0.0, 3.8), 'the fans\' banner, "WE ♥ THE GRASS" painted on a strip of paper, taped at the '
                              "top corners; pivot at the middle of its top edge (held up by two fans, or hung)")


# ------------------------------------------------------------- costumes --
def tiara():
    c = R.card("dazzlina_tiara", (-3.0, 0.0, 3.0, 4.0), grow=1.2, inner=1.5, bias=(1.0, 1.2))
    band = [(-2.8, 0.6), (-1.4, 1.2), (1.4, 1.2), (2.8, 0.6), (2.6, 1.6), (1.6, 2.4), (0.6, 2.6), (0.0, 3.6),
            (-0.6, 2.6), (-1.6, 2.4), (-2.6, 1.6)]
    det = []
    for i, (x, y) in enumerate(((-2.2, 1.3), (-1.4, 1.8), (-0.6, 2.0), (0.6, 2.0), (1.4, 1.8), (2.2, 1.3),
                                (0.0, 2.8))):
        det.append(fill_svg(c.ell(x, y, 0.36, 0.36, 8), SEQUINS[i % len(SEQUINS)]))
        det.append(fill_svg(c.ell(x - 0.12, y + 0.12, 0.12, 0.12, 6), "#ffffff"))
    c.shape(band, SILVER, det, hl=(0.4, 0.3, 0.3, 0.3), seed=20, tension=0.4, lo=(2, 2), so=(2, 2), soft=1)
    # a sparkle star over the peak
    c.shape(star_pts(*c.u(0.0, 3.4), 6, 2.4, n=4, rot=-90), dict(fill="#fff2a0", light="#ffffff", shadow="#f2c840",
                                                                line="#4a3006"),
            d=smooth_path(star_pts(*c.u(0.0, 3.4), 7, 2.6, n=4, rot=-90), tension=0.1), seed=21, lo=(1, 1),
            so=(1, 1), soft=0.6, grow=1.0, inner=1.2, bias=(0.8, 1.0))
    return c.save((0.0, 0.6), "Dazzlina's tiara of sequins for the Amazy Dayzee (scale it 1.2x with her); "
                              "pivot at its foot, on her head between the petals")


def baron_hat():
    c = R.card("baron_hat", (-2.5, 0.0, 2.5, 5.0))
    brim = [(-2.3, 0.4), (-1.0, 0.1), (1.0, 0.1), (2.3, 0.4), (2.1, 1.1), (1.0, 0.9), (-1.0, 0.9), (-2.1, 1.1)]
    crown = [(-1.4, 0.8), (1.4, 0.8), (1.6, 4.4), (0.8, 4.7), (-0.8, 4.7), (-1.6, 4.4)]
    c.shape(crown, TOPHAT, hl=(0.3, 0.22, 0.14, 0.2), hl_alpha=0.22, seed=30, tension=0.2)
    c.shape([(-1.45, 1.0), (1.45, 1.0), (1.5, 1.9), (-1.5, 1.9)], HATBAND, seed=31, tension=0.15, lo=(1, 1),
            so=(1, 1), soft=0.6)
    c.shape(brim, TOPHAT, seed=32, tension=0.4, lo=(1, 1), so=(1.5, 1.5), soft=0.8)
    return c.save((0.0, 0.6), "the Baron's tiny top hat (Hayzee in the play), black with a purple band; "
                              "pivot at the brim's middle, set on top of his head card, tipped a little")


# --------------------------------------------------------------- Petunia --
PINK = dict(fill="#f6c2da", light="#fde4f0", shadow="#de94b6", line="#4a1630")


def petunia_petals():
    """Petunia Primrose's pink petals: a 112x128 replacement for the Crazee Dayzee's petal texture
    (c_pansy image 8), drawn new in its layout: four round petals (up, left, right, down) round the
    face's place, each outlined, with a soft sheen."""
    p = Part("petunia_petals", 112, 128, grow=2.4, inner=2.6, bias=(1.6, 1.8))
    for cx, cy, rx, ry, seed in ((58, 28, 25, 22, 1), (58, 101, 24, 24, 2), (30, 62, 22, 30, 3), (86, 62, 22, 30, 4)):
        d = smooth_path(ellipse_pts(cx, cy, rx, ry, 16, wobble=0.03, seed=seed), tension=0.5, jitter=0.4, seed=seed)
        p.shape(d, PINK["fill"], PINK["line"], PINK["light"], PINK["shadow"], (5, 5), (6, 6), soft=2.5,
                hl=(0.4, 0.3, 0.26, 0.18))
    p.save(OUT)


# --------------------------------------------------------------- preview --
def preview():
    W, H = 860, 220
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    x = 10
    for n in ("banner_grass", "paper_leaf", "paper_grass", "dazzlina_tiara", "baron_hat", "petunia_petals"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        k = 1 if n in ("banner_grass", "petunia_petals") else 2
        im = im.resize((im.width * k, im.height * k), Image.NEAREST)
        c.alpha_composite(im, (x, 40))
        d.text((x, 22), n + ("" if k == 1 else " x2"), fill=(40, 30, 30, 255))
        x += im.width + 18
    d.text((10, 200), "Chapter 4's optional props, 1:1 at 8 texels/unit unless marked", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    paper_leaf()
    paper_grass()
    banner()
    tiara()
    baron_hat()
    petunia_petals()
    R.write_json()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
