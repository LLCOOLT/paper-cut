#!/usr/bin/env python3
"""The Paper Mario 64 partners' own TTYD models (docs/PM64_LINK.md, "Their own models"): the animations a partner
needs in the field and in battle, as recipes (tools/port/anim_recipe.py) on the disc's models of them.

The Thousand-Year Door's disc has a paper model of each of the eight (c_kurio Goombario, c_kameki Kooper,
c_pinky Bombette, c_paretta Parakarry, c_resaresa Bow, c_akarin Watt, c_opuku Sushie, c_pokopi Lakilester), made
for cameos: each stands (S_1) and talks (T_1), and little else. Every other move is made here from those, with
offsets of ours on the model's own groups (a hop, a lean, a squash, a spin), its timing taken from the nearest TTYD
species' own animation of that move (a Goomba's walk for Goombario's, a Koopa's for Kooper's, a Boo's for Bow's:
DONORS, frame counts only). Our numbers only: the game makes the models from the player's disc at startup
(pm64_partner_pc.c, anim_recipe_pc.c) and nothing of the disc is in the repository.

The names, the same for all eight (pm64_partner_pc.c's modelAnim picks one by what the partner is doing):

  S_1 stand, T_1 talk (the disc's)      W_1 walk, R_1 run       J_1 jump up, F_1 fall
  D_1 hurt, D_2 worn out (low HP), D_3 knocked out              X_1 dizzy, N_1 asleep, Z_2 stopped (still)
  G_1 guard, Y_1 glad / victory         A_1 its move (field and battle), A_2 its second move

  pm64_partner_motions.py write       the eight recipes (port/assets/luigi/pm64_<name>_recipe.json) and their C
                                      tables (port/src/common/pm64_<name>_recipe.inc)
  pm64_partner_motions.py check       fails if either is out of date
Then anim_attach.py check --picks pm64_goombario,... (no part comes off) and char_sheet.py for a look.
"""
import json
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# each partner's model and its groups by role: whole (the top group: turns about the feet), up (the body over the
# feet), feet (left, right: lifted in turn on a walk), extra (one more part that moves on its own: Bombette's key,
# Lakilester's cloud), floats (no feet on the ground: walks are a glide and a bob)
PARTNERS = {
    "goombario": {"model": "c_kurio", "whole": "|locator1", "up": "pPlane3", "feet": ("pPlane6", "pPlane5"),
                  "floats": False, "height": 13.0},
    "kooper": {"model": "c_kameki", "whole": "|locator1", "up": "locator3", "feet": ("locator5", "locator4"),
               "floats": False, "height": 20.0},
    "bombette": {"model": "c_pinky", "whole": "|locator1", "up": "pPlane7", "feet": ("locator5", "locator4"),
                 "extra": "locator3", "floats": False, "height": 15.0},
    "parakarry": {"model": "c_paretta", "whole": "|locator1", "up": "locator2", "feet": (),
                  "floats": True, "height": 23.0, "own": ("W_1", "R_1")},
    "bow": {"model": "c_resaresa", "whole": "|locator1", "up": "pPlane14", "feet": (), "floats": True,
            "height": 14.0},
    "watt": {"model": "c_akarin", "whole": "|locator1", "up": "locator3", "feet": (), "floats": True,
             "height": 15.0},
    "sushie": {"model": "c_opuku", "whole": "|locator3", "up": "locator1", "feet": (), "floats": True,
               "height": 16.0},
    "lakilester": {"model": "c_pokopi", "whole": "|locator1", "up": "pPlane2", "feet": (), "extra": "locator3",
                   "floats": True, "height": 25.0},
}

