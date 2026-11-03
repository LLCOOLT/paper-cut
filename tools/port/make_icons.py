#!/usr/bin/env python3
"""Draw Paper Cut's logo and write every platform's icon into port/assets/icon/.

The logo is drawn from shapes (no game assets), in the launcher's colours: a little
paper theatre, with a navy backdrop, red curtains tied back with gold, a valance with
gold trim and a wooden floor, and a big folded-paper star on a short stick filling the
stage opening. It's drawn in 1024 units across, supersampled and scaled down; each size
is drawn at that size, simpler as it gets smaller:
  128 px and up   full detail (curtain folds, backdrop stars, floor planks, star facets)
  48-127 px       flat colours, bigger star, thicker trim
  47 px and under bold shapes only: a star nearly filling the tile, no stick or rim

Each platform gets its own shape:
  Windows .ico, Android, Linux and the window icon: the full tile, rounded corners
  macOS .icns: the tile inset to 824/1024 on a transparent canvas, in a rounded square
               (a superellipse, like Apple's), with a soft shadow
  iOS: a full-bleed opaque square; iOS rounds it itself
The files are committed, so builds don't need this script or Pillow; run it
again after changing the drawing.

usage: python make_icons.py        (needs Pillow: pip install pillow)
"""
import io
import math
import os
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "port", "assets", "icon")

# the launcher's palette (port/settings/launcher_style.cpp)
RED_TOP = (150, 14, 28)
RED_BOTTOM = (214, 40, 48)
RED_DEEP = (120, 14, 26)
GOLD = (246, 196, 69)
GOLD_DARK = (176, 116, 20)
GOLD_TRIM = (204, 146, 30)
PAPER = (255, 247, 228)
PAPER_SHADE = (236, 214, 176)
WHITE = (255, 255, 255)
NAVY_TOP = (30, 36, 92)
NAVY_BOTTOM = (78, 48, 122)
WOOD_TOP = (150, 96, 52)
WOOD_BOTTOM = (92, 56, 28)
STAR_DOT = (255, 244, 200)
PLANK = (80, 46, 22)
SHADOW = (20, 6, 14)

TILE_RADIUS = 230  # the rounded tile's corner, in units
MAC_BODY = 824  # macOS: the tile's size on a 1024 canvas


# --------------------------------------------------------------------------- drawing helpers


class Canvas:
    """An opaque RGB drawing at size*ss pixels, in 1024 units across."""

    def __init__(self, size):
        self.size = size
        self.lod = 0 if size >= 128 else (1 if size >= 48 else 2)
        ss = 4 if size >= 256 else (8 if size >= 48 else 16)
        self.S = size * ss
        self.u = self.S / 1024.0
        self.img = Image.new("RGB", (self.S, self.S), NAVY_TOP)

    def px(self, n):
        """n output pixels, in units."""
        return n * 1024.0 / self.size

    def at_least(self, units, pixels):
        return max(units, self.px(pixels))

    def P(self, pts):
        return [(x * self.u, y * self.u) for x, y in pts]

    def mask(self, fill=0):
        return Image.new("L", (self.S, self.S), fill)

    def poly(self, pts):
        m = self.mask()
        ImageDraw.Draw(m).polygon(self.P(pts), fill=255)
        return m

    def rect(self, x0, y0, x1, y1):
        return self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])

    def ellipse(self, cx, cy, rx, ry=None):
        ry = rx if ry is None else ry
        m = self.mask()
        u = self.u
        ImageDraw.Draw(m).ellipse([(cx - rx) * u, (cy - ry) * u, (cx + rx) * u, (cy + ry) * u], fill=255)
        return m

    def rrect(self, x0, y0, x1, y1, r):
        m = self.mask()
        u = self.u
        ImageDraw.Draw(m).rounded_rectangle([x0 * u, y0 * u, x1 * u, y1 * u], radius=r * u, fill=255)
        return m

    def paint(self, color, mask, alpha=1.0):
        if alpha < 1.0:
            mask = mask.point(lambda v: int(v * alpha))
        self.img.paste(Image.new("RGB", (self.S, self.S), color), (0, 0), mask)

    def paint_image(self, rgb, mask):
        self.img.paste(rgb, (0, 0), mask)

    def gradient(self, top, bottom, y0=0, y1=1024, folds=0, fold_amp=0.0):
        """Top to bottom between rows y0..y1 (units), with optional vertical fold shading."""
        S = self.S
        col = Image.new("RGB", (1, S))
        span = max(1e-6, y1 - y0)
        for y in range(S):
            t = min(1.0, max(0.0, (y / self.u - y0) / span))
            col.putpixel((0, y), tuple(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3)))
        img = col.resize((S, S), Image.NEAREST)
        if folds:
            row = Image.new("L", (S, 1))
            for x in range(S):
                fold = 0.5 + 0.5 * math.cos(x / S * math.pi * 2 * folds)
                row.putpixel((x, 0), int(255 * (1 - fold_amp + fold_amp * fold)))
            img = ImageChops.multiply(img, row.resize((S, S), Image.NEAREST).convert("RGB"))
        return img

    def shadow(self, mask, dx, dy, blur, alpha, color=SHADOW):
        m = shift(mask, dx * self.u, dy * self.u)
        if blur * self.u >= 0.5:
            m = m.filter(ImageFilter.GaussianBlur(blur * self.u))
        self.paint(color, m, alpha)


