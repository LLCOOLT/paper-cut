"""Chapter 3's texture swaps (Super Luigi): the lobby's fighter posters as racing posters, and the
Tube Game's "Pianta Parlor" sign as "Circuit Break".

Run: python ch3_posters.py -> ../ch3_posters/*.svg, *.png, preview.png
ch3.md, New assets 8: "tou_02's fighter posters to racing posters (Revvo on four of them, 'Big Green
01' on one after the race); yuu_02's 'Pianta Parlor' neon to 'Circuit Break'. Through the texture
pack mechanism (the posters' Dolphin names), shipped in the built-in mod's textures/."
Each replacement keeps the size and layout of the texture it replaces (STYLE.md R10), measured on
the decoded disc in scratch: tou_02's poster atlases are 128x128 with four 64x64 posters (a title
strip across the top, a character painted full bleed on a loud ground, thin dark borders), and
yuu_02's sign is 320x192 with 1-bit alpha (arched letters over a sunburst in a red arch). The
posters are opaque (CMPR fills); the sign keeps its cut-out alpha.
The lettering is lettering.py's hand-cut capitals, as on the game's own signs: on the sign, set on the
arch and faced in a red-to-yellow ramp with a grey edge and a dark drop, like the Pianta Parlor's.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, stroke_svg, fill_svg, poly_path, smooth_path, ellipse_pts, star_pts, clean  # noqa
import lettering as lt  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch3_posters")

HELM = dict(fill="#3e4a6e", light="#64729a", shadow="#283250", line="#0c1024")
SPIKE = dict(fill="#d8dce6", light="#ffffff", shadow="#a0a6b8", line="#20242e")
SKIN = dict(fill="#f2c64a", light="#fbe08a", shadow="#c89a22", line="#4a3006")
RED = dict(fill="#d8303a", light="#f06a6a", shadow="#a01824", line="#3e0608")
GREEN = dict(fill="#3aaa3a", light="#7cd26a", shadow="#1e7a24", line="#0e300c")
GOLD = dict(fill="#ecba3c", light="#fadc7c", shadow="#b2821c", line="#4a3006")
TYRE = dict(fill="#2a2a30", light="#4a4a52", shadow="#16161a", line="#060608")


def txt(x, y, s, size, fill, stroke="#1a0a04", sw=2.0, anchor="middle", seed=0):
    """a headline in the hand-cut capitals: cap height `size` px, baseline y, a dark edge sw px wide"""
    return lt.text_svg(s, x, y, size, fill, edge=stroke, edge_w=sw / 2, anchor=anchor, seed=seed, jitter=0.25,
                       tilt=4, bob=0.04)


def sh(p, pts, tone, ox, oy, k=1.0, **kw):
    """a small outlined piece on a poster, pts in poster pixels (64x64) offset to its atlas slot"""
    q = [(ox + x * k, oy + y * k) for x, y in pts]
    kw.setdefault("grow", 0.9)
    kw.setdefault("inner", 1.0)
    kw.setdefault("bias", (0.7, 0.8))
    p.shape(smooth_path(q, tension=kw.pop("tension", 0.5), jitter=0.2, seed=kw.pop("seed", 1)), tone["fill"],
            tone["line"], tone.get("light"), tone.get("shadow"), (2, 2), (2.5, 2.5), soft=0.7, **kw)


def ground(p, ox, oy, c0, c1, rays=True):
    """the poster's loud ground: a flat colour with a sunburst of a second tone"""
    p.raw(f'<rect x="{ox}" y="{oy}" width="64" height="64" fill="{c0}"/>')
    if rays:
        cx, cy = ox + 32, oy + 40
        for k in range(12):
            a0, a1 = k * math.pi / 6, k * math.pi / 6 + math.pi / 12
            pts = [(cx, cy), (cx + 70 * math.cos(a0), cy + 70 * math.sin(a0)), (cx + 70 * math.cos(a1), cy + 70 * math.sin(a1))]
            p.raw(f'<path d="{poly_path(pts)}" fill="{c1}" clip-path="url(#slot{ox}_{oy})"/>')


def title(p, ox, oy, s, colour="#ffd24a"):
    p.raw(f'<rect x="{ox}" y="{oy}" width="64" height="12" fill="#1a1020" fill-opacity="0.85"/>')
    size = min(7.4, 58 / lt.width(s, 1.0))
    p.raw(txt(ox + 32, oy + 6 + size / 2, s, size, colour, "#5a1a08", 1.6, seed=len(s)))


