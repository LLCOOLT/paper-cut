"""The Nightmare's cards cut from the model's own pieces (`python cutout.py cut_nightmare`).

The design: the style model's hooded shadow in a violet robe, a round dark head ringed in gold with gold spikes, glowing
eyes and a jagged grin, a gold sunburst brooch with a long ribbon, gold hands and feet
(designs/nightmare/concept_nightmare_a_seed4.png). Qwen-Image-Edit set it out on white (designs/nightmare/parts_qwen.png);
the narrowed eyes, the laughing mouth, the cracks of light and the compass needle are Qwen's edits of that sheet
(parts_qwen_*.png). Here: the head is cut from the robe under the grin (the hood's dark runs on across the seam), the
sleeves at their seams with the hands, the torn stumps are the sleeves' tops, the needle turns on the brooch, and the
shards of the shatter are pieces of the cracked robe. Points are in the sheet's pixels (1024 square); the model's (0, 0)
is under the feet.
"""
import os

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/nightmare/parts_qwen.png"
FACE = "designs/nightmare/parts_qwen_%s.png"  # Qwen's edits of that sheet: narrow, laugh, crack, needle
OUT = "nightmare/qwen"
MODEL = "c_sl_nightmare"
PX = 2.0  # 16 sheet pixels a unit: about 58 units to the spikes' tips (Luigi is 24)
ORIGIN = (495, 980)
LINE = "#120a1c"
LINE_DARK = 26  # the robe's violet is dark itself (value 37): only the black is line


def have(path, fallback=SRC):
    """a Qwen edit if it has been made, else the sheet it was made from"""
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


HEAD = [(190, 150), (300, 90), (400, 35), (480, 40), (560, 55), (655, 55), (705, 120), (725, 180), (795, 195), (770, 300),
        (740, 335), (705, 400), (640, 428), (545, 432), (528, 392), (420, 392), (405, 428), (330, 425), (285, 398),
        (240, 330), (225, 290)]
NECK = (478, 400)
BODY = [(250, 328), (745, 318), (716, 448), (786, 612), (800, 850), (680, 880), (680, 992), (310, 992), (310, 882),
        (200, 850), (226, 612), (276, 452)]
SLEEVE_F = [(210, 318), (302, 333), (290, 400), (282, 455), (244, 560), (238, 614), (172, 624), (162, 676), (96, 668),
            (98, 600), (110, 543), (160, 466)]
SLEEVE_B = [(702, 318), (792, 318), (852, 418), (905, 532), (938, 598), (948, 662), (884, 678), (854, 642), (800, 630),
            (782, 614), (752, 520), (712, 452)]
SH_F, SH_B = (262, 360), (735, 352)  # the shoulders: the sheet's left is the front (the -x side, the way it faces)
STUB_F = [(210, 318), (302, 333), (290, 400), (280, 440), (262, 418), (246, 462), (226, 430), (204, 472), (188, 436),
          (160, 468)]
STUB_B = [(702, 318), (792, 318), (850, 418), (830, 470), (812, 432), (792, 470), (772, 428), (752, 468), (712, 452)]
NEEDLE_PIVOT = (472, 440)  # the brooch's centre

CARDS = {
    "body": dict(poly=BODY, pivot=(495, 850), desc="the robe, the brooch and the ribbon, the feet; the sleeves cut away"),
    "body_cracked": dict(poly=BODY, pivot=(495, 850), src=have(FACE % "crack"), desc="the robe cracking with light"),
    "face": dict(poly=HEAD, pivot=NECK, desc="the head: the gold ring and spikes, the glowing eyes, the grin"),
    "face_open": dict(poly=HEAD, pivot=NECK, src=have(FACE % "laugh"), desc="the head laughing, the mouth wide (Hatesong)"),
    "face_cracked": dict(poly=HEAD, pivot=NECK, src=have(FACE % "crack"), desc="the head cracking with light"),
    "face_narrow": dict(poly=HEAD, pivot=NECK, src=have(FACE % "narrow"), desc="the head, the eyes narrowed to slits"),
    "arm_front": dict(poly=SLEEVE_F, pivot=SH_F, desc="the near sleeve and its gold hand; shoulder pivot"),
    "arm_back": dict(poly=SLEEVE_B, pivot=SH_B, desc="the far sleeve and its gold hand; shoulder pivot"),
    "stub_front": dict(poly=STUB_F, pivot=SH_F, desc="the near sleeve torn off at the shoulder"),
    "stub_back": dict(poly=STUB_B, pivot=SH_B, desc="the far sleeve torn off at the shoulder"),
}
BINDS = [["body", "body_cracked"], ["face", "face_open", "face_cracked", "face_narrow"]]
ANCHORS = {"neck": NECK, "brooch": NEEDLE_PIVOT, "hand_front": (130, 640), "hand_back": (905, 630), "face": (478, 250)}

# the needle: Qwen's arrow lying by her feet, a third its size, stood up with its gold head to the north
NEEDLE = [(675, 1018), (680, 925), (700, 905), (975, 720), (1000, 705), (1005, 735), (985, 775), (810, 950), (800, 985),
          (760, 985), (690, 1015)]
CARDS["needle"] = dict(poly=NEEDLE, pivot=(840, 862), src=have(FACE % "needle"), scale=0.3, rotate=-133.0,
                       at=NEEDLE_PIVOT, desc="the compass needle on the brooch, its gold head to the north")
# the shards of the shatter: three pieces of the cracked head, light running through them
for _k, _p in enumerate(([(300, 150), (385, 118), (352, 222)], [(560, 118), (652, 150), (602, 232)],
                         [(420, 300), (522, 290), (470, 366)])):
    CARDS["shard_%d" % _k] = dict(poly=_p, pivot=tuple(sum(v[i] for v in _p) / 3 for i in (0, 1)), src=have(FACE % "crack"),
                                  desc="a shard of the cracked head")
