#!/usr/bin/env python3
"""Write every one of Paper Cut's icon files, and the README's art, from the brand's own drawing.

The icon is "snipping a star": a gold folded-paper star in a dashed cut line, the launcher title's
scissors cutting round it, over the stage's night blue. It is drawn by the launcher's code
(launcherBrandIcon in port/settings/launcher_brand.cpp), which the About page also draws, through
tools/brand_export: Dear ImGui's draw lists rasterised on the CPU, supersampled. Every size is drawn
at its own size, simpler as it gets smaller (no sparkles or cut line under 48 px; under 24 px a flat
star and two-handled scissors). This script asks brand_export for the sizes, then shapes and packs:
  Windows .ico, Linux, the window icon (papercut-64.rgba), docs/images/icon-128.png:
               the tile with rounded corners
  macOS .icns: the tile inset to 824/1024 on a transparent canvas, in Apple's rounded square (a
               superellipse), with a soft shadow
  iOS:         a full-bleed opaque square, no alpha (iOS rounds it, and rejects alpha)
  Android:     an adaptive icon (Android 8+; the app's minimum is 9): the backdrop and the star and
               scissors as separate 108 dp layers, the pieces inside the 66 dp safe circle, and a
               monochrome layer for themed icons (Android 13+)
  README:      banner.png, banner@2x.png and social-preview.png, the launcher's own wordmark on its own
               stage (launcher_stage.cpp, at rest), saved as 256-colour PNGs
No game assets, no downloads. The files are committed, so builds need neither this script nor Pillow.

usage: python make_icons.py [icons|readme|all] [--exe path/to/brand_export.exe]
  (needs Pillow, and brand_export built by tools/brand_export/build.sh; default all)
"""
import argparse
import io
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "port", "assets", "icon")
DOCS = os.path.join(ROOT, "docs", "images")

TILE_RADIUS = 230 / 1024  # the rounded tile's corner, of its size
MAC_BODY = 824 / 1024  # macOS: the tile's size on its canvas

ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
ICNS = [("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128), ("ic08", 256), ("ic09", 512), ("ic10", 1024),
        ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)]
# the names port/ios/ios.cmake lists
IOS = {"AppIcon60x60@2x.png": 120, "AppIcon60x60@3x.png": 180, "AppIcon76x76@2x~ipad.png": 152,
       "AppIcon83.5x83.5@2x~ipad.png": 167}
# Android's densities: the adaptive layers are 108 dp
ANDROID = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}

ADAPTIVE_XML = """<?xml version="1.0" encoding="utf-8"?>
<!-- Paper Cut's adaptive icon (tools/port/make_icons.py): the stage's night blue behind, the star and
     the scissors in front, a monochrome copy for themed icons. -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@mipmap/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />
</adaptive-icon>
"""


class Renders:
    """brand_export's drawings, made on demand into a temporary folder."""

    def __init__(self, exe, tmp):
        self.exe, self.tmp = exe, tmp

    def run(self, *args):
        subprocess.run([self.exe, self.tmp, *map(str, args)], cwd=ROOT, check=True, stderr=subprocess.DEVNULL)

    def path(self, kind, size):
        return os.path.join(self.tmp, f"{kind}_{size}.png")

    def icons(self, sizes):
        todo = sorted({s for s in sizes if not os.path.exists(self.path("icon", s))})
        if todo:
            self.run("icon", *todo)

    def icon(self, size):
        self.icons([size])
        return Image.open(self.path("icon", size)).convert("RGBA")


def shape_mask(w, n=None):
    """A w x w mask: the rounded tile, or (n) a superellipse of that power; anti-aliased."""
    ss = 8
    m = Image.new("L", (w * ss, w * ss), 0)
    d = ImageDraw.Draw(m)
    if n is None:
        d.rounded_rectangle([0, 0, w * ss - 1, w * ss - 1], radius=TILE_RADIUS * w * ss, fill=255)
    else:
        r = w * ss / 2
        pts = []
        for i in range(720):
            a = 2 * math.pi * i / 720
            ca, sa = math.cos(a), math.sin(a)
            pts.append((r + r * math.copysign(abs(ca) ** (2 / n), ca), r + r * math.copysign(abs(sa) ** (2 / n), sa)))
        d.polygon(pts, fill=255)
    return m.resize((w, w), Image.BOX)


def tile(r, size, n=None):
    img = r.icon(size)
    img.putalpha(shape_mask(size, n))
    return img


def mac(r, size):
    """macOS: the tile inset on a transparent canvas, in a superellipse, a soft shadow under it."""
    body = max(1, round(size * MAC_BODY))
    art = tile(r, body, n=5.0)
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


def monochrome(fg):
    """Android's themed icon: white, its alpha the foreground's where it's lit (the dark cut lines,
    the handles' holes and the shadow fall away, so the star keeps an outline)."""
    src = fg.load()
    out = Image.new("RGBA", fg.size, (255, 255, 255, 0))
    dst = out.load()
    for y in range(fg.height):
        for x in range(fg.width):
            cr, cg, cb, ca = src[x, y]
            lum = (0.299 * cr + 0.587 * cg + 0.114 * cb) / 255
            k = min(1.0, max(0.0, (lum - 0.25) / 0.2))
            dst[x, y] = (255, 255, 255, int(ca * k + 0.5))
    return out


