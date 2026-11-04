"""The drawing helpers of Luigi's drawn faces (luigi_icons.py): a figure in a 100x100 design box,
the HP heart, the SVG render and the HUD's sticker frame.

Measured on the game's own HUD icons (icon.tpl): the face and a pink HP heart with two eyes behind it
at the top right, the pair cut out as one sticker: a white border 2 to 3 px wide with a light grey
inner edge and a dark grey (#424242) rim 1 to 2 px heavier on the bottom right, 1-bit alpha.
"""
import io
import os
import sys

from PIL import Image, ImageChops, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
from papercut import smooth_path, ellipse_pts, stroke_svg, fill_svg, harden  # noqa

import resvg_py

HEART = dict(fill="#f2607a", light="#ff9aaa", shadow="#c83a56", line="#5a0a1c")


class Fig:
    """Shapes in a 100x100 design box, scaled k and offset (ox, oy) into a Part."""

    def __init__(self, p, ox, oy, k, line):
        self.p, self.ox, self.oy, self.k, self.lw = p, ox, oy, k, line

    def q(self, pts):
        return [(self.ox + x * self.k, self.oy + y * self.k) for x, y in pts]

    def shape(self, pts, tone, details=(), hl=None, tension=0.5, seed=1, d=None, light=True, lw=None):
        g = self.lw if lw is None else lw
        self.p.shape(d or smooth_path(self.q(pts), tension=tension, jitter=0.15, seed=seed), tone["fill"], tone["line"],
                     tone.get("light") if light else None, tone.get("shadow"), (max(1, 5 * self.k), max(1, 5 * self.k)),
                     (max(1.2, 5 * self.k), max(1.2, 5 * self.k)), details=details, soft=max(0.5, 2.0 * self.k),
                     hl=hl, grow=g * 0.5, inner=g * 0.55, bias=(g * 0.3, g * 0.35))

    def ell(self, cx, cy, rx, ry, n=12, rot=0):
        return smooth_path(ellipse_pts(self.ox + cx * self.k, self.oy + cy * self.k, rx * self.k, ry * self.k, n,
                                       rot=rot), tension=0.5)

    def oval(self, cx, cy, rx, ry, tone, details=(), hl=None, rot=0, lw=None):
        self.shape(None, tone, details, hl, d=self.ell(cx, cy, rx, ry, 14, rot), lw=lw)

    def dot(self, cx, cy, rx, ry, colour, rot=0):
        return fill_svg(self.ell(cx, cy, rx, ry, 10, rot), colour)

    def poly(self, pts, colour, tension=0.5):
        return fill_svg(smooth_path(self.q(pts), tension=tension), colour)

    def line(self, pts, colour, w):
        return stroke_svg("M" + " L".join("%.1f %.1f" % pt for pt in self.q(pts)), colour, w * self.k)

    def curve(self, a, b, c, colour, w):
        (ax, ay), (bx, by), (cx, cy) = self.q([a, b, c])
        return stroke_svg(f"M{ax:.1f} {ay:.1f} Q{bx:.1f} {by:.1f} {cx:.1f} {cy:.1f}", colour, w * self.k)


def heart(f):
    """the HP heart: pink, two eyes, behind the face at the top right"""
    pts = [(50, 96), (14, 62), (4, 34), (14, 10), (34, 6), (50, 22), (66, 6), (86, 10), (96, 34), (86, 62)]
    f.shape(pts, HEART, [f.dot(40, 40, 5, 11, "#7a1028"), f.dot(60, 40, 5, 11, "#7a1028")],
            hl=(0.3, 0.22, 0.2, 0.12), seed=20, tension=0.5)


def render(p):
    im = Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=p.svg())))).convert("RGBA")
    return im


def sticker(art, size):
    """the HUD sticker: a white border round the art's silhouette, a light grey inner edge and a dark
    grey rim heavier on the bottom right, as on the game's HUD icons"""
    a = art.getchannel("A").point(lambda v: 255 if v >= 128 else 0)

    def grow(m, r):
        return m.filter(ImageFilter.MaxFilter(2 * r + 1))
    white = grow(a, 3)
    shifted = Image.new("L", a.size, 0)
    shifted.paste(grow(a, 4), (1, 1))   # a shift, not a wrap (ImageChops.offset wraps round the edges)
    rim = ImageChops.lighter(grow(a, 4), shifted)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste((66, 66, 66, 255), (0, 0), rim)
    out.paste((236, 230, 236, 255), (0, 0), white)
    out.paste((189, 189, 189, 255), (0, 0), grow(a, 1))
    out.alpha_composite(art)
    return harden(out)
