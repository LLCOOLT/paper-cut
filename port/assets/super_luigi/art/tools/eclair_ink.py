"""Princess Eclair, drawn: `python eclair_ink.py` writes port/assets/super_luigi/art/eclair/ink/*.png and parts.json.

One idea: she's a pastry. A small, round princess whose bell gown is cut in petals like a cream puff's case, cream with
a toasted-gold scalloped hem, under a big soft bob of golden curls with a bun at the back. A slim tiara carries the
curved golden plate (the compass's seventh piece) that she lifts out in chapter 6. The design comes from the ttyd style
model's concepts (art/designs/eclair/concept_eclair_a_seed3.png), redrawn card by card: no generated pixels are used.
Every contour is placed by hand; nothing is mirrored. Her eyes and mouths at rest come from the game's face cards by
recipe (art/recipes/model_eclair.recipe); the ones drawn here are their fallback.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "..", "tools", "port"))
from ink import Card, cmpr_round_trip, save  # noqa: E402

OUT = os.path.join(HERE, "..", "eclair", "ink")
LINE = "#3e2414"  # toasted brown, one for the whole princess
CREAM, CREAM_T, CREAM_D = "#f7edd2", "#ead7a8", "#d3b67e"
GOLD, GOLD_D, TOAST = "#ebbb4c", "#c48a2a", "#b9752c"
SKIN, SKIN_D, BLUSH = "#f9dcc2", "#e8b597", "#f1a79a"
HAIR, HAIR_T, HAIR_D = "#f5c84a", "#e1a630", "#c7861f"
GLOVE, GLOVE_D = "#f7f3ea", "#c9c0b6"
GEM = "#e0574a"


def skirt():
    """the bell gown: cut in petals like a cream puff's paper case, a toasted-gold scalloped hem; flat cream with a
    hard second tone on every other petal, an airbrush falling to the lower right"""
    c = Card(128, 96, LINE)
    hem = [(116, 90), (106, 85), (96, 92), (85, 86), (74, 93), (62, 87), (51, 93), (40, 86), (29, 92), (18, 85), (9, 89)]
    s = c.shape([(47, 3), (64, 1, 1.2), (81, 4), (90, 19), (101, 41), (113, 64), (122, 81, 1.3, "c")] + hem[:1] + hem[1:] +
                [(3, 83, 1.3, "c"), (11, 63), (24, 40), (36, 18)], CREAM, width=3.3, seed=201,
                air=dict(dark=CREAM_D, amount=0.55, to=(5, 6), blur=10))
    # every other petal a shade deeper: the case's folds
    for pts in ([(60, 2), (68, 2), (98, 88), (86, 90), (73, 60)], [(42, 4), (52, 3), (55, 60), (51, 92), (40, 88), (44, 50)],
                [(30, 14), (36, 10), (20, 60), (18, 88), (6, 88), (14, 56)]):
        s.tone(pts, CREAM_T)
    # the toasted hem: a band following the scallops
    band = [(124, 70), (128, 96), (0, 96), (0, 72), (10, 76), (22, 75), (35, 77), (48, 79), (62, 78), (76, 79), (90, 77),
            (104, 75), (116, 72)]
    s.tone(band, GOLD)
    s.tone([(84, 82), (100, 79), (116, 78), (126, 86), (126, 96), (84, 96)], GOLD_D)
    # the petals' edges: from each scallop's notch up toward the waist, tapering away
    for (x0, y0), (x1, y1) in (((106, 85), (80, 22)), ((85, 86), (72, 30)), ((62, 87), (62, 34)), ((40, 86), (52, 30)),
                               ((18, 85), (40, 24))):
        mx, my = (x0 + x1) / 2 + (2 if x0 > 64 else -2), (y0 + y1) / 2
        s.ink([(x0, y0, 2.8), (mx, my, 1.6), (x1, y1, 0.1)])
    s.ink([(6, 76, 1.2), (30, 79, 2.0), (62, 81, 2.4), (96, 79, 2.0), (122, 74, 1.0)])
    return c


def bodice():
    """a short cream bodice, pointed at the waist, three gold buttons a little off the middle"""
    c = Card(48, 48, LINE)
    s = c.shape([(10, 9), (22, 4), (34, 5), (42, 12), (40, 26), (35, 38), (26, 46, 1.4, "c"), (15, 38), (8, 26)], CREAM, width=3.2, seed=211,
                air=dict(dark=CREAM_D, amount=0.6, to=(4, 4), blur=6))
    s.tone([(30, 2), (48, 6), (48, 48), (28, 48), (34, 26)], CREAM_T)
    s.tone([(8, 4), (40, 2), (41, 11), (24, 13), (8, 12)], GOLD)
    s.ink([(9, 12, 1.6), (24, 13, 2.2), (41, 11, 1.4)])
    for x, y in ((21, 21), (21, 29), (22, 37)):
        s.tone([(x - 3, y - 2), (x, y - 3), (x + 3, y - 1), (x + 2, y + 2), (x - 2, y + 2)], GOLD)
    return c


def arm(front=True):
    """a round puffed sleeve on a slim gloved arm; shoulder pivot"""
    c = Card(32, 56, LINE)
    fill = CREAM if front else CREAM_T
    glove = GLOVE if front else "#e3ddd2"
    c.shape([(12, 22), (19, 21), (21, 36), (21, 50, 1.2), (16, 54, 1.3), (11, 50, 1.2), (10, 36)], glove, width=2.8, seed=221 if front else 223,
            air=dict(dark=GLOVE_D, amount=0.7, to=(3, 2), blur=4))
    s = c.shape([(7, 7), (15, 2), (25, 4), (30, 12), (28, 22), (20, 27, 1.3), (10, 26, 1.3), (3, 19)], fill, width=3.0,
                seed=225 if front else 227, air=dict(dark=CREAM_D, amount=0.75, to=(3, 4), blur=5))
    s.ink([(14, 25, 2.0), (16, 18, 1.2), (19, 12, 0.1)])
    return c


def hand(kind):
    """small round gloved hands, as Peach's are"""
    c = Card(32, 32, LINE)
    air = dict(dark=GLOVE_D, amount=0.7, to=(3, 3), blur=4)
    if kind == "open":  # relaxed, fingers down together, thumb forward
        c.shape([(11, 4), (19, 3), (22, 10), (24, 20), (21, 27), (14, 29), (9, 26), (7, 19), (4, 15), (5, 11), (9, 12)], GLOVE, width=2.8, seed=231, air=air)
        c.ink([(14, 28, 1.8), (14, 22, 1.0), (15, 18, 0.1)])
    elif kind == "palm":  # raised and open, fingers up (for an arm held up)
        c.shape([(10, 3), (16, 2), (21, 6), (25, 3), (28, 6), (24, 14), (23, 22), (19, 28), (12, 29), (8, 24), (7, 14)], GLOVE, width=2.8, seed=233, air=air)
        c.ink([(15, 3, 1.8), (15, 10, 1.0), (16, 14, 0.1)])
    else:  # clasp: both hands held together at the chest
        c.shape([(6, 9), (13, 4), (21, 4), (27, 9), (28, 17), (24, 25), (16, 28), (8, 25), (4, 17)], GLOVE, width=2.8, seed=235, air=air)
        c.ink([(16, 4, 1.8), (15, 13, 1.4), (16, 22, 0.1)])
        c.ink([(5, 15, 1.6), (11, 15, 0.8), (14, 16, 0.1)])
    return c


