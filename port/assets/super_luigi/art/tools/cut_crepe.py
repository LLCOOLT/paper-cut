"""Minister Crepe's cards cut from the model's own parts (`python cutout.py cut_crepe`).

The design: the style model's seed 5 with the crepe roll painted in and blended by the model (designs/crepe/design_crepe_e.png).
Qwen-Image-Edit split it into flat pieces (designs/crepe/parts_qwen_e1.png: head and hat, the coat with its arms, the roll,
the legs) and drew its glove in more poses (designs/crepe/parts_qwen_gloves.png). Here each card is cut from those pieces: the
arms off the coat at the armpit, hung down from the shoulder; the hat off the head at the brim; the head set on the collar.
Points are in the piece sheet's pixels (1024 square); the model's (0, 0) is between his feet.
"""
import math

import numpy as np

SRC = "designs/crepe/parts_qwen_e1.png"
GLOVES = "designs/crepe/parts_qwen_gloves.png"
ROLL = "designs/crepe/parts_qwen_roll.png"  # Qwen's crepe roll over the same sheet: spotted, its spiral end to the front
FACE = "designs/crepe/parts_qwen_head_%s.png"  # Qwen's head, blinking, talking or gold-eyed, over the same sheet
PROPS = "designs/crepe/parts_qwen_props.png"
LETTER = "designs/crepe/parts_qwen_letter.png"
OUT = "crepe/qwen"
MODEL = "c_sl_crepe"
PX = 3.0  # 24 sheet pixels a unit: about 37 units to the top of his hat
ORIGIN = (518, 945)
LINE = "#2a1518"
PURPLE = "#5c285b"  # the coat's own, sampled

HEAD_D = (240, -6)  # the head piece moved onto the collar


def rot(p, c, deg):
    """p turned about c as PIL's rotate(deg) turns the image (counter-clockwise on screen)"""
    t = math.radians(deg)
    x, y = p[0] - c[0], p[1] - c[1]
    return (c[0] + x * math.cos(t) + y * math.sin(t), c[1] - x * math.sin(t) + y * math.cos(t))


def moved(p, d=HEAD_D):
    return (p[0] + d[0], p[1] + d[1])


SH_B, WR_B, TURN_B = (360, 418), (248, 553), 30.0   # his far arm (the sheet's left)
SH_F, WR_F, TURN_F = (640, 425), (768, 548), -38.0  # his near arm
GLOVE_K = 0.64  # the glove sheet's gloves, to the coat's size

