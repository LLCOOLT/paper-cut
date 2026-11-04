"""Hand-inked cards, as MANIFESTO.md sections 2 to 5 describe them. Nothing here reads the disc.

A card is built at 4x from shapes whose contours are placed point by point (never primitives, never mirrored). Its
line is inked along the contour, inside the cut edge: a line of discs whose radius swells and thins, heaviest where a
weight asks for it (at a join, under a chin). Shading is one of the game's three manners: flat with a hard second
tone (`tone`), or an airbrush that darkens toward a rim (`air`). `finish` takes it down to texels with 1-bit alpha
and through a CMPR round trip.

  c = Card(120, 192, line="#2b1712")
  body = c.shape([(x, y), (x, y, weight), ...], "#5a2350", width=5, seed=3, air=dict(dark="#3a1438", amount=0.8))
  body.tone([...], "#4a1c44")      # a hard second tone, clipped to the shape and under its line
  c.ink([(x, y, w), ...])          # an interior line, tapering as the widths say
  rows = c.finish()                # RGBA rows
"""
import math

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

SS = 4


def rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32)


def _spline(pts, closed, step=0.5):
    """Catmull-Rom through the points (centripetal), with each point's weight carried along: [(x, y, w)]. A point
    marked as a corner (x, y, w, "c") is met by both sides without a tangent across it: soft, as a knife turns."""
    p = [(q[0], q[1], q[2] if len(q) > 2 else 1.0, len(q) > 3) for q in pts]
    n = len(p)
    out = []
    segs = n if closed else n - 1
    for i in range(segs):
        def at(j):
            return p[j % n] if closed else p[max(0, min(n - 1, j))]
        p0, p1, p2, p3 = at(i - 1), at(i), at(i + 1), at(i + 2)
        if p1[3]:
            p0 = (2 * p1[0] - p2[0], 2 * p1[1] - p2[1], p1[2], False)
        if p2[3]:
            p3 = (2 * p2[0] - p1[0], 2 * p2[1] - p1[1], p2[2], False)
        d = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        k = max(2, int(d / step))
        for s in range(k):
            t = s / k

            def cr(a, b, c, e):
                return 0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - e) * t * t + (3 * b - a - 3 * c + e) * t * t * t)
            out.append((cr(p0[0], p1[0], p2[0], p3[0]), cr(p0[1], p1[1], p2[1], p3[1]),
                        p1[2] + (p2[2] - p1[2]) * (t * t * (3 - 2 * t))))
    if not closed:
        out.append((p[-1][0], p[-1][1], p[-1][2]))
    return np.array(out, dtype=np.float64)


def _swell(n, seed, amount=0.42):
    """the pen's pressure along the contour, slow and uneven: a mean of 1, 1 +- amount"""
    r = np.random.RandomState(seed)
    u = np.arange(n) / max(1, n)
    v = np.zeros(n)
    for k, a in ((1, 1.0), (2, 0.8), (3, 0.5), (5, 0.25)):
        v += a * np.sin(2 * math.pi * (k * u + r.rand()))
    v /= max(1e-6, np.abs(v).max())
    return 1.0 + amount * v


class Shape:
    def __init__(self, card, mask, line_mask, inside):
        self.card, self.mask, self.line_mask, self.inside = card, mask, line_mask, inside

    def tone(self, pts, colour, soft=0.0):
        """a hard second tone (or, with soft in texels, a soft-edged one) over the shape's fill, under its line"""
        c = self.card
        m = c._poly(_spline(pts, True))
        if soft:
            m = m.filter(ImageFilter.GaussianBlur(soft * SS))
        a = np.asarray(m, dtype=np.float32) / 255 * self.inside
        c._paint(a, rgb(colour))
        c._paint(self.line_mask, rgb(c.line))
        return self

    def ink(self, pts, closed=False):
        """an interior line kept inside this shape"""
        self.card.ink(pts, closed, clip=self.mask)
        return self


