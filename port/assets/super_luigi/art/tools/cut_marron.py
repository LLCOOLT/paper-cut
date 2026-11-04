"""King Marron's cards cut from the model's own pieces (`python cutout.py cut_marron`).

The design: the style model's chestnut king, brown-bearded, a gold crown set with green leaves, a green robe with a gold
clasp and trim, brown boots (designs/king_marron/concept_marron_a_seed5.png). Qwen-Image-Edit split it into flat pieces
(designs/king_marron/parts_qwen.png: the head wearing the crown, the crown again alone, the robe in two halves with an arm
and hand each, the two boots); his blinking, talking and smiling-shut heads are Qwen's edits of that sheet
(parts_qwen_head_*.png). Here: the robe's halves meet at the clasp (the right half moved in over the gap); each arm is cut
from its half at the sleeve's seam, its hand on it; the head sits down into the robe's collar, the beard over it; both feet
are the boot whose toe points the way he faces, the far one a step behind.
Points are in the piece sheet's pixels (1024 square); the model's (0, 0) is between his feet.
"""
import os

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/king_marron/parts_qwen.png"
FACE = "designs/king_marron/parts_qwen_head_%s.png"  # Qwen's sheet with him blinking, talking or smiling
OUT = "king_marron/qwen"
MODEL = "c_sl_marron"
PX = 2.6  # about 21 sheet pixels a unit: about 38 units to the top of his crown, a stout head shorter than Luigi
FEET_D = (0, -50)  # the boots moved up under the robe's hem
ORIGIN = (530, 958 + FEET_D[1])  # between his boots, on the ground
LINE = "#2a1810"

HEAD_D = (275, 55)  # the head moved down onto the collar, centred on the robe
ROBE_R_D = (-10, 0)  # the right half moved in over the gap at the clasp


def have(path, fallback):
    """a Qwen edit if it has been made, else the piece sheet it was made from"""
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


def moved(p, d):
    return (p[0] + d[0], p[1] + d[1])


HEAD_PIVOT = (240, 465)  # the chin, under the beard
BOOT = [(392, 792), (512, 792), (518, 840), (504, 850), (505, 915), (502, 955), (460, 962), (350, 958), (338, 930),
        (340, 890), (365, 868), (398, 850), (392, 830)]
BOOT_TOP = (452, 800)

CARDS = {
    "head": dict(poly=[(148, 100), (165, 82), (188, 98), (205, 120), (255, 75), (270, 58), (290, 68), (300, 100), (345, 110),
                       (360, 92), (378, 96), (392, 120), (398, 200), (388, 262), (408, 280), (412, 320), (392, 345), (384, 400),
                       (340, 450), (250, 474), (150, 455), (105, 420), (88, 360), (110, 315), (155, 285), (170, 240), (175, 215),
                       (160, 140)],
                 pivot=HEAD_PIVOT, at=moved(HEAD_PIVOT, HEAD_D), air=0, desc="the head, beard and crown; pivot at the chin"),
    "robe_l": dict(poly=[(345, 500), (345, 450), (358, 428), (378, 423), (412, 440), (450, 480), (482, 540), (500, 590),
                         (502, 770), (352, 770), (350, 636), (372, 517)],
                   pivot=(426, 700), desc="the robe's half on the side he faces, the arm cut away at the sleeve's seam"),
    "robe_r": dict(poly=[(492, 600), (500, 560), (530, 535), (555, 485), (605, 450), (670, 432), (740, 435), (770, 470),
                         (757, 517), (700, 522), (665, 540), (735, 655), (735, 745), (500, 770)],
                   pivot=(615, 700), at=moved((615, 700), ROBE_R_D), desc="the robe's other half, its gold clasp over the gap"),
    "arm_l": dict(poly=[(372, 517), (345, 500), (320, 520), (290, 555), (255, 590), (232, 598), (212, 612), (205, 640),
                        (210, 690), (230, 712), (270, 716), (300, 700), (318, 685), (335, 650), (352, 636), (360, 580)],
                  pivot=(352, 530), desc="the arm on the side he faces, sleeve, cuff and hand; pivot at the shoulder"),
    "arm_r": dict(poly=[(665, 540), (700, 520), (750, 518), (775, 560), (805, 620), (835, 628), (840, 665), (858, 690),
                        (852, 735), (800, 742), (760, 735), (738, 712), (735, 672), (728, 652), (700, 600)],
                  pivot=(700, 535), at=moved((700, 535), ROBE_R_D), desc="the other arm; pivot at the shoulder"),
    "foot_front": dict(poly=BOOT, pivot=BOOT_TOP, at=moved((440, 800), FEET_D), desc="the near boot; pivot at its top"),
    "foot_back": dict(poly=BOOT, pivot=BOOT_TOP, at=moved((610, 800), FEET_D), desc="the far boot, the same, a step behind"),
}
# the face: whole heads from Qwen's sheets. Its edits moved his nose and moustache from one face to the next, so an
# eye or mouth overlay would not sit; the head swaps whole, as the game's own blinking heads do
for _n, _v, _d in (("head_blink", "blink", "eyes shut"), ("head_talk", "talk", "talking"),
                   ("head_smile", "smile", "eyes happy-shut, a broad smile")):
    CARDS[_n] = dict(CARDS["head"], src=have(FACE % _v, SRC), desc="the head, " + _d)
BINDS = [["head", "head_blink", "head_talk", "head_smile"]]
ANCHORS = {"waist": (527, 690), "neck": moved(HEAD_PIVOT, HEAD_D), "hand_l": (250, 680), "hand_r": moved((800, 700), ROBE_R_D)}
