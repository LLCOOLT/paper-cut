"""The race scoreboard (Super Luigi ch3): the giant board behind the first corner that tips over onto
the pile-up (and buries Revvo).

Run: python scoreboard.py -> ../scoreboard/*.svg, *.png, parts.json, preview.png
ch3.md, New assets 7: "a big flat board on legs, with a fall animation: can be a plain textured box
with a fall done in script". Cards: the board's face (the standings at the start: Revvo on top, Big
Green 01 last), its back (plain struts), and the legs; the board pivots on its bottom edge to fall.
8 texels/unit, outlined, lit from the top left. The board's lettering is lettering.py's hand-cut capitals.
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, fill_svg, poly_path, smooth_path, ellipse_pts, star_pts, clean  # noqa
import lettering as lt  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "scoreboard")
R = Rig("sl_scoreboard", OUT)

FRAME = dict(fill="#d23a3a", light="#ec6a5e", shadow="#a01e26", line="#3e0a0e")
PANEL = "#1e2a52"
PANEL_ROW = "#28386a"
WOOD = dict(fill="#b8875a", light="#d4a678", shadow="#8a5e36", line="#3a220c")
STEEL = dict(fill="#9aa0b0", light="#c2c8d4", shadow="#6e7486", line="#1e2230")
BULB = "#ffe68a"
GOLDTXT = "#ffd24a"
TEXT = "#f4f0e6"
# the grid at the start: position, number, name, the kart's colour (kart/README.md's recolours). Only Revvo
# and Luigi are named in ch3.md, so the others go by their number.
GRID = [(1, "13", "REVVO", "#d8303a"), (2, "07", "KART", "#3a6ad8"), (3, "22", "KART", "#f0c83a"),
        (4, "45", "KART", "#8a4ac8"), (5, "88", "KART", "#f08a2a"), (6, "64", "KART", "#eef0f2"),
        (7, "99", "KART", "#2a2a32"), (8, "01", "LUIGI", "#3aaa3a")]


def text(c, x, y, s, size, colour, anchor="start"):
    """painted board lettering (lettering.py's hand-cut capitals): cap height about 0.72 of `size` units,
    baseline at y"""
    px, py = c.u(x, y)
    return lt.text_svg(s, px, py, 0.72 * size * c.U, colour, anchor=anchor, seed=sum(map(ord, s)), jitter=0.2,
                       tilt=2.5, bob=0.03)


def face():
    c = R.card("board_face", (-30.0, -1.0, 30.0, 33.0))
    outer = [(-29.0, 0.0), (29.0, 0.0), (29.2, 31.6), (-29.2, 31.6)]
    inner = c.pts([(-27.0, 2.0), (27.0, 2.0), (27.0, 29.6), (-27.0, 29.6)])
    det = [fill_svg(poly_path(inner), PANEL)]
    # bulbs round the frame
    for k in range(15):
        x = -27.6 + k * 55.2 / 14
        for y in (0.95, 30.6):
            det.append(fill_svg(c.ell(x, y, 0.42, 0.42, 8), BULB))
    for k in range(1, 8):
        y = 0.95 + k * 29.65 / 8
        for x in (-28.1, 28.1):
            det.append(fill_svg(c.ell(x, y, 0.42, 0.42, 8), BULB))
    # the header: CIRCUIT BREAK in gold between two checkered flags, LAP 1/3 on the right
    det.append(text(c, 0.0, 25.6, "CIRCUIT BREAK", 3.6, GOLDTXT, "middle"))
    det.append(stroke_svg(c.line([(-26.0, 24.4), (26.0, 24.4)]), GOLDTXT, 2))
    for fx in (-25.6, 22.0):
        for i in range(4):
            for j in range(3):
                if (i + j) % 2 == 0:
                    det.append(fill_svg(poly_path(c.pts([(fx + i * 0.9, 25.4 + j * 0.9), (fx + i * 0.9 + 0.9, 25.4 + j * 0.9),
                                                         (fx + i * 0.9 + 0.9, 26.3 + j * 0.9), (fx + i * 0.9, 26.3 + j * 0.9)])),
                                        TEXT))
    # eight rows, two columns of four
    for i, (pos, num, name, col) in enumerate(GRID):
        colx = -26.0 if i < 4 else 0.8
        y = 21.2 - (i % 4) * 4.7
        det.append(fill_svg(poly_path(c.pts([(colx, y - 1.5), (colx + 25.2, y - 1.5), (colx + 25.2, y + 2.4),
                                             (colx, y + 2.4)])), PANEL_ROW))
        det.append(text(c, colx + 0.6, y - 0.6, str(pos), 2.8, GOLDTXT if pos == 1 else TEXT))
        det.append(fill_svg(poly_path(c.pts([(colx + 3.6, y - 0.9), (colx + 7.6, y - 0.9), (colx + 7.6, y + 1.8),
                                             (colx + 3.6, y + 1.8)])), col))
        det.append(stroke_svg(poly_path(c.pts([(colx + 3.6, y - 0.9), (colx + 7.6, y - 0.9), (colx + 7.6, y + 1.8),
                                               (colx + 3.6, y + 1.8)])), "#0a0e20", 1.5))
        det.append(text(c, colx + 5.6, y - 0.3, num, 1.9, "#ffffff" if col in ("#2a2a32", "#3a6ad8", "#8a4ac8")
                        else "#14141c", "middle"))
        det.append(text(c, colx + 8.4, y - 0.6, name, 2.3, TEXT))
        if pos == 1:   # a little crown by the leader
            det.append(fill_svg(poly_path(c.pts([(colx + 22.4, y - 0.6), (colx + 24.6, y - 0.6), (colx + 24.8, y + 1.6),
                                                 (colx + 24.0, y + 0.7), (colx + 23.5, y + 1.8), (colx + 23.0, y + 0.7),
                                                 (colx + 22.2, y + 1.6)])), GOLDTXT))
    c.shape(outer, FRAME, det, hl=(0.3, 0.1, 0.3, 0.05), seed=1, tension=0.1, jitter=0.4, lo=(6, 6), so=(8, 8))
    return c.save((0.0, 0.0), "the board's face: CIRCUIT BREAK and the starting grid (Revvo first, Luigi last), "
                              "a red frame with bulbs; pivot at the middle of the bottom edge (it falls about it)")


def back():
    c = R.card("board_back", (-30.0, -1.0, 30.0, 33.0))
    outer = [(-29.0, 0.0), (29.0, 0.0), (29.2, 31.6), (-29.2, 31.6)]
    det = []
    for k in range(8):   # plank seams
        y = 0.0 + k * 31.6 / 8
        det.append(stroke_svg(c.line([(-29.2, y), (29.2, y)]), WOOD["shadow"], 2.5))
    for x in (-20.0, 0.0, 20.0):   # vertical struts
        det.append(fill_svg(poly_path(c.pts([(x - 1.2, 0.0), (x + 1.2, 0.0), (x + 1.2, 31.6), (x - 1.2, 31.6)])),
                            WOOD["shadow"]))
        det.append(stroke_svg(c.line([(x - 1.0, 0.2), (x - 1.0, 31.4)]), WOOD["light"], 1.6))
    det.append(stroke_svg(c.line([(-20.0, 2.0), (0.0, 29.0), (20.0, 2.0)]), WOOD["shadow"], 9))
    c.shape(outer, WOOD, det, hl=(0.34, 0.22, 0.3, 0.12), seed=2, tension=0.1, jitter=0.4, lo=(6, 6), so=(8, 8))
    return c.save((0.0, 0.0), "the board's back: planks and struts, for when it lies face down; same size and pivot")


def legs():
    c = R.card("legs", (-30.0, -1.0, 30.0, 23.0))
    for x in (-22.0, 22.0):
        leg = [(x - 1.2, 0.0), (x + 1.2, 0.0), (x + 1.0, 22.0), (x - 1.0, 22.0)]
        c.shape(leg, STEEL, [stroke_svg(c.line([(x - 0.4, 0.6), (x - 0.4, 21.4)]), STEEL["light"], 2)], seed=3,
                tension=0.1, jitter=0.3, lo=(3, 3), so=(4, 4), soft=1.5)
        foot = [(x - 2.6, 0.0), (x + 2.6, 0.0), (x + 2.2, 1.4), (x - 2.2, 1.4)]
        c.shape(foot, STEEL, seed=4, tension=0.1, jitter=0.3, lo=(2, 2), so=(3, 3), soft=1)
    for y0, y1 in ((3.0, 19.0), (19.0, 3.0)):   # the X brace
        brace = [(-21.0, y0 - 0.6), (21.0, y1 - 0.6), (21.0, y1 + 0.6), (-21.0, y0 + 0.6)]
        c.shape(brace, STEEL, seed=5, tension=0.05, jitter=0.2, lo=(2, 2), so=(2, 2), soft=1, grow=1.5, inner=1.8)
    return c.save((0.0, 0.0), "two steel legs with an X brace; the board's bottom edge sits on their tops (y 22)")


def preview():
    W, H = 1060, 520
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    lg = Image.open(os.path.join(OUT, "legs.png")).convert("RGBA")
    fc = Image.open(os.path.join(OUT, "board_face.png")).convert("RGBA")
    bk = Image.open(os.path.join(OUT, "board_back.png")).convert("RGBA")
    # standing: legs on the ground at y 480, board's foot at 22 units up
    gy = 480
    c.alpha_composite(lg, (10, gy - 184 + 8))
    c.alpha_composite(fc, (10, gy - 176 - 264 - 8 + 8 + 8))
    d.text((10, 6), "standing (board_face over legs)", fill=(40, 30, 30, 255))
    c.alpha_composite(bk.resize((bk.width // 2, bk.height // 2), Image.NEAREST), (540, 40))
    d.text((540, 22), "board_back (x0.5)", fill=(40, 30, 30, 255))
    fallen = fc.resize((fc.width // 2, fc.height // 2), Image.NEAREST).rotate(-25, expand=True, resample=Image.NEAREST)
    c.alpha_composite(fallen, (800, gy - fallen.height))
    d.text((800, gy - fallen.height - 18), "tipping over (x0.5)", fill=(40, 30, 30, 255))
    d.rectangle([0, gy, W, gy + 2], fill=(180, 170, 150, 255))
    d.rectangle([520, gy - 192, 524, gy], fill=(60, 140, 60, 255))
    d.text((530, gy - 190), "Luigi", fill=(40, 90, 40, 255))
    d.text((10, 500), "The race scoreboard, 1:1 at 8 texels/unit unless marked", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    face()
    back()
    legs()
    R.meta["board_face"]["model_xy"] = [0.0, 22.0]
    R.meta["board_back"]["model_xy"] = [0.0, 22.0]
    R.write_json({"draw_order": ["legs", "board_back (when face down)", "board_face"],
                  "rest_note": "model_xy is where each pivot sits standing; the board falls about its pivot"})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