def shift(mask, dx, dy):
    out = Image.new("L", mask.size, 0)
    out.paste(mask, (int(round(dx)), int(round(dy))))
    return out


def soften(mask, r):
    """Round a mask's corners by r pixels (blur, then a steep threshold that keeps antialiasing)."""
    if r < 0.75:
        return mask
    b = mask.filter(ImageFilter.GaussianBlur(r))
    return b.point(lambda v: max(0, min(255, (v - 128) * 4 + 128)))


def grow(mask, r):
    """Dilate (r > 0) or erode (r < 0) a mask by about |r| pixels."""
    if abs(r) < 0.5:
        return mask
    b = mask.filter(ImageFilter.GaussianBlur(abs(r) / 1.6))
    if r > 0:
        return b.point(lambda v: 255 if v > 12 else int(v * 255 / 12))
    return b.point(lambda v: 0 if v < 243 else int((v - 243) * 255 / 12))


def star_pts(cx, cy, R, inner=0.5, n=5):
    pts = []
    for i in range(2 * n):
        r = R if i % 2 == 0 else R * inner
        a = -math.pi / 2 + i * math.pi / n
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def faceted_star(c, cx, cy, R, clip):
    """A folded-paper star: each arm has a lit half and a shaded half."""
    pts = star_pts(cx, cy, R)
    lit, shade = c.mask(), c.mask()
    dl, ds = ImageDraw.Draw(lit), ImageDraw.Draw(shade)
    for i in range(0, len(pts), 2):
        left, tip, right = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
        dl.polygon(c.P([(cx, cy), left, tip]), fill=255)
        ds.polygon(c.P([(cx, cy), tip, right]), fill=255)
    c.paint(PAPER, ImageChops.multiply(lit, clip))
    c.paint(PAPER_SHADE, ImageChops.multiply(shade, clip))


def superellipse(c, x0, y0, x1, y1, n=5.0, steps=360):
    """Apple's rounded square, near enough: |x|^n + |y|^n = 1 over the box (units)."""
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    pts = []
    for i in range(steps):
        a = 2 * math.pi * i / steps
        ca, sa = math.cos(a), math.sin(a)
        pts.append((cx + rx * math.copysign(abs(ca) ** (2 / n), ca), cy + ry * math.copysign(abs(sa) ** (2 / n), sa)))
    return c.poly(pts)


# --------------------------------------------------------------------------- the logo


