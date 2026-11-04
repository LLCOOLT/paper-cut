"""The Chestnut King's cards cut from the model's own pieces (`python cutout.py cut_chestnut`).

The design: the style model's round spiky chestnut husk with a face, on two green feet; Qwen-Image-Edit set it out on
white (designs/chestnut_king/parts_qwen.png, split b). His roar, his eyes shut tight, his sad and his calm faces are
Qwen's edits of that sheet (parts_qwen_roar/hurt/sad/calm.png); the chestnut inside the husk and his crown are Qwen
props added beside him (parts_qwen_nut.png, parts_qwen_crown.png). Here: the husk is cut down its middle into the two
halves that swing open; the face is cut from each sheet as an overlay on the husk (no line where it was cut); the nut
behind the halves is the chestnut, grown to fill him; the shards of the curse breaking are pieces of the husk; the
chestnuts of Chestnut Rain are the same nut, small. Points are in the sheet's pixels (1024 square); the model's (0, 0)
is under his feet.
"""
import os

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/chestnut_king/parts_qwen.png"
FACE = "designs/chestnut_king/parts_qwen_%s.png"
OUT = "chestnut_king/qwen"
MODEL = "c_sl_chestnut_king"
PX = 2.6  # about 21 sheet pixels a unit: about 45 units to the top spike, 49 to the crown (Luigi is 24)
ORIGIN = (505, 975)
LINE = "#0e1a14"
SPECKS = 0.05


def have(path, fallback=SRC):
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


HALF_L = [(100, 20), (515, 20), (515, 850), (100, 850)]
HALF_R = [(515, 20), (935, 20), (935, 850), (515, 850)]
FACE_POLY = [(245, 205), (690, 205), (725, 420), (705, 715), (250, 715), (228, 420)]
NECK = (480, 715)
BASE = (515, 790)  # the husk's base: the halves turn on it
# the chestnut beside his foot (parts_qwen_nut.png): its own outline, traced once from the sheet (the brown and the
# cream filled inside the line, grown by most of the line's width (the cut's rim draws its edge), sampled every 4 degrees from the middle), so neither the
# husk's spike over its shoulder nor his foot beside it comes along, and none of the nut is cut off
NUT = [(992, 822), (992, 832), (992, 842), (991, 853), (990, 863), (988, 874), (984, 884), (979, 893), (974, 902),
       (968, 911), (961, 919), (951, 924), (944, 932), (936, 938), (926, 942), (916, 945), (907, 948), (897, 949),
       (887, 951), (878, 952), (868, 953), (859, 953), (850, 953), (841, 953), (831, 953), (822, 953), (813, 952),
       (803, 951), (794, 950), (784, 947), (774, 945), (764, 942), (755, 938), (746, 933), (739, 924), (730, 919),
       (722, 911), (716, 902), (711, 893), (706, 884), (703, 873), (701, 863), (699, 853), (699, 842), (699, 832),
       (699, 822), (700, 812), (702, 802), (704, 792), (708, 782), (712, 773), (716, 764), (721, 756), (727, 748),
       (734, 741), (740, 734), (748, 728), (756, 722), (763, 716), (770, 711), (778, 706), (787, 702), (795, 697),
       (803, 692), (812, 687), (820, 680), (830, 673), (840, 667), (851, 667), (861, 673), (870, 681), (879, 687),
       (888, 692), (896, 697), (904, 702), (912, 706), (920, 711), (927, 717), (935, 722), (942, 728), (950, 734),
       (956, 741), (963, 748), (969, 756), (974, 764), (979, 773), (983, 782), (987, 792), (989, 802), (991, 812)]
NUT_BASE = (850, 950)
CROWN = [(735, 750), (1000, 750), (1000, 955), (735, 955)]  # the crown beside his foot (parts_qwen_crown.png)

CARDS = {
    "husk_left": dict(poly=HALF_L, pivot=(505, 790), rim=False, desc="the husk's left half (the face's side, -x); rz > 0 opens it"),
    "husk_right": dict(poly=HALF_R, pivot=(525, 790), rim=False, desc="the husk's right half; rz < 0 opens it"),
    # while open, the halves swap to Qwen's faceless husk (parts_qwen_husk.png: the same husk, its face gone), so the
    # open right half shows no half of a face; the closed King keeps his own husk under the face
    "husk_left_open": dict(poly=HALF_L, pivot=(505, 790), rim=False, src=have(FACE % "husk"), desc="the left half, open"),
    "husk_right_open": dict(poly=HALF_R, pivot=(525, 790), rim=False, src=have(FACE % "husk"), desc="the right half, open"),
    "head": dict(poly=FACE_POLY, pivot=NECK, rim=False, air=0, desc="his face: wide eyes, a row of fangs"),
    "head_roar": dict(poly=FACE_POLY, pivot=NECK, rim=False, air=0, src=have(FACE % "roar"), desc="roaring, jaws wide"),
    "head_hurt": dict(poly=FACE_POLY, pivot=NECK, rim=False, air=0, src=have(FACE % "hurt"), desc="eyes shut tight"),
    "head_sad": dict(poly=FACE_POLY, pivot=NECK, rim=False, air=0, src=have(FACE % "sad"), desc="sad, in tears"),
    "head_calm": dict(poly=FACE_POLY, pivot=NECK, rim=False, air=0, src=have(FACE % "calm"), desc="calm, eyes closed, smiling"),
    "nut": dict(poly=NUT, pivot=NUT_BASE, at=(515, 720), src=have(FACE % "nut"), scale=1.5, desc="the chestnut inside the husk"),
    "chestnut": dict(poly=NUT, pivot=(850, 810), src=have(FACE % "nut"), scale=0.5, desc="a chestnut of Chestnut Rain"),
    "crown": dict(poly=CROWN, pivot=(868, 935), at=(515, 165), src=have(FACE % "crown"), scale=0.8, desc="his gold crown"),
    "leg_front": dict(poly=[(255, 828), (495, 828), (495, 980), (255, 980)], pivot=(375, 850), desc="the near foot"),
    "leg_back": dict(poly=[(515, 828), (755, 828), (755, 975), (515, 975)], pivot=(635, 850), desc="the far foot"),
}
for _k, _p in enumerate(([(120, 330), (300, 300), (270, 560), (115, 570)], [(720, 150), (900, 300), (760, 420), (680, 260)],
                         [(150, 600), (330, 620), (310, 790), (170, 720)], [(700, 600), (850, 560), (820, 760), (660, 780)])):
    CARDS["husk_shard_%d" % (_k + 1)] = dict(poly=_p, pivot=tuple(sum(v[i] for v in _p) / 4 for i in (0, 1)),
                                             desc="a piece of the husk falling away")
BINDS = [["head", "head_roar", "head_hurt", "head_sad", "head_calm"], ["husk_left", "husk_left_open"],
         ["husk_right", "husk_right_open"]]
ANCHORS = {"husk": (515, 465), "skull": (480, 400), "base": BASE}
