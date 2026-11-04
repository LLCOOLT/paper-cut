"""Big Green 01, Luigi's race kart (Super Luigi, chapter 3; docs/super_luigi/ch3.md),
from the art in port/assets/super_luigi/art/kart/ (docs/LUIGI_ANIMS.md,
"Big Green 01"):

- `c_lp_kart`: the kart alone (the garage, the grid, the tag scene), and the
  same kart in the other teams' colours, `c_lp_kart_red`, `_blue`, `_yellow`,
  `_purple`, `_orange`, `_white`, `_black`, and wrecked, `c_lp_kart_wreck`
  (the course's debris): paper_model models of our own.
- `c_luigi_kart` (sl_kart_luigi.py): Luigi seated in it, a variant of
  c_luigi_pc (sl_attach.py), for the start, the crash and Kart Mode.

The kart faces -x, its origin on the ground midway between the axles; 34
units long, 12 tall. Parts (the art's cards):
  kart              the kart (the mover; in c_luigi_kart, Luigi's locator2 moves both)
  wheel_far_f wheel_far_b   the far wheels (wheel_far, a tone darker)
  kart_engine       the engine, the pipe at (15.6, 10.8)
  kart_seat         the seat back, behind Luigi
  kart_nose         the nose cone; binds 0 whole, 1 wrecked
  kart_body         the side panel, in front of Luigi's legs; binds 0 whole, 1 wrecked
  plate_01          the "01" plate
  wheel_f wheel_b   the near wheels (rz > 0 rolls them forward)
  steering_wheel    in front of Luigi's front glove
  puff              an exhaust puff at the pipe (hidden)
  flame             a backfire out of the pipe (hidden)
  smoke_1 smoke_2   smoke over a wreck (hidden)
"""
import json
import math
import os

import paper_model as PM

PX = 8.0
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "port", "assets", "super_luigi", "art",
                   "kart")
# each kart's number, as the race scoreboard lists them (art/tools/kart_plates.py)
PLATE_NUMBER = {"red": "13", "blue": "07", "yellow": "22", "purple": "45", "orange": "88", "white": "64", "black": "99"}
PLATE_CULL, PLATE_CULL_BACK = 1, 0  # animdrv's cull modes: this side reads right, that side the mirror image
COLOURS = ["red", "blue", "yellow", "purple", "orange", "white", "black"]
PIVOT = (0.0, 5.0)  # where the kart turns as a whole: its middle

# name, card, z, model point of the card's pivot (default the art's), visible
# (the style model's kart, art/tools/cut_kart.py: axles at x -9.95 and 9.95, 3.9 up; the silencer's mouth at (14.3, 11.7);
# the steering wheel's top at 16.9; the side pod from x -2 to 11, 5.4 to 9 up)
CARDS = [
    ("wheel_far_f", "wheel_far", -1.6, (-9.4, 4.0), True),
    ("wheel_far_b", "wheel_far", -1.6, (10.6, 4.0), True),
    ("kart_engine", "kart_engine", -1.3, None, True),
    ("flame", "flame", -1.25, (14.3, 11.8), False),
    ("kart_seat", "kart_seat", -1.0, None, True),
    ("kart_nose", "kart_nose", 0.9, None, True),
    ("kart_body", "kart_body", 1.0, None, True),
    ("plate_01", "plate_01", 1.1, (2.0, 6.6), True),
    ("wheel_f", "wheel_near_front", 1.2, None, True),
    ("wheel_b", "wheel_near_back", 1.2, None, True),
    ("steering_wheel", "steering_wheel", 1.3, None, True),
    ("puff", "exhaust_puff", 1.35, (15.2, 12.2), False),
    ("smoke_1", "smoke_puff", 1.4, (-2.0, 15.0), False),
    ("smoke_2", "smoke_puff", 1.45, (7.0, 17.0), False),
]
REAR = (9.95, 0.0)  # the back tyre on the ground: a wheelie turns about it


def _meta():
    return json.load(open(os.path.join(ART, "parts.json")))["cards"]


def _img(card):
    return PM.pad8(PM.read_png(os.path.join(ART, card + ".png")))


