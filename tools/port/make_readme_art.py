#!/usr/bin/env python3
"""Draw the README's art into docs/images/: the banner (1x and 2x), GitHub's social
preview, and a 128 px icon.

Drawn from shapes like make_icons.py (no game assets), matching the icon and the
launcher: a paper theatre with a navy starry backdrop, red curtains tied back with
gold, a scalloped valance with gold trim and a wooden floor. In the middle is
"Paper Cut" in Fredoka Bold, cream with a red outline and a soft shadow, between two
gold sparkles, and under it the subtitle in gold.
  banner.png, banner@2x.png  1600 x 400 (and 2x): opaque, rounded corners, transparent
                             outside, so it sits on GitHub's light and dark themes
  social-preview.png         1280 x 640: opaque and square-cornered (GitHub rounds it),
                             recomposed for 2:1 with the icon's paper star hanging above
                             the title; text kept 40 px inside the edges
  icon-128.png               the app icon (make_icons.draw_logo) at 128 px
Each is drawn at 4x and scaled down.

The title's look, as fractions of the title's font size S (for the launcher to match):
  fill cream (255,247,228); outline red (196,32,42), round, width T = 0.0565 S
  shadow of the outlined text: offset (1.6 T, 2.0 T), blur sigma 0.6 T, (20,10,30) at 60%
  sparkles: centred on the middle of the cap height, 0.42 S beyond each end of the
    text; four points, tip radius 0.177 S, waist 0.1875 x the tip radius; gold
    (246,196,69), with a shadow offset (0.016 S, 0.024 S), blur 0.016 S, 40%; and a
    small one (tip 0.073 S, 90%) 0.21 S further out and 0.21 S higher
  subtitle: size 0.25 S, baseline 0.5 S below the title's; gold, outline (60,20,30) of
    0.097 x its size, the same shadow rule at 50%

usage: python make_readme_art.py   (needs Pillow: pip install pillow)
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_icons as mi  # noqa: E402  (the icon's drawing and palette)

ROOT = mi.ROOT
OUT = os.path.join(ROOT, "docs", "images")
FONT = os.path.join(ROOT, "port", "assets", "fonts", "Fredoka-Bold.ttf")

M = 4  # drawn at this multiple, then scaled down
TITLE = "Paper Cut"
SUBTITLE = "A native port of Paper Mario: The Thousand-Year Door"

INK = (60, 20, 30)  # the launcher's dark outline (subtitle)
TITLE_RED = (196, 32, 42)  # kCurtain
TEXT_SHADOW = (20, 10, 30)


class Art:
    """An opaque RGB drawing of w x h units at M x."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("RGB", (w * M, h * M), mi.NAVY_TOP)

    def mask(self):
        return Image.new("L", self.img.size, 0)

    def poly(self, pts):
        m = self.mask()
        ImageDraw.Draw(m).polygon([(x * M, y * M) for x, y in pts], fill=255)
        return m

    def rect(self, x0, y0, x1, y1):
        return self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])

    def ellipse(self, cx, cy, rx, ry=None):
        ry = rx if ry is None else ry
        m = self.mask()
        ImageDraw.Draw(m).ellipse([(cx - rx) * M, (cy - ry) * M, (cx + rx) * M, (cy + ry) * M], fill=255)
        return m

    def gradient(self, top, bottom, y0, y1, folds_width=0.0, fold_amp=0.0, x_origin=0.0):
        """Top to bottom over rows y0..y1; optional vertical folds every folds_width units."""
        sw, sh = self.img.size
        col = Image.new("RGB", (1, sh))
        for y in range(sh):
            t = min(1.0, max(0.0, (y / M - y0) / max(1e-6, y1 - y0)))
            col.putpixel((0, y), tuple(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3)))
        img = col.resize((sw, sh), Image.NEAREST)
        if folds_width:
            row = Image.new("L", (sw, 1))
            for x in range(sw):
                fold = 0.5 + 0.5 * math.cos((x / M - x_origin) / folds_width * math.pi * 2)
                row.putpixel((x, 0), int(255 * (1 - fold_amp + fold_amp * fold)))
            img = ImageChops.multiply(img, row.resize((sw, sh), Image.NEAREST).convert("RGB"))
        return img

    def paint(self, color, m, alpha=1.0):
        if alpha < 1.0:
            m = m.point(lambda v: int(v * alpha))
        self.img.paste(Image.new("RGB", self.img.size, color), (0, 0), m)

    def shadow(self, m, dx, dy, blur, alpha, color=mi.SHADOW):
        s = self.mask()
        s.paste(m, (int(dx * M), int(dy * M)))
        if blur:
            s = s.filter(ImageFilter.GaussianBlur(blur * M))
        self.paint(color, s, alpha)

    def text(self, text, size, cx, baseline, fill, outline, thickness, shadow_alpha):
        """Like launcherOutlinedText: a shadow down and right, an outline all round, then the fill."""
        font = ImageFont.truetype(FONT, int(size * M))
        width = font.getlength(text) / M
        x = cx - width / 2
        st = int(round(thickness * M))

        def layer(dx, dy, stroke):
            m = self.mask()
            ImageDraw.Draw(m).text(((x + dx) * M, (baseline + dy) * M), text, font=font, fill=255, anchor="ls",
                                   stroke_width=stroke, stroke_fill=255)
            return m

        sh = layer(thickness * 1.6, thickness * 2.0, st).filter(ImageFilter.GaussianBlur(thickness * 0.6 * M))
        self.paint(TEXT_SHADOW, sh, shadow_alpha)
        self.paint(outline, layer(0, 0, st))
        self.paint(fill, layer(0, 0, 0))
        return width


