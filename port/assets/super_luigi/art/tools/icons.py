"""The friends' partner icons (Super Luigi): Blooey, Jerry, Torque, Hayzee and Screamy, and Blooey
burnt (chapter 6), in the two forms the game's partner UI uses.

Run: python icons.py -> ../icons/<friend>/*.svg, *.png, and ../icons/preview.png

Measured on the game's own icons (icon.tpl, decoded into scratch; image n is icon.bin's tplId + 8):
  hud   the face beside the partner's HP on the status bar (statuswindow.c party_icon[], ICON_1A4..
        1B0; Goombella's is image 448): 48x48 (52x52 for Bobbery's and Vivian's wider heads), the
        partner's head in 3/4 view low and left, a pink HP heart with two eyes behind it at the top
        right, and the pair cut out as one sticker: a white border 2 to 3 px wide with a light grey
        inner edge and a dark grey (#424242) rim 1 to 2 px heavier on the bottom right, 2 to 3 px of
        clear margin, 1-bit alpha. The face itself has a thin dark line (about 2 px).
  menu  the battle menu's partner list and the pause menu's party page (battle_seq_command.c,
        win_party.c: ICON_155..161; Goombella's is image 354): 40x40, the whole partner standing,
        filling the square top to bottom, no border, a dark line in a tint of each colour, about
        3 px (measured median 3 to 4, heavier at the bottom), 1-bit alpha.
The game has no hurt or low-HP faces for partners; Yoshi's colours are its only variants. Each
friend here gets a hud and a menu icon, and Blooey a burnt pair for chapter 6.
Each figure is drawn in a 100x100 design box (y down, feet at the bottom) and scaled into the icon.
"""
import io
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, ellipse_pts, stroke_svg, fill_svg, harden, clean  # noqa

import resvg_py

OUT = os.path.join(os.path.dirname(__file__), "..", "icons")

# Colours sampled from each friend's own model textures (a/c_lp_*-, decoded into scratch).
BLOOEY = dict(fill="#dca040", light="#f0c46a", shadow="#b47a28", line="#382820")       # c_lp_gesso t_0
BLOOEY_BAND = dict(fill="#8e5434", light="#a06842", shadow="#744228", line="#382820")  # the mask band
BLOOEY_EYE = "#e0b058"
SOOT = dict(fill="#4a4446", light="#6a6466", shadow="#2e2a2c", line="#141012")
SOOT_BAND = dict(fill="#2a2224", light="#3a3234", shadow="#1a1416", line="#0a0606")
JERRY = dict(fill="#b03432", light="#d85a48", shadow="#7e2838", line="#0c2020")       # c_lp_bom t_3: dark teal line
FEET_J = dict(fill="#b03830", light="#cc5a4a", shadow="#883038", line="#381808")      # t_0
FUSE = dict(fill="#b07018", light="#c88a30", shadow="#885010", line="#402018")        # t_2
SKIN_T = dict(fill="#e0c880", light="#f0dca0", shadow="#c8a868", line="#402020")      # c_lp_met t_2
SHELL = dict(fill="#5a8424", light="#86b448", shadow="#3e5e16", line="#281028")       # t_4
RIM = dict(fill="#e4e2e6", light="#f4f2f6", shadow="#b8b6c0", line="#2a2a30")         # t_4's white rim
WRENCH = dict(fill="#b8b8c8", light="#dcdce6", shadow="#8a8a9c", line="#283038")      # t_3
PETAL = dict(fill="#a8d8b8", light="#c8ecd4", shadow="#84b898", line="#184030")       # c_lp_pansy t_3
FACE_H = dict(fill="#f2d890", light="#fae8b4", shadow="#dcbc70", line="#8a5a34")
GLOVE = dict(fill="#e4e2e6", light="#f6f4f8", shadow="#b8b6c0", line="#303030")       # t_4, t_5
SHOE_H = dict(fill="#a8c0ea", light="#c8d8f4", shadow="#88a0d0", line="#304058")      # t_0
GHOST = dict(fill="#e8e4e4", light="#f6f4f4", shadow="#dcc8c0", line="#2a2222")       # c_lp_moa t_5
GHOST_EYE = dict(fill="#e08070", light="#ec9a8a", shadow="#b06050", line="#402820")   # t_3, t_4
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


# ------------------------------------------------------------- the friends --
# Each is drawn as its model stands in the game (c_lp_*, rendered flat from the stand pose into
# scratch): the whole friend, front on. The HUD form shows the same figure, larger.