def card_rect(meta, card, at=None):
    """(rect, pivot) of a card placed with its pivot at `at` (default the art's)"""
    m = meta[card]
    px, py = m["pivot"]
    ax, ay = at if at is not None else m["model_xy"]
    w, h = m["size"]
    tpu = float(m.get("texels_per_unit", PX))  # the cards cut from the design: art/tools/cutout.py's density
    x0, y1 = ax - px / tpu, ay + py / tpu
    return (x0, y1 - h / tpu, x0 + w / tpu, y1), (ax, ay)


def card_list(colour=None, wrecked=False):
    """[(name, images, rect, z, pivot, visible, cull)] for the kart's cards: the plate twice, one side each (the second its
    mirror image), so its number never reads backwards when the kart faces the other way; last, so the recipes' numbers hold"""
    meta = _meta()
    # the rivals and the wreck, seen at race distance, take the half-density cut (<card>_lo, art/tools/kart_cards.py);
    # Luigi's own kart the full one
    lo = lambda n: n + "_lo" if (colour or wrecked) and n + "_lo" in meta else n
    out = []
    for name, card, z, at, vis in CARDS:
        src = card
        if colour and card in ("kart_body", "kart_nose", "kart_seat"):
            src = "%s_%s" % (card, colour)
        if wrecked and card in ("kart_body", "kart_nose"):
            src = card + "_wrecked"
        src = lo(src)
        imgs = [_img(src)]
        if card in ("kart_body", "kart_nose"):
            # bind 1: the crash's dented, scorched drawing in the kart's own
            # colour (kart_body_wrecked_<colour>, kart_nose_wrecked_<colour>)
            imgs.append(_img(lo(card + "_wrecked" + ("_" + colour if colour else ""))))
        if card == "plate_01":
            imgs = [_img("plate_blank" if wrecked else "plate_" + PLATE_NUMBER.get(colour, "01"))]
        rect, piv = card_rect(meta, card if card == "plate_01" or src not in meta else src, at)
        out.append((name, imgs, rect, z, piv, vis, PLATE_CULL if card == "plate_01" else 3))
        if card == "plate_01":
            back = (name + "_back", [[list(reversed(row)) for row in imgs[0]]], rect, z, piv, vis, PLATE_CULL_BACK)
    out.append(back)
    return out


class Kart:
    """the kart alone (paper_model), in a colour"""

    def __init__(self, name, colour=None, wrecked=False):
        self.NAME, self.colour, self.wrecked = name, colour, wrecked
        self.MOTIONS = kart_motions("kart", PIVOT, None)
        if wrecked:
            self.MOTIONS["S_1"] = wreck_rest()
        self.CAMERA = {"height": 30.0, "width": 48.0, "center": (0.0, -4.0), "res": (384, 240)}

    def parts(self):
        out = [PM.Part("|root"), PM.Part("kart", "|root", pivot=PIVOT)]
        for name, imgs, rect, z, piv, vis, cull in card_list(self.colour, self.wrecked):
            out.append(PM.Part(name, "kart", imgs[0], rect, z, piv, imgs[1:], vis, cull))
        return out

    def build(self, anims=()):
        anims = list(anims)
        if not any(x.name == "Z_1" for x in anims):
            anims.insert(0, PM.still("Z_1"))
        return PM.build(self.NAME, self.parts(), anims)


# ---------------------------------------------------------------- motions ----
# Key poses as luigi_motions.py's. `mover` is the group that carries the kart
# (and in c_luigi_kart, Luigi with it), `pivot` its turning point; `seat` the
# seated rider's pose (c_luigi_kart's), merged into every key, or None.

def _p(*ds):
    out = {}
    for d in ds:
        for g, ch in (d or {}).items():
            out.setdefault(g, {}).update(ch)
    return out


def _whole(mover, rz=0.0, tx=0.0, ty=0.0, sx=None, sy=None, about=PIVOT, origin=(0.0, 0.0)):
    """the mover turned rz about `about` (its own pivot is `origin`), moved"""
    a = math.radians(rz)
    px, py = about[0] - origin[0], about[1] - origin[1]
    dx = px - (math.cos(a) * px - math.sin(a) * py)
    dy = py - (math.sin(a) * px + math.cos(a) * py)
    ch = {"rz": rz, "tx": tx + dx, "ty": ty + dy}
    if sx is not None:
        ch["sx"] = sx
    if sy is not None:
        ch["sy"] = sy
    return {mover: ch}