# the nearest TTYD species' frame counts for each move (their disc animations' lengths: c_kuribo KUR_*, c_nokoteki
# NKT_* and PTP_*, c_bomhey BOM_*, c_teresa, c_bubble, c_puku, c_jugemu JGM_*); a move a species hasn't got: ours
DONORS = {
    "goombario": {"W": 40, "R": 16, "D": 8, "N": 60, "Y": 8, "A": 30, "X": 40, "K": 40},   # Goomba
    "kooper": {"W": 40, "R": 20, "D": 10, "N": 120, "Y": 10, "A": 40, "X": 40, "K": 40},    # Koopa Troopa
    "bombette": {"W": 40, "R": 20, "D": 20, "N": 40, "Y": 20, "A": 40, "X": 40, "K": 40},   # Bob-omb
    "parakarry": {"W": 30, "R": 14, "D": 10, "N": 30, "Y": 10, "A": 40, "X": 30, "K": 40},  # Paratroopa
    "bow": {"W": 10, "R": 8, "D": 8, "N": 120, "Y": 24, "A": 12, "X": 80, "K": 32},          # Boo
    "watt": {"W": 20, "R": 20, "D": 8, "N": 120, "Y": 24, "A": 20, "X": 120, "K": 32},       # Ember
    "sushie": {"W": 24, "R": 12, "D": 10, "N": 60, "Y": 20, "A": 24, "X": 40, "K": 40},      # Cheep Cheep
    "lakilester": {"W": 30, "R": 20, "D": 12, "N": 60, "Y": 12, "A": 40, "X": 60, "K": 60},  # Lakitu
}


# The whole body follows the partner's own PM64 sprite at the game (pm64_partner_pc.c, "moved as the sprite moves":
# where its drawing is, how it leans, squashes and stretches, frame by frame at PM64's timing, measured on the
# player's ROM), so the recipes leave the whole group alone but in these: what a flat sprite can't tell (lying
# knocked out, held still, Kooper's shell turning edge on, Watt's bolts alone). The other tracks (feet, a key, a
# cloud, the body over the feet) stay, and play at the sprite's own rate.
KEEP_WHOLE = {
    "*": ("D_3", "Z_2"),
    "kooper": ("D_3", "Z_2", "A_1"),
    "watt": ("D_3", "Z_2", "A_2"),
}


def wave(length, amp, cycles=1.0, base=0.0, phase=0.0, rect=False, lift=False, steps=8):
    """a loop's sine (or |sine|, rect: a hop; lift: its upper half only, a foot raised then set down) of amp about
    base over length frames, cycles times: points"""
    pts = []
    n = max(2, int(round(steps * cycles)))
    for i in range(n + 1):
        t = length * i / n
        s = math.sin(2 * math.pi * (cycles * i / n) + phase)
        v = base + amp * (abs(s) if rect else max(s, 0.0) if lift else s)
        pts.append([round(t, 3), round(v, 4)])
    return pts


def keys(*pairs, ease=None):
    """[(frame, value), ...] as points (ease: "linear" or "step" on all)"""
    return [[float(t), float(v)] + ([ease] if ease else []) for t, v in pairs]


def jitter(length, amp, every=2):
    """a shake: +amp, -amp every few frames, linear"""
    pts, i, t = [], 0, 0
    while t <= length:
        pts.append([float(t), amp if i % 2 == 0 else -amp, "linear"])
        t += every
        i += 1
    pts[-1][1] = 0.0
    return pts