def mirror(left):
    """a symmetric outline from its left half, top to bottom (x mirrored about 50)"""
    return left + [(100 - x, y) for x, y in reversed(left) if x != 50]


def blooey(f, soot=False):
    """c_lp_gesso: one orange-gold Blooper card. A rounded-point mantle with fins at the sides, a
    narrow waist, the two outer tentacles flaring out sideways and the inner ones hanging down, and a
    brown mask band with two ringed eyes."""
    t, band = (SOOT, SOOT_BAND) if soot else (BLOOEY, BLOOEY_BAND)
    pts = mirror([(50, 0), (40, 4), (31, 11), (24, 19), (18, 28), (14, 36), (15, 42), (22, 47), (24, 53), (21, 61),
                  (12, 66), (4, 72), (8, 77), (17, 77), (20, 82), (19, 89), (25, 93), (31, 89), (35, 94), (40, 100),
                  (45, 98), (48, 93), (50, 92)])
    det = []
    if soot:   # a few white patches left on the scorched skin
        det += [f.dot(40, 20, 5, 3.5, "#e8e2dc"), f.dot(66, 66, 6, 3, "#e8e2dc"), f.dot(30, 84, 3.4, 2.6, "#d8d2cc")]
    f.shape(pts, t, det, hl=None if soot else (0.42, 0.2, 0.2, 0.13), seed=3, tension=0.5)
    by = 46
    f.shape([(23, by - 6), (50, by - 7), (77, by - 6), (79, by + 1), (76, by + 7), (50, by + 7), (24, by + 7),
             (21, by + 1)], band, seed=4, tension=0.45, light=False, lw=f.lw * 0.7)
    for ex in (38, 62):   # two ringed eyes on the band
        f.p.raw(f.dot(ex, by, 7, 6.6, BLOOEY_EYE if not soot else "#e8e2dc"))
        f.p.raw(f.dot(ex, by, 3.6, 3.4, band["fill"]))
    if soot:   # a little smoke wisp off the top
        f.p.raw(f.curve((56, 2), (66, -8), (58, -14), "#8a8488", 5))


def jerry(f):
    """c_lp_bom: a round red bomb, two tall white eyes to the front, a brown fuse stem arcing up and
    right to a crossbar (no key, no cap), and two big red feet."""
    cx, cy, r = 50, 57, 31
    f.oval(33, 88, 14, 9, dict(FEET_J, fill="#9a3030"), hl=(0.4, 0.3, 0.3, 0.2))   # the far foot
    # the fuse: a thick brown stem out of the top, curving up and right to a crossbar
    stem = "M%.1f %.1f C%.1f %.1f %.1f %.1f %.1f %.1f" % (*f.q([(53, 30)])[0], *f.q([(56, 18)])[0],
                                                         *f.q([(66, 12)])[0], *f.q([(77, 13)])[0])
    bar = [(70, 3), (86, 22)]
    for colour, w in ((FUSE["line"], 11), (FUSE["fill"], 5.5)):
        f.p.raw(stroke_svg(stem, colour, w * f.k))
        f.p.raw(f.line(bar, colour, w))
    det = [f.dot(cx - 15, cy - 8, 5.5, 10, "#f6f2f6"), f.dot(cx - 2, cy - 8, 5.5, 10, "#f6f2f6"),
           f.dot(cx + 17, cy - 15, 5, 4.4, "#e2704a")]   # the eyes, and the orange glint top right
    f.oval(cx, cy, r, r * 0.95, JERRY, det, hl=(0.34, 0.24, 0.26, 0.16))
    f.oval(64, 90, 19, 10, FEET_J, hl=(0.4, 0.3, 0.3, 0.2))   # the near foot, in front


def wrench_pts(cx, cy, R, w, ang):
    """an open-end wrench head: a disc with a slot of half-width w opening toward angle ang (deg)"""
    a1 = math.asin(min(0.95, w / R))
    pts = []
    for i in range(15):
        a = a1 + (2 * math.pi - 2 * a1) * i / 14
        pts.append((R * math.cos(a), R * math.sin(a)))
    pts += [(-R * 0.1, -w), (-R * 0.1, w)]
    c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    return [(cx + x * c - y * s, cy + x * s + y * c) for x, y in reversed(pts)]


