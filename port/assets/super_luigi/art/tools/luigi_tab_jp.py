"""The Japanese pause tab, ルイージ in place of マリオ, drawn: the stand-in for the one the game makes
from the disc's own tab and kana (../recipes/luigi_tabs.recipe), used only if that can't be made.

Run: python luigi_tab_jp.py -> ../icons/luigi/drawn/ui/tab_jp.svg, .png

Measured on the original (w/jp/win.tpl image 1, decoded into scratch; the JP disc and the EU disc's
Japanese, the same data): 96x40, the red board of the other tabs (a darker ragged edge), and マリオ
in thick pale-pink square-ended strokes (#f4dde0), each kana a little off square, with a dark red
drop below right. The kana here are drawn the same way, stroke by stroke as they're written.
"""
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, harden, smooth_path  # noqa
from icons import render  # noqa

UI = os.path.join(os.path.dirname(__file__), "..", "icons", "luigi", "drawn", "ui")
NAME = "tab_jp"   # the recipe's fallback for tex1_96x40_7971872074128e2f_14

# strokes in a 10-unit cell (y down), as the kana are written: (cell width, strokes)
KANA = [
    (10, [[(3.2, 1.6), (3.2, 5.6), (1.2, 9.0)], [(6.4, 1.2), (6.4, 8.6), (9.2, 6.2)]]),            # ル
    (10, [[(8.6, 0.8), (1.4, 6.0)], [(5.6, 3.6), (5.6, 9.6)]]),                                    # イ
    (8, [[(0.6, 5.0), (7.6, 5.0)]]),                                                               # ー
    (13, [[(0.8, 0.4), (3.0, 2.0)], [(0.2, 4.6), (2.4, 6.2)], [(1.0, 9.6), (5.0, 7.4), (7.6, 3.6)],
          [(9.2, 0.0), (9.6, 1.0)], [(12.4, -0.4), (12.8, 0.6)]]),                                 # ジ
]


def board(p):
    """the tab board, as luigi_icons.py's: red, a darker ragged edge"""
    d = smooth_path([(3, 4), (48, 2), (93, 4), (94, 20), (93, 36), (48, 38), (3, 36), (2, 20)], tension=0.3,
                    jitter=0.6, seed=51)
    p.shape(d, "#cc4a36", "#a01a04", "#d8604a", "#b03a26", (3, 3), (3, 3), soft=2.0, grow=1.6, inner=1.6,
            bias=(0.6, 0.8))


def kana(p):
    rnd = random.Random(11)
    s, x, y0 = 1.85, 7.5, 10.0
    d = ""
    for w, strokes in KANA:
        tilt, bob = rnd.uniform(-0.05, 0.05), rnd.uniform(-0.5, 0.5)
        for st in strokes:
            pts = [(x + (px + (py - 5) * tilt + rnd.uniform(-0.15, 0.15)) * s,
                    y0 + bob + (py + rnd.uniform(-0.15, 0.15)) * s) for px, py in st]
            d += "M" + " L".join("%.1f %.1f" % pt for pt in pts)
        x += (w + 0.6) * s

    def stroke(dx, dy, colour, width):
        moved = re.sub(r"(-?[0-9.]+) (-?[0-9.]+)",
                       lambda m: "%.1f %.1f" % (float(m.group(1)) + dx, float(m.group(2)) + dy), d)
        return (f'<path d="{moved}" fill="none" stroke="{colour}" stroke-width="{width}" '
                f'stroke-linecap="square" stroke-linejoin="round"/>')
    p.raw(stroke(1.5, 1.7, "#a82a1c", 4.8))   # the drop below right
    p.raw(stroke(0, 0, "#b83a2c", 5.6))       # the edge
    p.raw(stroke(0, 0, "#f4dde0", 3.9))       # the face


def build():
    p = Part(NAME, 96, 40)
    board(p)
    kana(p)
    os.makedirs(UI, exist_ok=True)
    with open(os.path.join(UI, NAME + ".svg"), "w", encoding="utf-8") as fh:
        fh.write(p.svg())
    harden(render(p)).save(os.path.join(UI, NAME + ".png"))
    return NAME


if __name__ == "__main__":
    print(build())
