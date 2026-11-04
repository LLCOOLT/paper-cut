"""Minister Crepe, drawn: `python crepe_ink.py` writes port/assets/super_luigi/art/crepe/ink/*.png and parts.json.

One idea: he's tall, thin and polite, and he wears a crepe: rolled round his shoulders, its spiral under his chin, its long
end hanging down his back. The rest is a long plum coat flaring to its hem, thin legs and round shoes, a long pointed nose,
and a tall soft hat bent over backwards. The design comes from the ttyd style model's concepts (crepe_a, seed 6), redrawn
card by card: no generated pixels are used. Every contour here is placed by hand; nothing is mirrored. The face cards shown at rest come from the game's by recipe
(art/recipes/model_crepe.recipe); the ones drawn here are their fallback.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "..", "tools", "port"))
from ink import Card, cmpr_round_trip, save  # noqa: E402

OUT = os.path.join(HERE, "..", "crepe", "ink")
LINE = "#2a1518"  # wine-umber, one for the whole man
# batter and griddle; blackcurrant; skin; glove
CREPE, CREPE_T, CREPE_D = "#efcd84", "#d7a350", "#b77c34"
COAT, COAT_D, COAT_L, COAT_M = "#6a2a5c", "#3c1538", "#7a3468", "#52204a"
CREAM = "#efe3c4"
STOCK = "#3a2838"
SKIN, SKIN_D = "#f2d9b6", "#d8ae86"
GLOVE, GLOVE_D = "#f2eee6", "#b9b2b4"
SHOE, SHOE_D = "#3b2a3a", "#241825"
GOLD, GOLD_D = "#e6b73c", "#a97a1c"


def _y(pts, k, kx=1.0):
    """a contour resized (the widths in its points are kept)"""
    return [(q[0] * kx, q[1] * k) + tuple(q[2:]) for q in pts]


def robe():
    """the long coat: narrow at the shoulders, flaring to a wide hem, flat plum with one hard second tone down its back"""
    c = Card(112, 136, LINE)
    s = c.shape([(30, 7), (44, 2), (66, 3), (79, 7), (85, 22), (90, 60), (98, 100), (108, 128, 1.3, "c"), (84, 133, 1.3), (58, 128),
                 (32, 134, 1.3), (4, 127, 1.2, "c"), (12, 96), (19, 60), (24, 30)], COAT, width=4.2, seed=11,
                air=dict(dark=COAT_D, amount=0.45, to=(4, 4), blur=10))
    s.tone([(64, -4), (96, -4), (118, 140), (74, 140), (78, 96), (70, 44)], COAT_M)
    # the cravat at his throat, the coat's opening below it, three gold buttons
    s.tone([(35, 4), (50, 3), (49, 18), (45, 34), (40, 40), (34, 30), (32, 16)], CREAM)
    s.ink([(40, 40, 3.2), (36, 70, 2.6), (32, 100, 2.2), (28, 130, 3.4)])
    for x, y in ((47, 54), (45, 70), (43, 86)):
        s.tone([(x - 3, y - 2), (x, y - 4), (x + 3, y - 1), (x + 2, y + 3), (x - 2, y + 3)], GOLD)
    return c


def cape():
    """the crepe's long end: rolled, hanging down his back nearly to his heels, its spiral at the bottom"""
    c = Card(56, 128, LINE)
    s = c.shape([(14, 4), (34, 2), (43, 10), (44, 50), (48, 88), (52, 108), (49, 120, 1.2), (34, 127, 1.3), (17, 122), (10, 98), (8, 56), (10, 18)],
                CREPE, width=3.8, seed=23, air=dict(dark=CREPE_T, amount=0.6, to=(5, 3), blur=8))
    s.tone([(30, 20), (40, 26), (42, 44), (34, 40)], CREPE_T)
    s.tone([(14, 58), (22, 62), (24, 80), (16, 78)], CREPE_T)
    # the roll's spiral end, big enough to read as a crepe at a glance
    s.ink([(31, 98, 3.2), (44, 104, 3.0), (44, 118, 2.8), (31, 123, 2.6), (19, 116, 2.4), (20, 103, 2.2), (31, 101, 2.0), (38, 109, 1.7),
           (33, 116, 1.4), (27, 111, 0.6)])
    return c


