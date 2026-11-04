"""Luigi's drawn menu art: the level-up BP panel (battle_common.tpl image 51, JP 38), and the drawn
stand-ins (fallbacks) for what the game makes from the disc.

Luigi's faces in the game's Mario icons (ICON_1A3, 19E, 1A1) and the pause menu's first tab are made
on the player's machine from the disc's own images (../recipes/luigi_icons.recipe and
luigi_tabs.recipe, RECIPES.md): nothing from the disc is in the repo. This script draws, in the
paper style, without the disc:
  drawn/ui/levelup   level-up's BP panel: a gold tile, a coin with Luigi's head
  drawn/ui/tab_us                       the "Luigi" tab, used if the recipe's can't be made
(drawn/luigi_{hud,head,emblem} are the faces' fallbacks, drawn by this script before the recipes,
as of commit be60e78b4; luigi_tab_jp.py draws drawn/ui/tab_jp.)

Run: python luigi_icons.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, harden  # noqa
from icons import Fig, render  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "icons", "luigi")

CAP = dict(fill="#39a639", light="#6cc34e", shadow="#2a8030", line="#182810")     # c_luigi's cap, STYLE R5/R6b
SKIN = dict(fill="#f0bc88", light="#f8d4a8", shadow="#d89c68", line="#5a2c10")
HAIR = dict(fill="#5a3418", light="#74482a", shadow="#42240e", line="#1e0e04")
MOUSTACHE = dict(fill="#3e2210", light="#583418", shadow="#2a1608", line="#140802")
DISC = dict(fill="#f2eeee", light="#faf8f8", shadow="#d8d2d4", line="#182810")
EYE = "#1e140c"
# the emblem: everything in the coin's golds, the line a dark gold-brown
GOLD = dict(fill="#e0b020", light="#f2cc48", shadow="#b88a10", line="#5a3606")
GOLD_IN = dict(fill="#e8bc30", light="#f2cc48", shadow="#c49618", line="#6a4008")


def head(f, gold=False):
    """Luigi's head in profile facing right: the cap and its brim, the L disc, his hair at the back,
    the ear, the long face and chin, the eye under the brim, the big nose and the moustache."""
    cap, skin, hair, tache, disc = (GOLD_IN, GOLD_IN, GOLD_IN, GOLD_IN, GOLD_IN) if gold else \
        (CAP, SKIN, HAIR, MOUSTACHE, DISC)
    line = GOLD_IN["line"] if gold else EYE
    # the face, with the eye under the brim and the mouth's corner: from under the cap to the long chin
    eye = [f.dot(60, 51, 6.0, 9.5, line)] + ([] if gold else [f.dot(58.5, 47, 2.2, 3.2, "#f6f2f2")])
    f.shape([(14, 40), (50, 36), (80, 40), (88, 54), (86, 72), (78, 90), (60, 99), (40, 97), (24, 88), (14, 70),
             (11, 52)], skin, eye + [f.curve((56, 84), (64, 89), (72, 84), line, 2.6)], hl=(0.42, 0.22, 0.3, 0.12),
            seed=32)
    # brown hair round the ear at the back
    f.shape([(10, 42), (26, 40), (34, 52), (32, 70), (22, 76), (12, 68), (8, 54)], hair, seed=31, light=False,
            lw=f.lw * 0.8)
    # the ear
    f.oval(24, 58, 8, 11, skin, [f.curve((22, 52), (28, 58), (22, 64), skin["line"], 2.2)], lw=f.lw * 0.7)
    # the moustache: two lobes under the nose
    f.shape([(52, 70), (62, 64), (76, 66), (92, 66), (99, 74), (92, 81), (80, 77), (68, 82), (55, 79)],
            tache, seed=33, light=False, lw=f.lw * 0.8)
    # the nose, big and round
    f.oval(84, 58, 13, 11, skin, hl=(0.36, 0.26, 0.3, 0.2))
    # the cap: a dome over the top of the head, the brim out to the right
    f.shape([(4, 40), (6, 26), (18, 12), (38, 3), (60, 4), (78, 13), (86, 27), (99, 32), (98, 39), (82, 41),
             (58, 38), (32, 38), (12, 43)], cap, hl=(0.4, 0.2, 0.4, 0.14), seed=34)
    # the white disc and its L
    f.oval(56, 20, 11, 10, disc, [f.line([(52.5, 14), (52.5, 26), (61, 26)], cap["line"] if gold else CAP["fill"],
                                         4.2)], lw=f.lw * 0.7)


def coin(f):
    """the emblem's gold coin: a dark rim, a lighter face"""
    f.oval(50, 50, 48, 48, GOLD, hl=(0.36, 0.24, 0.3, 0.16))


