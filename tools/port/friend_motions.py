"""The motions of Luigi's friends, posed key by key (runs inside Blender;
docs/LUIGI_ANIMS.md, "Luigi's friends").

In Super Luigi (docs/SUPER_LUIGI.md) the friends are Luigi's partners: they
follow him in the field and fight beside him. The disc has only Z_1, S_1 and
T_1 for each (and Z_2, S_2, T_2 for Blooey, burnt); these are the rest, named
as the game's own partners' are (partyPoseTbl in src/data/party_data.c, the
partners' battle pose tables in src/unit/party/), in the same format and units
as luigi_motions.py: key poses at game frames (60 a second), per group of the
model, offsets from its rest pose (tx ty tz; rz rx in degrees; sx sy as a
multiple of rest, snapped to sixteenths; vis; tex), plus

  v   a plane's corners moved in its own units, [(dx, dy)] in the shape's
      vertex order (the game's vertex animation, linear between keys)

The friends face left (-x) as every NPC drawing does; rz turns
counter-clockwise seen from the front, so a positive rz leans them forward
(toward -x), a negative one back.

Every friend has the same set (STANDARD), timed as the game's partners and
the matching enemies are (the table in docs/LUIGI_ANIMS.md), and its moves.

Each model's parts (the disc's names):
  c_lp_gesso  Blooey: pPlane1 the whole drawing (corners 0 bottom left,
              1 bottom right, 2 top left, 3 top right; pivot 3.4 up); bind 1 of
              its texture is the burnt Blooey
  c_lp_bom    Jerry: pPlane2 body (pivot at its middle), pPlane3 fuse (its
              child, pivot at its root), locator4/pPlane5 front foot,
              locator5/pPlane6 back foot, locator2 the whole; locator3/pPlane4
              the wind-up key, hidden
  c_lp_met    Torque: pPlane8 body (root of the rest), pPlane9 shell,
              pPlane10 wrench (the shell's child), pPlane3 head (pPlane2 its
              eye), group1 front legs (pPlane11, pPlane13), group2 back legs
              (pPlane12, pPlane14)
  c_lp_pansy  Hayzee: pPlane6 body, pPlane2 flower head (pPlane3 the mouth),
              pPlane5 front leaf arm, pPlane4 back leaf arm, pPlane8 front
              foot, pPlane7 back foot
  c_lp_moa    Screamy: locator3 the body's mover (pPlane5 body; group1 the
              eyes pPlane1 and pPlane3; pPlane2 mouth), locator2/pPlane7 front
              foot, locator1/pPlane4 back foot
"""
import math

import ttyd_blender as TB  # noqa: F401 (luigi_motions needs it loaded)
import luigi_motions as LM

FRIENDS = {}

# every friend's poses beyond the disc's Z_1 S_1 T_1, as the game's partners name
# them (field: partyPoseTbl's columns; battle: the partners' pose tables)
STANDARD = {
    "W_1": "walk (field and battle)",
    "R_1": "run (field and battle)",
    "J_1A": "jump, going up",
    "J_1B": "jump, coming down",
    "D_1": "hurt: knocked away in the field, hit in battle (loop)",
    "D_2": "worn out: low HP standing, poisoned, in peril (loop)",
    "W_2": "worn out, walking",
    "R_2": "worn out, running",
    "D_3": "KO: falls and stays down",
    "D_4": "tumbles back (a failed Run Away; knocked away in the field)",
    "D_5": "frozen or stopped stiff (one pose)",
    "N_1": "asleep (field: when Luigi sleeps; battle: Sleep) (loop)",
    "X_1": "dizzy or confused (loop)",
    "Q_1": "scared, trembling (loop)",
    "C_1": "ready, at the command menu (loop)",
    "G_1": "guarding (the Defend command, a guarded hit)",
    "G_2": "Superguard",
    "I_1": "holding up an item",
    "O_1": "appealing to the audience",
    "Y_1": "victory (loop)",
    "F_1": "the field ability",
}


def burnt(motion):
    """Blooey's burnt version of a motion: the same, drawn with texture bind 1"""
    keys = []
    for f, pose, *opt in motion["keys"]:
        p = {g: dict(c) for g, c in pose.items()}
        p.setdefault("pPlane1", {})["tex"] = 1
        keys.append((f, p, *opt))
    return dict(motion, keys=keys)


def soot(motion):
    """soot-black Blooey: bind 2 of his plane, the smoke card shown and curling
    (a sway of its own every 30 frames, over whatever the pose does)"""
    keys = []
    for f, pose, *opt in motion["keys"]:
        p = {g: dict(c) for g, c in pose.items()}
        p.setdefault("pPlane1", {})["tex"] = 2
        keys.append((f, p, *opt))
    end = motion["end"]
    times = sorted(set(list(range(0, end + 1, 10)) + [end]))
    tracks = dict(motion.get("tracks", {}))
    tracks["pSmoke"] = {
        "vis": [(t, 1, "step") for t in times[:1]],
        "rz": [(t, 8.0 * math.sin(t * math.pi / 15.0)) for t in times],
        "ty": [(t, 0.4 * math.sin(t * math.pi / 30.0)) for t in times],
        "sy": [(t, 1.0 + 0.0625 * round(2 * math.sin(t * math.pi / 15.0 + 1.0))) for t in times],
    }
    return dict(motion, keys=keys, tracks=tracks)


# ------------------------------------------------------------------ Blooey ----
# A Blooper swims by pulses: the tentacles draw in under him (tall, narrow), then
# flare and push (short, wide) and he shoots up and forward; he glides, slowly
# sinking, until the next. Blooey does it in the air, a little above the ground.
# One plane: the pulses are its scale and its corners (the head's two, the
# tentacles' two), as the game's Bloopers are drawn by vertex animation.


def _bl(ty=0.0, rz=0.0, sx=1.0, sy=1.0, bottom=(0.0, 0.0), top=(0.0, 0.0), tx=0.0, rx=0.0, skew=0.0):
    """a pose of Blooey's one plane: bottom = (in, up) for the tentacles' corners
    (in: toward the middle), top = (in, down) for the head's; skew moves the
    tentacles back (+x) and the head forward"""
    bi, bu = bottom
    ti, td = top
    return {"pPlane1": {"tx": tx, "ty": ty, "rz": rz, "rx": rx, "sx": sx, "sy": sy,
                        "v": [(bi + skew, bu), (-bi + skew, bu), (ti - skew / 2, -td), (-ti - skew / 2, -td)]}}


_CONTRACT = dict(sx=0.9375, sy=1.0625, bottom=(2.0, 0.6), top=(0.4, 0.0))   # tentacles drawn in
_FLARE = dict(sx=1.0625, sy=0.9375, bottom=(-1.6, 1.4), top=(-0.2, 0.3))    # the push
_SPREAD = dict(sx=1.0625, sy=0.9375, bottom=(-1.8, 1.4), top=(-0.2, 0.2))   # parachute
_LIMP = dict(sy=0.9375, bottom=(-1.2, 0.2), top=(-0.1, 0.4))                # tired, tentacles splayed

_GESSO = {}
# walk: one pulse a loop (40 frames, the partners' walk); rises 2 and glides down
_GESSO["W_1"] = {"end": 40, "loop": True, "keys": [
    (0, _bl(ty=0.9, rz=1, bottom=(0.5, 0.2))),
    (7, _bl(ty=0.5, rz=-1, **_CONTRACT)),
    (11, _bl(ty=0.9, rz=3, **_FLARE), {"ease": "vector"}),
    (17, _bl(ty=2.6, rz=4, bottom=(-0.6, 0.8))),
    (26, _bl(ty=2.9, rz=3, bottom=(0.0, 0.3))),
    (40, _bl(ty=0.9, rz=1, bottom=(0.5, 0.2))),
]}
# run: the same pulse in 20 frames, harder, leaning into it
_GESSO["R_1"] = {"end": 20, "loop": True, "keys": [
    (0, _bl(ty=1.2, rz=7, bottom=(0.6, 0.3))),
    (4, _bl(ty=0.8, rz=5, sx=0.875, sy=1.125, bottom=(2.6, 0.8), top=(0.5, 0.0))),
    (7, _bl(ty=1.3, rz=9, sx=1.0625, sy=0.875, bottom=(-1.8, 1.6), top=(-0.3, 0.4)), {"ease": "vector"}),
    (11, _bl(ty=2.6, rz=10, bottom=(-0.8, 0.9))),
    (20, _bl(ty=1.2, rz=7, bottom=(0.6, 0.3))),
]}
# jump, going up: one big push, then streamlined, tentacles trailing together
_GESSO["J_1A"] = {"end": 8, "keys": [
    (0, _bl(sx=1.125, sy=0.875, bottom=(-2.4, 1.6), top=(-0.3, 0.4))),
    (4, _bl(ty=1.6, sx=0.875, sy=1.125, bottom=(3.0, -0.6), top=(0.6, 0.0))),
    (8, _bl(ty=1.6, sx=0.9375, sy=1.125, bottom=(2.8, -0.5), top=(0.5, 0.0))),
]}
# jump, coming down: tentacles spread like a parachute, fluttering
_GESSO["J_1B"] = {"end": 16, "loop": True, "keys": [
    (0, _bl(**_SPREAD)),
    (8, _bl(ty=0.2, sx=1.0625, sy=0.9375, bottom=(-2.4, 1.9), top=(-0.3, 0.3))),
    (16, _bl(**_SPREAD)),
]}
# hurt: a shudder, tentacles flailing (8 frames, as Goombella's)
_GESSO["D_1"] = {"end": 8, "loop": True, "keys": [
    (0, _bl(ty=0.6, tx=0.4, rz=-10, sx=1.125, sy=0.875, bottom=(-2.0, 1.2), top=(-0.4, 0.3))),
    (4, _bl(ty=0.6, tx=-0.2, rz=6, sx=0.875, sy=1.125, bottom=(2.2, -0.4), top=(0.5, 0.0))),
    (8, _bl(ty=0.6, tx=0.4, rz=-10, sx=1.125, sy=0.875, bottom=(-2.0, 1.2), top=(-0.4, 0.3))),
]}
# worn out: sunk to the ground, tentacles splayed, a slow weak pulse (92, as Goombella's)
_GESSO["D_2"] = {"end": 92, "loop": True, "keys": [
    (0, _bl(rz=-3, **_LIMP)),
    (30, _bl(ty=0.3, rz=-2, sx=0.9375, sy=1.0, bottom=(0.6, 0.3), top=(0.2, 0.2))),
    (46, _bl(ty=0.5, rz=-1, sx=1.0, sy=0.9375, bottom=(-0.8, 0.4))),
    (92, _bl(rz=-3, **_LIMP)),
]}
# worn out, walking and running: low, drooping, the pulses weak
_GESSO["W_2"] = {"end": 40, "loop": True, "keys": [
    (0, _bl(ty=0.3, rz=-2, bottom=(-0.6, 0.2), top=(0.0, 0.3))),
    (8, _bl(ty=0.1, rz=-3, sx=0.9375, sy=1.0, bottom=(1.4, 0.4), top=(0.2, 0.2))),
    (13, _bl(ty=0.4, rz=0, sx=1.0625, sy=0.9375, bottom=(-1.2, 1.0), top=(-0.1, 0.4)), {"ease": "vector"}),
    (22, _bl(ty=1.2, rz=1, bottom=(-0.4, 0.5), top=(0.0, 0.2))),
    (40, _bl(ty=0.3, rz=-2, bottom=(-0.6, 0.2), top=(0.0, 0.3))),
]}
_GESSO["R_2"] = {"end": 20, "loop": True, "keys": [
    (0, _bl(ty=0.4, rz=3, bottom=(-0.6, 0.2), top=(0.0, 0.3))),
    (5, _bl(ty=0.2, rz=2, sx=0.9375, sy=1.0, bottom=(1.6, 0.5), top=(0.2, 0.2))),
    (8, _bl(ty=0.5, rz=5, sx=1.0625, sy=0.9375, bottom=(-1.4, 1.1), top=(-0.1, 0.4)), {"ease": "vector"}),
    (12, _bl(ty=1.4, rz=6, bottom=(-0.4, 0.5), top=(0.0, 0.2))),
    (20, _bl(ty=0.4, rz=3, bottom=(-0.6, 0.2), top=(0.0, 0.3))),
]}
# KO: a last weak pulse, then he goes limp and sinks flat, tentacles splayed
# (16 frames, held)
_GESSO["D_3"] = {"end": 16, "keys": [
    (0, _bl(ty=0.8, rz=-6, **_FLARE)),
    (6, _bl(ty=1.6, rz=-10, sx=0.9375, sy=1.0625, bottom=(1.0, 0.0))),
    (12, _bl(ty=-1.6, rz=-4, sx=1.1875, sy=0.5, bottom=(-3.0, 0.6), top=(-0.6, 0.0)), {"ease": "vector"}),
    (14, _bl(ty=-1.3, rz=-4, sx=1.125, sy=0.5625, bottom=(-2.8, 0.6), top=(-0.5, 0.0))),
    (16, _bl(ty=-1.6, rz=-4, sx=1.1875, sy=0.5, bottom=(-3.0, 0.6), top=(-0.6, 0.0))),
]}
# tumbling back: a somersault backward, tentacles flung out (20 frames, as Goombella's)
_GESSO["D_4"] = {"end": 20, "keys": [
    (0, _bl(ty=0.5, rz=-10, **_FLARE)),
    (6, _bl(ty=10.0, tx=1.5, rz=-120, sx=0.9375, sy=1.0625, bottom=(2.0, 0.0))),
    (12, _bl(ty=10.0, tx=3.0, rz=-250, sx=1.0625, sy=0.9375, bottom=(-1.8, 1.0))),
    (16, _bl(ty=0.4, tx=3.5, rz=-350, **_LIMP), {"ease": "vector"}),
    (20, _bl(ty=0.2, tx=3.5, rz=-360, **_LIMP)),
]}
# frozen or stopped: caught mid-pulse, tentacles drawn in
_GESSO["D_5"] = {"end": 0, "keys": [(0, _bl(ty=0.6, rz=2, **_CONTRACT))]}
# asleep: settled on the ground, breathing slow and deep (120, as Goombella's)
_GESSO["N_1"] = {"end": 120, "loop": True, "keys": [
    (0, _bl(rz=-2, sx=1.0625, sy=0.9375, bottom=(-1.4, 0.2), top=(-0.1, 0.3))),
    (60, _bl(rz=-1, sx=1.0, sy=1.0, bottom=(-0.6, 0.2), top=(0.1, 0.0))),
    (120, _bl(rz=-2, sx=1.0625, sy=0.9375, bottom=(-1.4, 0.2), top=(-0.1, 0.3))),
]}
# dizzy: wobbling round in a lazy circle, tentacles swaying (48, as Goombella's)
_GESSO["X_1"] = {"end": 48, "loop": True, "keys": [
    (0, _bl(ty=0.8, rz=10, skew=1.2, bottom=(-0.4, 0.4))),
    (12, _bl(ty=1.4, tx=-0.6, rz=0, sx=1.0625, sy=0.9375, skew=0.0, bottom=(-1.0, 0.6))),
    (24, _bl(ty=0.8, rz=-10, skew=-1.2, bottom=(-0.4, 0.4))),
    (36, _bl(ty=0.3, tx=0.6, rz=0, sx=0.9375, sy=1.0625, skew=0.0, bottom=(0.8, 0.2))),
    (48, _bl(ty=0.8, rz=10, skew=1.2, bottom=(-0.4, 0.4))),
]}
# scared: shrunk into himself, trembling (8 frames, as Goombella's)
_GESSO["Q_1"] = {"end": 8, "loop": True, "keys": [
    (0, _bl(ty=0.4, tx=0.3, rz=-4, sx=0.9375, sy=1.0, bottom=(2.4, 0.8), top=(0.4, 0.2))),
    (4, _bl(ty=0.4, tx=-0.3, rz=-2, sx=0.9375, sy=1.0, bottom=(2.6, 0.9), top=(0.4, 0.2))),
    (8, _bl(ty=0.4, tx=0.3, rz=-4, sx=0.9375, sy=1.0, bottom=(2.4, 0.8), top=(0.4, 0.2))),
]}
# ready: hovering, a small eager pulse every 24 frames (48 a loop, as Goombella's)
_GESSO["C_1"] = {"end": 48, "loop": True, "keys": [
    (0, _bl(ty=1.2, rz=2, bottom=(0.2, 0.2))),
    (6, _bl(ty=1.0, rz=1, sx=0.9375, sy=1.0625, bottom=(1.4, 0.4), top=(0.2, 0.0))),
    (10, _bl(ty=1.4, rz=3, sx=1.0625, sy=0.9375, bottom=(-1.2, 1.0), top=(-0.1, 0.2)), {"ease": "vector"}),
    (24, _bl(ty=2.0, rz=2, bottom=(-0.2, 0.3))),
    (30, _bl(ty=1.6, rz=1, sx=0.9375, sy=1.0625, bottom=(1.4, 0.4), top=(0.2, 0.0))),
    (34, _bl(ty=2.0, rz=3, sx=1.0625, sy=0.9375, bottom=(-1.2, 1.0), top=(-0.1, 0.2)), {"ease": "vector"}),
    (48, _bl(ty=1.2, rz=2, bottom=(0.2, 0.2))),
]}
# guarding: pulled into a tight ball, braced (40, as Goombella's; held)
_GESSO["G_1"] = {"end": 40, "keys": [
    (0, _bl(ty=0.4, rz=-4, sx=1.0625, sy=0.875, bottom=(2.8, 1.6), top=(0.2, 0.6)), {"ease": "vector"}),
    (40, _bl(ty=0.4, rz=-3, sx=1.0625, sy=0.875, bottom=(2.6, 1.5), top=(0.2, 0.6))),
]}
# Superguard: a hard push straight at the attacker
_GESSO["G_2"] = {"end": 40, "keys": [
    (0, _bl(ty=0.8, tx=-1.2, rz=12, sx=1.125, sy=0.875, bottom=(-2.4, 1.6), top=(-0.3, 0.4)), {"ease": "vector"}),
    (12, _bl(ty=0.8, tx=-0.6, rz=6, sx=1.0625, sy=0.9375, bottom=(-1.2, 0.8))),
    (40, _bl(ty=0.8, rz=2, bottom=(0.0, 0.2))),
]}
# holding up an item: stretches tall, tentacles up and together (10, as Goombella's; held)
_GESSO["I_1"] = {"end": 10, "keys": [
    (0, _bl(ty=0.6, **_FLARE)),
    (6, _bl(ty=2.2, sx=0.9375, sy=1.125, bottom=(1.6, 0.0), top=(0.3, 0.0))),
    (10, _bl(ty=2.0, sx=0.9375, sy=1.0625, bottom=(1.2, 0.0), top=(0.2, 0.0))),
]}
# appeal: a twirl (the paper turns right round) and a flourish of tentacles (56)
_GESSO["O_1"] = {"end": 56, "keys": [
    (0, _bl(ty=0.8, bottom=(0.0, 0.2))),
    (8, _bl(ty=0.4, **_CONTRACT)),
    (14, _bl(ty=2.4, sx=0.0625, sy=1.0625, bottom=(1.0, 0.4)), {"ease": "linear"}),
    (20, _bl(ty=3.4, sx=-1.0, sy=1.0, bottom=(-0.6, 0.6)), {"ease": "linear"}),
    (26, _bl(ty=3.6, sx=-0.0625, sy=1.0, bottom=(-1.0, 0.8)), {"ease": "linear"}),
    (32, _bl(ty=3.0, sx=1.0625, sy=0.9375, bottom=(-2.2, 1.6), top=(-0.2, 0.3))),
    (44, _bl(ty=1.6, rz=-4, sx=1.125, sy=0.875, bottom=(-2.6, 1.6), top=(-0.3, 0.4))),
    (56, _bl(ty=0.8, bottom=(0.0, 0.2))),
]}
# victory: a happy bounce, a pulse a loop (14, as Goombella's)
_GESSO["Y_1"] = {"end": 14, "loop": True, "keys": [
    (0, _bl(ty=0.6, **_CONTRACT)),
    (4, _bl(ty=1.0, sx=1.125, sy=0.875, bottom=(-2.2, 1.6), top=(-0.3, 0.4)), {"ease": "vector"}),
    (9, _bl(ty=3.2, sx=0.9375, sy=1.0625, bottom=(0.4, 0.0))),
    (14, _bl(ty=0.6, **_CONTRACT)),
]}
# Swim (field): Luigi rides on his head; Blooey flattens to carry him and
# paddles with long slow strokes (40 a loop)
_GESSO["F_1"] = {"end": 40, "loop": True, "keys": [
    (0, _bl(ty=0.4, rz=4, sx=1.0625, sy=0.875, bottom=(1.4, 0.6), top=(-0.4, 0.4))),
    (10, _bl(ty=0.0, rz=5, sx=1.125, sy=0.8125, bottom=(-1.8, 1.2), top=(-0.6, 0.6)), {"ease": "vector"}),
    (24, _bl(ty=0.8, rz=4, sx=1.0625, sy=0.875, bottom=(-0.8, 0.8), top=(-0.4, 0.4))),
    (40, _bl(ty=0.4, rz=4, sx=1.0625, sy=0.875, bottom=(1.4, 0.6), top=(-0.4, 0.4))),
]}