def head():
    """round, soft, the cheek a little forward"""
    c = Card(64, 64, LINE)
    c.shape([(30, 4), (45, 7), (55, 18), (58, 33), (53, 48), (42, 58, 1.3), (28, 61, 1.4), (15, 56, 1.3), (7, 44), (5, 28), (11, 13)],
            SKIN, width=3.0, seed=241, air=dict(dark=SKIN_D, amount=0.7, to=(5, 4), blur=7))
    return c


def hair_back():
    """the big soft bob behind her head: lumpy with curls, a bun up at the back"""
    c = Card(96, 88, LINE)
    s = c.shape([(28, 16), (38, 8), (50, 6), (58, 2), (70, 3), (77, 10), (76, 18), (85, 24), (90, 36), (92, 50), (88, 60), (91, 70),
                 (84, 80, 1.3), (72, 84), (64, 80), (54, 85), (44, 80), (34, 84, 1.3), (22, 80), (14, 72), (8, 60), (6, 46), (12, 30)],
                HAIR, width=3.4, seed=251, air=dict(dark=HAIR_D, amount=0.55, to=(5, 5), blur=8))
    s.tone([(60, 20), (90, 28), (96, 88), (64, 88), (74, 54)], HAIR_T)
    for pts in ([(84, 79, 2.6), (80, 70, 1.8), (82, 64, 0.8), (86, 64, 0.1)], [(54, 84, 2.4), (56, 76, 1.6), (60, 72, 0.1)],
                [(22, 79, 2.4), (24, 71, 1.4), (28, 68, 0.1)], [(76, 17, 2.6), (68, 14, 1.6), (62, 16, 0.1)],
                [(90, 48, 2.2), (84, 46, 1.2), (80, 48, 0.1)]):
        s.ink(pts)
    return c