def border(p, ox, oy):
    p.raw(f'<rect x="{ox + 0.5}" y="{oy + 0.5}" width="63" height="63" fill="none" stroke="#0a0a10" stroke-width="1.5"/>')


def revvo_head(p, ox, oy, k=1.0, crown=True):
    """Revvo: a Dark Koopatrol's spiked steel helmet, the eye slit, a yellow beak, and the crown"""
    sh(p, [(14, 44), (14, 28), (22, 18), (32, 15), (42, 18), (50, 28), (50, 44), (40, 47), (24, 47)], HELM, ox, oy, k,
       seed=3, hl=(0.38, 0.22, 0.26, 0.14))
    sh(p, [(18, 34), (46, 34), (46, 40), (18, 40)], dict(fill="#141824", line="#06080e"), ox, oy, k, tension=0.1, seed=4)
    for ex in (25, 37):   # angry eyes in the slit
        p.raw(fill_svg(poly_path([(ox + (ex - 3) * k, oy + 35.5 * k), (ox + (ex + 4) * k, oy + 37 * k),
                                  (ox + (ex + 3) * k, oy + 39.5 * k), (ox + (ex - 3) * k, oy + 39.5 * k)]), "#ffe24a"))
    sh(p, [(12, 46), (24, 43), (30, 50), (22, 55), (12, 52)], SKIN, ox, oy, k, seed=5)   # beak, facing -x
    for sx, sy in ((14, 28), (50, 28)):   # side spikes
        sh(p, [(sx - 3, sy + 2), (sx + (-6 if sx < 30 else 6), sy - 2), (sx + 3, sy - 1)], SPIKE, ox, oy, k, seed=6,
           tension=0.1)
    if crown:
        sh(p, [(22, 18), (42, 18), (44, 8), (39, 13), (35, 5), (32, 12), (29, 5), (25, 13), (20, 8)], GOLD, ox, oy, k,
           seed=7, tension=0.1, hl=(0.4, 0.5, 0.3, 0.2))
        p.raw(fill_svg(smooth_path(ellipse_pts(ox + 32 * k, oy + 15 * k, 2 * k, 2 * k, 8)), RED["fill"]))
    else:
        sh(p, [(28, 16), (32, 4), (36, 16)], SPIKE, ox, oy, k, seed=8, tension=0.1)


def kart(p, ox, oy, tone, num, k=1.0):
    sh(p, [(6, 46), (10, 38), (24, 36), (30, 30), (44, 30), (52, 36), (60, 38), (60, 48), (6, 50)], tone, ox, oy, k,
       seed=9, tension=0.25, hl=(0.4, 0.3, 0.3, 0.2))
    for wx in (16, 50):
        sh(p, ellipse_pts(wx, 50, 6, 6, 10), TYRE, ox, oy, k, seed=10)
        p.raw(fill_svg(smooth_path(ellipse_pts(ox + wx * k, oy + 50 * k, 2.4 * k, 2.4 * k, 8)), "#c8ccd6"))
    p.raw(f'<rect x="{ox + 31 * k}" y="{oy + 39 * k}" width="{14 * k}" height="{8 * k}" fill="#f4f0e6" '
          f'stroke="#14141c" stroke-width="1"/>')
    p.raw(txt(ox + 38 * k, oy + 46 * k, num, 7.5 * k, "#14141c", "#f4f0e6", 0.1))
    for y in (38, 43, 48):   # speed lines behind (+x: it's racing to the left)
        p.raw(stroke_svg(f"M{ox + 62 * k:.1f} {oy + y * k:.1f} L{ox + 64:.1f} {oy + y * k:.1f}", "#ffffff", 1.4))


def trophy(p, ox, oy):
    sh(p, [(18, 22), (46, 22), (44, 34), (38, 42), (35, 44), (36, 50), (42, 54), (22, 54), (28, 50), (29, 44),
           (26, 42), (20, 34)], GOLD, ox, oy, seed=11, tension=0.35, hl=(0.36, 0.25, 0.25, 0.18))
    for s in (-1, 1):
        x = 32 + s * 14
        p.raw(stroke_svg(f"M{ox + x:.1f} {oy + 25:.1f} Q{ox + x + s * 9:.1f} {oy + 28:.1f} {ox + x - s * 1:.1f} {oy + 36:.1f}",
                         GOLD["line"], 4))
        p.raw(stroke_svg(f"M{ox + x:.1f} {oy + 25:.1f} Q{ox + x + s * 9:.1f} {oy + 28:.1f} {ox + x - s * 1:.1f} {oy + 36:.1f}",
                         GOLD["fill"], 2))
    p.raw(fill_svg(smooth_path(ellipse_pts(ox + 32, oy + 23, 9, 2, 10)), "#fff0a8"))   # the lid's plate glint