def torque(f):
    """c_lp_met: a green shell with a white rim sweeping across it, his tan head peeking out low at the
    front (-x) with one dark eye, white claws, and a big open-end wrench standing up out of the top of
    the shell on a white collar."""
    f.shape([(46, 46), (46, 26), (59, 26), (59, 46)], WRENCH, seed=8, tension=0.2, light=False, lw=f.lw * 0.6)
    f.shape(wrench_pts(52, 15, 17, 6.5, -135), WRENCH, hl=(0.5, 0.42, 0.22, 0.16), seed=9, tension=0.25,
            lw=f.lw * 0.6)
    f.shape([(43, 47), (45, 38), (61, 38), (63, 47)], RIM, seed=10, tension=0.3)
    f.shape([(20, 74), (24, 60), (35, 49), (52, 43), (67, 48), (79, 60), (84, 76), (52, 78)], SHELL,
            hl=(0.46, 0.3, 0.24, 0.18), seed=7)
    det = [f.poly([(17, 84), (22, 80), (24, 88), (19, 92)], "#141010")]
    f.oval(28, 88, 19, 11, SKIN_T, det, hl=(0.42, 0.3, 0.26, 0.14), lw=f.lw * 0.75)
    f.shape([(12, 70), (22, 66), (42, 70), (62, 74), (84, 77), (88, 84), (72, 88), (50, 84), (30, 78), (16, 75)],
            RIM, seed=12, tension=0.5, lw=f.lw * 0.8)
    for x in (54, 76):   # white claws in front
        f.oval(x, 95, 8, 4.5, RIM, lw=f.lw * 0.6)


def hayzee(f):
    """c_lp_pansy: a four-petal mint flower (petals up, down, left and right) round a yellow oval face
    with two dark eyes, pink blush and a smile; white gloves at the sides and pale blue shoes. No hat."""
    cx, cy = 50, 46
    for x, rot in ((13, 25), (87, -25)):   # the gloves, out past the side petals
        f.oval(x, 76, 13, 10, GLOVE, rot=rot, hl=(0.4, 0.3, 0.3, 0.2), lw=f.lw * 0.7)
    f.oval(38, 93, 12, 6.5, SHOE_H, lw=f.lw * 0.7)
    f.oval(62, 93, 12, 6.5, SHOE_H, lw=f.lw * 0.7)
    pts = []
    for i in range(64):
        a = 2 * math.pi * i / 64
        k = 0.55 + 0.45 * abs(math.cos(2 * a)) ** 0.5
        ry = 46 if math.sin(a) < 0 else 41
        pts.append((cx + 39 * k * math.cos(a), cy + ry * k * math.sin(a)))
    f.shape(pts, PETAL, hl=(0.4, 0.14, 0.2, 0.09), seed=13, tension=0.5)
    det = [f.dot(cx - 6, cy - 6, 2.6, 5.4, "#2a2a2a"), f.dot(cx + 6, cy - 6, 2.6, 5.4, "#2a2a2a"),
           f.dot(cx - 11, cy + 4, 4.4, 2.8, "#f09090"), f.dot(cx + 11, cy + 4, 4.4, 2.8, "#f09090"),
           f.curve((cx - 6, cy + 7), (cx, cy + 14), (cx + 6, cy + 7), "#6a3418", 3.2)]
    f.oval(cx, cy, 17, 21, FACE_H, det, hl=(0.4, 0.24, 0.24, 0.14), lw=f.lw * 0.6)


def screamy(f):
    """c_lp_moa: a pale puff whose top rises off-centre into a wisp curling over to the right, two big
    salmon eyes under heavy lowered lids (the sleepy look), a small wavy mouth, and two white feet."""
    f.oval(17, 93, 12, 6, GHOST, lw=f.lw * 0.7)
    f.oval(83, 94, 12, 6, GHOST, lw=f.lw * 0.7)
    body = [(50, 89), (24, 87), (12, 78), (9, 66), (10, 52), (14, 43), (23, 35), (38, 29), (54, 22), (52, 14),
            (54, 6), (62, 1), (72, 1), (80, 5), (82, 11), (76, 8), (69, 9), (67, 14), (72, 20), (79, 26), (82, 36),
            (89, 48), (91, 62), (88, 78), (76, 87)]
    det = []
    for ex in (32, 68):   # the eyes: a flat lid along the top, the salmon eye hanging under it
        e = [(ex - 14, 60), (ex + 14, 60), (ex + 13, 70), (ex + 4, 81), (ex - 8, 79), (ex - 14, 70)]
        mx, my = ex, 68
        det.append(f.poly(e, GHOST_EYE["line"], tension=0.4))
        det.append(f.poly([(mx + (x - mx) * 0.84, my + (y - my) * 0.78 + 1.5) for x, y in e], GHOST_EYE["fill"],
                          tension=0.4))
        det.append(f.line([(ex - 15, 59), (ex + 15, 59)], GHOST_EYE["line"], 2.8))
    det.append(stroke_svg("M%.1f %.1f Q%.1f %.1f %.1f %.1f T%.1f %.1f" % (
        *f.q([(43, 82)])[0], *f.q([(46.5, 79)])[0], *f.q([(50, 82)])[0], *f.q([(57, 82)])[0]), GHOST["line"], 2.4 * f.k))
    det += [f.dot(16, 82, 5, 3, "#eebcb0"), f.dot(84, 82, 5, 3, "#eebcb0")]
    f.shape(body, GHOST, det, hl=(0.36, 0.42, 0.2, 0.1), seed=15, tension=0.55)