def hair_front():
    """the fringe: three fat curls swept back from the face"""
    c = Card(64, 40, LINE)
    s = c.shape([(4, 30, 1.3, "c"), (6, 18), (14, 8), (26, 3), (40, 3), (52, 8), (60, 18), (60, 28), (54, 34), (46, 30), (40, 34), (32, 28),
                 (24, 33), (16, 27), (10, 33, 1.3)], HAIR, width=3.2, seed=261, air=dict(dark=HAIR_T, amount=0.6, to=(4, 4), blur=5))
    s.ink([(10, 32, 2.4), (13, 24, 1.6), (19, 20, 0.1)])
    s.ink([(24, 32, 2.4), (28, 23, 1.6), (34, 20, 0.1)])
    s.ink([(40, 33, 2.4), (44, 24, 1.4), (50, 22, 0.1)])
    return c


def tiara(plate=True):
    """a slim gold band; set in it, the curved golden plate with its little red stone"""
    c = Card(40, 32, LINE)
    c.shape([(3, 27), (10, 22), (20, 20), (30, 21), (37, 26), (36, 31), (20, 28), (4, 31)], GOLD, width=2.4, seed=271,
            air=dict(dark=GOLD_D, amount=0.6, to=(2, 2), blur=3))
    if plate:
        s = c.shape([(9, 22), (12, 12), (18, 5), (25, 4), (30, 9), (26, 10), (21, 13), (19, 20)], GOLD, width=2.6, seed=273,
                    air=dict(dark=GOLD_D, amount=0.7, to=(3, 3), blur=3))
        s.tone([(19, 7), (24, 6), (25, 10), (20, 12)], "#f6dc86")
        c.shape([(14, 15), (18, 13), (20, 17), (16, 19)], GEM, width=1.2, seed=275)
    return c


def piece(glow=False):
    """the plate she lifts out: the curve alone (or glowing)"""
    c = Card(24, 24, LINE)
    s = c.shape([(4, 21), (6, 11), (11, 4), (18, 3), (22, 7), (17, 9), (13, 13), (12, 20)], "#ffe48a" if glow else GOLD, width=2.4,
                seed=281, air=None if glow else dict(dark=GOLD_D, amount=0.7, to=(2, 3), blur=3))
    s.tone([(11, 6), (17, 4), (18, 8), (13, 10)], "#fff6cc" if glow else "#f6dc86")
    c.shape([(7, 13), (11, 11), (12, 15), (8, 17)], GEM, width=1.0, seed=283)
    return c


def shoes():
    c = Card(48, 16, LINE)
    c.shape([(2, 12), (6, 5), (16, 3), (22, 8), (21, 14), (8, 15)], GOLD, width=2.4, seed=291, air=dict(dark=GOLD_D, amount=0.6, to=(2, 2), blur=3))
    c.shape([(26, 11), (30, 4), (40, 3), (46, 7), (44, 14), (30, 14)], GOLD_D, width=2.4, seed=293)
    return c