def draw_art(size, shape):
    """The theatre at size x size. shape: "tile" (rounded), "mac" (superellipse) or "square"."""
    c = Canvas(size)
    lod = c.lod
    if shape == "tile":
        outline = c.rrect(0, 0, 1024, 1024, TILE_RADIUS)
    elif shape == "mac":
        outline = superellipse(c, 0, 0, 1024, 1024)
    else:
        outline = c.mask(255)

    # the backdrop, with a few stars at full size
    c.paint_image(c.gradient(NAVY_TOP, NAVY_BOTTOM), c.mask(255))
    floor_y = 790 if lod < 2 else 860
    if lod == 0:
        for x, y, r in [(360, 300, 9), (650, 270, 12), (520, 210, 7), (440, 420, 6), (720, 440, 8)]:
            c.paint(STAR_DOT, c.poly(star_pts(x, y, r * 2.2, 0.35)), 0.75)

    # the floor: wood, planks at full size, a gold lip
    c.paint_image(c.gradient(WOOD_TOP, WOOD_BOTTOM, floor_y, 1024), c.rect(0, floor_y, 1024, 1024))
    if lod == 0:
        for y in (860, 930, 1000):
            c.paint(PLANK, c.rect(0, y, 1024, y + 5), 0.6)
    c.paint(GOLD_TRIM, c.rect(0, floor_y, 1024, floor_y + c.at_least(16, 1)))

    # the curtains: in at the top, pinched at the tie-back, flaring to the floor
    def edge(y):
        t = y / floor_y
        if lod == 2:
            return 300 - 170 * t
        tie = 0.62
        if t < tie:
            return 330 - 220 * (t / tie) ** 1.4
        return 110 + 90 * math.sin((t - tie) / (1 - tie) * math.pi / 2)

    left = [(0, 0)] + [(edge(floor_y * i / 60), floor_y * i / 60) for i in range(61)] + [(0, floor_y + 6)]
    right = [(1024 - x, y) for x, y in left]
    red = c.gradient(RED_TOP, RED_BOTTOM, folds=9 if lod == 0 else 0, fold_amp=0.22)
    for side in (left, right):
        m = c.poly(side)
        if lod < 2:
            c.shadow(m, 0, 10, 14, 0.4)
        c.paint_image(red, m)
    if lod < 2:  # the gold tie-backs
        for x in (110, 1024 - 110):
            c.paint(GOLD, c.ellipse(x, floor_y * 0.62, 30 if lod == 0 else c.px(1.6)))

    # the valance: scalloped at the bottom, with a gold trim line
    vh = 150 if lod < 2 else 130
    val = c.rect(0, 0, 1024, vh)
    if lod < 2:
        for i in range(7):
            val = ImageChops.lighter(val, c.ellipse((i + 0.5) * 1024 / 7, vh, 1024 / 14, 50))
    c.paint(RED_DEEP, val)
    c.paint(GOLD, c.rect(0, vh - 30, 1024, vh - 30 + c.at_least(18, 1)))

    # the star, filling the stage opening; bigger as the icon gets smaller
    cx, cy, R = {0: (512, 478, 318), 1: (512, 490, 360), 2: (512, 548, 470)}[lod]
    if lod < 2:  # a short stick from between the lower arms down into the floor
        bottom = cy + R * math.cos(math.radians(36))
        c.paint(GOLD_DARK, c.rrect(498, bottom - 120, 526, floor_y + 60, 10))
        c.paint(GOLD, c.rrect(498, bottom - 120, 512, floor_y + 60, 6))
    star = soften(c.poly(star_pts(cx, cy, R)), (12 if lod < 2 else 6) * c.u)
    c.shadow(star, 16 if lod < 2 else c.px(0.6), 22 if lod < 2 else c.px(0.8), 14 if lod < 2 else 0, 0.55)
    if lod == 0:
        c.paint(WHITE, grow(star, 10 * c.u))
        faceted_star(c, cx, cy, R * 1.05, star)
    else:
        c.paint(PAPER, star)

    # a gold rim around the tile (iOS draws its own edge, and small sizes have no room)
    if lod < 2 and shape != "square":
        c.paint(GOLD, ImageChops.subtract(outline, grow(outline, -(16 if lod == 0 else c.px(1.0)) * c.u)))

    out = c.img.convert("RGBA")
    out.putalpha(outline)
    return out.resize((size, size), Image.LANCZOS)