CARDS = {
    "hat": dict(poly=[(140, 98), (250, 74), (392, 96), (374, 140), (356, 200), (354, 230), (382, 238), (372, 258), (262, 254),
                      (146, 252), (142, 238), (178, 226), (178, 140)],
                pivot=(265, 250), at=moved((265, 250)), desc="the top hat; pivot at the band"),
    "head": dict(poly=[(140, 256), (200, 250), (310, 248), (362, 246), (382, 268), (356, 300), (348, 322), (322, 348), (252, 362),
                       (205, 350), (190, 334), (146, 336), (138, 320), (172, 304), (186, 286)],
                 pivot=(265, 345), at=moved((265, 345)), air=0, desc="the head and face, the moustache and the white curl; neck pivot"),
    "roll": dict(src=ROLL, poly=[(445, 294), (572, 294), (588, 330), (624, 345), (634, 376), (614, 412), (560, 422), (482, 426),
                                 (484, 462), (472, 497), (440, 512), (392, 507), (376, 476), (383, 440), (397, 420), (398, 370),
                                 (420, 344)],
                 pivot=(510, 400), desc="the crepe, rolled round his neck, its spiral end hanging in front; the collar"),
    "robe": dict(poly=[(400, 418), (378, 440), (362, 492), (348, 560), (334, 700), (320, 830), (328, 868), (500, 876), (684, 866),
                       (680, 800), (662, 640), (628, 520), (612, 482), (626, 446), (612, 420), (570, 404), (510, 400), (450, 404)],
                 patch=[([(380, 380), (684, 380), (684, 436), (470, 432), (458, 445), (456, 492), (422, 512), (380, 500)], PURPLE)],
                 pivot=(510, 868), desc="the coat; pivot at the hem"),
    "arm_back": dict(poly=[(400, 420), (372, 440), (364, 490), (330, 525), (290, 573), (262, 562), (214, 541), (206, 530),
                           (238, 490), (274, 450), (312, 426), (350, 414)],
                     patch=[([(330, 380), (420, 380), (420, 436), (330, 436)], PURPLE)],
                     pivot=SH_B, rotate=TURN_B, desc="the far sleeve; shoulder pivot"),
    "arm_front": dict(poly=[(614, 418), (660, 420), (700, 440), (722, 452), (772, 498), (803, 527), (740, 574), (700, 546), (650, 512),
                            (612, 482), (612, 440)],
                      patch=[([(600, 380), (690, 380), (690, 436), (600, 436)], PURPLE)],
                      pivot=SH_F, rotate=TURN_F, desc="the near sleeve; shoulder pivot"),
    "hand_back": dict(poly=[(196, 524), (216, 538), (262, 558), (294, 574), (284, 604), (262, 645), (240, 664), (198, 670),
                            (164, 668), (146, 642), (156, 598), (182, 556)],
                      pivot=WR_B, rotate=TURN_B, at=rot(WR_B, SH_B, TURN_B), desc="the far hand"),
    "hand_open": dict(poly=[(800, 520), (834, 542), (864, 578), (874, 622), (864, 664), (822, 670), (788, 664), (756, 652),
                            (740, 620), (728, 570)],
                      pivot=WR_F, rotate=TURN_F, at=rot(WR_F, SH_F, TURN_F), desc="the near hand, open"),
    "hand_point": dict(src=GLOVES, scale=GLOVE_K, poly=[(44, 255), (262, 255), (262, 612), (44, 612)], pivot=(153, 606),
                       rotate=180.0, at=rot(WR_F, SH_F, TURN_F), desc="a finger raised"),
    "hand_fist": dict(src=GLOVES, scale=GLOVE_K, poly=[(508, 330), (738, 330), (738, 610), (508, 610)], pivot=(623, 604),
                      rotate=180.0, at=rot(WR_F, SH_F, TURN_F), desc="a fist"),
    "hand_palm": dict(src=GLOVES, scale=GLOVE_K, poly=[(746, 300), (1000, 300), (1000, 612), (746, 612)], pivot=(873, 607),
                      rotate=180.0, at=rot(WR_F, SH_F, TURN_F), desc="palm up, offering"),
    "foot_back": dict(poly=[(812, 700), (900, 700), (902, 900), (918, 924), (912, 942), (820, 944), (812, 930)],
                      pivot=(856, 704), at=(578, 712), desc="the far boot; hip pivot"),
    "foot_front": dict(poly=[(708, 700), (797, 700), (797, 936), (784, 944), (694, 944), (690, 928), (708, 906)],
                       pivot=(752, 704), at=(462, 712), desc="the near boot, toe forward; hip pivot"),
}
# the face: the head's top (eyes) and bottom (moustache and mouth) as overlays from Qwen's heads, cut at y 290 between the
# eyes and the moustache, with no line along the cut
_EYES = [(140, 244), (200, 246), (310, 244), (364, 242), (386, 268), (378, 290), (140, 290)]
_MOUTH = [(128, 290), (384, 290), (372, 330), (334, 352), (300, 370), (196, 370), (128, 348)]
for _n, _src, _poly in (("eyes", SRC, _EYES), ("eyes_shut", FACE % "blink", _EYES), ("eyes_gold", FACE % "gold", _EYES),
                        ("mouth_shut", SRC, _MOUTH), ("mouth", FACE % "talk", _MOUTH)):
    CARDS[_n] = dict(src=_src, poly=_poly, pivot=(265, 345), at=moved((265, 345)), air=0, rim=False, desc="a face overlay")
CARDS["scroll"] = dict(src=LETTER, scale=0.3, poly=[(570, 200), (640, 188), (760, 108), (857, 70), (964, 240), (832, 332),
                                                   (802, 402), (760, 418), (718, 388), (700, 332), (575, 252)],
                       pivot=(760, 250), at=rot(WR_F, SH_F, TURN_F), desc="the letter, sealed")
CARDS["compass"] = dict(src=PROPS, scale=0.4, poly=[(660, 236), (756, 228), (756, 70), (842, 52), (946, 140), (934, 252),
                                                    (884, 284), (884, 334), (862, 396), (780, 412), (696, 400), (662, 352)],
                        pivot=(780, 330), at=rot(WR_F, SH_F, TURN_F), desc="the compass's base, its lid open on the empty socket")
BINDS = [["hand_open", "hand_point", "hand_fist", "hand_palm"], ["eyes", "eyes_shut", "eyes_gold"], ["mouth_shut", "mouth"]]
ANCHORS = {"top": (510, 868), "neck": moved((265, 345))}


def _blank():
    return np.zeros((8, 8, 4), dtype=np.uint8)


EXTRA = {n: _blank for n in ("nose", "tache")}
EXTRA_META = {n: dict(pivot=(4, 4), at=moved((240, 300)), desc="blank: his nose and moustache are the head card's own") for n in EXTRA}