def write_ico(path, images):
    """Windows .ico: one entry per image, each drawn at its own size."""
    big = images[-1]
    big.save(path, format="ICO", sizes=[im.size for im in images], append_images=images[:-1])


def write_icns(path, images):
    """macOS .icns from PNG entries (OSType -> image)."""
    chunks = b""
    for ostype, img in images:
        buf = io.BytesIO()
        img.save(buf, format="PNG", optimize=True)
        data = buf.getvalue()
        chunks += ostype.encode("ascii") + struct.pack(">I", len(data) + 8) + data
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)


def save(img, *parts):
    path = os.path.join(*parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)


def icons(r):
    r.icons(list(ICO_SIZES) + [512, 1024] + [max(1, round(s * MAC_BODY)) for _, s in ICNS] + list(IOS.values()))
    save(tile(r, 1024), OUT, "papercut-1024.png")
    save(tile(r, 512), OUT, "papercut-512.png")  # Add to Steam's artwork
    save(tile(r, 256), OUT, "papercut-256.png")  # Linux
    # Windows: one .ico (port/windows/papercut.rc embeds it)
    write_ico(os.path.join(OUT, "papercut.ico"), [tile(r, s) for s in ICO_SIZES])
    # macOS: .icns with the 1x and 2x sizes
    write_icns(os.path.join(OUT, "papercut.icns"), [(t, mac(r, s)) for t, s in ICNS])
    # iOS: opaque, no alpha
    for name, size in IOS.items():
        save(r.icon(size).convert("RGB"), OUT, "ios", name)
    # Android: the adaptive icon's layers per density, and the XML that joins them
    sizes = {d: round(108 * k) for d, k in ANDROID.items()}
    r.run("android", *sizes.values())
    for dens, size in sizes.items():
        d = os.path.join(OUT, "android", "mipmap-" + dens)
        old = os.path.join(d, "ic_launcher.png")  # the old, non-adaptive icon
        if os.path.exists(old):
            os.remove(old)
        save(Image.open(r.path("android_bg", size)).convert("RGB"), d, "ic_launcher_background.png")
        fg = Image.open(r.path("android_fg", size)).convert("RGBA")
        save(fg, d, "ic_launcher_foreground.png")
        save(monochrome(fg), d, "ic_launcher_monochrome.png")
    any_dpi = os.path.join(OUT, "android", "mipmap-anydpi-v26")
    os.makedirs(any_dpi, exist_ok=True)
    with open(os.path.join(any_dpi, "ic_launcher.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(ADAPTIVE_XML)
    # the window icon (raw RGBA, embedded by CMakeLists.txt)
    with open(os.path.join(OUT, "papercut-64.rgba"), "wb") as f:
        f.write(tile(r, 64).tobytes())
    print("make_icons: wrote", OUT)


def palette_png(img):
    """img as a 256-colour image (median cut, Floyd-Steinberg): a fraction of true colour's size. The
    banner's rounded corners keep their anti-aliasing: 16 entries hold 8 alpha levels in the colours
    along its top and bottom edges."""
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")
    if alpha.getextrema()[0] == 255:
        return rgba.convert("RGB").quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    q = rgba.convert("RGB").quantize(240, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    w, h = img.size
    a, px, qp = alpha.load(), rgba.load(), q.load()
    pal = q.getpalette()[:240 * 3]
    pal += [0, 0, 0] * (240 - len(pal) // 3)
    trns = [255] * 240
    for row in (h // 64, h - 1 - h // 64):  # just inside the top and bottom edges, clear of the corners
        cs = [px[x, row] for x in range(w // 8, w - w // 8)]
        edge = [sum(c[i] for c in cs) // len(cs) for i in range(3)]
        for k in range(8):
            pal += edge
            trns.append(round(k * 255 / 7))
    q.putpalette(pal)
    for y in range(h):
        for x in range(w):
            if a[x, y] < 255:
                qp[x, y] = 240 + (0 if y < h // 2 else 8) + round(a[x, y] * 7 / 255)
    q.info["transparency"] = bytes(trns)
    return q


def readme(r):
    r.run("banners")
    for name in ("banner.png", "banner@2x.png", "social-preview.png"):
        q = palette_png(Image.open(os.path.join(r.tmp, name)))
        kw = {"transparency": q.info["transparency"]} if "transparency" in q.info else {}
        q.save(os.path.join(DOCS, name), optimize=True, **kw)
    save(tile(r, 128), DOCS, "icon-128.png")
    print("make_icons: wrote", DOCS)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("what", nargs="?", default="all", choices=("icons", "readme", "all"))
    ap.add_argument("--exe", default=os.path.join(ROOT, "build-brand", "brand_export.exe"))
    args = ap.parse_args()
    if not os.path.exists(args.exe):
        print(f"make_icons: no {args.exe}; build it with tools/brand_export/build.sh", file=sys.stderr)
        return 1
    tmp = tempfile.mkdtemp(prefix="papercut-icons-")
    try:
        r = Renders(args.exe, tmp)
        if args.what in ("icons", "all"):
            icons(r)
        if args.what in ("readme", "all"):
            readme(r)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