def draw_logo(size, shape="tile"):
    """The logo at size x size for a platform: "tile", "mac" or "square" (opaque RGB)."""
    if shape == "square":
        return draw_art(size, "square").convert("RGB")
    if shape == "tile":
        return draw_art(size, "tile")
    # macOS: the tile inset on a transparent canvas, with a soft shadow below it
    body = max(1, round(size * MAC_BODY / 1024))
    art = draw_art(body, "mac")
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    off = (size - body) // 2
    if size >= 64:
        a = Image.new("L", (size, size), 0)
        a.paste(art.getchannel("A"), (off, off + round(size * 10 / 1024)))
        a = a.filter(ImageFilter.GaussianBlur(size * 14 / 1024)).point(lambda v: int(v * 0.35))
        sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        sh.putalpha(a)
        canvas.alpha_composite(sh)
    canvas.alpha_composite(art, (off, off))
    return canvas


def write_ico(path, images):
    """Windows .ico: one entry per image, each drawn at its own size."""
    big = images[-1]
    big.save(path, format="ICO", sizes=[im.size for im in images], append_images=images[:-1])


def write_icns(path, images):
    """macOS .icns from PNG entries (OSType -> image)."""
    chunks = b""
    for ostype, img in images:
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        data = buf.getvalue()
        chunks += ostype.encode("ascii") + struct.pack(">I", len(data) + 8) + data
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)


def main():
    os.makedirs(OUT, exist_ok=True)
    cache = {}

    def at(size, shape="tile"):
        if (size, shape) not in cache:
            cache[size, shape] = draw_logo(size, shape)
        return cache[size, shape]

    at(1024).save(os.path.join(OUT, "papercut-1024.png"))
    # Windows: one .ico (the .rc embeds it), every size drawn at its own size
    write_ico(os.path.join(OUT, "papercut.ico"), [at(s) for s in (16, 24, 32, 48, 64, 128, 256)])
    # macOS: .icns with the 1x and 2x sizes, inset in Apple's rounded square
    write_icns(os.path.join(OUT, "papercut.icns"),
               [(t, at(s, "mac")) for t, s in [("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128),
                                               ("ic08", 256), ("ic09", 512), ("ic10", 1024), ("ic11", 32),
                                               ("ic12", 64), ("ic13", 256), ("ic14", 512)]])
    # iOS: the names port/ios/ios.cmake lists; full-bleed and opaque, iOS rounds them
    os.makedirs(os.path.join(OUT, "ios"), exist_ok=True)
    for name, size in {"AppIcon60x60@2x.png": 120, "AppIcon60x60@3x.png": 180, "AppIcon76x76@2x~ipad.png": 152,
                       "AppIcon83.5x83.5@2x~ipad.png": 167}.items():
        at(size, "square").save(os.path.join(OUT, "ios", name))
    # Android: the (legacy, non-adaptive) launcher icon per density
    for dens, size in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
        d = os.path.join(OUT, "android", "mipmap-" + dens)
        os.makedirs(d, exist_ok=True)
        at(size).save(os.path.join(d, "ic_launcher.png"))
    # Linux (.desktop) and the window icon (raw RGBA, embedded by CMakeLists.txt)
    at(256).save(os.path.join(OUT, "papercut-256.png"))
    at(512).save(os.path.join(OUT, "papercut-512.png"))
    with open(os.path.join(OUT, "papercut-64.rgba"), "wb") as f:
        f.write(at(64).tobytes())
    print("make_icons: wrote", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