# -- moves --
# Torpedo, the tackle: winds up (A_1A, loop), shoots head first (A_1B, loop),
# bounces off the foe and rights himself (A_1C)
_GESSO["A_1A"] = {"end": 24, "loop": True, "keys": [
    (0, _bl(ty=1.2, tx=1.0, rz=-8, sx=0.875, sy=1.125, bottom=(2.6, 0.8), top=(0.4, 0.0))),
    (3, _bl(ty=1.2, tx=1.2, rz=-9, sx=0.875, sy=1.125, bottom=(2.8, 0.9), top=(0.4, 0.0))),
    (6, _bl(ty=1.2, tx=1.0, rz=-7, sx=0.875, sy=1.125, bottom=(2.6, 0.8), top=(0.4, 0.0))),
    (12, _bl(ty=1.4, tx=1.4, rz=-10, sx=0.875, sy=1.1875, bottom=(3.0, 0.9), top=(0.5, 0.0))),
    (15, _bl(ty=1.4, tx=1.6, rz=-11, sx=0.875, sy=1.1875, bottom=(3.2, 1.0), top=(0.5, 0.0))),
    (18, _bl(ty=1.4, tx=1.4, rz=-9, sx=0.875, sy=1.1875, bottom=(3.0, 0.9), top=(0.5, 0.0))),
    (24, _bl(ty=1.2, tx=1.0, rz=-8, sx=0.875, sy=1.125, bottom=(2.6, 0.8), top=(0.4, 0.0))),
]}
_GESSO["A_1B"] = {"end": 8, "loop": True, "keys": [
    (0, _bl(ty=9.0, tx=-3.0, rz=90, sx=0.875, sy=1.1875, bottom=(3.0, -0.8), top=(0.6, 0.0))),
    (4, _bl(ty=9.0, tx=-3.0, rz=90, sx=0.875, sy=1.1875, bottom=(2.4, -1.2), top=(0.6, 0.0), skew=0.6)),
    (8, _bl(ty=9.0, tx=-3.0, rz=90, sx=0.875, sy=1.1875, bottom=(3.0, -0.8), top=(0.6, 0.0))),
]}
_GESSO["A_1C"] = {"end": 24, "keys": [
    (0, _bl(ty=9.0, tx=-3.0, rz=90, sx=1.125, sy=0.8125, bottom=(-1.0, 1.0), top=(-0.4, 0.6)), {"ease": "vector"}),
    (8, _bl(ty=6.0, tx=2.0, rz=-40, sx=1.0625, sy=0.9375, bottom=(-2.4, 1.4), top=(-0.3, 0.3))),
    (16, _bl(ty=1.6, tx=2.4, rz=-8, **_SPREAD)),
    (24, _bl(ty=0.8, tx=2.4, rz=0, bottom=(0.0, 0.2))),
]}
# Ink Squirt: draws in a breath, head swelling (A_2A, held), then squirts,
# recoiling back (A_2B)
_GESSO["A_2A"] = {"end": 30, "keys": [
    (0, _bl(ty=0.8, bottom=(0.0, 0.2))),
    (20, _bl(ty=1.6, rz=-6, sx=1.125, sy=1.0625, bottom=(1.6, 0.4), top=(-1.2, -0.6))),
    (30, _bl(ty=1.7, rz=-7, sx=1.125, sy=1.125, bottom=(1.8, 0.4), top=(-1.4, -0.7))),
]}
_GESSO["A_2B"] = {"end": 24, "keys": [
    (0, _bl(ty=1.7, rz=-7, sx=1.125, sy=1.125, bottom=(1.8, 0.4), top=(-1.4, -0.7))),
    (2, _bl(ty=1.4, tx=1.6, rz=14, sx=0.875, sy=0.9375, bottom=(-1.4, 1.0), top=(1.0, 0.4)), {"ease": "vector"}),
    (6, _bl(ty=1.4, tx=2.0, rz=10, sx=0.9375, sy=0.9375, bottom=(-1.0, 0.8), top=(0.8, 0.3))),
    (14, _bl(ty=1.0, tx=1.6, rz=2, bottom=(-0.4, 0.4))),
    (24, _bl(ty=0.8, tx=1.6, bottom=(0.0, 0.2))),
]}
# Tentacle Hold: lunges, tentacles flung wide (A_3A), wraps and squeezes
# (A_3B, loop), lets go and springs back (A_3C)
_GESSO["A_3A"] = {"end": 16, "keys": [
    (0, _bl(ty=0.8, **_CONTRACT)),
    (6, _bl(ty=1.4, tx=1.0, rz=-6, sx=0.875, sy=1.125, bottom=(2.6, 0.6), top=(0.4, 0.0))),
    (11, _bl(ty=2.0, tx=-4.0, rz=18, sx=1.125, sy=0.875, bottom=(-3.0, 0.4), top=(-0.3, 0.4), skew=1.0),
     {"ease": "vector"}),
    (16, _bl(ty=1.8, tx=-4.4, rz=14, sx=1.125, sy=0.875, bottom=(-3.2, 0.2), top=(-0.3, 0.4), skew=1.2)),
]}
_GESSO["A_3B"] = {"end": 20, "loop": True, "keys": [
    (0, _bl(ty=1.8, tx=-4.4, rz=10, sx=1.0, sy=1.0625, bottom=(2.4, -0.6), top=(0.2, 0.0))),
    (10, _bl(ty=1.6, tx=-4.2, rz=6, sx=1.0625, sy=0.9375, bottom=(1.2, -0.2), top=(-0.2, 0.2))),
    (20, _bl(ty=1.8, tx=-4.4, rz=10, sx=1.0, sy=1.0625, bottom=(2.4, -0.6), top=(0.2, 0.0))),
]}
_GESSO["A_3C"] = {"end": 16, "keys": [
    (0, _bl(ty=1.8, tx=-4.4, rz=10, sx=1.0, sy=1.0625, bottom=(2.4, -0.6), top=(0.2, 0.0))),
    (5, _bl(ty=2.4, tx=-1.0, rz=-10, sx=1.125, sy=0.875, bottom=(-2.4, 1.6), top=(-0.3, 0.4)), {"ease": "vector"}),
    (16, _bl(ty=0.8, rz=0, bottom=(0.0, 0.2))),
]}

# -- story (docs/LUIGI_ANIMS.md, "Story poses") --
# diving into a well (F_2, 30, held: flipped head down, streamlined, sinking
# out of sight) and surfacing (F_3, 30: shooting up out of it, a flourish)
_GESSO["F_2"] = {"end": 30, "keys": [
    (0, _bl(ty=0.8, **_CONTRACT)),
    (6, _bl(ty=3.0, rz=-30, sx=1.0625, sy=0.9375, bottom=(-2.0, 1.4), top=(-0.2, 0.3)), {"ease": "vector"}),
    (14, _bl(ty=12.0, rz=-180, sx=0.875, sy=1.1875, bottom=(3.0, -0.8), top=(0.6, 0.0))),
    (22, _bl(ty=-2.0, rz=-180, sx=0.875, sy=1.1875, bottom=(3.2, -0.9), top=(0.6, 0.0))),
    (30, _bl(ty=-14.0, rz=-180, sx=0.875, sy=1.1875, bottom=(3.2, -0.9), top=(0.6, 0.0))),
]}
_GESSO["F_3"] = {"end": 30, "keys": [
    (0, _bl(ty=-16.0, sx=0.875, sy=1.1875, bottom=(3.0, -0.8), top=(0.6, 0.0))),
    (8, _bl(ty=6.0, sx=0.875, sy=1.1875, bottom=(2.8, -0.8), top=(0.5, 0.0)), {"ease": "linear"}),
    (12, _bl(ty=7.0, sx=1.125, sy=0.875, bottom=(-2.6, 1.6), top=(-0.3, 0.4)), {"ease": "vector"}),
    (20, _bl(ty=2.4, **_SPREAD)),
    (30, _bl(ty=0.8, bottom=(0.0, 0.2))),
]}