class Maker:
    def __init__(self, name):
        self.name = name
        self.p = PARTNERS[name]
        self.d = DONORS[name]
        self.anims = {}

    def add(self, anim, src, end, loop, tracks, rng=None, turns=None):
        spec = {"from": src}
        if rng is not None:
            spec["range"] = list(rng)
        add = {}
        driven = anim not in KEEP_WHOLE.get(self.name, KEEP_WHOLE["*"])
        for group, chan, pts in tracks:
            if group is None or not pts:
                continue
            if driven and group == self.p["whole"]:
                continue  # (the whole body moves as the PM64 sprite does, at the game: pm64_partner_pc.c)
            add.setdefault(group, {})[chan] = pts
        if add:
            spec["add"] = add
        spec["end"] = float(end)
        spec["loop"] = bool(loop)
        if turns is not None:
            spec["turns"] = turns
        self.anims[anim] = spec

    def still(self, src="S_1", frame=0):
        return src, (frame, frame)

    def make(self):
        p, d = self.p, self.d
        W, U = p["whole"], p["up"]
        h = p["height"]
        fl = p["floats"]
        feet = p["feet"]
        own = p.get("own", ())
        talk_open = None  # (T_1's frame with the mouth open: its middle)

        # walk and run: a hop a step on the feet (a glide and a bob for a floater), a lean forwards (the models
        # face -x, so forwards is +rz)
        if "W_1" not in own:
            n = d["W"]
            if fl:
                tr = [(W, "ty", wave(n, 1.0, 1, base=1.0)), (W, "rz", wave(n, 2.0, 1, base=5.0, phase=1.0))]
                self.add("W_1", "S_1", n, True, tr)
            else:
                tr = [(W, "ty", wave(n, 1.6, 2, rect=True)), (W, "rz", wave(n, 3.0, 1, base=2.0)),
                      (U, "sy", wave(n, 0.02, 2, base=1.0, phase=math.pi / 2))]
                if len(feet) == 2:
                    # (a foot raised no further than the body lets it: anim_attach.py)
                    tr += [(feet[0], "ty", wave(n, 0.8, 1, lift=True)),
                           (feet[1], "ty", wave(n, 0.8, 1, lift=True, phase=math.pi))]
                self.add("W_1", "S_1", n, True, tr)
        if "R_1" not in own:
            n = d["R"]
            if fl:
                tr = [(W, "ty", wave(n, 1.2, 1, base=1.5)), (W, "rz", wave(n, 2.0, 1, base=10.0, phase=1.0))]
            else:
                tr = [(W, "ty", wave(n, 2.6, 2, rect=True)), (W, "rz", wave(n, 2.0, 1, base=8.0)),
                      (U, "sy", wave(n, 0.02, 2, base=1.0, phase=math.pi / 2))]
                if len(feet) == 2:
                    tr += [(feet[0], "ty", wave(n, 0.9, 1, lift=True)),
                           (feet[1], "ty", wave(n, 0.9, 1, lift=True, phase=math.pi))]
            self.add("R_1", "S_1", n, True, tr)

        # jump up (a crouch, then stretched) and falling (stretched, the feet down)
        self.add("J_1", "S_1", 12, False, [
            (W, "sy", keys((0, 1.0), (3, 0.82), (6, 1.14), (12, 1.04))),
            (W, "sx", keys((0, 1.0), (3, 1.12), (6, 0.92), (12, 0.97))),
        ], rng=(0, 0))
        self.add("F_1", "S_1", 10, True, [
            (W, "sy", keys((0, 1.05), (5, 1.08), (10, 1.05))),
            (W, "rz", keys((0, -3), (5, -5), (10, -3))),
        ], rng=(0, 0))

        # hurt: thrown back (the top away from where it faces), squashed, shaking; T_1's open mouth
        nt = 14
        talk_open = (nt / 2, nt / 2)
        n = max(16, d["D"] * 2)
        self.add("D_1", "T_1", n, True, [
            (W, "rz", [[0.0, -14.0], [n / 2, -10.0], [float(n), -14.0]]),
            (W, "tx", jitter(n, 0.6)),
            (W, "sx", keys((0, 1.1), (n / 2, 1.06), (n, 1.1))),
            (W, "sy", keys((0, 0.9), (n / 2, 0.94), (n, 0.9))),
        ], rng=talk_open)
        # worn out: bent forwards, breathing slow and deep
        self.add("D_2", "S_1", 60, True, [
            (W, "rz", wave(60, 2.0, 1, base=9.0)),
            (U, "sy", wave(60, 0.05, 1, base=0.96)),
        ], rng=(0, 0))
        # knocked out: on its back, flat on the floor (turned about its feet, then lifted by its half width)
        lie = round(0.42 * h, 2)
        self.add("D_3", "S_1", 30, True, [
            (W, "rz", keys((0, -90), (30, -90))),
            (W, "ty", keys((0, lie), (30, lie))),
            (W, "sy", keys((0, 0.92), (30, 0.92))),
        ], rng=(0, 0))
        # stopped (frozen, a Stop): the stand's first frame, held
        self.add("Z_2", "S_1", 30, True, [], rng=(0, 0))

        # dizzy: swaying in circles; asleep: low, breathing, a nod
        n = d["X"]
        self.add("X_1", "S_1", n, True, [
            (W, "rz", wave(n, 9.0, 2)),
            (W, "tx", wave(n, 1.2, 2, phase=math.pi / 2)),
        ], rng=(0, 0))
        n = d["N"]
        self.add("N_1", "S_1", n, True, [
            (W, "sy", wave(n, 0.04, 1, base=0.93)),
            (W, "sx", wave(n, 0.02, 1, base=1.04, phase=math.pi)),
            (W, "rz", wave(n, 3.0, 1, base=-6.0)),
            (W, "ty", keys((0, -1.0 if fl else 0.0), (n, -1.0 if fl else 0.0))),
        ], rng=(0, 0))

        # guard: braced, crouched and leaning back
        self.add("G_1", "S_1", 20, True, [
            (W, "sy", keys((0, 0.86), (10, 0.88), (20, 0.86))),
            (W, "sx", keys((0, 1.08), (20, 1.08))),
            (W, "rz", keys((0, -7), (20, -7))),
        ], rng=(0, 0))

        # glad and victory: hopping, talking (the mouth open)
        n = max(24, d["Y"] * 3)
        self.add("Y_1", "T_1", n, True, [
            (W, "ty", wave(n, 5.0 if not fl else 3.0, 2, rect=True)),
            (W, "sy", wave(n, 0.06, 2, base=1.0, phase=math.pi / 2)),
        ], turns=None)

        self.moves()
        return self.anims

    def moves(self):
        """each partner's own two moves (A_1 its field ability and first attack, A_2 its second)"""
        p, d = self.p, self.d
        W, U = p["whole"], p["up"]
        n = d["A"]
        nm = self.name
        if nm == "goombario":
            # Headbonk: crouch, spring up head first, land; Tattle: a lean in, looking hard
            self.add("A_1", "S_1", 24, False, [
                (W, "sy", keys((0, 1.0), (6, 0.78), (10, 1.18), (18, 1.0), (24, 1.0))),
                (W, "sx", keys((0, 1.0), (6, 1.14), (10, 0.9), (18, 1.0), (24, 1.0))),
                (W, "rz", keys((0, 0), (6, 6), (10, 24), (18, 8), (24, 0))),
                (W, "ty", keys((0, 0), (6, 0), (12, 6), (20, 0), (24, 0))),
            ], rng=(0, 0))
            self.add("A_2", "T_1", n, True, [
                (W, "rz", wave(n, 2.0, 1, base=8.0)),
                (W, "tx", keys((0, -1.5), (n, -1.5))),
            ])
        elif nm == "kooper":
            # into his shell and spinning (the paper turned edge on and round, squashed down); a shell toss's wind-up
            self.add("A_1", "S_1", 12, True, [
                (W, "ry", keys((0, 0), (12, 360), ease="linear")),
                (W, "sy", keys((0, 0.62), (12, 0.62))),
                (W, "sx", keys((0, 1.1), (12, 1.1))),
            ], rng=(0, 0))
            self.add("A_2", "S_1", n, False, [
                (W, "rz", keys((0, 0), (n * 0.4, -12), (n * 0.6, 18), (n, 0))),
                (W, "sy", keys((0, 1.0), (n * 0.4, 0.9), (n * 0.6, 1.06), (n, 1.0))),
            ], rng=(0, 0))
        elif nm == "bombette":
            # lit: swelling and shaking, the key spinning fast; Body Slam: a run up and a dive
            ex = p.get("extra")
            self.add("A_1", "S_1", n, True, [
                (W, "sx", wave(n, 0.05, 4, base=1.1)),
                (W, "sy", wave(n, 0.05, 4, base=1.1, phase=math.pi)),
                (W, "tx", jitter(n, 0.5)),
                (ex, "rx", keys((0, 0), (n, 1440), ease="linear")),
            ], rng=(0, 0))
            self.add("A_2", "S_1", 20, False, [
                (W, "rz", keys((0, 0), (6, -10), (10, 40), (16, 30), (20, 0))),
                (W, "tx", keys((0, 0), (6, 1), (12, -8), (20, 0))),
            ], rng=(0, 0))
        elif nm == "parakarry":
            # carrying (flying hard, leant forward: his own flight); Sky Dive: up, then down headfirst
            self.add("A_1", "R_1", 30, True, [
                (W, "rz", keys((0, 12), (30, 12))),
                (W, "ty", wave(30, 1.0, 2, base=1.0)),
            ])
            self.add("A_2", "W_1", 24, False, [
                (W, "ty", keys((0, 0), (8, 6), (16, -2), (24, 0))),
                (W, "rz", keys((0, 0), (8, -10), (14, 50), (20, 20), (24, 0))),
            ])
        elif nm == "bow":
            # Smack: the fan swung back and through; Outta Sight / Spook: puffed up, turning a little
            self.add("A_1", "T_1", 16, True, [
                (W, "rz", keys((0, -16), (6, -22), (9, 20), (13, 10), (16, -16))),
                (W, "tx", keys((0, 1.0), (9, -2.0), (16, 1.0))),
            ])
            self.add("A_2", "T_2", n * 2, True, [
                (W, "sx", wave(n * 2, 0.08, 1, base=1.12)),
                (W, "sy", wave(n * 2, 0.08, 1, base=1.12, phase=math.pi)),
                (W, "ry", wave(n * 2, 12.0, 1)),
            ])
        elif nm == "watt":
            # straining to light up: shaking, swelling; the spark burst (his own B_1: the bolts, him hidden)
            self.add("A_1", "S_1", n, True, [
                (W, "sx", wave(n, 0.07, 3, base=1.1)),
                (W, "sy", wave(n, 0.07, 3, base=1.1, phase=math.pi / 2)),
                (W, "tx", jitter(n, 0.7)),
                (W, "rz", jitter(n, 4.0, every=3)),
            ])
            self.add("A_2", "B_1", 20, True, [])
        elif nm == "sushie":
            # swimming with Mario on her back (low, leant forward, a strong kick); Squirt: a lean back and a spit
            self.add("A_1", "S_1", n, True, [
                (W, "rz", wave(n, 3.0, 1, base=10.0)),
                (W, "ty", wave(n, 0.8, 2, base=-2.0)),
                (W, "sx", wave(n, 0.04, 2, base=1.05)),
            ])
            self.add("A_2", "T_1", 20, False, [
                (W, "rz", keys((0, 0), (8, -14), (12, 8), (20, 0))),
                (W, "sx", keys((0, 1.0), (8, 0.92), (12, 1.1), (20, 1.0))),
            ])
        elif nm == "lakilester":
            # straining (his ride over spikes and lava; Spiny Flip's wind-up): the cloud puffing, him leaning in;
            # the throw: back and over
            self.add("A_1", "S_1", 20, True, [
                (W, "tx", jitter(20, 0.5, every=2)),
                (W, "sx", wave(20, 0.03, 2, base=1.03)),
                (W, "sy", wave(20, 0.03, 2, base=0.98, phase=math.pi)),
            ])
            self.add("A_2", "S_1", n, False, [
                (W, "rz", keys((0, 0), (n * 0.4, -14), (n * 0.55, 20), (n, 0))),
                (W, "ty", keys((0, 0), (n * 0.4, 1.5), (n * 0.55, -1.0), (n, 0))),
            ], rng=(0, 0))