def _wheels(deg):
    return {"wheel_f": {"rz": deg}, "wheel_b": {"rz": deg}, "wheel_far_f": {"rz": deg}, "wheel_far_b": {"rz": deg}}


def _puff(k):
    """the exhaust puff, k 0 (at the pipe, small) .. 1 (risen, big)"""
    return {"puff": {"vis": 1, "sx": 0.5 + 0.75 * k, "sy": 0.5 + 0.75 * k, "tx": 2.0 * k, "ty": 1.5 * k}}


def _smoke(k, which="smoke_1", drift=1.0, flipped=True):
    """smoke rising (k 0 .. 1); flipped: the kart is on its back, so the smoke's
    up is the kart's down, and the drawing is turned back upright"""
    f = -1.0 if flipped else 1.0
    if flipped:  # turned 180 about PIVOT and resting UPSIDE_TY up: a local y maps to 2 * 5 + UPSIDE_TY - y
        base = {"smoke_1": 15.0, "smoke_2": 17.0}[which]
        ty = (2 * PIVOT[1] + UPSIDE_TY - base) - (17.0 + 12.0 * k)  # from the upturned floor (16.9 up) and rising
    else:
        ty = 7.0 * k
    return {which: {"vis": 1, "sx": 0.75 + 0.75 * k, "sy": 0.75 + 0.75 * k, "tx": 3.0 * k * drift * f, "ty": ty,
                    "rz": 180.0 if flipped else 0.0}}


WRECK_RZ = 5.0  # the wreck at rest: its front wheels off, it sits nose down on the ground, turned about the back tyre


def wreck_rest():
    """c_lp_kart_wreck's S_1 (the race's wrecks, loop 40): the front wheels off (the near one lies flat on the ground in
    front), nose down, the steering wheel knocked askew, smoke rising from the seat and the engine in turn"""
    a = math.radians(WRECK_RZ)
    # the near front wheel squashed to a sliver (sy 0.35) and moved 5 ahead: its middle on the ground (1.4 up, half its
    # squashed height) once the kart's turn is applied: y' = (x - REAR x) sin a + y cos a
    x, y = -9.95 - 5.0, 3.9
    ty = (1.4 - (x - REAR[0]) * math.sin(a)) / math.cos(a) - y
    rest = _p(_whole("kart", rz=WRECK_RZ, about=REAR, origin=PIVOT), {
        "wheel_far_f": {"vis": 0}, "wheel_f": {"tx": -5.0, "ty": ty, "sy": 0.35, "rz": 0.0},
        "steering_wheel": {"rz": 28.0}})
    keys = []
    for f in range(0, 41, 4):
        keys.append((f, _p(rest, _smoke((f % 40) / 40.0, flipped=False),
                           _smoke(((f + 20) % 40) / 40.0, "smoke_2", -1.0, flipped=False)), {"ease": "linear"}))
    return {"end": 40, "loop": True, "keys": keys}


UPSIDE_TY = 6.9  # upside down (turned 180 about PIVOT), it rests on its seat and steering wheel (16.9 up)