# burnt Blooey (chapter 6): every one of these drawn with the burnt texture
# (the disc's own, brown: BURN_*) and, as the plan has him, soot black with
# embers and a wisp of smoke curling up off his head (SOOT_*: the drawing and
# the smoke card the port makes, friend_extras.py)
_BASE = list(_GESSO)
for _n in _BASE:
    _GESSO["BURN_" + _n] = burnt(_GESSO[_n])
for _n in _BASE:
    _GESSO["SOOT_" + _n] = soot(_GESSO[_n])
# and his still, stand and talk, soot black (the disc's are his plain and brown
# ones: these are ours, the stand bobbing and the talk rocking as theirs do)
_GESSO["SOOT_Z_1"] = soot({"end": 0, "keys": [(0, _bl())]})
_GESSO["SOOT_S_1"] = soot({"end": 70, "loop": True, "keys": [
    (0, _bl()), (20, _bl(ty=0.75)), (42, _bl()), (44, _bl(ty=0.375)), (46, _bl()), (48, _bl(ty=0.1875)),
    (50, _bl()), (70, _bl())]})
_GESSO["SOOT_T_1"] = soot({"end": 14, "loop": True, "keys": [
    (0, _bl()), (4, _bl(rz=1.5)), (9, _bl(rz=-1.5)), (14, _bl())]})
FRIENDS["c_lp_gesso"] = _GESSO


# ------------------------------------------------------------ the helpers ----

def _pose(*parts, **groups):
    """a key pose: dicts merged in order, then group=dict keywords (a later
    channel replaces an earlier one)"""
    out = {}
    for p in parts + (groups,):
        for g, ch in p.items():
            out.setdefault(g, {}).update(ch)
    return out