def sparkle_pts(cx, cy, r, k=0.3):
    """The launcher's title sparkle: four long points, a narrow waist."""
    kk = r * k / 1.6
    return [(cx, cy - r), (cx + kk, cy - kk), (cx + r, cy), (cx + kk, cy + kk),
            (cx, cy + r), (cx - kk, cy + kk), (cx - r, cy), (cx - kk, cy - kk)]


def stage(a, floor_y, planks, valance, scallop, curtain, stars, stars_clear):
    """The theatre: backdrop and stars, floor, curtains, valance.
    planks: the rows where floor boards start; curtain: its inner x at the top, at the
    tie-back and at the floor; stars: how many; stars_clear: boxes (x0, y0, x1, y1) kept empty."""
    W, H = a.w, a.h
    a.img.paste(a.gradient(mi.NAVY_TOP, mi.NAVY_BOTTOM, 0, floor_y), (0, 0))
    top_x, tie_x, foot_x = curtain
    for i in range(stars):  # scattered by a fixed rule
        x = top_x - 20 + ((i * 0.6180339887) % 1.0) * (W - 2 * top_x + 40)
        y = valance + 40 + ((i * 0.7548776662 + 0.3) % 1.0) * (floor_y - valance - 70)
        if any(x0 < x < x1 and y0 < y < y1 for x0, y0, x1, y1 in stars_clear):
            continue
        r = 2.2 + (i % 4) * 1.1
        if i % 5 == 0:
            a.paint(mi.STAR_DOT, a.poly(sparkle_pts(x, y, r * 3.2, 0.45)), 0.85)
        else:
            a.paint(mi.STAR_DOT, a.ellipse(x, y, r * 0.75), 0.55 + 0.1 * (i % 3))

    # the floor: wood, planks with staggered board ends, a gold lip
    a.img.paste(a.gradient(mi.WOOD_TOP, mi.WOOD_BOTTOM, floor_y, H), (0, 0), a.rect(0, floor_y, W, H))
    for y in planks[1:]:
        a.paint(mi.PLANK, a.rect(0, y, W, y + 2), 0.6)
    for i, y0 in enumerate(planks):
        for k in range(int(W / 200) + 1):
            x = (k + 0.5 * (i % 2)) * 200 + 40
            a.paint(mi.PLANK, a.rect(x, y0 + 2, x + 2, min(H, y0 + 24)), 0.45)
    a.paint(mi.GOLD_TRIM, a.rect(0, floor_y, W, floor_y + 5))

    # the curtains: in at the top, pinched at the tie-back, flaring to the floor
    tie = 0.6

    def edge(y):
        t = y / floor_y
        if t < tie:
            return top_x - (top_x - tie_x) * (t / tie) ** 1.4
        return tie_x + (foot_x - tie_x) * math.sin((t - tie) / (1 - tie) * math.pi / 2)

    left = [(0, 0)] + [(edge(floor_y * i / 80), floor_y * i / 80) for i in range(81)] + [(0, floor_y + 4)]
    right = [(W - x, y) for x, y in left]
    for side, origin in ((left, 0.0), (right, W)):
        m = a.poly(side)
        a.shadow(m, 0, 3, 5, 0.45)
        a.img.paste(a.gradient(mi.RED_TOP, mi.RED_BOTTOM, 0, floor_y, 34, 0.24, origin), (0, 0), m)
    for x in (tie_x, W - tie_x):  # the gold tie-backs
        ty = floor_y * tie
        a.shadow(a.ellipse(x, ty, 11), 1, 2, 2, 0.5)
        a.paint(mi.GOLD_DARK, a.ellipse(x, ty, 11))
        a.paint(mi.GOLD, a.ellipse(x - 1.5, ty - 1.5, 8.5))

    # the valance: scallops, a gold trim line, gold beads under the scallops (like the launcher)
    val = a.rect(0, 0, W, valance)
    xs = [scallop * 0.5 + i * scallop for i in range(int(W / scallop) + 2)]
    for x in xs:
        val = ImageChops.lighter(val, a.ellipse(x, valance, scallop * 0.5))
    a.shadow(val, 0, 3, 4, 0.5)
    a.img.paste(a.gradient((150, 18, 30), TITLE_RED, 0, valance + scallop * 0.5), (0, 0), val)
    a.paint(mi.GOLD, a.rect(0, valance - 5, W, valance + 1))
    for x in xs:
        a.paint(mi.GOLD, a.ellipse(x, valance + scallop * 0.5, 4))