def flag(p, ox, oy):
    p.raw(stroke_svg(f"M{ox + 14} {oy + 60} L{ox + 22} {oy + 16}", "#3a2a1a", 3))
    for i in range(6):
        for j in range(4):
            x0, y0 = 22 + i * 6, 16 + j * 6 + i * 0.8
            col = "#f4f0e6" if (i + j) % 2 == 0 else "#14141c"
            p.raw(fill_svg(poly_path([(ox + x0, oy + y0), (ox + x0 + 6, oy + y0 + 0.8), (ox + x0 + 6, oy + y0 + 6.8),
                                      (ox + x0, oy + y0 + 6)]), col))


SLOTS = [(0, 0), (64, 0), (0, 64), (64, 64)]


def atlas(name, posters):
    p = Part(name, 128, 128)
    for ox, oy in SLOTS:
        p.defs.append(f'<clipPath id="slot{ox}_{oy}"><rect x="{ox}" y="{oy}" width="64" height="64"/></clipPath>')
    for (ox, oy), poster in zip(SLOTS, posters):
        poster(p, ox, oy)
        border(p, ox, oy)
    p.save(OUT)
    return p


def p_king(p, ox, oy):
    ground(p, ox, oy, "#c8283a", "#e04a3a")
    revvo_head(p, ox - 2, oy + 6, 1.0)
    title(p, ox, oy, "KING REVVO")


def p_kart(p, ox, oy):
    ground(p, ox, oy, "#2a3a8a", "#3a4ea8")
    revvo_head(p, ox + 14, oy + 6, 0.6, crown=True)
    kart(p, ox - 2, oy + 6, RED, "13", 1.0)
    title(p, ox, oy, "REVVO #13")


def p_wins(p, ox, oy):
    ground(p, ox, oy, "#6a2a8a", "#8a3aa8")
    trophy(p, ox, oy)
    revvo_head(p, ox + 30, oy + 28, 0.55)
    title(p, ox, oy, "31 WINS!")


def p_long_live(p, ox, oy):
    ground(p, ox, oy, "#e0a020", "#f2c03a")
    revvo_head(p, ox + 4, oy + 4, 0.95)
    title(p, ox, oy, "ALL HAIL!", "#ffffff")


def p_big_green(p, ox, oy):
    ground(p, ox, oy, "#1e7a3a", "#2e9a4a")
    kart(p, ox - 2, oy + 6, GREEN, "01", 1.0)
    p.raw(txt(ox + 32, oy + 29, "NEW KING!", 6.4, "#ffffff", "#0e300c", 1.6, seed=3))
    title(p, ox, oy, "BIG GREEN 01")


def p_race_day(p, ox, oy):
    ground(p, ox, oy, "#e04a2a", "#f06a3a")
    flag(p, ox, oy)
    title(p, ox, oy, "RACE DAY!")


def p_circuit(p, ox, oy):
    ground(p, ox, oy, "#1a6ac8", "#2a82e0")
    kart(p, ox - 6, oy - 2, dict(fill="#3a6ad8", light="#78a2f0", shadow="#2444a0", line="#0c1a48"), "07", 0.75)
    kart(p, ox + 12, oy + 16, dict(fill="#f0c83a", light="#fae08a", shadow="#c09a1e", line="#4a3406"), "22", 0.75)
    title(p, ox, oy, "CIRCUIT BREAK")


def p_cup(p, ox, oy):
    ground(p, ox, oy, "#2a2a6a", "#3a3a8a")
    trophy(p, ox, oy)
    title(p, ox, oy, "ONE CROWN")


def p_fans(p, ox, oy):
    ground(p, ox, oy, "#18a088", "#22b89c")
    p.raw(txt(ox + 32, oy + 34, "30,000", 9.6, "#ffd24a", "#3a1a08", 2.2, seed=4))
    p.raw(txt(ox + 32, oy + 48, "FANS!", 9, "#ffffff", "#3a1a08", 2.2, seed=5))
    for sx, sy in ((10, 54), (54, 20), (52, 56)):
        p.raw(fill_svg(poly_path(star_pts(ox + sx, oy + sy, 5, 2)), "#ffe68a"))
    title(p, ox, oy, "8 KARTS")