# ------------------------------------------------------- the menu textures --
# Named as Dolphin names a replacement (tex1_WxH_<XXH64 of the CMPR data>_14), for the "*#luigi"
# texture set (superluigi_aurora.c).
UI = os.path.join(OUT, "ui")
DRAWN_UI = os.path.join(OUT, "drawn", "ui")
LEVELUP = "tex1_128x128_4d465d3c205b172e_14"   # battle_common.tpl image 51 (JP 38): level-up's BP panel
TILE = dict(fill="#d4aa3c", light="#e8c45a", shadow="#a8822a", line="#4a3006")


def tab(p):
    """the pause menu's first tab, as Mario's (win.tpl image 1): a red board with a darker ragged
    edge, "Luigi" in chunky pale-pink cut letters with a deeper pink drop below right"""
    from papercut import smooth_path
    board = smooth_path([(3, 4), (48, 2), (93, 4), (94, 20), (93, 36), (48, 38), (3, 36), (2, 20)], tension=0.3,
                        jitter=0.6, seed=51)
    p.shape(board, "#cc4a36", "#a01a04", "#d8604a", "#b03a26", (3, 3), (3, 3), soft=2.0, grow=1.6, inner=1.6,
            bias=(0.6, 0.8))
    # glyphs on a 12-unit grid (x-height from 4, the g's tail to 15.5): L u i g i
    bars = {
        "L": (7, [(0, 0, 3, 12), (0, 9, 7, 12)]),
        "u": (8, [(0, 4, 3, 12), (5, 4, 8, 12), (0, 9.2, 8, 12)]),
        "i": (3, [(0, 4.4, 3, 12), (0, 0.4, 3, 3)]),
        "g": (8, [(0, 4, 8, 6.6), (0, 4, 3, 11), (5, 4, 8, 15.5), (0, 8.6, 8, 11), (0, 13, 8, 15.5)]),
    }
    s, x0, y0 = 2.05, 13.0, 7.5
    word = "Luigi"
    import random
    rnd = random.Random(7)
    layers = []
    x = x0
    for ch in word:
        w, rects = bars[ch]
        tilt, bob = rnd.uniform(-0.06, 0.06), rnd.uniform(-0.6, 0.6)
        polys = []
        for (a, b, c, d) in rects:
            pts = [(a, b), (c, b), (c, d), (a, d)]
            pts = [(px + rnd.uniform(-0.25, 0.25), py + rnd.uniform(-0.25, 0.25)) for px, py in pts]
            polys.append([(x + (px + (py - 6) * tilt) * s, y0 + bob + py * s) for px, py in pts])
        layers.append(polys)
        x += (w + 1.4) * s
    path = "".join("M" + " L".join("%.1f %.1f" % pt for pt in poly) + " Z" for polys in layers for poly in polys)
    shift = lambda dx, dy: "".join("M" + " L".join("%.1f %.1f" % (a + dx, b + dy) for a, b in poly) + " Z"
                                   for polys in layers for poly in polys)
    p.raw(f'<path d="{shift(1.6, 1.8)}" fill="#a82a1c" fill-rule="nonzero"/>')
    p.raw(f'<path d="{path}" fill="#b83a2c" stroke="#b83a2c" stroke-width="1.8" stroke-linejoin="round"/>')
    p.raw(f'<path d="{path}" fill="#f4dde0" fill-rule="nonzero"/>')


def levelup(p):
    """level-up's BP panel, as Mario's (battle_common.tpl image 51): a gold tile with a bevelled
    dark edge, and on it a gold coin with Luigi's head drawn in a dark gold line"""
    from papercut import smooth_path
    tile = smooth_path([(6, 6), (64, 4), (122, 6), (124, 64), (122, 122), (64, 124), (6, 122), (4, 64)],
                       tension=0.25, jitter=0.4, seed=61)
    p.shape(tile, TILE["fill"], TILE["line"], TILE["light"], TILE["shadow"], (6, 6), (7, 7), soft=3.0, grow=3.0,
            inner=3.0, bias=(1.2, 1.6))
    f = Fig(p, 20, 20, 0.88, line=4.0)
    coin(f)
    head(Fig(p, 33, 32, 0.62, line=3.6), gold=True)


def make_ui():
    """the drawn level-up panel and tab into drawn/ui/, the fallbacks of their recipes"""
    out = []
    for folder, name, w, h, draw in ((DRAWN_UI, "levelup", 128, 128, levelup), (DRAWN_UI, "tab_us", 96, 40, tab)):
        os.makedirs(folder, exist_ok=True)
        p = Part(name, w, h)
        draw(p)
        im = harden(render(p))
        with open(os.path.join(folder, name + ".svg"), "w", encoding="utf-8") as fh:
            fh.write(p.svg())
        im.save(os.path.join(folder, name + ".png"))
        out.append(name)
    return out


if __name__ == "__main__":
    print(", ".join(make_ui()))