def eyes(kind):
    c = Card(32, 24, LINE)
    if kind == "open":
        c.shape([(4, 5), (9, 4), (11, 12), (8, 19), (4, 17), (2, 10)], "#3a2316", width=1.0, seed=301)
        c.shape([(18, 4), (24, 4), (26, 12), (23, 19), (18, 17), (16, 10)], "#3a2316", width=1.0, seed=302)
    elif kind == "happy":
        c.ink([(2, 14, 1.4), (6, 9, 2.4), (11, 13, 1.4)])
        c.ink([(16, 13, 1.4), (21, 8, 2.4), (26, 12, 1.4)])
    else:
        c.ink([(2, 12, 1.6), (6, 14, 2.4), (11, 12, 1.6)])
        c.ink([(16, 12, 1.6), (21, 14, 2.4), (26, 12, 1.6)])
    return c


def mouth(kind):
    c = Card(16, 16, LINE)
    if kind == "open":
        c.shape([(3, 4), (12, 4), (10, 11), (5, 11)], "#b23a36", width=1.6, seed=311)
    else:
        c.ink([(3, 7, 1.0), (7, 9, 1.8), (12, 7, 1.0)])
    return c


def blush():
    c = Card(16, 8, LINE)
    c.shape([(2, 4), (5, 1), (11, 1), (14, 4), (11, 7), (5, 7)], BLUSH, width=0.01, seed=321)
    return c


# name, card, pivot in texels, where the pivot sits (model units), what it is
CARDS = {
    "hair_back": (hair_back, (48, 80), (0.8, 15.4), "the bob and bun behind her head; neck pivot"),
    "arm_back": (lambda: arm(False), (16, 8), (1.6, 15.0), "the far arm; shoulder pivot"),
    "shoes": (shoes, (24, 14), (-1.0, 0.0), "her toes under the hem"),
    "skirt": (skirt, (64, 92), (0.0, 0.0), "the bell gown; pivot at the hem's middle"),
    "bodice": (bodice, (26, 46), (0.0, 11.0), "the bodice; pivot at its point"),
    "head": (head, (32, 60), (-0.4, 15.2), "the head; neck pivot"),
    "hair_front": (hair_front, (32, 36), (-0.6, 21.6), "the fringe"),
    "tiara": (tiara, (20, 30), (0.4, 22.6), "the tiara with the plate in it"),
    "tiara_empty": (lambda: tiara(False), (20, 30), (0.0, 0.0), "the tiara once the plate is out"),
    "arm_front": (lambda: arm(True), (16, 8), (-1.4, 15.0), "the near arm; shoulder pivot"),
    "hand_open": (lambda: hand("open"), (15, 5), (0.0, 0.0), "the near hand, relaxed"),
    "hand_palm": (lambda: hand("palm"), (16, 26), (0.0, 0.0), "the hand raised, open"),
    "hand_clasp": (lambda: hand("clasp"), (16, 6), (0.0, 0.0), "both hands held together"),
    "piece": (piece, (12, 20), (0.0, 0.0), "the curved plate, lifted out"),
    "piece_glow": (lambda: piece(True), (12, 20), (0.0, 0.0), "the plate, shining"),
    "eyes": (lambda: eyes("open"), (14, 12), (0.0, 0.0), "fallback: the recipe's eyes replace it"),
    "eyes_shut": (lambda: eyes("shut"), (14, 12), (0.0, 0.0), "fallback"),
    "eyes_happy": (lambda: eyes("happy"), (14, 12), (0.0, 0.0), "eyes shut in a smile"),
    "mouth": (lambda: mouth("shut"), (8, 8), (0.0, 0.0), "fallback"),
    "mouth_open": (lambda: mouth("open"), (8, 8), (0.0, 0.0), "fallback"),
    "blush": (blush, (8, 4), (0.0, 0.0), "a cheek"),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    cards = {}
    for name, (fn, pivot, at, desc) in CARDS.items():
        img = cmpr_round_trip(fn().finish())
        save(img, os.path.join(OUT, name + ".png"))
        cards[name] = {"size": [img.shape[1], img.shape[0]], "pivot": list(pivot), "model_xy": list(at), "desc": desc}
    json.dump({"model": "c_sl_eclair", "texels_per_unit": 8, "cards": cards}, open(os.path.join(OUT, "parts.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