def recipe_of(name):
    p = PARTNERS[name]
    return {
        "_help": "Paper Mario 64's %s on the disc's own model of him or her (%s): the moves a partner needs, made "
                 "from the model's own animations by tools/port/anim_recipe.py at startup. Our numbers only. Made by "
                 "tools/port/pm64_partner_motions.py: don't edit." % (name.capitalize(), p["model"]),
        "model": p["model"],
        "anims": Maker(name).make(),
    }


def paths(name):
    return (os.path.join(REPO, "port", "assets", "luigi", "pm64_%s_recipe.json" % name),
            os.path.join(REPO, "port", "src", "common", "pm64_%s_recipe.inc" % name))


def symbol(name):
    return "kPm64%sRecipe" % name.capitalize()


def main(argv):
    cmd = argv[0] if argv else ""
    if cmd not in ("write", "check"):
        print(__doc__)
        return 2
    bad = 0
    for name in PARTNERS:
        js, inc = paths(name)
        text = json.dumps(recipe_of(name), indent=1) + "\n"
        try:
            old = open(js, encoding="utf-8").read()
        except OSError:
            old = None
        if cmd == "write":
            if old != text:
                open(js, "w", encoding="utf-8", newline="\n").write(text)
        elif old != text:
            print("%s out of date: run pm64_partner_motions.py write" % js)
            bad = 1
        r = subprocess.run([sys.executable, os.path.join(HERE, "anim_recipe.py"), "cinc", js, "--symbol",
                            symbol(name), "--out", inc] + (["--check"] if cmd == "check" else []))
        bad |= r.returncode != 0
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
