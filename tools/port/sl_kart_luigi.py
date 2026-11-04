"""Luigi in Big Green 01 (Super Luigi, chapter 3): `c_luigi_kart`, a variant of
c_luigi_pc (sl_attach.py) with the kart's cards (sl_kart.py) round him: he
sits in it, his legs hidden in the body, his hands on the wheel, and the
kart's poses move him with it (Luigi's locator2 carries both). For the
grid, the reverse start and the crash, and Kart Mode (drawn in place of the
tube's model). Its poses are the kart's (sl_kart.POSES), with Luigi riding:
calm at the wheel, leaning into it flat out, thrown forward screaming in
reverse, slammed back at the wall, flailing end over end, and gone under the
kart once it lands on its back (he crawls out: the story's own scene, with
c_lp_kart's C_2 under him).
"""
import sl_kart as K

NAME = "c_luigi_kart"
BASE = "c_luigi_pc"

# Luigi seated: hips on the seat (his hips are at (3, 4.75) in the kart),
# leaning to the wheel, his arms long enough to reach it, legs hidden
SEAT = {"locator3": {"tx": 3.0, "ty": 0.7, "rz": 10.0}, "locator4": {"vis": 0}, "locator5": {"vis": 0},
        "pPlane16": {"rz": -70.0}, "pPlane15": {"rz": -62.0},
        "pPlane3": {"rz": -20.0}, "pPlane2": {"rz": -20.0}}
_SCREAM = {"pPlane12": {"vis": 1}, "group1": {"sy": 1.1875}}
RIDER = {
    "idle": {},
    "drive": {"locator3": {"tx": 3.0, "ty": 0.7, "rz": 14.0}},
    "fast": {"locator3": {"tx": 3.0, "ty": 0.5, "rz": 20.0}, "pPlane14": {"rz": 6.0}, "group1": {"sy": 0.75}},
    "reverse": dict(_SCREAM, locator3={"tx": 2.0, "ty": 0.9, "rz": 28.0}, pPlane14={"rz": 10.0},
                    pPlane16={"rz": 150.0}, pPlane15={"rz": 170.0}),
    "reverse2": dict(_SCREAM, locator3={"tx": 2.0, "ty": 1.1, "rz": 30.0}, pPlane14={"rz": 14.0},
                     pPlane16={"rz": 175.0}, pPlane15={"rz": 140.0}),
    "hit": dict(_SCREAM, locator3={"tx": 3.5, "ty": 0.4, "rz": -22.0, "sx": 0.875}, pPlane14={"rz": -12.0},
                pPlane16={"rz": -40.0}, pPlane15={"rz": -30.0}),
    "tumble": dict(_SCREAM, locator3={"tx": 3.0, "ty": 1.5, "rz": -10.0}, pPlane14={"rz": -8.0},
                   pPlane16={"rz": -190.0}, pPlane15={"rz": -200.0}),
    "gone": {"locator3": {"tx": 3.0, "ty": 0.7, "rz": 10.0, "vis": 0}},
    "hurt": {"locator3": {"tx": 3.0, "ty": 0.7, "rz": 4.0}, "pPlane14": {"rz": 10.0}, "group1": {"sy": 0.25},
             "pPlane16": {"rz": -100.0}, "pPlane15": {"rz": -20.0}},
}

# the style model's kart (art/tools/cut_kart.py) seats him higher and further forward: hips at (0.5, 9.2); the poses
# above are written against the drawn kart's (3, 4.75)
SEAT_D = (-2.5, 4.4)
for _pose in [SEAT] + list(RIDER.values()):
    if "locator3" in _pose:
        _pose["locator3"] = dict(_pose["locator3"], tx=_pose["locator3"]["tx"] + SEAT_D[0],
                                 ty=_pose["locator3"]["ty"] + SEAT_D[1])

MOTIONS = K.kart_motions("locator2", (0.0, 0.0), SEAT, RIDER)
# T_1: talking at the wheel (loop 20)
MOTIONS["T_1"] = {"end": 20, "loop": True, "keys": [
    (0, dict(SEAT)), (5, K._p(SEAT, {"pPlane12": {"vis": 1}, "pPlane14": {"rz": -3.0}})),
    (10, dict(SEAT)), (15, K._p(SEAT, {"pPlane12": {"vis": 1}, "pPlane14": {"rz": 2.0}})), (20, dict(SEAT))]}
POSES = dict(K.POSES, T_1="talking at the wheel (loop 20)")
CAMERA = {"height": 34.0, "width": 52.0, "center": (0.0, -4.0), "res": (416, 272)}


def cards():
    out = [{"name": "kart", "parent": "locator2", "pivot": K.PIVOT, "z": 0.0}]
    for name, imgs, rect, z, piv, vis, cull in K.card_list():
        out.append({"name": name, "parent": "kart", "images": imgs, "rect": rect, "z": z, "pivot": piv,
                    "visible": vis, "cull": cull})
    return out