def roll():
    """the rolled crepe round his shoulders, its spiral end under his chin: the man's one idea"""
    c = Card(128, 56, LINE)
    s = c.shape([(4, 30), (9, 13), (24, 4), (44, 7), (70, 8), (98, 6), (118, 13), (126, 28), (118, 43), (96, 49), (70, 47), (46, 51), (25, 54), (9, 46)],
                CREPE, width=3.8, seed=31, air=dict(dark=CREPE_T, amount=0.7, to=(3, 6), blur=6))
    for pts in ([(60, 12), (84, 13), (94, 20), (70, 22)], [(98, 30), (114, 26), (116, 36), (100, 40)], [(56, 36), (74, 34), (78, 42), (58, 45)]):
        s.tone(pts, CREPE_T)
    s.ink([(24, 6, 3.6), (40, 14, 3.4), (42, 32, 3.2), (32, 47, 3.0), (14, 45, 2.8), (9, 28, 2.6), (18, 15, 2.4), (32, 17, 2.2), (35, 30, 2.0),
           (27, 38, 1.7), (19, 30, 1.2), (24, 24, 0.4)])
    return c


def head():
    c = Card(64, 72, LINE)
    c.shape([(30, 3), (46, 6), (56, 18), (58, 36), (54, 52), (46, 64, 1.4), (32, 70, 1.5), (18, 68, 1.3), (10, 58), (6, 40), (8, 20), (16, 8)],
            SKIN, width=3.5, seed=41, air=dict(dark=SKIN_D, amount=0.8, to=(5, 5), blur=6))
    c.shape([(52, 30), (60, 27), (63, 37), (60, 47), (53, 48)], SKIN, width=3.4, seed=43, swell=0.25)
    c.ink([(58, 33, 2.0), (57, 39, 1.4), (58, 44, 0.4)])
    return c


def hat():
    """tall and soft, its crown bent over backwards, a gold band"""
    c = Card(72, 80, LINE)
    s = c.shape([(4, 76, 1.3, "c"), (11, 70), (12, 44), (13, 18), (16, 7), (32, 3), (48, 5), (61, 10), (70, 19), (68, 30), (60, 26), (54, 24), (54, 46),
                 (56, 70), (66, 76, 1.3, "c"), (36, 79, 1.4)], COAT, width=3.8, seed=53, air=dict(dark=COAT_D, amount=0.6, to=(4, 3), blur=7))
    s.tone([(46, 20), (62, 8), (76, 24), (68, 80), (58, 80), (48, 50)], COAT_M)
    s.tone([(2, 82), (6, 64), (36, 68), (68, 63), (72, 82), (36, 86)], GOLD)
    s.ink([(7, 64, 2.2), (36, 67, 3.0), (64, 63, 2.2)])
    s.ink([(54, 24, 3.4), (44, 17, 2.4), (32, 15, 1.0), (24, 16, 0.0)])
    return c


def sleeve(front=True):
    c = Card(40, 64, LINE)
    pts = [(12, 8), (24, 2), (34, 8), (36, 32), (38, 56, 1.3, "c"), (20, 61, 1.3), (4, 56, 1.2, "c"), (6, 32)]
    if front:
        s = c.shape(pts, COAT, width=3.5, seed=61, air=dict(dark=COAT_D, amount=0.5, to=(4, 4), blur=5))
        s.tone([(22, -2), (40, -2), (40, 48), (26, 50), (30, 24)], COAT_M)
        s.tone([(0, 46), (20, 52), (42, 44), (42, 66), (0, 66)], CREAM)
        s.ink([(5, 47, 2.2), (20, 51, 3.0), (37, 45, 2.0)])
    else:
        s = c.shape(pts, COAT_M, width=3.5, seed=63, air=dict(dark=COAT_D, amount=0.6, to=(4, 4), blur=5))
        s.tone([(0, 46), (20, 52), (42, 44), (42, 66), (0, 66)], "#cbbf9f")
        s.ink([(5, 47, 2.0), (20, 51, 2.6), (37, 45, 1.8)])
    return c