class Card:
    def __init__(self, w, h, line="#2b1712"):
        self.w, self.h, self.line = w, h, line
        self.rgb = np.zeros((h * SS, w * SS, 3), dtype=np.float32)
        self.a = np.zeros((h * SS, w * SS), dtype=np.float32)

    def _poly(self, sp):
        im = Image.new("L", (self.w * SS, self.h * SS), 0)
        ImageDraw.Draw(im).polygon([(x * SS, y * SS) for x, y, _w in sp], fill=255)
        return im

    def _discs(self, sp, radius):
        im = Image.new("L", (self.w * SS, self.h * SS), 0)
        d = ImageDraw.Draw(im)
        for (x, y, _w), r in zip(sp, radius):
            if r > 0.02:
                x, y, r = x * SS, y * SS, r * SS
                d.ellipse((x - r, y - r, x + r, y + r), fill=255)
        return im

    def _paint(self, a, colour):
        a = a[..., None]
        self.rgb = self.rgb * (1 - a) + colour * a

    def shape(self, pts, fill, width=4.5, seed=1, swell=0.42, air=None, ramp=None):
        """A closed shape: its fill, an airbrush if asked, its line along the inside of the cut edge.
        air: dict(dark=colour, amount=0..1, to=(dx, dy) the side it falls toward in texels, blur=texels)
        ramp: dict(a=colour at the top left, b=colour at the bottom right): one broad fall over the whole shape"""
        sp = _spline(pts, True)
        mask = self._poly(sp)
        m = np.asarray(mask, dtype=np.float32) / 255
        r = width * sp[:, 2] * _swell(len(sp), seed, swell)
        band = self._discs(sp, r)
        line = np.asarray(ImageChops.multiply(band, mask), dtype=np.float32) / 255
        inside = m * (1 - line)
        self._paint(m, rgb(fill))
        if ramp:
            yy, xx = np.mgrid[0:self.h * SS, 0:self.w * SS]
            ys, xs = np.nonzero(m > 0.5)
            dx, dy = ramp.get("dir", (0.6, 0.8))
            t = (xx * dx + yy * dy).astype(np.float32)
            lo, hi = t[ys, xs].min(), t[ys, xs].max()
            t = np.clip((t - lo) / max(1.0, hi - lo), 0, 1)[..., None]
            self.rgb = np.where(m[..., None] > 0, rgb(ramp["a"]) * (1 - t) + rgb(ramp["b"]) * t, self.rgb)
        if air:
            dx, dy = air.get("to", (5.0, 6.0))
            blur = air.get("blur", 9.0)
            amount = air.get("amount", 0.8)
            off = ImageChops.offset(mask, int(-dx * SS), int(-dy * SS)).filter(ImageFilter.GaussianBlur(blur * SS))
            s = 1 - np.asarray(off, dtype=np.float32) / 255
            s = np.clip(s * amount * m, 0, 1)
            self._paint(s, rgb(air["dark"]))
        self._paint(line, rgb(self.line))
        self.a = np.maximum(self.a, m)
        return Shape(self, mask, line, inside)

    def ink(self, pts, closed=False, clip=None, colour=None):
        """an interior line: each point's third value is its width in texels, so it can start heavy and taper to nothing"""
        sp = _spline(pts, closed, step=0.35)
        band = self._discs(sp, sp[:, 2] / 2.0)
        if clip is not None:
            band = ImageChops.multiply(band, clip)
        self._paint(np.asarray(band, dtype=np.float32) / 255, rgb(colour or self.line))
        return self

    def finish(self):
        """RGBA rows at texel size: 1-bit alpha, clear texels black, through CMPR"""
        w, h = self.w, self.h
        a = self.a.reshape(h, SS, w, SS).mean(axis=(1, 3))
        col = (self.rgb * self.a[..., None]).reshape(h, SS, w, SS, 3).sum(axis=(1, 3))
        cov = self.a.reshape(h, SS, w, SS).sum(axis=(1, 3))[..., None]
        col = col / np.maximum(cov, 1e-3)
        opaque = a >= 0.5
        out = np.zeros((h, w, 4), dtype=np.uint8)
        out[..., :3] = np.clip(col + 0.5, 0, 255).astype(np.uint8) * opaque[..., None]
        out[..., 3] = opaque * 255
        return out


def cmpr_round_trip(img):
    """RGBA array through the game's encoding (tools/port/luigi_pack.py), as the pack will encode it"""
    import luigi_pack as LP
    rows = [[tuple(int(v) for v in px) for px in row] for row in img]
    h, w = img.shape[:2]
    pw, ph = (w + 3) // 4 * 4, (h + 3) // 4 * 4
    return np.array(LP.cmpr_decode(LP.cmpr_encode(rows), pw, ph), dtype=np.uint8).reshape(ph, pw, 4)[:h, :w]


def save(img, path):
    Image.fromarray(img, "RGBA").save(path)
