#!/usr/bin/env python3
"""Draw Paper Cut's logo and write every platform's icon into port/assets/icon/.

The logo is drawn from shapes (no game assets): a stage-curtain red square
with gold trim, an arched paper door cut from cream paper with a white edge
and a drop shadow, a gold sparkle on it, and a dashed cut line around it.
The files are committed, so builds don't need this script or Pillow; run it
again after changing the drawing.

usage: python make_icons.py        (needs Pillow: pip install pillow)
"""
import io
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "port", "assets", "icon")

RED_TOP = (150, 14, 28)
RED_BOTTOM = (214, 40, 48)
GOLD = (246, 196, 69)
GOLD_DARK = (176, 116, 20)
PAPER = (255, 247, 228)
PAPER_SHADE = (240, 222, 188)
WHITE = (255, 255, 255)
INK = (60, 20, 30)


def door_polygon(cx, top, width, height, steps=96):
    """An arched door: a half circle on a rectangle, as a polygon."""
    r = width / 2
    pts = []
    for i in range(steps + 1):
        a = math.pi + math.pi * i / steps
        pts.append((cx + r * math.cos(a), top + r + r * math.sin(a)))
    bottom = top + height
    pts += [(cx + r, bottom), (cx - r, bottom)]
    return pts


def offset_polygon(pts, d):
    """The door grown by d: scaled about its centre (it's convex)."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    sx, sy = (w + 2 * d) / w, (h + 2 * d) / h
    return [(cx + (x - cx) * sx, cy + (y - cy) * sy) for x, y in pts]


def sparkle(cx, cy, r, thin=0.26):
    k = r * thin
    return [(cx, cy - r), (cx + k, cy - k), (cx + r, cy), (cx + k, cy + k),
            (cx, cy + r), (cx - k, cy + k), (cx - r, cy), (cx - k, cy - k)]


def draw_logo(size):
    """The logo at size x size (drawn 4x larger and scaled down)."""
    S = size * 4
    u = S / 1024.0  # drawing units: 1024 across
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # the square: curtain red, top to bottom, with fold shading
    bg = Image.new("RGBA", (S, S))
    px = bg.load()
    for y in range(S):
        t = y / (S - 1)
        base = [RED_TOP[c] + (RED_BOTTOM[c] - RED_TOP[c]) * t for c in range(3)]
        for x in range(S):
            fold = 0.5 + 0.5 * math.cos(x / S * math.pi * 2 * 5)  # five folds
            shade = 0.82 + 0.18 * fold
            px[x, y] = tuple(int(base[c] * shade) for c in range(3)) + (255,)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(225 * u), fill=255)
    img.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(img)

    # gold trim across the top, like the launcher's valance
    trim = Image.new("L", (S, S), 0)
    td = ImageDraw.Draw(trim)
    td.rectangle([0, int(118 * u), S, int(142 * u)], fill=255)
    for i in range(11):
        x = (i + 0.5) * S / 11
        td.ellipse([x - 13 * u, 152 * u, x + 13 * u, 178 * u], fill=255)
    img.paste(Image.new("RGBA", (S, S), GOLD + (255,)), (0, 0), Image.composite(trim, Image.new("L", (S, S), 0), mask))
    d.rectangle([0, 0, S, int(150 * u)], fill=None)

    # the door: shadow, white edge, cream paper, a darker inner panel
    door = door_polygon(512 * u, 290 * u, 430 * u, 580 * u)
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(x + 26 * u, y + 30 * u) for x, y in offset_polygon(door, 18 * u)],
                                   fill=(30, 6, 14, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(14 * u))
    img.alpha_composite(shadow)
    d = ImageDraw.Draw(img)
    d.polygon(offset_polygon(door, 18 * u), fill=WHITE)
    d.polygon(door, fill=PAPER)
    d.polygon(offset_polygon(door, -44 * u), fill=PAPER_SHADE)
    d.polygon(offset_polygon(door, -62 * u), fill=PAPER)

    # the dashed cut line around the door
    cut = offset_polygon(door, 52 * u)
    dash, gap, w = 34 * u, 22 * u, max(1, int(12 * u))
    pts = cut + [cut[0]]
    run, on = 0.0, True
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        seg = math.hypot(x1 - x0, y1 - y0)
        pos = 0.0
        while pos < seg:
            step = min(seg - pos, (dash if on else gap) - run)
            if on:
                a, b = pos / seg, (pos + step) / seg
                d.line([(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a), (x0 + (x1 - x0) * b, y0 + (y1 - y0) * b)],
                       fill=WHITE, width=w)
            pos += step
            run += step
            if run >= (dash if on else gap) - 1e-6:
                run, on = 0.0, not on

    # the sparkle on the door, with a darker gold edge
    cx, cy, r = 512 * u, 530 * u, 140 * u
    d.polygon(sparkle(cx + 8 * u, cy + 10 * u, r), fill=(120, 60, 20, 120))
    d.polygon(sparkle(cx, cy, r + 10 * u), fill=GOLD_DARK)
    d.polygon(sparkle(cx, cy, r), fill=GOLD)
    d.polygon(sparkle(cx - 10 * u, cy - 14 * u, r * 0.42, 0.3), fill=(255, 236, 170))
    # a small second sparkle up and to the right
    d.polygon(sparkle(735 * u, 330 * u, 52 * u + 6 * u), fill=GOLD_DARK)
    d.polygon(sparkle(735 * u, 330 * u, 52 * u), fill=GOLD)

    return img.resize((size, size), Image.LANCZOS)


def write_ico(path, sizes, master):
    master.save(path, format="ICO", sizes=[(s, s) for s in sizes])


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
    master = draw_logo(1024)
    master.save(os.path.join(OUT, "papercut-1024.png"))
    cache = {1024: master}

    def at(size):
        if size not in cache:
            cache[size] = draw_logo(size) if size <= 64 else master.resize((size, size), Image.LANCZOS)
        return cache[size]

    # Windows: one .ico (the .rc embeds it), small sizes drawn at their own size
    at(16), at(24), at(32), at(48), at(64)
    write_ico(os.path.join(OUT, "papercut.ico"), [16, 24, 32, 48, 64, 128, 256], master)
    # macOS: .icns with the 1x and 2x sizes
    write_icns(os.path.join(OUT, "papercut.icns"),
               [("icp4", at(16)), ("icp5", at(32)), ("icp6", at(64)), ("ic07", at(128)), ("ic08", at(256)),
                ("ic09", at(512)), ("ic10", at(1024)), ("ic11", at(32)), ("ic12", at(64)), ("ic13", at(256)),
                ("ic14", at(512))])
    # iOS: the names port/ios/Info.plist lists (CFBundleIconFiles), and the App Store size
    for name, size in {"AppIcon60x60@2x.png": 120, "AppIcon60x60@3x.png": 180, "AppIcon76x76@2x~ipad.png": 152,
                       "AppIcon83.5x83.5@2x~ipad.png": 167}.items():
        at(size).convert("RGB").save(os.path.join(OUT, "ios", name)) if os.path.isdir(os.path.join(OUT, "ios")) \
            else (os.makedirs(os.path.join(OUT, "ios")), at(size).convert("RGB").save(os.path.join(OUT, "ios", name)))
    # Android: the launcher icon per density
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