def glove(kind):
    """plump, as the game's gloves are: round finger ends, a rolled cuff, finger lines that start at a crotch and die away"""
    c = Card(64, 64, LINE)
    air = dict(dark=GLOVE_D, amount=0.75, to=(5, 5), blur=5)
    w = 3.4
    if kind == "open":  # relaxed, fingers down, the thumb forward
        c.shape([(22, 12), (13, 20), (5, 28), (3, 35), (8, 40), (15, 37), (17, 34), (15, 44), (13, 52), (15, 59), (21, 61), (26, 58), (29, 49), (31, 57),
                 (34, 62), (40, 63), (44, 58), (45, 49), (48, 56), (53, 58), (58, 53), (58, 44), (56, 32), (53, 20), (47, 12)], GLOVE, width=w, seed=71, air=air)
        c.ink([(29, 47, 2.8), (29, 40, 1.8), (30, 34, 0.2)])
        c.ink([(45, 47, 2.8), (44, 40, 1.8), (43, 33, 0.2)])
        c.ink([(18, 33, 2.4), (22, 29, 1.4), (27, 28, 0.2)])
    elif kind == "point":  # one long finger: away from the wrist, so a raised arm raises it
        c.shape([(23, 12), (16, 22), (13, 34), (8, 46), (4, 55), (5, 61), (11, 63), (16, 60), (21, 52), (26, 44), (33, 48), (43, 50), (51, 44), (56, 30), (52, 17), (45, 11)],
                GLOVE, width=w, seed=73, air=air)
        c.ink([(26, 43, 2.8), (32, 37, 2.0), (40, 38, 0.2)])
        c.ink([(40, 49, 2.4), (44, 42, 1.4), (46, 37, 0.2)])
        c.ink([(19, 25, 2.2), (28, 29, 1.6), (36, 26, 0.2)])
    elif kind == "fist":
        c.shape([(22, 12), (12, 21), (8, 34), (12, 47), (21, 55), (31, 56), (41, 57), (51, 50), (57, 38), (54, 22), (46, 12)], GLOVE, width=w, seed=75, air=air)
        c.ink([(23, 54, 2.6), (24, 46, 1.6), (26, 41, 0.2)])
        c.ink([(34, 55, 2.6), (35, 47, 1.6), (36, 42, 0.2)])
        c.ink([(45, 53, 2.4), (44, 45, 1.4), (43, 41, 0.2)])
        c.ink([(9, 31, 2.8), (21, 33, 2.4), (33, 31, 1.4), (39, 26, 0.2)])
    else:  # palm: flat, fingers together, the thumb out, offering
        c.shape([(24, 12), (16, 20), (8, 23), (3, 29), (5, 36), (13, 36), (16, 45), (20, 56), (28, 62), (38, 61), (48, 54), (54, 40), (54, 22), (46, 12)],
                GLOVE, width=w, seed=77, air=air)
        c.ink([(28, 61, 2.4), (29, 51, 1.4), (31, 42, 0.2)])
        c.ink([(40, 59, 2.4), (40, 50, 1.4), (40, 41, 0.2)])
        c.ink([(13, 36, 2.4), (20, 32, 1.4), (26, 31, 0.2)])
    # the rolled cuff
    c.shape([(20, 11), (29, 5), (44, 4), (51, 9), (50, 16), (36, 18), (24, 18)], GLOVE, width=3.0, seed=79, swell=0.3, air=dict(dark=GLOVE_D, amount=0.5, to=(3, 3), blur=3))
    return c


def leg(front=True):
    """a thin stockinged leg and a round shoe, toe forward"""
    c = Card(48, 80, LINE)
    pts = [(19, 0), (31, 0), (31, 56), (36, 63), (44, 69), (46, 77, 1.3), (30, 79, 1.4), (8, 78, 1.3), (2, 72), (7, 64), (18, 60)]
    if front:
        s = c.shape(pts, STOCK, width=3.2, seed=81, air=dict(dark=SHOE_D, amount=0.6, to=(3, 3), blur=4))
        s.tone([(-2, 58), (20, 58), (40, 64), (50, 82), (-2, 82)], SHOE)
    else:
        s = c.shape(pts, SHOE_D, width=3.2, seed=83, air=dict(dark="#160e18", amount=0.6, to=(3, 3), blur=4))
    return c


def eyes(kind):
    c = Card(48, 40, LINE)
    if kind == "open":
        c.shape([(8, 6), (14, 8), (15, 18), (11, 26), (6, 24), (4, 14)], "#1a0f12", width=1.0, seed=91)
        c.shape([(26, 5), (33, 7), (35, 17), (31, 26), (25, 24), (23, 14)], "#1a0f12", width=1.0, seed=92)
    elif kind == "gold":
        c.shape([(8, 6), (14, 8), (15, 18), (11, 26), (6, 24), (4, 14)], GOLD, width=1.6, seed=93)
        c.shape([(26, 5), (33, 7), (35, 17), (31, 26), (25, 24), (23, 14)], GOLD, width=1.6, seed=94)
    else:
        c.ink([(3, 16, 2.0), (9, 18, 3.0), (15, 16, 2.0)])
        c.ink([(23, 15, 2.0), (29, 17, 3.0), (36, 15, 2.0)])
    return c


def nose():
    """long and pointed, his own: drawn, as no game face card has one"""
    c = Card(40, 24, LINE)
    c.shape([(37, 3), (27, 5), (15, 9), (4, 13, 0.8, "c"), (14, 17), (27, 21), (37, 20)], SKIN, width=3.0, seed=95,
            air=dict(dark=SKIN_D, amount=0.8, to=(2, 4), blur=3))
    return c


def tache():
    c = Card(56, 24, LINE)
    c.shape(_y([(3, 7), (9, 12), (20, 12), (31, 8), (38, 12), (50, 11), (60, 5), (61, 15), (50, 24), (36, 22), (30, 19), (20, 25), (9, 21)], 0.85, 0.88), "#4b2a1c", width=2.8, seed=97)
    return c


