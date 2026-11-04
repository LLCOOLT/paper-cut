"""Fuselle's cards cut from the model's own pieces (`python cutout.py cut_fuselle`).

The design: the style model's pink Bob-omb girl with a red bow; Qwen-Image-Edit split it into flat pieces on white
(designs/fuselle/parts_qwen.png: the round pink body with her face and cap, the red bow, the wind-up key, each shoe).
Her happy-shut and talking faces are Qwen's edits of that sheet (parts_qwen_blink.png, parts_qwen_talk.png): whole-body
swaps. Here: the shoes stand under the body, behind it; the key is turned to stick out of her back (+x, as she faces -x),
its shaft in her; the bow sits on her crown. Points are in the sheet's pixels (1024 square); the model's (0, 0) is
under her, on the floor.
"""
import os

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/fuselle/parts_qwen.png"
FACE = "designs/fuselle/parts_qwen_%s.png"
OUT = "fuselle/qwen"
MODEL = "c_sl_fuselle"
PX = 6.0  # 48 sheet pixels a unit: her body about 11 units across, about 14 to the top of the bow (a Bob-omb's size)
DENSITY = 6  # 48 texels a unit: one texel a sheet pixel at that size
ORIGIN = (350, 800)
LINE = "#2a1418"
THRESH = 70  # the sheet's white sticker borders and their grey shadows go with the ground
SPECKS = 0.05  # and the wobble strokes Qwen drew beside the pieces


def have(path, fallback=SRC):
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


BODY = [(90, 480), (110, 360), (190, 270), (350, 225), (510, 270), (600, 370), (620, 490), (590, 620), (500, 720),
        (350, 760), (200, 720), (110, 620)]
BODY_PIVOT = (350, 740)  # the bottom of her body, between the shoes
SHOE_F = [(455, 780), (640, 780), (640, 930), (455, 930)]
SHOE_B = [(635, 715), (815, 715), (815, 895), (635, 895)]
KEY = [(690, 420), (870, 420), (905, 690), (850, 720), (690, 600)]
BOW = [(615, 140), (935, 140), (935, 395), (615, 395)]

CARDS = {
    "body": dict(poly=BODY, pivot=BODY_PIVOT, desc="her round pink body, her face and cap"),
    "body_blink": dict(poly=BODY, pivot=BODY_PIVOT, src=have(FACE % "blink"), desc="the same, her eyes happily shut"),
    "body_talk": dict(poly=BODY, pivot=BODY_PIVOT, src=have(FACE % "talk"), desc="the same, her mouth open, talking"),
    "shoe_front": dict(poly=SHOE_F, pivot=(548, 905), at=(285, 800), desc="the near shoe, toe forward (-x)"),
    "shoe_back": dict(poly=SHOE_B, pivot=(725, 870), at=(420, 795), desc="the far shoe"),
    "key": dict(poly=KEY, pivot=(865, 688), at=(585, 470), rotate=-114.0, desc="the wind-up key, its shaft in her back"),
    "bow": dict(poly=BOW, pivot=(735, 245), at=(395, 262), scale=1.0, desc="the red bow on her crown"),
}
BINDS = [["body", "body_blink", "body_talk"]]
ANCHORS = {"body": BODY_PIVOT, "crown": (350, 235)}