def sign():
    p = Part("sign_circuit_break", 320, 192)
    cx, cy = 160, 150
    # the red arch and the sunburst inside it
    burst = []
    for k in range(19):
        a = math.pi + k * math.pi / 18
        r = 84 if k % 2 == 0 else 42
        burst.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    p.raw(f'<path d="{poly_path([(cx - 84, cy)] + burst + [(cx + 84, cy)])}" fill="#ffc83a"/>')
    p.raw(f'<path d="{poly_path([(cx - 50, cy)] + [(cx + 50 * math.cos(math.pi + k * math.pi / 12), cy + 50 * math.sin(math.pi + k * math.pi / 12)) for k in range(13)])}" fill="#ffe680"/>')
    p.raw(f'<path d="M{cx - 92} {cy} A92 92 0 0 1 {cx + 92} {cy}" fill="none" stroke="#d8281e" stroke-width="7"/>')
    # CIRCUIT on the arc and BREAK straight below, as the Pianta Parlor's letters: each turned to the arch,
    # a red-to-yellow face, a light grey edge, and a dark drop below right
    stops = '<stop offset="{0}" stop-color="#ffd84a"/><stop offset="{1}" stop-color="#ff9a2a"/><stop offset="1" stop-color="#e8401e"/>'
    r0, size = 102, 36   # on the arch the ramp is radial, so every letter runs red at its top to yellow at its foot
    p.defs.append(f'<radialGradient id="lg_arc" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="{r0 + size}">'
                  + stops.format(r0 / (r0 + size), (r0 + size * 0.4) / (r0 + size)) + '</radialGradient>')
    p.defs.append(f'<linearGradient id="lg_line" gradientUnits="userSpaceOnUse" x1="0" y1="183" x2="0" y2="{183 - 36}">'
                  + stops.format(0, 0.4) + '</linearGradient>')
    for word, y, size, seed, arc, grad in (("CIRCUIT", cy - r0, size, 7, (cx, cy), "lg_arc"),
                                           ("BREAK", 183, 36, 8, None, "lg_line")):
        p.raw(lt.text_svg(word, cx, y, size, f"url(#{grad})", edge="#c4c0c4", edge_w=3.2, shadow="#5a1408",
                          shadow_off=(3, 3.5), seed=seed, jitter=0.25, tilt=3, bob=0.03, arc=arc,
                          spacing=1.2 if arc else 0.6))
    for fx, s in ((20, 1), (300, -1)):   # two small checkered flags at the arch's feet
        p.raw(stroke_svg(f"M{fx} {cy + 8} L{fx + s * 6} {cy - 40}", "#5a2a0a", 4))
        for i in range(4):
            for j in range(3):
                col = "#f4f0e6" if (i + j) % 2 == 0 else "#14141c"
                x0, y0 = fx + s * (6 + i * 5), cy - 40 + j * 5 + i
                p.raw(fill_svg(poly_path([(x0, y0), (x0 + s * 5, y0 + 1), (x0 + s * 5, y0 + 6), (x0, y0 + 5)]), col))
    p.save(OUT)
    return p


def preview():
    W, H = 830, 520
    c = Image.new("RGBA", (W, H), (120, 116, 110, 255))
    d = ImageDraw.Draw(c)
    x = 10
    for n in ("posters_revvo", "posters_revvo_after", "posters_racing"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im.resize((256, 256), Image.NEAREST), (x, 30))
        d.text((x, 12), n + " (x2)", fill=(255, 255, 255, 255))
        x += 272
    im = Image.open(os.path.join(OUT, "sign_circuit_break.png")).convert("RGBA")
    c.alpha_composite(im, (10, 310))
    d.text((340, 400), "sign_circuit_break (1:1, replaces yuu_02's Pianta Parlor sign)", fill=(255, 255, 255, 255))
    return c


def build():
    clean(OUT)
    atlas("posters_revvo", [p_king, p_kart, p_wins, p_long_live])
    atlas("posters_revvo_after", [p_king, p_kart, p_wins, p_big_green])
    atlas("posters_racing", [p_race_day, p_circuit, p_cup, p_fans])
    sign()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