def mouth(kind):
    if kind == "shut":
        c = Card(8, 8, LINE)
        return c
    c = Card(24, 32, LINE)
    c.shape([(4, 4), (20, 4), (19, 18), (12, 28), (6, 20)], "#a12a22", width=2.6, seed=98)
    return c


def scroll():
    c = Card(48, 72, LINE)
    s = c.shape([(7, 10), (26, 6), (41, 8), (40, 36), (42, 60), (22, 64), (6, 66), (9, 38)], "#ece2c6", width=3.4, seed=101,
                air=dict(dark="#c9b78e", amount=0.7, to=(3, 3), blur=4))
    s.ink([(14, 22, 1.6), (24, 21, 1.6), (34, 23, 0.6)])
    s.ink([(13, 32, 1.6), (24, 31, 1.6), (33, 33, 0.6)])
    s.ink([(14, 42, 1.6), (22, 41, 1.6), (28, 43, 0.6)])
    return c


def compass():
    c = Card(56, 56, LINE)
    s = c.shape([(28, 3), (44, 9), (53, 26), (47, 44), (30, 53), (12, 47), (3, 30), (9, 12)], GOLD, width=3.8, seed=103,
                air=dict(dark=GOLD_D, amount=0.8, to=(4, 4), blur=4))
    d = c.shape([(28, 13), (39, 17), (43, 28), (38, 38), (27, 43), (17, 39), (13, 28), (18, 17)], "#f2ead6", width=2.6, seed=104)
    d.ink([(28, 18, 3.0), (29, 28, 2.0), (28, 37, 0.5)])
    return c


# name, card, pivot in texels, where the pivot sits (model units), what it is
CARDS = {
    "cape": (cape, (28, 6), (3.6, 20.4), "the crepe's long end, hanging down his back; pivot at the shoulder"),
    "arm_back": (lambda: sleeve(False), (20, 6), (2.0, 19.6), "the far sleeve; shoulder pivot"),
    "foot_back": (lambda: leg(False), (25, 4), (1.4, 9.5), "the far leg and shoe; hip pivot"),
    "foot_front": (lambda: leg(True), (25, 4), (-1.6, 9.5), "the near leg and shoe; hip pivot"),
    "robe": (robe, (56, 131), (0.0, 4.0), "the long coat; pivot at the hem"),
    "roll": (roll, (64, 28), (-0.6, 20.6), "the rolled crepe on his shoulders, its spiral under his chin"),
    "head": (head, (34, 68), (-1.5, 21.4), "the head and ear; pivot at the neck"),
    "hat": (hat, (36, 76), (-1.2, 28.6), "the tall soft hat, its crown bent back; pivot at the band"),
    "arm_front": (lambda: sleeve(True), (20, 6), (-1.0, 19.6), "the near sleeve; shoulder pivot"),
    "hand_open": (lambda: glove("open"), (36, 8), (0.0, 0.0), "the near hand"),
    "hand_point": (lambda: glove("point"), (36, 8), (0.0, 0.0), "a finger raised"),
    "hand_fist": (lambda: glove("fist"), (36, 8), (0.0, 0.0), "a fist"),
    "hand_palm": (lambda: glove("palm"), (36, 8), (0.0, 0.0), "palm up, offering"),
    "eyes": (lambda: eyes("open"), (24, 20), (0.0, 0.0), "fallback: the recipe's eyes replace it"),
    "eyes_shut": (lambda: eyes("shut"), (24, 20), (0.0, 0.0), "fallback"),
    "eyes_gold": (lambda: eyes("gold"), (24, 20), (0.0, 0.0), "fallback"),
    "nose": (nose, (36, 12), (0.0, 0.0), "his long pointed nose (drawn: no game face has it)"),
    "tache": (tache, (28, 12), (0.0, 0.0), "fallback"),
    "mouth_shut": (lambda: mouth("shut"), (4, 4), (0.0, 0.0), "fallback, blank"),
    "mouth": (lambda: mouth("open"), (12, 12), (0.0, 0.0), "fallback"),
    "scroll": (scroll, (24, 60), (0.0, 0.0), "the letter"),
    "compass": (compass, (28, 44), (0.0, 0.0), "the compass's base"),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    cards = {}
    for name, (fn, pivot, at, desc) in CARDS.items():
        c = fn()
        img = c.finish()
        img = cmpr_round_trip(img)
        save(img, os.path.join(OUT, name + ".png"))
        cards[name] = {"size": [img.shape[1], img.shape[0]], "pivot": list(pivot), "model_xy": list(at), "desc": desc}
    json.dump({"model": "c_sl_crepe", "texels_per_unit": 8, "cards": cards}, open(os.path.join(OUT, "parts.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