def kart_motions(mover, origin, seat, rider=None):
    """the kart's poses; `rider`: extra channels per pose for a rider (c_luigi_kart)"""
    rider = rider or {}

    def k(*ds, who=None):
        return _p(seat, *ds, rider.get(who, {}) if who else None)

    w = lambda **kw: _whole(mover, origin=origin, **kw)
    M = {}
    # S_1: parked (the engine off; loop 60, a still breath)
    M["S_1"] = {"end": 60, "loop": True, "keys": [(0, k(w(), who="idle")), (60, k(w(), who="idle"))]}
    # S_2: the engine running: a shake, puffs from the pipe (loop 12)
    M["S_2"] = {"end": 12, "loop": True, "keys": [
        (0, k(w(ty=0.0), _puff(0.0), who="idle")), (3, k(w(ty=0.15, rz=0.4), _puff(0.4), who="idle")),
        (6, k(w(ty=0.0), _puff(0.7), who="idle")), (9, k(w(ty=0.15, rz=-0.4), _puff(1.0), who="idle")),
        (12, k(w(ty=0.0), _puff(0.0), who="idle"))]}
    # W_1: driving: the wheels roll (a turn every 16 frames), a bob, puffs (loop 16)
    M["W_1"] = {"end": 16, "loop": True, "keys": [
        (0, k(w(), _wheels(0), _puff(0.0), who="drive"), {"ease": "linear"}),
        (4, k(w(ty=0.35, rz=0.8), _wheels(90), _puff(0.35), who="drive"), {"ease": "linear"}),
        (8, k(w(), _wheels(180), _puff(0.7), who="drive"), {"ease": "linear"}),
        (12, k(w(ty=0.35, rz=-0.8), _wheels(270), _puff(1.0), who="drive"), {"ease": "linear"}),
        (16, k(w(), _wheels(360), _puff(0.0), who="drive"), {"ease": "linear"})]}
    # R_1: flat out: a turn every 8 frames, nose down, the flame flickering (loop 8)
    M["R_1"] = {"end": 8, "loop": True, "keys": [
        (0, k(w(rz=1.5), _wheels(0), {"flame": {"vis": 1, "sx": 0.75}}, who="fast"), {"ease": "linear"}),
        (2, k(w(rz=1.5, ty=0.3), _wheels(90), {"flame": {"vis": 1, "sx": 1.0}}, who="fast"), {"ease": "linear"}),
        (4, k(w(rz=1.5), _wheels(180), {"flame": {"vis": 1, "sx": 0.625}}, who="fast"), {"ease": "linear"}),
        (6, k(w(rz=1.5, ty=0.3), _wheels(270), {"flame": {"vis": 1, "sx": 1.0}}, who="fast"), {"ease": "linear"}),
        (8, k(w(rz=1.5), _wheels(360), {"flame": {"vis": 1, "sx": 0.75}}, who="fast"), {"ease": "linear"})]}
    # B_1: full throttle in reverse (the start): the wheels roll backwards, the
    # nose up in a wheelie, the flame out of the pipe (loop 8)
    M["B_1"] = {"end": 8, "loop": True, "keys": [
        (0, k(w(rz=-9.0, about=REAR), _wheels(0), {"flame": {"vis": 1, "sx": 1.0}}, who="reverse"),
         {"ease": "linear"}),
        (2, k(w(rz=-11.0, about=REAR, ty=0.3), _wheels(-90), {"flame": {"vis": 1, "sx": 1.25}},
              who="reverse2"), {"ease": "linear"}),
        (4, k(w(rz=-9.0, about=REAR), _wheels(-180), {"flame": {"vis": 1, "sx": 0.875}}, who="reverse"),
         {"ease": "linear"}),
        (6, k(w(rz=-11.0, about=REAR, ty=0.3), _wheels(-270), {"flame": {"vis": 1, "sx": 1.25}},
              who="reverse2"), {"ease": "linear"}),
        (8, k(w(rz=-9.0, about=REAR), _wheels(-360), {"flame": {"vis": 1, "sx": 1.0}}, who="reverse"),
         {"ease": "linear"})]}
    # C_1: the crash, backwards into the wall: crumpled against it, thrown up
    # end over end, down on its back, a bounce, smoking (60, held)
    up = lambda rz, ty, tx=0.0: w(rz=rz, ty=ty, tx=tx)
    wreck = {"kart_body": {"tex": 1}, "kart_nose": {"tex": 1}}
    M["C_1"] = {"end": 60, "keys": [
        (0, k(w(rz=-9.0, about=REAR), {"flame": {"vis": 1}}, who="reverse")),
        (3, k(w(rz=-4.0, about=REAR, sx=0.8125), wreck, who="hit"), {"ease": "vector"}),
        (7, k(up(-40.0, 8.0, -4.0), wreck, _wheels(-200), who="tumble")),
        (14, k(up(-120.0, 16.0, -9.0), wreck, _wheels(-300), _smoke(0.2, flipped=False), who="tumble")),
        (21, k(up(-170.0, 12.0, -13.0), wreck, _wheels(-380), _smoke(0.4), who="gone")),
        (26, k(up(-180.0, UPSIDE_TY, -15.0), wreck, _wheels(-420), _smoke(0.6), who="gone"), {"ease": "vector"}),
        (31, k(up(-178.0, UPSIDE_TY + 3.0, -15.5), wreck, _wheels(-450), _smoke(0.8), who="gone")),
        (36, k(up(-180.0, UPSIDE_TY, -16.0), wreck, _wheels(-470), _smoke(1.0), _smoke(0.0, "smoke_2"), who="gone"),
         {"ease": "vector"}),
        (40, k(up(-180.0, UPSIDE_TY + 0.6, -16.0), wreck, _wheels(-480), _smoke(0.3, "smoke_2"), who="gone")),
        (44, k(up(-180.0, UPSIDE_TY, -16.0), wreck, _wheels(-485), _smoke(0.5, "smoke_2"), who="gone")),
        (60, k(up(-180.0, UPSIDE_TY, -16.0), wreck, _wheels(-490), _smoke(1.0, "smoke_2"), who="gone")),
    ]}
    # C_2: on its back, smoking, a wheel still turning (loop 40), where C_1 ends
    # (C_1 moves it 16 units back; C_2 is in place: set the position to match)
    M["C_2"] = {"end": 40, "loop": True, "keys": [
        (f, k(up(-180.0 + (0.6 if f % 20 else 0.0), UPSIDE_TY), wreck,
              {"wheel_f": {"rz": -12.0 * f}, "wheel_far_b": {"rz": -6.0 * f}},
              _smoke((f % 40) / 40.0), _smoke(((f + 20) % 40) / 40.0, "smoke_2", -1.0), who="gone"), {"ease": "linear"})
        for f in (0, 10, 19, 20, 30, 39, 40)]}
    # F_1: flipped back onto its wheels (the pit stop): a shudder, a hop, half
    # a turn, a hard landing and a backfire (40)
    M["F_1"] = {"end": 40, "keys": [
        (0, k(up(-180.0, UPSIDE_TY), wreck, who="gone")),
        (4, k(up(-176.0, UPSIDE_TY + 0.5), wreck, who="gone")),
        (8, k(up(-182.0, UPSIDE_TY), wreck, who="gone")),
        (16, k(up(-270.0, 14.0), who="gone"), {"ease": "linear"}),
        (22, k(up(-360.0, 0.0, 0.0), {"kart": {}}, who="idle"), {"ease": "vector"}),
        (24, k(w(rz=0.0, sy=0.875, ty=-0.3), {"flame": {"vis": 1, "sx": 1.25, "sy": 1.25}}, _puff(0.5), who="idle")),
        (28, k(w(sy=1.0625), {"flame": {"vis": 1, "sx": 0.75, "sy": 0.75}}, _puff(1.0), who="idle")),
        (32, k(w(), who="idle")),
        (40, k(w(), who="idle")),
    ]}
    # D_1: spinning out (a wreck on the course): two paper turns on a hop,
    # wobbling back down (40)
    M["D_1"] = {"end": 40, "keys": [
        (0, k(w(), who="hurt")),
        (5, k(w(sx=0.0625, ty=2.0, rz=4.0), who="hurt"), {"ease": "linear"}),
        (10, k(w(sx=-1.0, ty=3.0), who="hurt"), {"ease": "linear"}),
        (15, k(w(sx=0.0625, ty=2.5, rz=-4.0), who="hurt"), {"ease": "linear"}),
        (20, k(w(sx=1.0, ty=1.5), who="hurt"), {"ease": "linear"}),
        (24, k(w(sx=0.0625, ty=0.5), who="hurt"), {"ease": "linear"}),
        (28, k(w(sx=-1.0, ty=0.0), who="hurt"), {"ease": "linear"}),
        (32, k(w(sx=0.0625), who="hurt"), {"ease": "linear"}),
        (36, k(w(sx=1.0, rz=2.0), who="idle"), {"ease": "vector"}),
        (40, k(w(), who="idle")),
    ]}
    return M


KART = Kart("c_lp_kart")
WRECK = Kart("c_lp_kart_wreck", wrecked=True)
for _c in COLOURS:
    globals()["KART_" + _c.upper()] = Kart("c_lp_kart_" + _c, _c)

POSES = {
    "S_1": "parked (loop 60)", "S_2": "the engine running: a shake, puffs (loop 12)",
    "W_1": "driving: wheels rolling, a bob, puffs (loop 16)", "R_1": "flat out, the flame flickering (loop 8)",
    "B_1": "full throttle in reverse: the wheels backwards, a wheelie, the flame (loop 8)",
    "C_1": "the crash backwards into a wall: crumpled, end over end, on its back, smoking (60, held; "
           "it ends 16 units back of where it started)",
    "C_2": "on its back, smoking (loop 40)", "F_1": "flipped back onto its wheels, a backfire (40)",
    "D_1": "spinning out (40)",
}