def title_block(a, cx, baseline, S):
    """The title (font size S), its sparkles, and the subtitle under it (the rule is in the docstring)."""
    T = 0.0565 * S
    tw = a.text(TITLE, S, cx, baseline, mi.PAPER, TITLE_RED, T, 0.6)
    cap = -ImageFont.truetype(FONT, int(S * M)).getbbox("P", anchor="ls")[1] / M
    sy = baseline - cap / 2
    for side in (-1, 1):
        sx = cx + side * (tw / 2 + 0.42 * S)
        r = 0.177 * S
        a.shadow(a.poly(sparkle_pts(sx, sy, r)), 0.016 * S, 0.024 * S, 0.016 * S, 0.4, TEXT_SHADOW)
        a.paint(mi.GOLD, a.poly(sparkle_pts(sx, sy, r)))
        a.paint(mi.GOLD, a.poly(sparkle_pts(sx + side * 0.21 * S, sy - 0.21 * S, 0.073 * S)), 0.9)
    sub = 0.25 * S
    a.text(SUBTITLE, sub, cx, baseline + 0.5 * S, mi.GOLD, INK, 0.097 * sub, 0.5)
    return tw


def hanging_star(a, cx, cy, R, top):
    """The icon's folded-paper star, hanging on a gold thread from y = top."""
    a.paint(mi.GOLD_DARK, a.rect(cx - 1.6, top, cx + 1.6, cy - R * 0.9))
    star = a.poly(mi.star_pts(cx, cy, R))
    a.shadow(star, 6, 9, 6, 0.55)
    a.paint(mi.WHITE, a.poly(mi.star_pts(cx, cy, R + 5)))
    lit, shade = a.mask(), a.mask()
    dl, ds = ImageDraw.Draw(lit), ImageDraw.Draw(shade)
    pts = mi.star_pts(cx, cy, R * 1.05)
    for i in range(0, len(pts), 2):
        left, tip, right = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
        dl.polygon([(p[0] * M, p[1] * M) for p in ((cx, cy), left, tip)], fill=255)
        ds.polygon([(p[0] * M, p[1] * M) for p in ((cx, cy), tip, right)], fill=255)
    a.paint(mi.PAPER, ImageChops.multiply(lit, star))
    a.paint(mi.PAPER_SHADE, ImageChops.multiply(shade, star))


def draw_banner():
    a = Art(1600, 400)
    stage(a, floor_y=334, planks=(334, 356, 380), valance=58, scallop=40, curtain=(250, 90, 154), stars=46,
          stars_clear=[(370, 115, 1230, 300)])
    title_block(a, 800, 222, 124)
    out = a.img.convert("RGBA")
    corners = Image.new("L", a.img.size, 0)
    ImageDraw.Draw(corners).rounded_rectangle([0, 0, a.img.width - 1, a.img.height - 1], radius=28 * M, fill=255)
    out.putalpha(corners)
    return out


def draw_social():
    a = Art(1280, 640)
    stage(a, floor_y=548, planks=(548, 570, 594, 618), valance=74, scallop=46, curtain=(262, 104, 176), stars=70,
          stars_clear=[(180, 290, 1100, 515), (530, 90, 750, 310)])
    hanging_star(a, 640, 214, 84, 74)
    title_block(a, 640, 418, 146)
    return a.img


def main():
    os.makedirs(OUT, exist_ok=True)
    banner = draw_banner()
    banner.resize((3200, 800), Image.LANCZOS).save(os.path.join(OUT, "banner@2x.png"), optimize=True)
    banner.resize((1600, 400), Image.LANCZOS).save(os.path.join(OUT, "banner.png"), optimize=True)
    draw_social().resize((1280, 640), Image.LANCZOS).save(os.path.join(OUT, "social-preview.png"), optimize=True)
    mi.draw_logo(128).save(os.path.join(OUT, "icon-128.png"), optimize=True)
    print("make_readme_art: wrote", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