def heart(f):
    """the HP heart: pink, two eyes, behind the face at the top right"""
    pts = [(50, 96), (14, 62), (4, 34), (14, 10), (34, 6), (50, 22), (66, 6), (86, 10), (96, 34), (86, 62)]
    f.shape(pts, HEART, [f.dot(40, 40, 5, 11, "#7a1028"), f.dot(60, 40, 5, 11, "#7a1028")],
            hl=(0.3, 0.22, 0.2, 0.12), seed=20, tension=0.5)


FRIENDS = {"blooey": blooey, "jerry": jerry, "torque": torque, "hayzee": hayzee, "screamy": screamy}


# ----------------------------------------------------------------- icons --
def render(p):
    im = Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=p.svg())))).convert("RGBA")
    return im


def sticker(art, size):
    """the HUD sticker: a white border round the art's silhouette, a light grey inner edge and a dark
    grey rim heavier on the bottom right, as on the game's partner HUD icons"""
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


def make(friend, kind, soot=False):
    name = f"{friend}{'_soot' if soot else ''}_{kind}"
    folder = os.path.join(OUT, friend)
    draw = FRIENDS[friend]
    kw = {"soot": True} if soot else {}
    if kind == "menu":
        p = Part(name, 40, 40)
        f = Fig(p, 1.5, 0.5, 0.37, line=2.4)
        draw(f, **kw)
        im = harden(render(p))
    else:
        p = Part(name, 48, 48)
        heart(Fig(p, 23, 3, 0.21, line=2.6))
        f = Fig(p, 3, 9, 0.36, line=2.6)
        draw(f, **kw)
        im = sticker(render(p), 48)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, name + ".svg"), "w", encoding="utf-8") as fh:
        fh.write(p.svg())
    im.save(os.path.join(folder, name + ".png"))
    return name


def preview(names):
    W, H = 640, 300
    c = Image.new("RGBA", (W, H), (120, 116, 130, 255))
    d = ImageDraw.Draw(c)
    d.text((8, 6), "Partner icons: hud 48x48 and menu 40x40, at x2; below, at x1 (game size)", fill=(255, 255, 255, 255))
    x = 8
    for friend, soot in (("blooey", False), ("jerry", False), ("torque", False), ("hayzee", False),
                         ("screamy", False), ("blooey", True)):
        stem = friend + ("_soot" if soot else "")
        hud = Image.open(os.path.join(OUT, friend, f"{stem}_hud.png")).convert("RGBA")
        menu = Image.open(os.path.join(OUT, friend, f"{stem}_menu.png")).convert("RGBA")
        c.alpha_composite(hud.resize((96, 96), Image.NEAREST), (x, 24))
        c.alpha_composite(menu.resize((80, 80), Image.NEAREST), (x + 8, 126))
        c.alpha_composite(hud, (x, 214))
        c.alpha_composite(menu, (x + 52, 218))
        d.text((x, 270), stem, fill=(255, 255, 255, 255))
        x += 104
    return c


def build():
    names = []
    for friend in FRIENDS:
        clean(os.path.join(OUT, friend))
        names += [make(friend, "hud"), make(friend, "menu")]
    names += [make("blooey", "hud", soot=True), make("blooey", "menu", soot=True)]
    preview(names).convert("RGB").save(os.path.join(OUT, "preview.png"))
    return names


if __name__ == "__main__":
    print(", ".join(build()))
