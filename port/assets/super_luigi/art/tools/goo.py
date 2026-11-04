"""The goo (Super Luigi ch6): the smoking green pit in the hall and the moat in jin_70.

Run: python goo.py -> ../goo/*.svg, *.png, preview.png
ch6.md, New assets 7: "the pit in the hall (a smoking green surface prop with a bubbling texture) and
the moat in jin_70 (the same material, a wide plane)". The Chestnut King's drool is the same goo.
The surface is a map texture (STYLE.md R10): a power-of-two tile that wraps both ways, opaque, 3 to 5
flat tones close in hue, light blobs and a dark drop line under each, no outlines. Two frames, so
it can swap slowly (or scroll) to churn. The bubbles that rise and pop are 1-bit cards; the smoke
over it is an effect texture with soft alpha (R10: effects use IA4).
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, poly_path, ellipse_pts, stroke_svg, fill_svg, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "goo")
BASE, LIGHT, DARK, DEEP, GLINT = "#5aa82a", "#86cc3e", "#3a7a1c", "#24520e", "#c4ee70"
LINE = "#1a3a08"
N = 128


def wrapped(p, pts, colour, drop=None):
    """draw a closed blob at every wrap offset, so the tile repeats seamlessly"""
    for dx in (-N, 0, N):
        for dy in (-N, 0, N):
            q = [(x + dx, y + dy) for x, y in pts]
            d = smooth_path(q, tension=0.55, jitter=0.0)
            if drop:
                p.raw(f'<path d="{d}" fill="{drop}" transform="translate(0 2)"/>')
            p.raw(fill_svg(d, colour))


def surface(frame):
    p = Part(f"goo_surface_{frame}", N, N)
    p.raw(f'<rect width="{N}" height="{N}" fill="{BASE}"/>')
    rnd = random.Random(7)
    # slow swirls: dark troughs and light crests, shifted a little between the frames
    for k in range(7):
        cx, cy = rnd.uniform(0, N), rnd.uniform(0, N)
        rx, ry = rnd.uniform(14, 26), rnd.uniform(6, 10)
        sx = 3.0 * frame * (1 if k % 2 else -1)
        wrapped(p, ellipse_pts(cx + sx, cy, rx, ry, 10, rot=rnd.uniform(-20, 20), wobble=0.2, seed=k), DARK)
        wrapped(p, ellipse_pts(cx + sx - rx * 0.3, cy - ry * 0.9, rx * 0.6, ry * 0.45, 8, wobble=0.15, seed=k + 20),
                LIGHT)
    # bubbles sitting on the surface: a rim, a dark hollow and a glint
    rb = random.Random(30 + frame)
    for k in range(9):
        cx, cy, r = rb.uniform(0, N), rb.uniform(0, N), rb.uniform(3, 7)
        wrapped(p, ellipse_pts(cx, cy, r, r * 0.8, 10), LIGHT, drop=DEEP)
        wrapped(p, ellipse_pts(cx + r * 0.1, cy + r * 0.1, r * 0.6, r * 0.45, 8), DARK)
        wrapped(p, ellipse_pts(cx - r * 0.45, cy - r * 0.35, r * 0.18, r * 0.14, 6), GLINT)
    p.save(OUT)


def bubble(frame):
    """a bubble rising and popping: 0 swelling, 1 full, 2 popping"""
    p = Part(f"goo_bubble_{frame}", 32, 32, grow=1.0, inner=1.2, bias=(0.8, 1.0))
    if frame < 2:
        r = 8 if frame == 0 else 12
        p.shape(smooth_path(ellipse_pts(16, 30 - r, r, r * 0.92, 14), tension=0.5), BASE, LINE, LIGHT, DARK,
                (2, 2), (3, 3), soft=0.8, hl=(0.36, 0.26, 0.24, 0.16),
                details=[fill_svg(smooth_path(ellipse_pts(16 - r * 0.4, 30 - r * 1.4, r * 0.18, r * 0.14, 6)), "#ffffff")])
    else:
        for k in range(6):   # a ring of droplets thrown out
            a = math.radians(200 + k * 28)
            x, y = 16 + 11 * math.cos(a), 22 + 9 * math.sin(a)
            p.shape(smooth_path(ellipse_pts(x, y, 2.4, 2.2, 8), tension=0.5), BASE, LINE, LIGHT, DARK, (1, 1), (1, 1),
                    soft=0.4)
        p.shape(smooth_path([(6, 30), (10, 25), (16, 27), (22, 25), (26, 30)], tension=0.5), BASE, LINE, LIGHT, DARK,
                (1, 1), (1.5, 1.5), soft=0.5)
    p.save(OUT)


def smoke():
    """an effect texture: a soft pale-green wisp, for puffs drifting up off the goo"""
    w, h = 64, 64
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for cx, cy, r in ((32, 44, 14), (24, 30, 11), (38, 22, 9), (30, 12, 6)):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(200, 236, 170, 200))
    im = im.filter(ImageFilter.GaussianBlur(3))
    im.save(os.path.join(OUT, "goo_smoke.png"))


def preview():
    c = Image.new("RGBA", (560, 300), (40, 36, 44, 255))
    d = ImageDraw.Draw(c)
    for i in range(2):
        t = Image.open(os.path.join(OUT, f"goo_surface_{i}.png")).convert("RGBA")
        for x in range(2):
            for y in range(2):
                c.alpha_composite(t, (10 + i * 270 + x * 128, 24 + y * 128))
        d.text((10 + i * 270, 8), f"goo_surface_{i}, tiled 2x2", fill=(255, 255, 255, 255))
    x = 10
    for n in ("goo_bubble_0", "goo_bubble_1", "goo_bubble_2", "goo_smoke"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (x + 400, 120 if n != "goo_smoke" else 60))
        x += 36
    return c


def build():
    os.makedirs(OUT, exist_ok=True)
    clean(OUT)
    surface(0)
    surface(1)
    for f in range(3):
        bubble(f)
    smoke()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
