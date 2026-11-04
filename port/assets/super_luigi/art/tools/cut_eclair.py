"""Princess Eclair's cards cut from the model's own pieces (`python cutout.py cut_eclair`).

The design: the style model's princess with a one-plate tiara (designs/eclair/design_eclair_tiara.png). Qwen-Image-Edit split
it into flat pieces (designs/eclair/parts_qwen_b.png: the head, the back hair, the gown with its arms, an arm hanging down,
gloves, shoes). To make her the waffle kingdom's own and not a cream Peach, Qwen then took her earrings off, gave her the
one-plate tiara and caramel hair, and covered the gown in a toasted waffle grid (parts_qwen_tiara_still.png); from that, her
back hair caramel too and her face in the game's simple manner, black eyes with one highlight and no blush, though it lost
the tiara (parts_qwen_fix.png). So the tiara is its own card, cut from the sheet before. Her blinking, talking and
smiling-shut faces are Qwen's edits of the fix sheet (parts_qwen_head_*.png). Here: the gown keeps its puffed sleeves and
their cuffs, cut where the glove begins; each arm is the hanging arm's glove, hung from a cuff; the head sits on the
neckline with the back hair behind it, falling down her back; her gown reaches the ground.
Points are in the piece sheet's pixels (1024 square); the model's (0, 0) is between her feet.
"""
import math
import os

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/eclair/parts_qwen_fix.png"
FACE = "designs/eclair/parts_qwen_head_%s.png"  # Qwen's head, blinking, talking or smiling, over the same sheet
TIARA = "designs/eclair/parts_qwen_tiara_%s.png"  # the sheet still wearing the tiara, and Qwen's edit of it glowing
OUT = "eclair/qwen"
MODEL = "c_sl_eclair"
PX = 3.9  # about 31 sheet pixels a unit: about 28 units to her tiara, a touch over Peach beside her in the game
ORIGIN = (340, 950)  # her hem on the ground
LINE = "#2a1a14"

HEAD_D = (16, 46)  # the head piece moved onto the neckline
HAIR_D = (-330, 120)  # the back hair moved behind the head, toward her back
CUFF_L, CUFF_R = (172, 512), (462, 510)  # the gown's cuffs: the sheet's left (the -x side, the way she faces) and right


def have(path, fallback):
    """a Qwen edit if it has been made, else the piece sheet it was made from"""
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


if not os.path.exists(os.path.join(_ART, SRC)):
    SRC = "designs/eclair/parts_qwen_b.png"


def moved(p, d):
    return (p[0] + d[0], p[1] + d[1])


ARM = [(594, 497), (664, 497), (668, 560), (684, 600), (672, 636), (598, 636), (592, 590)]
ARM_TOP = (630, 500)

CARDS = {
    "gown": dict(poly=[(157, 495), (186, 528), (226, 548), (232, 576), (200, 580), (150, 620), (100, 700), (52, 800), (36, 900),
                       (60, 975), (330, 985), (650, 970), (664, 900), (620, 800), (560, 700), (500, 640), (450, 600), (425, 570),
                       (420, 545), (448, 524), (476, 496), (492, 466), (482, 428), (440, 410), (400, 414), (366, 424), (320, 432),
                       (262, 422), (230, 416), (180, 412), (146, 428), (136, 462), (148, 490)],
                 pivot=(340, 948), desc="the gown, its puffed sleeves and cuffs; pivot at the hem"),
    "arm_l": dict(poly=ARM, pivot=ARM_TOP, at=CUFF_L, rotate=-12.0, desc="the arm on the side she faces, a long glove; pivot at the cuff"),
    "arm_r": dict(poly=ARM, pivot=ARM_TOP, at=CUFF_R, rotate=-12.0, flip=True, desc="the other arm; pivot at the cuff"),
    "hair": dict(poly=[(526, 36), (700, 30), (960, 40), (970, 330), (960, 580), (700, 580), (700, 392), (566, 392), (526, 330)],
                 pivot=(740, 300), at=moved((740, 300), HAIR_D), patch=[([(612, 90), (660, 106), (715, 114), (770, 106), (818, 90), (824, 110), (770, 130), (715, 146), (660, 130), (608, 110)], "#bf6727")],
                 desc="the back hair, falling down her back (the gold band Qwen left on it painted out)"),
    "head": dict(poly=[(96, 150), (150, 70), (250, 20), (380, 30), (450, 80), (505, 140), (512, 230), (520, 300), (480, 380),
                       (420, 410), (300, 425), (190, 410), (140, 380), (96, 300)],
                 pivot=(300, 395), at=moved((300, 395), HEAD_D), air=0, desc="the head and hair (the tiara is its own card); pivot at the chin"),
}
# the face: overlays from Qwen's heads, the eyes cut between brows and nose, the mouth below the nose, with no line on the cut
_EYES = [(196, 252), (398, 252), (398, 348), (196, 348)]
_MOUTH = [(222, 348), (350, 348), (350, 394), (222, 394)]
for _n, _src, _poly in (("eyes", SRC, _EYES), ("eyes_shut", have(FACE % "blink", SRC), _EYES),
                        ("eyes_happy", have(FACE % "smile", SRC), _EYES),
                        ("mouth_shut", SRC, _MOUTH), ("mouth", have(FACE % "talk", SRC), _MOUTH),
                        ("mouth_smile", have(FACE % "smile", SRC), _MOUTH)):
    CARDS[_n] = dict(src=_src, poly=_poly, pivot=(300, 395), at=moved((300, 395), HEAD_D), air=0, rim=False, desc="a face overlay")
# the tiara: cut from the edit that still wore it (TIARA % "still"), its own card on her hair, and the same card in her
# hand when she lifts it (the rig places it); its pivot at the middle of its band. The glowing one is Qwen's edit of that sheet.
TIARA_POLY = [(176, 104), (214, 94), (252, 71), (271, 49), (290, 64), (312, 82), (340, 97), (370, 104), (372, 118), (366, 134),
              (300, 128), (271, 127), (230, 132), (200, 140), (183, 124)]
TIARA_PIVOT, TIARA_ON = (271, 130), (296, 118)  # where its band sits on her hair, in the head sheet's pixels
# the gem's deep red is dark enough to be taken for line and browned: painted a shade lighter, its highlight kept
_GEM = [(274 + 9 * math.cos(t * math.pi / 6), 101 + 13 * math.sin(t * math.pi / 6)) for t in range(12)]
_SHINE = [(268, 90), (272, 89), (273, 95), (269, 97)]
CARDS["tiara"] = dict(src=have(TIARA % "still", SRC), scale=1.25, poly=TIARA_POLY, pivot=TIARA_PIVOT, at=moved(TIARA_ON, HEAD_D),
                      patch=[(_GEM, "#e0303c"), (_SHINE, "#fff0f0")], desc="the one-plate tiara, the seventh piece set in it")
CARDS["tiara_glow"] = dict(CARDS["tiara"], src=have(TIARA % "glow", CARDS["tiara"]["src"]), air=0, desc="the tiara, its piece blazing")
BINDS = [["eyes", "eyes_shut", "eyes_happy"], ["mouth_shut", "mouth", "mouth_smile"], ["tiara", "tiara_glow"]]
ANCHORS = {"waist": (322, 575), "neck": moved((300, 395), HEAD_D),
           "hand_l": (CUFF_L[0] - 24, CUFF_L[1] + 112), "hand_r": (CUFF_R[0] + 24, CUFF_R[1] + 112),
           "tiara_on": moved(TIARA_ON, HEAD_D)}