def _gait(end, feet, lift=1.2, reach=0.8, body=None, bob=0.5, dip=0.2, rock=4.0, lean=0.0, sway=None,
          foot_rz=10.0, extra=None):
    """two feet stepping in turn, `end` frames for both steps, keyed every eighth:
    each foot planted behind (+reach), lifted (`lift`, toes up) on the way, lands
    ahead (-reach; a sharp key), then slides back under the body; the body bobs
    up while a foot is in the air, dips as it lands, and rocks toward the
    planted foot. extra(frame, step, which) adds to a key."""
    keys = []
    for k in range(9):
        f = int(round(k * end / 8.0))
        pose = {}
        for i, names in enumerate(feet):
            t = ((k / 8.0) - 0.5 * i) % 1.0
            if t < 0.25:
                u = t / 0.25
                ch = {"tx": reach * (1 - 2 * u * 0.6), "ty": lift * u, "rz": foot_rz * u}
            elif t < 0.5:
                u = (t - 0.25) / 0.25
                ch = {"tx": reach * (0.4 - 1.4 * u), "ty": lift * (1 - u), "rz": foot_rz * (1 - u)}
            else:
                u = (t - 0.5) / 0.5
                ch = {"tx": -reach + 2 * reach * u, "ty": 0.0, "rz": 0.0}
            for g in (names if isinstance(names, tuple) else (names,)):  # a pair steps together
                pose[g] = dict(ch)
        step = k % 4  # 0 push off, 1 lifted, 2 the footfall, 3 settling
        which = (k // 4) % 2  # 0: the first foot is stepping
        if body:
            side = 1 if which == 0 else -1
            pose[body] = {"ty": {0: 0.0, 1: bob, 2: -dip, 3: bob * 0.3}[step],
                          "rz": lean + side * rock * {0: 0.3, 1: 1.0, 2: 0.6, 3: 0.4}[step]}
            if step == 2:
                pose[body]["sy"] = 0.9375
        if sway:
            pose = _pose(pose, sway(k, step, which))
        if extra:
            pose = _pose(pose, extra(f, step, which))
        keys.append((f, pose, {"ease": "vector"} if step == 2 else {}))
    return {"end": end, "loop": True, "keys": keys}


def _loop(end, *frames):
    """a loop from (frame, pose[, opts]) keys, the first pose again at the end"""
    keys = list(frames)
    if keys[-1][0] != end:
        keys.append((end,) + tuple(keys[0][1:]))
    return {"end": end, "loop": True, "keys": keys}


def _once(end, *frames):
    return {"end": end, "keys": list(frames)}


def _shake(end, a, b):
    """a loop between two poses, a at its ends"""
    return _loop(end, (0, a), (end // 2, b))


# ------------------------------------------------------------------- Jerry ----
# A Bob-omb waddles: short steps on his little feet, the round body rocking from
# side to side over the planted foot, the fuse lagging behind every bounce.
# Parts: locator2 the whole (pivot at his feet), pPlane2 the body (pivot low at
# its back), pPlane3 the fuse (the body's child, pivot at its root), locator4
# the front foot, locator5 the back foot.

_B, _F, _FF, _BF, _W = "pPlane2", "pPlane3", "locator4", "locator5", "locator2"


def _fuse_lag(amount):
    """the fuse swings behind the body's bounce"""
    return lambda f, step, which: {_F: {"rz": {0: -0.4, 1: -1.0, 2: 0.8, 3: 0.5}[step] * amount}}


def _jb(body=None, fuse=None, ff=None, bf=None, whole=None):
    out = {}
    for g, ch in ((_B, body), (_F, fuse), (_FF, ff), (_BF, bf), (_W, whole)):
        if ch:
            out[g] = dict(ch)
    return out


_BOM = {}
_BOM["W_1"] = _gait(40, [_FF, _BF], lift=1.1, reach=0.8, body=_B, bob=0.5, rock=5.0, extra=_fuse_lag(10.0))
_BOM["R_1"] = _gait(20, [_FF, _BF], lift=1.6, reach=1.3, body=_B, bob=0.8, rock=4.0, lean=7.0,
                    extra=_fuse_lag(16.0))
_BOM["J_1A"] = _once(8,
    (0, _jb(body={"sy": 0.875, "sx": 1.0625, "ty": -0.3}, fuse={"rz": 10}, ff={"tx": -0.3}, bf={"tx": 0.3})),
    (4, _jb(body={"sy": 1.125, "sx": 0.9375, "ty": 0.6}, fuse={"rz": -22},
            ff={"ty": -0.6, "rz": -12}, bf={"ty": -0.6, "rz": -8})),
    (8, _jb(body={"sy": 1.0625, "sx": 0.9375, "ty": 0.6}, fuse={"rz": -18},
            ff={"ty": -0.6, "rz": -12}, bf={"ty": -0.6, "rz": -8})))
_BOM["J_1B"] = _loop(16,
    (0, _jb(body={"sy": 0.9375, "sx": 1.0625, "ty": 0.4}, fuse={"rz": 14},
            ff={"tx": -0.9, "ty": 0.3, "rz": 14}, bf={"tx": 0.9, "ty": 0.2, "rz": -8})),
    (8, _jb(body={"sy": 0.9375, "sx": 1.0625, "ty": 0.5}, fuse={"rz": 20},
            ff={"tx": -1.0, "ty": 0.4, "rz": 16}, bf={"tx": 1.0, "ty": 0.3, "rz": -10})))
_BOM["D_1"] = _shake(8,
    _jb(body={"rz": -10, "sx": 1.125, "sy": 0.875}, fuse={"rz": 24}, whole={"tx": 0.5}, ff={"rz": -8}),
    _jb(body={"rz": 6, "sx": 0.9375, "sy": 1.0625}, fuse={"rz": -18}, whole={"tx": -0.3}, ff={"rz": 6}))
_TIRED = _jb(body={"rz": -7, "ty": -0.4, "sy": 0.9375}, fuse={"rz": 34})
_BOM["D_2"] = _loop(92, (0, _TIRED), (46, _pose(_TIRED, _jb(body={"rz": -5, "ty": -0.1, "sy": 1.0}, fuse={"rz": 28}))))
_BOM["W_2"] = _gait(40, [_FF, _BF], lift=0.6, reach=0.6, body=_B, bob=0.3, rock=3.0, lean=-6.0,
                    extra=lambda f, s, w: {_F: {"rz": 32 + {0: 0, 1: -4, 2: 6, 3: 3}[s]}})
_BOM["R_2"] = _gait(20, [_FF, _BF], lift=0.9, reach=1.0, body=_B, bob=0.5, rock=3.0, lean=2.0,
                    extra=lambda f, s, w: {_F: {"rz": 30 + {0: 0, 1: -6, 2: 8, 3: 4}[s]}})
# KO: tips over onto his back, feet up, the fuse limp (16, held)
_BOM["D_3"] = _once(16,
    (0, _jb(body={"rz": 6, "sy": 0.9375}, fuse={"rz": -10})),
    (6, _jb(whole={"rz": -30, "ty": 1.5}, body={"sy": 1.0625}, fuse={"rz": 20}, ff={"ty": 0.4, "rz": -10})),
    (11, _jb(whole={"rz": -92, "ty": 2.0, "tx": 1.0}, body={"sx": 1.0625, "sy": 0.9375}, fuse={"rz": 50},
             ff={"rz": -20}, bf={"rz": -10}), {"ease": "vector"}),
    (13, _jb(whole={"rz": -86, "ty": 2.6, "tx": 1.0}, fuse={"rz": 40}, ff={"rz": -24}, bf={"rz": -14})),
    (16, _jb(whole={"rz": -90, "ty": 2.0, "tx": 1.0}, fuse={"rz": 46}, ff={"rz": -20}, bf={"rz": -10})))
_BOM["D_4"] = _once(20,
    (0, _jb(body={"rz": -8}, fuse={"rz": 20})),
    (6, _jb(whole={"rz": -120, "ty": 12.0, "tx": 1.5}, fuse={"rz": 30})),
    (12, _jb(whole={"rz": -250, "ty": 12.0, "tx": 3.0}, fuse={"rz": 30})),
    (16, _jb(whole={"rz": -350, "ty": 0.0, "tx": 3.5}, body={"sy": 0.875, "sx": 1.0625}, fuse={"rz": -10}),
     {"ease": "vector"}),
    (20, _jb(whole={"rz": -360, "tx": 3.5}, fuse={"rz": 6})))
_BOM["D_5"] = _once(0, (0, _jb(body={"rz": 5, "ty": 0.3}, fuse={"rz": -12}, ff={"tx": -0.7, "ty": 0.8, "rz": 8})))
_BOM["N_1"] = _loop(120,
    (0, _jb(body={"rz": -3, "sx": 1.0625, "sy": 0.9375}, fuse={"rz": 30})),
    (60, _jb(body={"rz": -2, "sx": 1.0, "sy": 1.0}, fuse={"rz": 24})))
_BOM["X_1"] = _loop(48,
    (0, _jb(body={"rz": 10}, fuse={"rz": -20}, whole={"tx": 0.0})),
    (12, _jb(body={"rz": 0, "ty": 0.3}, fuse={"rz": 10}, whole={"tx": -0.5})),
    (24, _jb(body={"rz": -10}, fuse={"rz": 26}, whole={"tx": 0.0})),
    (36, _jb(body={"rz": 0, "ty": 0.3}, fuse={"rz": -6}, whole={"tx": 0.5})))
_BOM["Q_1"] = _shake(8,
    _jb(body={"sy": 0.9375, "ty": -0.2, "rz": -4}, fuse={"rz": -8}, whole={"tx": 0.25}, ff={"tx": 0.4}),
    _jb(body={"sy": 0.9375, "ty": -0.2, "rz": -2}, fuse={"rz": -4}, whole={"tx": -0.25}, ff={"tx": 0.4}))
_BOM["C_1"] = _loop(48,
    (0, _jb(body={"sy": 0.9375, "sx": 1.0625}, fuse={"rz": 8})),
    (6, _jb(body={"ty": 0.6, "sy": 1.0625}, fuse={"rz": -10})),
    (12, _jb(body={"ty": 0.2}, fuse={"rz": 6})),
    (24, _jb(body={"sy": 0.9375, "sx": 1.0625}, fuse={"rz": 8})),
    (30, _jb(body={"ty": 0.6, "sy": 1.0625}, fuse={"rz": -10})),
    (36, _jb(body={"ty": 0.2}, fuse={"rz": 6})))
_GUARD = _jb(body={"ty": -0.5, "sy": 0.875, "sx": 1.0625, "rz": 8}, fuse={"rz": 16}, ff={"tx": -0.5}, bf={"tx": 0.6})
_BOM["G_1"] = _once(40, (0, _GUARD, {"ease": "vector"}), (40, _pose(_GUARD, _jb(body={"rz": 7}))))
_BOM["G_2"] = _once(40,
    (0, _jb(body={"rz": 16, "sx": 1.125, "sy": 0.875}, fuse={"rz": 28}, whole={"tx": -1.2}), {"ease": "vector"}),
    (10, _jb(body={"rz": 6}, fuse={"rz": -10}, whole={"tx": -0.6})),
    (40, {}))
_BOM["I_1"] = _once(10,
    (0, _jb(body={"sy": 0.9375})),
    (6, _jb(body={"sy": 1.125, "sx": 0.9375, "ty": 0.8}, fuse={"rz": -14}, whole={"ty": 0.6})),
    (10, _jb(body={"sy": 1.0625, "ty": 0.8}, fuse={"rz": -8}, whole={"ty": 0.6})))
_BOM["O_1"] = _once(56,
    (0, {}),
    (8, _jb(body={"sy": 0.875, "sx": 1.0625}, fuse={"rz": 10})),
    (14, _jb(whole={"ty": 2.5, "sx": 0.0625}, body={"sy": 1.0625}, fuse={"rz": -14}), {"ease": "linear"}),
    (20, _jb(whole={"ty": 3.6, "sx": -1.0}, fuse={"rz": -18}), {"ease": "linear"}),
    (26, _jb(whole={"ty": 3.0, "sx": -0.0625}, fuse={"rz": -10}), {"ease": "linear"}),
    (32, _jb(whole={"ty": 0.0, "sx": 1.0}, body={"sy": 0.875, "sx": 1.125}, fuse={"rz": 22}), {"ease": "vector"}),
    (40, _jb(body={"sy": 1.0625, "rz": -6}, fuse={"rz": -8})),
    (56, {}))
_BOM["Y_1"] = _loop(14,
    (0, _jb(body={"sy": 0.875, "sx": 1.125}, fuse={"rz": 12})),
    (6, _jb(whole={"ty": 2.6}, body={"sy": 1.125, "sx": 0.9375}, fuse={"rz": -16}, ff={"ty": -0.4}, bf={"ty": -0.4})),
    (11, _jb(whole={"ty": 0.4}, body={"sy": 1.0}, fuse={"rz": -4})))
# Big Bang / Blast: he swells, shaking, and bursts (field F_1: the whole of it)
_SWELL_A = _jb(body={"sx": 1.375, "sy": 1.3125, "rz": 7}, fuse={"rz": -30}, whole={"tx": 0.7},
               ff={"tx": -0.6}, bf={"tx": 0.6})
_SWELL_B = _jb(body={"sx": 1.4375, "sy": 1.5, "rz": -7}, fuse={"rz": 28}, whole={"tx": -0.7, "ty": 0.4},
               ff={"tx": -0.7}, bf={"tx": 0.7})
_BURST = [
    (0, _SWELL_B),
    (4, _jb(body={"sx": 1.875, "sy": 1.875}, fuse={"rz": -45}, whole={"ty": 1.2}), {"ease": "vector"}),
    (8, _jb(body={"sx": 1.5, "sy": 0.5}, fuse={"rz": 45}, ff={"tx": -1.0}, bf={"tx": 1.0}), {"ease": "vector"}),
    (14, _jb(body={"sx": 0.875, "sy": 1.25}, fuse={"rz": -20}, whole={"ty": 1.6})),
    (22, _jb(body={"sx": 1.125, "sy": 0.875}, fuse={"rz": 10})),
    (30, {}),
]
_BOM["A_2A"] = _shake(8, _SWELL_A, _SWELL_B)
_BOM["A_2B"] = _once(30, *_BURST)
_BOM["F_1"] = _once(48, (0, {}), (6, _jb(body={"sy": 0.875, "sx": 1.0625}, fuse={"rz": 12})),
                    (12, _SWELL_A), (14, _SWELL_B), (16, _SWELL_A),
                    *[(k[0] + 18,) + tuple(k[1:]) for k in _BURST[1:]])
# Lit Fuse: rears back, fuse fizzing (A_1A, loop), then a hop forward that
# flings the bomb (A_1B)
# (The first cut leaned 12 degrees and hopped 1.4: at the battle camera's distance
# he seemed to stand still through the whole move, seen in Bulkhead's fight with
# dense captures. His moves now read from the stands: a deep lean and squash, the
# fuse whipping, a real hop, and back on his mark by the end.)
_REARB = _jb(body={"rz": -26, "sy": 0.875, "sx": 1.125, "ty": -0.3}, fuse={"rz": 40},
             ff={"tx": -1.2, "ty": 1.0, "rz": 18}, bf={"tx": 0.8}, whole={"tx": 1.6})
_BOM["A_1A"] = _shake(8, _REARB, _pose(_REARB, _jb(fuse={"rz": 58}, body={"rz": -30, "sy": 0.8125, "sx": 1.1875}),
                                       {_W: {"tx": 1.9}}))
_BOM["A_1B"] = _once(32,
    (0, _REARB),
    (3, _jb(body={"rz": 30, "sx": 0.875, "sy": 1.1875}, fuse={"rz": -40}, whole={"tx": -2.4, "ty": 3.0},
            ff={"tx": -1.4, "ty": -0.4}, bf={"tx": 1.0, "ty": 1.2, "rz": -18}), {"ease": "vector"}),
    (8, _jb(body={"rz": 22, "sx": 0.9375, "sy": 1.125}, fuse={"rz": -10}, whole={"tx": -3.6, "ty": 4.0},
            ff={"tx": -1.0}, bf={"ty": 1.0, "rz": -12})),
    (13, _jb(body={"rz": 6, "sx": 1.25, "sy": 0.75, "ty": -0.4}, fuse={"rz": 30}, whole={"tx": -4.0},
             ff={"tx": -0.6}, bf={"tx": 0.6}), {"ease": "vector"}),
    (20, _jb(body={"rz": -4, "sx": 0.9375, "sy": 1.0625}, fuse={"rz": -12}, whole={"tx": -3.0})),
    (32, {}))
# Short Fuse: hunkers down and winds himself up, the key turning, the fuse
# trembling (A_3A, loop); springs up charged (A_3B)
_CROUCH = _jb(body={"ty": -0.8, "sy": 0.75, "sx": 1.1875, "rz": 6}, fuse={"rz": -6}, ff={"tx": -0.5}, bf={"tx": 0.5})
_BOM["A_3A"] = _loop(16,
    (0, _pose(_CROUCH, {"locator3": {"vis": 1}, "pPlane4": {"sx": 1.0}})),
    (4, _pose(_CROUCH, _jb(fuse={"rz": 24}, body={"rz": 2}, whole={"tx": 0.5}),
              {"locator3": {"vis": 1}, "pPlane4": {"sx": 0.0625}}), {"ease": "linear"}),
    (8, _pose(_CROUCH, _jb(fuse={"rz": -20}, body={"rz": 9, "sy": 0.6875}), {"locator3": {"vis": 1}, "pPlane4": {"sx": -1.0}})),
    (12, _pose(_CROUCH, _jb(fuse={"rz": 24}, body={"rz": 2}, whole={"tx": -0.5}),
               {"locator3": {"vis": 1}, "pPlane4": {"sx": -0.0625}}), {"ease": "linear"}))
_BOM["A_3B"] = _once(26,
    (0, _CROUCH),
    (5, _jb(body={"sy": 1.375, "sx": 0.8125}, fuse={"rz": -36}, whole={"ty": 4.5}), {"ease": "vector"}),
    (10, _jb(body={"sy": 1.125, "sx": 0.9375}, fuse={"rz": 20}, whole={"ty": 5.0})),
    (15, _jb(body={"sy": 0.75, "sx": 1.25}, fuse={"rz": 28}), {"ease": "vector"}),
    (26, {}))
# -- story --
# the fuse lit, hurrying (L_1, a 36-frame loop, as Bobbery's): quick waddling
# steps, leaning in, the fuse fizzing and whipping
_BOM["L_1"] = _gait(36, [_FF, _BF], lift=1.4, reach=1.1, body=_B, bob=0.7, rock=4.0, lean=8.0,
                    extra=lambda f, s, w: {_F: {"rz": {0: -14, 1: 10, 2: -18, 3: 8}[s]}})
FRIENDS["c_lp_bom"] = _BOM


# ------------------------------------------------------------------ Torque ----
# A Buzzy Beetle scuttles: four short legs in diagonal pairs, quick and busy,
# the shell riding level over them, the head bobbing out front with each step
# and the wrench on his shell wobbling behind. Parts: locator2 the whole,
# pPlane8 the body (the legs' and shell's parent), pPlane9 the shell (pPlane10
# the wrench, its child), pPlane3 the head (pPlane2 the eye), the legs
# pPlane11 pPlane13 (front, near and far: group1) and pPlane12 pPlane14 (back:
# group2).

_MB, _MS, _MWR, _MH, _MW = "pPlane8", "pPlane9", "pPlane10", "pPlane3", "locator2"
_LEGS_A = ("pPlane11", "pPlane14")  # a diagonal pair: front near, back far
_LEGS_B = ("pPlane13", "pPlane12")


def _tq(body=None, shell=None, wrench=None, head=None, whole=None, legs=None, **more):
    out = {}
    for g, ch in ((_MB, body), (_MS, shell), (_MWR, wrench), (_MH, head), (_MW, whole)):
        if ch:
            out[g] = dict(ch)
    if legs:
        for g in _LEGS_A + _LEGS_B:
            out[g] = dict(legs)
    for g, ch in more.items():
        out[g] = dict(ch)
    return out


def _tqp(base, **kw):
    """a pose: base with _tq's channels on top"""
    return _pose(base, _tq(**kw))


def _scuttle(end, lift=0.8, reach=0.6, bob=0.3, lean=0.0, head_bob=0.3, wrench=6.0, droop=0.0):
    """four legs in diagonal pairs (keyed every eighth of `end`), the head
    bobbing and the wrench wobbling with each step"""
    def extra(f, step, which):
        return _tq(head={"tx": {0: 0.0, 1: -head_bob, 2: head_bob * 0.4, 3: 0.0}[step],
                         "rz": droop + {0: 0.0, 1: -2.0, 2: 2.0, 3: 0.5}[step]},
                   wrench={"rz": droop * 2 + wrench * {0: -0.3, 1: -1.0, 2: 1.0, 3: 0.4}[step]},
                   shell={"ty": {0: 0.0, 1: 0.1, 2: -0.1, 3: 0.0}[step]})
    return _gait(end, [_LEGS_A, _LEGS_B], lift=lift, reach=reach, body=_MB, bob=bob, dip=0.1, rock=1.0,
                 lean=lean, foot_rz=14.0, extra=extra)


_MET = {}
_MET["W_1"] = _scuttle(20)
_MET["R_1"] = _scuttle(16, lift=1.0, reach=0.9, bob=0.4, lean=4.0, head_bob=0.5, wrench=10.0)
_MET["J_1A"] = _once(8,
    (0, _tq(body={"ty": -0.3, "sy": 0.9375}, head={"tx": 0.3}, legs={"ty": 0.2})),
    (4, _tq(body={"ty": 0.4}, head={"rz": -8.0, "tx": -0.3}, wrench={"rz": -16.0}, legs={"ty": -0.6, "rz": -12.0})),
    (8, _tq(body={"ty": 0.4}, head={"rz": -6.0, "tx": -0.3}, wrench={"rz": -12.0}, legs={"ty": -0.6, "rz": -12.0})))
_MET["J_1B"] = _loop(16,
    (0, _tq(head={"rz": 6.0}, wrench={"rz": 12.0}, pPlane11={"tx": -0.7, "rz": 10}, pPlane13={"tx": -0.7, "rz": 10},
            pPlane12={"tx": 0.7, "rz": -10}, pPlane14={"tx": 0.7, "rz": -10})),
    (8, _tq(head={"rz": 8.0}, wrench={"rz": 16.0}, pPlane11={"tx": -0.9, "rz": 14}, pPlane13={"tx": -0.9, "rz": 14},
            pPlane12={"tx": 0.9, "rz": -14}, pPlane14={"tx": 0.9, "rz": -14})))
_MET["D_1"] = _shake(8,
    _tq(whole={"tx": 0.5}, shell={"rz": -6.0}, head={"tx": 0.8, "rz": -8.0}, wrench={"rz": 20.0},
        legs={"rz": 10.0}),
    _tq(whole={"tx": -0.3}, shell={"rz": 4.0}, head={"tx": 0.4, "rz": 4.0}, wrench={"rz": -14.0}, legs={"rz": -8.0}))
_SAG = _tq(body={"ty": -0.3}, head={"rz": 12.0, "ty": -0.4}, wrench={"rz": 24.0}, legs={"ty": 0.1})
_MET["D_2"] = _loop(92, (0, _SAG), (46, _pose(_SAG, _tq(head={"rz": 9.0, "ty": -0.2}, wrench={"rz": 20.0}))))
_MET["W_2"] = _scuttle(24, lift=0.5, reach=0.4, bob=0.2, head_bob=0.2, wrench=4.0, droop=10.0)
_MET["R_2"] = _scuttle(16, lift=0.7, reach=0.6, bob=0.3, lean=2.0, head_bob=0.3, wrench=6.0, droop=8.0)
# KO: flipped onto his back, legs up, a last kick (16, held)
_FLIP = _tq(whole={"rz": 180.0, "ty": 10.4}, head={"rz": 10.0}, legs={"rz": 20.0}, wrench={"rz": 100.0, "ty": -1.0})
_MET["D_3"] = _once(16,
    (0, _tq(body={"sy": 0.9375}, head={"tx": 0.6})),
    (6, _tq(whole={"rz": 100.0, "ty": 9.0, "tx": 0.5}, head={"rz": 6.0}, legs={"rz": 14.0})),
    (11, _FLIP, {"ease": "vector"}),
    (13, _pose(_FLIP, _tq(whole={"rz": 180.0, "ty": 11.0}, legs={"rz": 30.0}))),
    (16, _pose(_FLIP, _tq(legs={"rz": 24.0}))))
_MET["D_4"] = _once(20,
    (0, _tq(head={"tx": 0.6}, shell={"rz": -4.0})),
    (6, _tq(whole={"rz": -120.0, "ty": 10.0, "tx": 1.5}, legs={"rz": -10.0})),
    (12, _tq(whole={"rz": -250.0, "ty": 10.0, "tx": 3.0}, legs={"rz": -10.0})),
    (16, _tq(whole={"rz": -350.0, "tx": 3.5}, body={"sy": 0.9375}), {"ease": "vector"}),
    (20, _tq(whole={"rz": -360.0, "tx": 3.5})))
_MET["D_5"] = _once(0, (0, _tq(head={"tx": -0.3, "rz": -4.0}, wrench={"rz": -10.0}, pPlane11={"ty": 0.8, "tx": -0.4},
                               pPlane14={"ty": 0.8, "tx": -0.4})))
_TUCK = _tq(body={"ty": -0.4}, head={"tx": 1.8, "ty": -0.3}, legs={"ty": 0.5})
_MET["N_1"] = _loop(120, (0, _tqp(_TUCK, shell={"sy": 1.0}, wrench={"rz": 10.0})),
                    (60, _tqp(_TUCK, shell={"sy": 1.0625}, wrench={"rz": 6.0})))
_MET["X_1"] = _loop(48,
    (0, _tq(head={"rz": 10.0}, wrench={"rz": -30.0}, whole={"tx": 0.0})),
    (12, _tq(head={"rz": 0.0, "tx": -0.3}, wrench={"rz": 0.0}, whole={"tx": -0.4})),
    (24, _tq(head={"rz": -10.0}, wrench={"rz": 30.0}, whole={"tx": 0.0})),
    (36, _tq(head={"rz": 0.0, "tx": 0.3}, wrench={"rz": 0.0}, whole={"tx": 0.4})))
_MET["Q_1"] = _shake(8, _tqp(_TUCK, whole={"tx": 0.25}, wrench={"rz": 4.0}),
                     _tqp(_TUCK, whole={"tx": -0.25}, wrench={"rz": -4.0}))
_MET["C_1"] = _loop(48,
    (0, _tq(head={"tx": 0.0}, pPlane11={"ty": 0.0}, pPlane12={"ty": 0.0})),
    (6, _tq(head={"tx": -0.4, "rz": -3.0}, wrench={"rz": -8.0}, pPlane11={"ty": 0.6}, pPlane14={"ty": 0.6})),
    (12, _tq(head={"tx": 0.0}, wrench={"rz": 4.0})),
    (24, _tq(head={"tx": 0.0})),
    (30, _tq(head={"tx": -0.4, "rz": -3.0}, wrench={"rz": -8.0}, pPlane13={"ty": 0.6}, pPlane12={"ty": 0.6})),
    (36, _tq(head={"tx": 0.0}, wrench={"rz": 4.0})))
_GUARDT = _tq(body={"ty": -0.6}, head={"tx": 1.8, "ty": -0.4}, legs={"ty": 0.6}, shell={"sy": 1.0625})
_MET["G_1"] = _once(40, (0, _GUARDT, {"ease": "vector"}), (40, _GUARDT))
_MET["G_2"] = _once(40,
    (0, _tq(whole={"tx": -1.4}, shell={"rz": 10.0}, head={"tx": -0.6}, wrench={"rz": -20.0}), {"ease": "vector"}),
    (10, _tq(whole={"tx": -0.7}, shell={"rz": 4.0}, wrench={"rz": 8.0})),
    (40, {}))
_MET["I_1"] = _once(10,
    (0, {}),
    (6, _tq(body={"rz": -10.0, "ty": 0.4}, head={"rz": -14.0}, pPlane11={"ty": 1.4, "rz": -20}, pPlane13={"ty": 1.4, "rz": -20})),
    (10, _tq(body={"rz": -9.0, "ty": 0.4}, head={"rz": -12.0}, pPlane11={"ty": 1.4, "rz": -20},
             pPlane13={"ty": 1.4, "rz": -20})))
_MET["O_1"] = _once(56,
    (0, {}),
    (8, _tq(body={"ty": -0.3, "sy": 0.9375})),
    (14, _tq(whole={"ty": 2.5, "sx": 0.0625}, legs={"ty": -0.4}), {"ease": "linear"}),
    (20, _tq(whole={"ty": 3.4, "sx": -1.0}, legs={"ty": -0.4}), {"ease": "linear"}),
    (26, _tq(whole={"ty": 2.8, "sx": -0.0625}), {"ease": "linear"}),
    (32, _tq(whole={"sx": 1.0}, body={"sy": 0.9375}, wrench={"rz": 20.0}), {"ease": "vector"}),
    (40, _tq(head={"rz": -8.0, "tx": -0.4}, wrench={"rz": -10.0})),
    (56, {}))
_MET["Y_1"] = _loop(14,
    (0, _tq(body={"sy": 0.9375, "ty": -0.2}, wrench={"rz": 10.0})),
    (6, _tq(whole={"ty": 2.4}, head={"rz": -6.0}, wrench={"rz": -14.0}, legs={"ty": -0.4, "rz": -10})),
    (11, _tq(whole={"ty": 0.4}, wrench={"rz": 0.0})))
# Shell Slide: pulls in head and legs (A_1A, held), slides spinning (A_1B,
# loop: the paper turning round), pops back out (A_1C)
_IN = _tq(head={"tx": 2.0, "vis": 0}, group1={"vis": 0}, group2={"vis": 0}, body={"ty": -0.6})
_MET["A_1A"] = _once(12,
    (0, {}),
    (6, _tq(head={"tx": 1.6}, legs={"ty": 0.6}, body={"ty": -0.3})),
    (8, _IN, {"ease": "step"}),
    (12, _IN))
_MET["A_1B"] = _loop(8,
    (0, _tqp(_IN, whole={"sx": 1.0})),
    (2, _tqp(_IN, whole={"sx": 0.0625}), {"ease": "linear"}),
    (4, _tqp(_IN, whole={"sx": -1.0})),
    (6, _tqp(_IN, whole={"sx": -0.0625}), {"ease": "linear"}))
_MET["A_1C"] = _once(20,
    (0, _IN, {"ease": "step"}),
    (2, _tq(whole={"ty": 2.0}, head={"tx": 0.8}, legs={"ty": -0.4})),
    (8, _tq(body={"sy": 0.9375}, head={"tx": -0.3}), {"ease": "vector"}),
    (20, {}))
# Wrench Work: takes the wrench from his shell into his jaws (A_2A, held) and
# works it, turning hard (A_2B, loop); Tune-up in the field (F_1) the same
_HOLD = _tq(wrench={"tx": -5.6, "ty": -3.8, "rz": 70.0}, head={"rz": -4.0})
_MET["A_2A"] = _once(16,
    (0, {}),
    (6, _tq(wrench={"tx": -2.0, "ty": 2.0, "rz": 30.0}, head={"rz": -10.0})),
    (12, _HOLD, {"ease": "vector"}),
    (16, _HOLD))
_MET["A_2B"] = _loop(16,
    (0, _HOLD),
    (5, _tqp(_HOLD, wrench={"tx": -5.6, "ty": -3.8, "rz": 30.0}, head={"rz": 4.0, "ty": -0.3}), {"ease": "vector"}),
    (10, _tqp(_HOLD, wrench={"tx": -5.6, "ty": -3.6, "rz": 66.0}, head={"rz": -2.0})))
_MET["F_1"] = _loop(24,
    (0, _HOLD),
    (6, _tqp(_HOLD, wrench={"tx": -5.6, "ty": -3.8, "rz": 26.0}, head={"rz": 4.0, "ty": -0.3}, body={"ty": -0.2}),
     {"ease": "vector"}),
    (12, _tqp(_HOLD, whole={"ty": 0.8})),
    (18, _tqp(_HOLD, wrench={"tx": -5.6, "ty": -3.8, "rz": 30.0}, head={"rz": 3.0}), {"ease": "vector"}))
# Spike Guard: hunkers down, the shell up and bristling, braced (loop)
_SPIKE = _tq(body={"ty": -0.5}, shell={"sy": 1.125, "sx": 1.0625, "ty": 0.3}, head={"tx": 1.6, "ty": -0.4},
             legs={"ty": 0.5}, wrench={"rz": 0.0})
_MET["A_3A"] = _once(12, (0, {}), (6, _tqp(_SPIKE, shell={"sy": 1.1875, "ty": 0.4}), {"ease": "vector"}),
                     (12, _SPIKE))
_MET["A_3B"] = _shake(10, _tqp(_SPIKE, whole={"tx": 0.15}), _tqp(_SPIKE, whole={"tx": -0.15}, shell={"sy": 1.1875}))
# -- story --
# Tune-up's finish (F_2, 30, held): pops his head out, a front claw up (a
# thumbs-up), the wrench back on his shell with a nod
_MET["F_2"] = _once(30,
    (0, _HOLD),
    (6, _tq(wrench={"rz": -10.0}, head={"tx": -0.6, "rz": -6.0}, body={"ty": 0.4}), {"ease": "vector"}),
    (12, _tq(head={"tx": -0.4, "rz": -4.0}, pPlane11={"ty": 2.0, "tx": -1.0, "rz": -40.0}, wrench={"rz": 8.0})),
    (16, _tq(head={"tx": -0.4, "rz": 2.0}, pPlane11={"ty": 2.2, "tx": -1.0, "rz": -44.0}, wrench={"rz": -4.0})),
    (30, _tq(head={"tx": -0.4, "rz": -2.0}, pPlane11={"ty": 2.0, "tx": -1.0, "rz": -40.0})))
FRIENDS["c_lp_met"] = _MET


def _named(table):
    """a pose builder for one model: keywords (friendly names) to its groups;
    the first argument, if any, a pose to build on"""
    def make(*base, **kw):
        out = _pose(*base) if base else {}
        for k, ch in kw.items():
            for g in table[k] if isinstance(table[k], tuple) else (table[k],):
                out.setdefault(g, {}).update(ch)
        return out
    return make


# ------------------------------------------------------------------ Hayzee ----
# A Dayzee sways and hops: light skipping steps, the flower head swaying on its
# stalk a beat behind the body and its petals bobbing (the head squashing and
# stretching), the leaf arms swinging. Singing opens the mouth (bind 1).
# Parts: locator2 the whole, pPlane6 the body, pPlane2 the head (pPlane3 the
# mouth), pPlane5 the front leaf arm, pPlane4 the back one, pPlane8 the front
# foot, pPlane7 the back foot.

_hz0 = _named({"whole": "locator2", "body": "pPlane6", "head": "pPlane2", "mouth": "pPlane3",
               "arm_f": "pPlane5", "arm_b": "pPlane4", "arms": ("pPlane5", "pPlane4"),
               "foot_f": "pPlane8", "foot_b": "pPlane7"})


def _hz(*base, **kw):
    """Hayzee's poses. Her leaf arms sit low behind the big flower head: a turn
    of more than about 45 degrees hides them, so the arm angles written below
    (as wide gestures) are played at a third, kept within 45"""
    for k in ("arm_f", "arm_b", "arms"):
        if k in kw and "rz" in kw[k]:
            kw[k] = dict(kw[k], rz=max(-45.0, min(45.0, kw[k]["rz"] * 0.35)))
    return _hz0(*base, **kw)


def _skip(end, lift=0.9, reach=0.7, bob=0.8, lean=0.0, sway=5.0, swing=14.0, droop=0.0):
    def extra(f, step, which):
        side = 1 if which == 0 else -1
        return _hz(head={"rz": droop + side * sway * {0: 0.3, 1: 0.6, 2: 1.0, 3: 0.7}[step],
                         "sy": {0: 1.0, 1: 1.0, 2: 0.9375, 3: 1.0625}[step]},
                   arm_f={"rz": side * swing - droop}, arm_b={"rz": -side * swing - droop})
    return _gait(end, ["pPlane8", "pPlane7"], lift=lift, reach=reach, body="pPlane6", bob=bob, dip=0.2,
                 rock=2.0, lean=lean, extra=extra)


_SING = dict(mouth={"tex": 1})
_PAN = {}
_PAN["W_1"] = _skip(40)
_PAN["R_1"] = _skip(20, lift=1.3, reach=1.1, bob=1.1, lean=7.0, sway=7.0, swing=22.0)
_PAN["J_1A"] = _once(8,
    (0, _hz(body={"sy": 0.875, "ty": -0.2}, head={"sy": 0.9375})),
    (4, _hz(body={"sy": 1.0625, "ty": 0.5}, head={"sy": 1.125, "rz": -4.0}, arms={"rz": -50.0},
            foot_f={"ty": -0.5, "rz": -10.0}, foot_b={"ty": -0.5, "rz": -6.0})),
    (8, _hz(body={"ty": 0.5}, head={"sy": 1.0625, "rz": -3.0}, arms={"rz": -44.0},
            foot_f={"ty": -0.5, "rz": -10.0}, foot_b={"ty": -0.5, "rz": -6.0})))
_PAN["J_1B"] = _loop(16,
    (0, _hz(head={"sy": 0.9375, "rz": 3.0}, arm_f={"rz": 60.0}, arm_b={"rz": -60.0}, foot_f={"tx": -0.6, "rz": 10.0},
            foot_b={"tx": 0.6, "rz": -8.0})),
    (8, _hz(head={"sy": 0.9375, "rz": 5.0}, arm_f={"rz": 70.0}, arm_b={"rz": -70.0}, foot_f={"tx": -0.7, "rz": 12.0},
            foot_b={"tx": 0.7, "rz": -10.0})))
_PAN["D_1"] = _shake(8,
    _hz(whole={"tx": 0.4}, body={"rz": -8.0}, head={"rz": -12.0, "sx": 1.0625, "sy": 0.9375}, arms={"rz": 30.0}),
    _hz(whole={"tx": -0.3}, body={"rz": 5.0}, head={"rz": 8.0, "sx": 0.9375, "sy": 1.0625}, arms={"rz": -20.0}))
_WILT = _hz(body={"ty": -0.3, "rz": 4.0}, head={"rz": 16.0, "ty": -0.4, "sy": 0.9375}, arms={"rz": 30.0})
_PAN["D_2"] = _loop(92, (0, _WILT), (46, _hz(_WILT, head={"rz": 12.0, "ty": -0.2, "sy": 1.0})))
_PAN["W_2"] = _skip(40, lift=0.5, reach=0.5, bob=0.3, sway=3.0, swing=6.0, droop=12.0)
_PAN["R_2"] = _skip(20, lift=0.8, reach=0.8, bob=0.6, lean=3.0, sway=4.0, swing=10.0, droop=8.0)
_PAN["D_3"] = _once(16,
    (0, _hz(head={"rz": -6.0}, arms={"rz": -20.0})),
    (6, _hz(whole={"rz": -40.0, "ty": 1.0}, head={"rz": -10.0, "sy": 1.0625}, arms={"rz": -40.0})),
    (11, _hz(whole={"rz": -90.0, "ty": 2.6, "tx": 1.0}, head={"rz": 8.0, "sy": 0.875}, arms={"rz": 20.0},
             foot_f={"rz": -20.0}), {"ease": "vector"}),
    (13, _hz(whole={"rz": -86.0, "ty": 3.0, "tx": 1.0}, head={"rz": 4.0, "sy": 0.9375}, arms={"rz": 24.0},
             foot_f={"rz": -24.0})),
    (16, _hz(whole={"rz": -90.0, "ty": 2.6, "tx": 1.0}, head={"rz": 8.0, "sy": 0.875}, arms={"rz": 20.0},
             foot_f={"rz": -20.0})))
_PAN["D_4"] = _once(20,
    (0, _hz(head={"rz": -8.0})),
    (6, _hz(whole={"rz": -120.0, "ty": 12.0, "tx": 1.5}, arms={"rz": 40.0})),
    (12, _hz(whole={"rz": -250.0, "ty": 12.0, "tx": 3.0}, arms={"rz": 40.0})),
    (16, _hz(whole={"rz": -350.0, "tx": 3.5}, head={"sy": 0.875}), {"ease": "vector"}),
    (20, _hz(whole={"rz": -360.0, "tx": 3.5})))
_PAN["D_5"] = _once(0, (0, _hz(head={"rz": 6.0}, arm_f={"rz": -30.0}, foot_f={"ty": 0.6, "tx": -0.5})))
_PAN["N_1"] = _loop(120,
    (0, _hz(head={"rz": 14.0, "sy": 0.9375, "ty": -0.3}, arms={"rz": 20.0}, body={"ty": -0.2})),
    (60, _hz(head={"rz": 10.0, "sy": 1.0, "ty": 0.0}, arms={"rz": 14.0}, body={"ty": 0.0})))
_PAN["X_1"] = _loop(48,
    (0, _hz(head={"rz": 14.0}, body={"rz": 4.0}, arm_f={"rz": 30.0}, arm_b={"rz": -20.0})),
    (12, _hz(head={"rz": 0.0, "sy": 1.0625}, whole={"tx": -0.4}, arms={"rz": 0.0})),
    (24, _hz(head={"rz": -14.0}, body={"rz": -4.0}, arm_f={"rz": -20.0}, arm_b={"rz": 30.0})),
    (36, _hz(head={"rz": 0.0, "sy": 0.9375}, whole={"tx": 0.4}, arms={"rz": 0.0})))
_PAN["Q_1"] = _shake(8, _hz(head={"sy": 0.9375, "rz": -6.0}, arms={"rz": -70.0}, whole={"tx": 0.25}, body={"ty": -0.2}),
                     _hz(head={"sy": 0.9375, "rz": -4.0}, arms={"rz": -66.0}, whole={"tx": -0.25}, body={"ty": -0.2}))
_PAN["C_1"] = _loop(48,
    (0, _hz(head={"rz": 4.0}, arm_f={"rz": 10.0}, arm_b={"rz": -10.0})),
    (12, _hz(head={"rz": -2.0, "sy": 1.0625}, body={"ty": 0.4}, arm_f={"rz": -14.0}, arm_b={"rz": 14.0})),
    (24, _hz(head={"rz": -4.0}, arm_f={"rz": 10.0}, arm_b={"rz": -10.0})),
    (36, _hz(head={"rz": 2.0, "sy": 1.0625}, body={"ty": 0.4}, arm_f={"rz": -14.0}, arm_b={"rz": 14.0})))
_GD = _hz(body={"ty": -0.4, "rz": 6.0}, head={"rz": 10.0, "sy": 0.9375}, arm_f={"rz": -100.0}, arm_b={"rz": -80.0})
_PAN["G_1"] = _once(40, (0, _GD, {"ease": "vector"}), (40, _GD))
_PAN["G_2"] = _once(40,
    (0, _hz(whole={"tx": -1.2}, body={"rz": 14.0}, head={"rz": 10.0}, arm_f={"rz": -110.0}), {"ease": "vector"}),
    (10, _hz(whole={"tx": -0.6}, body={"rz": 4.0}, arm_f={"rz": -40.0})),
    (40, {}))
_PAN["I_1"] = _once(10,
    (0, {}),
    (6, _hz(body={"ty": 0.6}, head={"rz": -8.0, "sy": 1.0625}, arms={"rz": -150.0})),
    (10, _hz(body={"ty": 0.6}, head={"rz": -6.0}, arms={"rz": -145.0})))
_PAN["O_1"] = _once(56,
    (0, {}),
    (8, _hz(body={"sy": 0.875}, head={"sy": 0.9375}, arms={"rz": 30.0})),
    (14, _hz(whole={"ty": 2.5, "sx": 0.0625}, arms={"rz": -90.0}), {"ease": "linear"}),
    (20, _hz(whole={"ty": 3.6, "sx": -1.0}, arms={"rz": -120.0}, **_SING), {"ease": "linear"}),
    (26, _hz(whole={"ty": 3.0, "sx": -0.0625}, arms={"rz": -90.0}, **_SING), {"ease": "linear"}),
    (32, _hz(whole={"sx": 1.0}, body={"sy": 0.875}, head={"sy": 1.125}, arm_f={"rz": -130.0}, arm_b={"rz": 130.0},
             **_SING), {"ease": "vector"}),
    (44, _hz(head={"rz": -6.0}, arm_f={"rz": -120.0}, arm_b={"rz": 120.0}, **_SING)),
    (56, {}))
_PAN["Y_1"] = _loop(14,
    (0, _hz(body={"sy": 0.875}, head={"sy": 0.9375}, arms={"rz": 20.0})),
    (6, _hz(whole={"ty": 2.6}, head={"sy": 1.125, "rz": -4.0}, arm_f={"rz": -140.0}, arm_b={"rz": 140.0},
            mouth={"tex": 1})),
    (11, _hz(whole={"ty": 0.4}, arms={"rz": -40.0}, mouth={"tex": 1})))
# Lullaby: a slow swaying song, arms rocking (A_1, loop 48)
_PAN["A_1"] = _loop(48,
    (0, _hz(head={"rz": 8.0}, body={"rz": 3.0}, arm_f={"rz": -40.0}, arm_b={"rz": -20.0}, **_SING)),
    (12, _hz(head={"rz": 0.0, "sy": 1.0625}, body={"ty": 0.3}, arm_f={"rz": -60.0}, arm_b={"rz": -40.0},
             mouth={"tex": 0})),
    (16, _hz(head={"rz": -2.0, "sy": 1.0625}, body={"ty": 0.3}, arm_f={"rz": -60.0}, arm_b={"rz": -40.0}, **_SING)),
    (24, _hz(head={"rz": -8.0}, body={"rz": -3.0}, arm_f={"rz": -20.0}, arm_b={"rz": -40.0}, **_SING)),
    (36, _hz(head={"rz": 0.0, "sy": 1.0625}, body={"ty": 0.3}, arm_f={"rz": -40.0}, arm_b={"rz": -60.0},
             mouth={"tex": 0})),
    (40, _hz(head={"rz": 2.0, "sy": 1.0625}, body={"ty": 0.3}, arm_f={"rz": -40.0}, arm_b={"rz": -60.0}, **_SING)))
# Encore: an eager cheer, arms pumping, hopping (A_2, loop 14)
_PAN["A_2"] = _loop(14,
    (0, _hz(body={"sy": 0.9375}, arm_f={"rz": -150.0}, arm_b={"rz": -60.0}, **_SING)),
    (5, _hz(whole={"ty": 1.6}, head={"sy": 1.125}, arm_f={"rz": -60.0}, arm_b={"rz": -150.0}, **_SING)),
    (10, _hz(whole={"ty": 0.3}, head={"sy": 0.9375}, arm_f={"rz": -130.0}, arm_b={"rz": -80.0}, mouth={"tex": 0})))
# Showstopper: winds up, arms back (A_3A, held), then the grand finale, head
# thrown back, arms flung wide, singing out (A_3B, held)
_FINALE = _hz(whole={"ty": 0.6}, body={"rz": -10.0, "sy": 1.0625}, head={"rz": -18.0, "sy": 1.125},
              arm_f={"rz": -160.0}, arm_b={"rz": 150.0}, **_SING)
_PAN["A_3A"] = _once(20,
    (0, {}),
    (14, _hz(body={"rz": 8.0, "sy": 0.875, "ty": -0.3}, head={"rz": 10.0, "sy": 0.9375}, arms={"rz": 60.0})),
    (20, _hz(body={"rz": 9.0, "sy": 0.875, "ty": -0.3}, head={"rz": 11.0, "sy": 0.9375}, arms={"rz": 64.0})))
_PAN["A_3B"] = _once(30,
    (0, _hz(body={"rz": 9.0, "sy": 0.875, "ty": -0.3}, head={"rz": 11.0, "sy": 0.9375}, arms={"rz": 64.0})),
    (5, _FINALE, {"ease": "vector"}),
    (10, _hz(_FINALE, head={"rz": -15.0, "sy": 1.0625})),
    (30, _hz(_FINALE, head={"rz": -16.0, "sy": 1.0625})))
# Sing (field): a bright little song, bouncing on the beat (F_1, loop 40)
_PAN["F_1"] = _loop(40,
    (0, _hz(head={"rz": 6.0}, arm_f={"rz": -70.0}, arm_b={"rz": -30.0}, **_SING)),
    (10, _hz(whole={"ty": 0.8}, head={"rz": 0.0, "sy": 1.125}, arm_f={"rz": -110.0}, arm_b={"rz": -60.0}, **_SING)),
    (16, _hz(head={"rz": -3.0}, mouth={"tex": 0}, arm_f={"rz": -90.0}, arm_b={"rz": -40.0})),
    (20, _hz(head={"rz": -6.0}, arm_f={"rz": -30.0}, arm_b={"rz": -70.0}, **_SING)),
    (30, _hz(whole={"ty": 0.8}, head={"rz": 0.0, "sy": 1.125}, arm_f={"rz": -60.0}, arm_b={"rz": -110.0}, **_SING)),
    (36, _hz(head={"rz": 3.0}, mouth={"tex": 0}, arm_f={"rz": -40.0}, arm_b={"rz": -90.0})))
# -- story --
# a held note (F_2, a 40-frame loop): head back, mouth wide, swaying, arms out
_NOTE = _hz(body={"rz": -4.0, "sy": 1.0625}, head={"rz": -12.0, "sy": 1.0625}, arm_f={"rz": -120.0},
            arm_b={"rz": 120.0}, **_SING)
_PAN["F_2"] = _loop(40, (0, _NOTE), (20, _hz(_NOTE, head={"rz": -8.0, "sy": 1.125}, body={"rz": -2.0, "ty": 0.3})))
# conducting (A_3C, a 24-frame loop): beating time with the front leaf
_PAN["A_3C"] = _loop(24,
    (0, _hz(head={"rz": -4.0}, arm_f={"rz": -130.0}, arm_b={"rz": 30.0})),
    (6, _hz(head={"rz": 2.0, "sy": 0.9375}, arm_f={"rz": -40.0}, arm_b={"rz": 30.0}, body={"ty": -0.2}),
     {"ease": "vector"}),
    (12, _hz(head={"rz": 0.0}, arm_f={"rz": -100.0}, arm_b={"rz": 30.0})),
    (18, _hz(head={"rz": 4.0, "sy": 0.9375}, arm_f={"rz": -60.0}, arm_b={"rz": 20.0}, body={"ty": -0.2}),
     {"ease": "vector"}))
# the bow (O_2, 30, held at the end upright): a deep bow from the stalk
_PAN["O_2"] = _once(30,
    (0, {}),
    (10, _hz(body={"rz": 20.0, "ty": -0.3}, head={"rz": 34.0, "sy": 0.9375}, arm_f={"rz": 60.0},
             arm_b={"rz": -60.0})),
    (20, _hz(body={"rz": 22.0, "ty": -0.3}, head={"rz": 36.0, "sy": 0.9375}, arm_f={"rz": 60.0},
             arm_b={"rz": -60.0})),
    (30, {}))

# -- her gestures, the leaves in front --
# Where her arms carry the pose (singing, cheering, the show, talking, the bow,
# victory, joining), the leaf arms come forward of the flower head (their z
# moved to the front) and swing as wide as the gesture wants; at rest and
# everywhere else they stay behind, small. `up` is how far each leaf is raised
# from hanging at her side, in degrees (the front leaf points left, the back
# one right): 0 at rest, 90 straight up beside the head, at most 110.
_FRONT = 0.7  # the leaves' z, forward of the head (0.2) and mouth (0.3)


_OUT = 3.2  # in front, the leaves move out to the head's sides, clear of the face


def _hzf(*base, up_f=0.0, up_b=0.0, **kw):
    pose = _hz(*base, **kw)
    up_f, up_b = min(up_f, 110.0), min(up_b, 110.0)  # past that they'd cross the face
    pose["pPlane5"] = dict(pose.get("pPlane5", {}), rz=-up_f, tz=_FRONT + 0.3, tx=-_OUT, ty=0.6)
    pose["pPlane4"] = dict(pose.get("pPlane4", {}), rz=up_b, tz=_FRONT - 0.1, tx=_OUT - 0.6, ty=0.6)
    return pose


_PAN["F_1"] = _loop(40,
    (0, _hzf(head={"rz": 6.0}, up_f=70, up_b=30, **_SING)),
    (10, _hzf(whole={"ty": 0.8}, head={"rz": 0.0, "sy": 1.125}, up_f=120, up_b=70, **_SING)),
    (16, _hzf(head={"rz": -3.0}, mouth={"tex": 0}, up_f=95, up_b=50)),
    (20, _hzf(head={"rz": -6.0}, up_f=30, up_b=70, **_SING)),
    (30, _hzf(whole={"ty": 0.8}, head={"rz": 0.0, "sy": 1.125}, up_f=70, up_b=120, **_SING)),
    (36, _hzf(head={"rz": 3.0}, mouth={"tex": 0}, up_f=50, up_b=95)))
_NOTEF = _hzf(body={"rz": -4.0, "sy": 1.0625}, head={"rz": -12.0, "sy": 1.0625}, up_f=130, up_b=130, **_SING)
_PAN["F_2"] = _loop(40, (0, _NOTEF), (20, _hzf(_NOTEF, head={"rz": -8.0, "sy": 1.125}, body={"rz": -2.0, "ty": 0.3},
                                              up_f=120, up_b=120, **_SING)))
_PAN["A_1"] = _loop(48,
    (0, _hzf(head={"rz": 8.0}, body={"rz": 3.0}, up_f=60, up_b=20, **_SING)),
    (12, _hzf(head={"rz": 0.0, "sy": 1.0625}, body={"ty": 0.3}, up_f=80, up_b=50, mouth={"tex": 0})),
    (16, _hzf(head={"rz": -2.0, "sy": 1.0625}, body={"ty": 0.3}, up_f=80, up_b=50, **_SING)),
    (24, _hzf(head={"rz": -8.0}, body={"rz": -3.0}, up_f=20, up_b=60, **_SING)),
    (36, _hzf(head={"rz": 0.0, "sy": 1.0625}, body={"ty": 0.3}, up_f=50, up_b=80, mouth={"tex": 0})),
    (40, _hzf(head={"rz": 2.0, "sy": 1.0625}, body={"ty": 0.3}, up_f=50, up_b=80, **_SING)))
_PAN["A_2"] = _loop(14,
    (0, _hzf(body={"sy": 0.9375}, up_f=150, up_b=40, **_SING)),
    (5, _hzf(whole={"ty": 1.6}, head={"sy": 1.125}, up_f=40, up_b=150, **_SING)),
    (10, _hzf(whole={"ty": 0.3}, head={"sy": 0.9375}, up_f=130, up_b=70, mouth={"tex": 0})))
_PAN["A_3A"] = _once(20,
    (0, _hzf()),
    (14, _hzf(body={"rz": 8.0, "sy": 0.875, "ty": -0.3}, head={"rz": 10.0, "sy": 0.9375}, up_f=-30, up_b=-30)),
    (20, _hzf(body={"rz": 9.0, "sy": 0.875, "ty": -0.3}, head={"rz": 11.0, "sy": 0.9375}, up_f=-34, up_b=-34)))
_FIN = _hzf(whole={"ty": 0.6}, body={"rz": -10.0, "sy": 1.0625}, head={"rz": -18.0, "sy": 1.125}, up_f=150, up_b=150,
            **_SING)
_PAN["A_3B"] = _once(30,
    (0, _hzf(body={"rz": 9.0, "sy": 0.875, "ty": -0.3}, head={"rz": 11.0, "sy": 0.9375}, up_f=-34, up_b=-34)),
    (5, _FIN, {"ease": "vector"}),
    (10, _hzf(_FIN, head={"rz": -15.0, "sy": 1.0625}, up_f=140, up_b=140, **_SING)),
    (30, _hzf(_FIN, head={"rz": -16.0, "sy": 1.0625}, up_f=140, up_b=140, **_SING)))
_PAN["A_3C"] = _loop(24,
    (0, _hzf(head={"rz": -4.0}, up_f=140, up_b=20)),
    (6, _hzf(head={"rz": 2.0, "sy": 0.9375}, body={"ty": -0.2}, up_f=50, up_b=20), {"ease": "vector"}),
    (12, _hzf(head={"rz": 0.0}, up_f=110, up_b=20)),
    (18, _hzf(head={"rz": 4.0, "sy": 0.9375}, body={"ty": -0.2}, up_f=70, up_b=30), {"ease": "vector"}))
_PAN["O_1"] = _once(56,
    (0, _hzf()),
    (8, _hzf(body={"sy": 0.875}, head={"sy": 0.9375}, up_f=-20, up_b=-20)),
    (14, _hzf(whole={"ty": 2.5, "sx": 0.0625}, up_f=90, up_b=90), {"ease": "linear"}),
    (20, _hzf(whole={"ty": 3.6, "sx": -1.0}, up_f=120, up_b=120, **_SING), {"ease": "linear"}),
    (26, _hzf(whole={"ty": 3.0, "sx": -0.0625}, up_f=90, up_b=90, **_SING), {"ease": "linear"}),
    (32, _hzf(whole={"sx": 1.0}, body={"sy": 0.875}, head={"sy": 1.125}, up_f=150, up_b=150, **_SING),
     {"ease": "vector"}),
    (44, _hzf(head={"rz": -6.0}, up_f=140, up_b=140, **_SING)),
    (56, _hzf()))
_PAN["O_2"] = _once(30,
    (0, _hzf()),
    (10, _hzf(body={"rz": 20.0, "ty": -0.3}, head={"rz": 34.0, "sy": 0.9375}, up_f=-40, up_b=60)),
    (20, _hzf(body={"rz": 22.0, "ty": -0.3}, head={"rz": 36.0, "sy": 0.9375}, up_f=-40, up_b=60)),
    (30, _hzf()))
_PAN["Y_1"] = _loop(14,
    (0, _hzf(body={"sy": 0.875}, head={"sy": 0.9375}, up_f=20, up_b=20)),
    (6, _hzf(whole={"ty": 2.6}, head={"sy": 1.125, "rz": -4.0}, up_f=150, up_b=150, mouth={"tex": 1})),
    (11, _hzf(whole={"ty": 0.4}, up_f=60, up_b=60, mouth={"tex": 1})))
_PAN["I_1"] = _once(10,
    (0, _hzf()),
    (6, _hzf(body={"ty": 0.6}, head={"rz": -8.0, "sy": 1.0625}, up_f=150, up_b=150)),
    (10, _hzf(body={"ty": 0.6}, head={"rz": -6.0}, up_f=145, up_b=145)))
# talking, her own (in place of the disc's, which keeps her leaves behind):
# the mouth flapping as the disc's does (bind 1), a leaf gesturing in front
_PAN["T_1"] = _loop(14,
    (0, _hzf(head={"rz": 0.0}, up_f=20, up_b=10)),
    (4, _hzf(head={"rz": -3.0, "sy": 1.0625}, body={"ty": 0.2}, up_f=60, up_b=15, mouth={"tex": 1})),
    (7, _hzf(head={"rz": -4.0, "sy": 1.0625}, body={"ty": 0.3}, up_f=70, up_b=20, mouth={"tex": 1})),
    (11, _hzf(head={"rz": -2.0}, body={"ty": 0.1}, up_f=45, up_b=15, mouth={"tex": 0})))
# joining (E_2, 40, held at the end): a happy hop, both leaves flung up in
# front, a little sway of the head
_PAN["E_2"] = _once(40,
    (0, _hzf(body={"sy": 0.875, "ty": -0.2}, up_f=-20, up_b=-20)),
    (8, _hzf(whole={"ty": 3.0}, body={"sy": 1.125}, head={"sy": 1.125, "rz": -8.0}, up_f=150, up_b=150, **_SING),
     {"ease": "vector"}),
    (16, _hzf(whole={"ty": 0.0}, body={"sy": 0.875}, head={"sy": 0.9375}, up_f=120, up_b=120, **_SING),
     {"ease": "vector"}),
    (24, _hzf(head={"rz": 8.0}, up_f=130, up_b=100, **_SING)),
    (32, _hzf(head={"rz": -6.0}, up_f=100, up_b=130, **_SING)),
    (40, _hzf(head={"rz": 0.0}, up_f=110, up_b=110, mouth={"tex": 0})))
# Soft Shoe's heel (A_4, 24, held at the end upright): he rocks back on the
# back foot, the front foot raised (the wind-up), then plants its heel down and
# forward on the foe with a lean in and a "ta!", and settles back. (The heel
# played the Showstopper's finale, A_3B, before.) Forward is -x.
_PAN["A_4"] = _once(24,
    (0, _hzf()),
    (6, _hzf(body={"rz": -7.0, "ty": 0.3}, head={"rz": -9.0, "sy": 1.0625}, foot_f={"tx": -0.6, "ty": 1.3, "rz": -24.0},
             foot_b={"tx": 0.3}, up_f=50, up_b=70)),
    (9, _hzf(body={"rz": 9.0, "ty": -0.1}, head={"rz": 12.0, "sy": 0.9375}, foot_f={"tx": -1.5, "ty": 0.1, "rz": 16.0},
             foot_b={"tx": 0.4}, up_f=120, up_b=20, **_SING), {"ease": "vector"}),
    (14, _hzf(body={"rz": 7.0}, head={"rz": 9.0, "sy": 1.0}, foot_f={"tx": -1.3, "rz": 10.0}, foot_b={"tx": 0.3},
              up_f=110, up_b=25, **_SING)),
    (24, _hzf()))
# Lullaby's wind-up (A_1A, 24, held): a breath drawn in, rising on the stalk,
# both leaves lifting in front; the song (A_1) goes on from it
_BREATH = _hzf(body={"sy": 1.0625, "ty": 0.4}, head={"rz": -8.0, "sy": 1.0625}, up_f=70, up_b=70)
_PAN["A_1A"] = _once(24,
    (0, _hzf()),
    (10, _hzf(body={"sy": 1.03125, "ty": 0.3}, head={"rz": -5.0, "sy": 1.03125}, up_f=40, up_b=40)),
    (18, _BREATH),
    (24, _hzf(_BREATH, head={"rz": -7.0, "sy": 1.0625}, up_f=70, up_b=70)))
# Lullaby's end (A_1B, 40, back to rest): "sleep tight", the head laid on the
# back leaf as on a pillow, the front one hushing, sinking a little; then up
_PILLOW = _hzf(body={"ty": -0.3, "rz": 2.0}, head={"rz": 18.0, "sy": 0.9375, "ty": -0.2}, up_f=40, up_b=110)
_PAN["A_1B"] = _once(40,
    (0, _hzf(head={"rz": 2.0, "sy": 1.0625}, body={"ty": 0.3}, up_f=50, up_b=80, **_SING)),
    (10, _PILLOW, {"ease": "vector"}),
    (28, _hzf(_PILLOW, head={"rz": 20.0, "sy": 0.9375, "ty": -0.3}, up_f=36, up_b=112)),
    (40, _hzf()))
# Encore's wind-up (A_2A, 18, held): a dip, then the front leaf flung out at
# Luigi, leaning to him, "again!"
_POINT = _hzf(body={"rz": 7.0}, head={"rz": 8.0}, up_f=110, up_b=30, **_SING)
_PAN["A_2A"] = _once(18,
    (0, _hzf()),
    (6, _hzf(body={"sy": 0.875, "ty": -0.2}, head={"sy": 0.9375}, up_f=-10, up_b=-10)),
    (12, _hzf(body={"rz": 8.0, "sy": 1.0625}, head={"rz": 10.0, "sy": 1.0625}, up_f=115, up_b=30, **_SING),
     {"ease": "vector"}),
    (18, _POINT))
# Encore landed (A_2B, 40, held): a hop with a paper twirl, landing with the
# front leaf high, presenting him
_TADA = _hzf(head={"rz": -6.0}, up_f=140, up_b=60, **_SING)
_PAN["A_2B"] = _once(40,
    (0, _hzf(body={"sy": 0.875, "ty": -0.2}, up_f=20, up_b=20)),
    (5, _hzf(whole={"ty": 2.0, "sx": 0.0625}, up_f=90, up_b=90, **_SING), {"ease": "linear"}),
    (10, _hzf(whole={"ty": 3.2, "sx": -1.0}, up_f=120, up_b=120, **_SING), {"ease": "linear"}),
    (15, _hzf(whole={"ty": 2.0, "sx": -0.0625}, up_f=90, up_b=90, **_SING), {"ease": "linear"}),
    (20, _hzf(whole={"sx": 1.0}, body={"sy": 0.875}, head={"sy": 1.125}, up_f=150, up_b=60, **_SING),
     {"ease": "vector"}),
    (30, _TADA),
    (40, _hzf(_TADA, mouth={"tex": 0})))
# Encore missed (A_2C, 30, held): the cheer goes out of him, head and leaves
# drooping
_DROOP = _hzf(body={"ty": -0.3, "rz": 3.0}, head={"rz": 14.0, "ty": -0.4, "sy": 0.9375}, up_f=-20, up_b=-20)
_PAN["A_2C"] = _once(30,
    (0, _hzf()),
    (10, _DROOP, {"ease": "vector"}),
    (30, _hzf(_DROOP, head={"rz": 12.0, "ty": -0.3, "sy": 0.9375}, up_f=-16, up_b=-16)))
FRIENDS["c_lp_pansy"] = _PAN


# ----------------------------------------------------------------- Screamy ----
# A Smorg-kin floats: hovering a little above the ground and bobbing, the feet
# dangling and paddling, the body flowing behind its moves (squash and stretch)
# and the eyes and mouth stretching with feeling. Parts: locator4 the whole,
# locator3 the body's mover (pPlane5 the body; group1 the eyes, pPlane1 and
# pPlane3; pPlane2 the mouth), locator2 the front foot, locator1 the back one.

_sc = _named({"whole": "locator4", "body": "locator3", "eyes": "group1", "mouth": "pPlane2",
              "foot_f": "locator2", "foot_b": "locator1", "feet": ("locator2", "locator1")})


def _float(end, high=1.6, bob=0.8, lean=0.0, paddle=14.0, droop=0.0):
    """hovering: the body rises and falls once a loop (twice at speed), the feet
    paddling in turn below it, the body stretching as it rises"""
    q = end // 4
    poses = []
    for k in range(4):
        up = k in (1, 2)
        poses.append((k * q, _sc(body={"ty": high + (bob if k == 1 else bob * 0.6 if k == 2 else 0.0),
                                       "rz": lean - droop + (1.5 if k % 2 else -1.5),
                                       "sy": 1.0625 if k == 1 else 0.9375 if k == 3 else 1.0,
                                       "sx": 0.9375 if k == 1 else 1.0625 if k == 3 else 1.0},
                                 eyes={"ty": -droop * 0.05},
                                 foot_f={"ty": high * 0.7 + (0.4 if up else 0.0), "rz": paddle if k % 2 else -paddle * 0.4,
                                         "tx": -0.3 if k % 2 else 0.3},
                                 foot_b={"ty": high * 0.7 + (0.4 if up else 0.0), "rz": -paddle * 0.4 if k % 2 else paddle,
                                         "tx": 0.3 if k % 2 else -0.3})))
    return _loop(end, *poses)


_MOA = {}
_MOA["W_1"] = _float(40, high=0.6)
_MOA["R_1"] = _float(20, high=0.8, bob=0.7, lean=8.0, paddle=22.0)
_MOA["J_1A"] = _once(8,
    (0, _sc(body={"sy": 0.875, "sx": 1.0625, "ty": 0.6})),
    (4, _sc(body={"sy": 1.1875, "sx": 0.875, "ty": 1.6}, feet={"ty": 0.4, "rz": -10.0}, eyes={"sy": 1.0625})),
    (8, _sc(body={"sy": 1.125, "sx": 0.9375, "ty": 1.6}, feet={"ty": 0.4, "rz": -10.0})))
_MOA["J_1B"] = _loop(16,
    (0, _sc(body={"sy": 0.9375, "sx": 1.0625, "ty": 1.4}, feet={"ty": 1.2, "rz": 14.0})),
    (8, _sc(body={"sy": 0.875, "sx": 1.125, "ty": 1.6}, feet={"ty": 1.4, "rz": 20.0})))
_MOA["D_1"] = _shake(8,
    _sc(whole={"tx": 0.5}, body={"rz": -10.0, "sx": 1.125, "sy": 0.875, "ty": 1.0}, mouth={"sx": 1.375, "sy": 1.25},
        eyes={"sy": 0.5}),
    _sc(whole={"tx": -0.3}, body={"rz": 6.0, "sx": 0.875, "sy": 1.125, "ty": 1.0}, mouth={"sx": 1.25, "sy": 1.25},
        eyes={"sy": 0.5}))
_SLUMP = _sc(body={"ty": 0.0, "sx": 1.125, "sy": 0.875, "rz": -4.0}, eyes={"sy": 0.6875, "ty": -0.3})
_MOA["D_2"] = _loop(92, (0, _SLUMP), (46, _sc(_SLUMP, body={"ty": 0.3, "sx": 1.0625, "sy": 0.9375, "rz": -3.0})))
_MOA["W_2"] = _float(40, high=0.6, bob=0.4, paddle=8.0, droop=4.0)
_MOA["R_2"] = _float(20, high=0.8, bob=0.4, lean=4.0, paddle=12.0, droop=3.0)
_MOA["D_3"] = _once(16,
    (0, _sc(body={"ty": 1.0}, mouth={"sx": 1.25})),
    (6, _sc(body={"ty": 2.0, "sy": 1.125, "sx": 0.9375}, eyes={"sy": 0.5}, mouth={"sx": 1.5, "sy": 1.25})),
    (11, _sc(body={"ty": -1.6, "sy": 0.625, "sx": 1.25}, eyes={"sy": 0.25, "ty": -1.0}, mouth={"sy": 0.5}),
     {"ease": "vector"}),
    (13, _sc(body={"ty": -1.3, "sy": 0.6875, "sx": 1.1875}, eyes={"sy": 0.25, "ty": -0.9}, mouth={"sy": 0.5})),
    (16, _sc(body={"ty": -1.6, "sy": 0.625, "sx": 1.25}, eyes={"sy": 0.25, "ty": -1.0}, mouth={"sy": 0.5})))
_MOA["D_4"] = _once(20,
    (0, _sc(body={"ty": 1.0, "rz": -8.0})),
    (6, _sc(whole={"rz": -120.0, "ty": 10.0, "tx": 1.5}, body={"ty": 1.0})),
    (12, _sc(whole={"rz": -250.0, "ty": 10.0, "tx": 3.0}, body={"ty": 1.0})),
    (16, _sc(whole={"rz": -350.0, "tx": 3.5}, body={"sy": 0.875, "sx": 1.125}), {"ease": "vector"}),
    (20, _sc(whole={"rz": -360.0, "tx": 3.5}, body={"ty": 0.8})))
_MOA["D_5"] = _once(0, (0, _sc(body={"ty": 1.2, "sy": 1.0625}, mouth={"sx": 1.25}, foot_f={"ty": 1.0, "rz": 10.0})))
_MOA["N_1"] = _loop(120,
    (0, _sc(body={"ty": 0.0, "sx": 1.0625, "sy": 0.9375}, eyes={"sy": 0.25})),
    (60, _sc(body={"ty": 0.2, "sx": 1.0, "sy": 1.0}, eyes={"sy": 0.25})))
_MOA["X_1"] = _loop(48,
    (0, _sc(body={"rz": 12.0, "ty": 1.2}, eyes={"rz": 20.0}, whole={"tx": 0.0})),
    (12, _sc(body={"rz": 0.0, "ty": 1.6}, eyes={"rz": 0.0}, whole={"tx": -0.5})),
    (24, _sc(body={"rz": -12.0, "ty": 1.2}, eyes={"rz": -20.0}, whole={"tx": 0.0})),
    (36, _sc(body={"rz": 0.0, "ty": 0.8}, eyes={"rz": 0.0}, whole={"tx": 0.5})))
_MOA["Q_1"] = _shake(8, _sc(body={"ty": 0.4, "sy": 0.875, "sx": 0.9375}, eyes={"sy": 1.25}, whole={"tx": 0.25}),
                     _sc(body={"ty": 0.4, "sy": 0.875, "sx": 0.9375}, eyes={"sy": 1.25}, whole={"tx": -0.25}))
_MOA["C_1"] = _float(48, high=1.4, bob=0.6, paddle=10.0)
_GM = _sc(body={"ty": 0.2, "sx": 1.1875, "sy": 0.8125, "rz": 6.0}, eyes={"sy": 0.6875})
_MOA["G_1"] = _once(40, (0, _GM, {"ease": "vector"}), (40, _GM))
_MOA["G_2"] = _once(40,
    (0, _sc(whole={"tx": -1.4}, body={"ty": 1.0, "rz": 14.0, "sx": 1.125, "sy": 0.875}), {"ease": "vector"}),
    (10, _sc(whole={"tx": -0.7}, body={"ty": 1.2, "rz": 4.0})),
    (40, _sc(body={"ty": 1.0})))
_MOA["I_1"] = _once(10,
    (0, _sc(body={"ty": 1.0})),
    (6, _sc(body={"ty": 2.4, "sy": 1.125, "sx": 0.9375}, eyes={"sy": 1.125}, mouth={"sx": 1.25})),
    (10, _sc(body={"ty": 2.2, "sy": 1.0625}, mouth={"sx": 1.125})))
_MOA["O_1"] = _once(56,
    (0, _sc(body={"ty": 1.0})),
    (8, _sc(body={"ty": 0.6, "sy": 0.875, "sx": 1.125})),
    (14, _sc(whole={"ty": 2.5, "sx": 0.0625}, body={"ty": 1.4}), {"ease": "linear"}),
    (20, _sc(whole={"ty": 3.6, "sx": -1.0}, body={"ty": 1.6}, mouth={"sx": 1.5, "sy": 1.25}), {"ease": "linear"}),
    (26, _sc(whole={"ty": 3.0, "sx": -0.0625}, body={"ty": 1.4}), {"ease": "linear"}),
    (32, _sc(whole={"sx": 1.0}, body={"ty": 0.6, "sy": 0.875, "sx": 1.125}), {"ease": "vector"}),
    (40, _sc(body={"ty": 1.4, "sy": 1.0625}, eyes={"sy": 1.125})),
    (56, _sc(body={"ty": 1.0})))
_MOA["Y_1"] = _loop(14,
    (0, _sc(body={"ty": 0.4, "sy": 0.875, "sx": 1.125})),
    (6, _sc(whole={"ty": 2.8}, body={"ty": 1.0, "sy": 1.125, "sx": 0.9375}, mouth={"sx": 1.375, "sy": 1.25},
            feet={"ty": 0.6, "rz": 16.0})),
    (11, _sc(whole={"ty": 0.4}, body={"ty": 0.8})))
# Swarm: gathers itself (A_1A, held), then darts at the foe again and again (A_1B, loop)
_MOA["A_1A"] = _once(12,
    (0, _sc(body={"ty": 1.0})),
    (8, _sc(whole={"tx": 1.2}, body={"ty": 1.4, "sx": 0.875, "sy": 1.125, "rz": -8.0}, eyes={"sy": 0.75})),
    (12, _sc(whole={"tx": 1.3}, body={"ty": 1.4, "sx": 0.875, "sy": 1.125, "rz": -9.0}, eyes={"sy": 0.75})))
_MOA["A_1B"] = _loop(8,
    (0, _sc(whole={"tx": 0.8}, body={"ty": 1.4, "sx": 0.875, "sy": 1.125, "rz": -6.0})),
    (3, _sc(whole={"tx": -2.4}, body={"ty": 1.6, "sx": 1.25, "sy": 0.8125, "rz": 14.0}, mouth={"sx": 1.375}),
     {"ease": "vector"}),
    (5, _sc(whole={"tx": -1.6}, body={"ty": 1.4, "sx": 1.0625, "sy": 0.9375, "rz": 6.0})))
# Scream: draws itself in (A_2A, held), then screams, stretched tall, mouth wide,
# shaking (A_2B, loop)
_MOA["A_2A"] = _once(20,
    (0, _sc(body={"ty": 1.0})),
    (14, _sc(body={"ty": 0.6, "sx": 0.875, "sy": 0.875}, eyes={"sy": 0.5}, mouth={"sx": 0.625, "sy": 0.75})),
    (20, _sc(body={"ty": 0.6, "sx": 0.875, "sy": 0.8125}, eyes={"sy": 0.4375}, mouth={"sx": 0.625, "sy": 0.75})))
_YELL = _sc(body={"ty": 1.6, "sx": 0.9375, "sy": 1.25, "rz": -6.0}, eyes={"sy": 1.25, "ty": 0.6},
            mouth={"sx": 2.0, "sy": 1.75})
_MOA["A_2B"] = _shake(6, _sc(_YELL, whole={"tx": 0.3}), _sc(_YELL, whole={"tx": -0.3}, body={"sy": 1.3125}))
# Shade: spreads itself wide and flat overhead as a canopy (A_3, loop)
_SHADE = _sc(body={"ty": 4.0, "sx": 1.375, "sy": 0.6875}, eyes={"sy": 0.75}, feet={"ty": 3.4, "rz": 20.0})
_MOA["A_3"] = _loop(40, (0, _SHADE), (20, _sc(_SHADE, body={"ty": 4.6, "sx": 1.4375, "sy": 0.6875})))
# Float (field): rises high, hanging in the air, bobbing slowly (F_1, loop)
_MOA["F_1"] = _float(60, high=5.0, bob=1.0, paddle=6.0)
# -- story --
# Float carrying Luigi (F_2, a 30-frame loop): straining upward, stretched
# tall, feet kicking (Luigi hangs below: his SL_HANG)
_MOA["F_2"] = _loop(30,
    (0, _sc(body={"ty": 4.0, "sy": 1.1875, "sx": 0.875}, eyes={"sy": 0.6875}, mouth={"sx": 0.75},
            foot_f={"ty": 3.0, "rz": 20.0}, foot_b={"ty": 3.0, "rz": -10.0})),
    (15, _sc(body={"ty": 4.8, "sy": 1.125, "sx": 0.9375}, eyes={"sy": 0.6875}, mouth={"sx": 0.75},
             foot_f={"ty": 3.6, "rz": -10.0}, foot_b={"ty": 3.6, "rz": 20.0})))
# Look (F_3, a 40-frame loop): eyes wide, peering this way and that
_MOA["F_3"] = _loop(40,
    (0, _sc(body={"ty": 0.4, "rz": 6.0}, eyes={"sx": 1.25, "sy": 1.375, "tx": -0.4})),
    (20, _sc(body={"ty": 0.8, "rz": -6.0}, eyes={"sx": 1.25, "sy": 1.375, "tx": 0.4})))
# a sigh (E_1, 40): deflates, eyes drooping, and fills out again
_MOA["E_1"] = _once(40,
    (0, {}),
    (12, _sc(body={"ty": 0.4, "sy": 1.0625}, eyes={"sy": 0.75})),
    (26, _sc(body={"ty": -0.6, "sy": 0.8125, "sx": 1.125}, eyes={"sy": 0.4375, "ty": -0.4}, mouth={"sx": 1.25})),
    (40, {}))

# Screamy floats: the stand, idle and talk too (in place of the disc's, which
# stand on the ground; as his own), and every pose but the ones that put him
# down (KO, asleep, worn out, frozen, tumbling) hovers HOVER above the ground,
# feet dangling, his shadow left on the ground
HOVER = 3.0
_MOA["Z_1"] = _once(0, (0, _sc()))
_MOA["S_1"] = _loop(120,
    (0, _sc(body={"ty": 0.0, "sy": 1.0}, eyes={"sy": 1.0}, feet={"rz": 4.0})),
    (30, _sc(body={"ty": 0.7, "sy": 1.0625, "sx": 0.9375}, feet={"rz": 8.0, "ty": 0.3})),
    (60, _sc(body={"ty": 0.9, "sy": 1.0, "rz": 1.5}, feet={"rz": 2.0, "ty": 0.4})),
    (64, _sc(body={"ty": 0.9, "rz": 1.5}, eyes={"sy": 0.25}, feet={"rz": 2.0, "ty": 0.4})),
    (68, _sc(body={"ty": 0.85, "rz": 1.2}, eyes={"sy": 1.0}, feet={"rz": 2.0, "ty": 0.4})),
    (90, _sc(body={"ty": 0.3, "sy": 0.9375, "sx": 1.0625, "rz": -1.0}, feet={"rz": -2.0})))
_MOA["T_1"] = _loop(16,
    (0, _sc(body={"ty": 0.3}, mouth={"sx": 1.0})),
    (4, _sc(body={"ty": 0.6, "rz": 2.5, "sy": 1.0625}, mouth={"sx": 1.375, "sy": 1.0625})),
    (8, _sc(body={"ty": 0.8, "rz": 3.5, "sy": 1.0625}, mouth={"sx": 1.5, "sy": 1.0625})),
    (12, _sc(body={"ty": 0.6, "rz": 2.0}, mouth={"sx": 1.25})))
for _n, _m in _MOA.items():
    if _n in ("D_2", "D_3", "D_4", "D_5", "N_1", "W_2", "R_2"):
        continue
    _keys = []
    for _f, _p, *_o in _m["keys"]:
        _p = {g: dict(c) for g, c in _p.items()}  # (poses are shared between keys)
        for _g in ("locator3", "locator2", "locator1"):
            _p.setdefault(_g, {})
            _p[_g]["ty"] = _p[_g].get("ty", 0.0) + HOVER
        _keys.append((_f, _p, *_o))
    _m["keys"] = _keys
FRIENDS["c_lp_moa"] = _MOA


# ------------------------------------------------------------------- tools ----

def build(model, root=None, names=None):
    """the model's motions as Actions on the model built by ttyd_blender.build_model"""
    import bpy
    root = root or bpy.data.objects[model]
    return LM.build_all(FRIENDS[model], names, root)


def anims(model, root=None, names=None):
    """[(ttyd_anim.Anim, writer report)] for the model's motions"""
    import bpy
    root = root or bpy.data.objects[model]
    return LM.anims(FRIENDS[model], names, root)
