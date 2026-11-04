"""A paper character from the art agent's cards (port/assets/super_luigi/art/<who>/:
a PNG per card and parts.json with each card's pivot in texels and where it
sits in the model, `model_xy`), as a paper_model model with motions, for
sl_models.py (docs/LUIGI_ANIMS.md, "Super Luigi's models"). Used by
sl_crepe.py, sl_eclair.py, sl_chestnut.py and sl_cranberry.py."""
import json
import os

import paper_model as PM

PX = 8.0
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "port", "assets", "super_luigi", "art")


class Rig:
    """layout: [(name, parent, card or None, z, [bind cards], visible, at)]: `at`
    the model point the card's pivot sits on (default the art's model_xy); a
    locator's `at` is its pivot. The first entry is the root."""

    def __init__(self, name, art, layout, motions, camera=None, poses=None):
        self.NAME, self.art, self.layout = name, art, layout
        self.MOTIONS = motions
        self.CAMERA = camera or {"height": 32.0, "width": 40.0, "center": (0.0, -2.0), "res": (320, 256)}
        self.POSES = poses or {}

    def meta(self):
        return json.load(open(os.path.join(ART, self.art, "parts.json")))["cards"]

    def texels_per_unit(self):
        """8 for the drawn art (the game's own); a card cut from a design says its own (art/tools/cutout.py, DENSITY)"""
        return float(json.load(open(os.path.join(ART, self.art, "parts.json"))).get("texels_per_unit", PX))

    def img(self, card):
        return PM.pad8(PM.read_png(os.path.join(ART, self.art, card + ".png")))

    def parts(self):
        meta = self.meta()
        sheet = self.texels_per_unit()
        out = []
        for name, parent, card, z, binds, vis, at in self.layout:
            if card is None:
                out.append(PM.Part(name, parent, None, None, z, at or (0.0, 0.0), (), vis))
                continue
            m = meta[card]
            tpu = float(m.get("texels_per_unit", sheet))  # a giant's large card may be cut at half the density
            px, py = m["pivot"]
            w, h = m["size"]
            ax, ay = at if at is not None else m["model_xy"]
            x0, y1 = ax - px / tpu, ay + py / tpu
            rect = (x0, y1 - h / tpu, x0 + w / tpu, y1)
            out.append(PM.Part(name, parent, self.img(card), rect, z, (ax, ay), [self.img(b) for b in binds], vis))
        return out

    def build(self, anims=()):
        anims = list(anims)
        if not any(x.name == "Z_1" for x in anims):
            anims.insert(0, PM.still("Z_1"))
        return PM.build(self.NAME, self.parts(), anims)


# ------------------------------------------------------------ pose helpers ----

def P(*ds):
    """poses merged, later ones winning per channel"""
    out = {}
    for d in ds:
        for g, ch in (d or {}).items():
            out.setdefault(g, {}).update(ch)
    return out


def loop(end, poses, **kw):
    """a looping motion through key poses [(frame, pose[, opts])]"""
    m = {"end": end, "loop": True, "keys": list(poses)}
    m.update(kw)
    return m


def once(end, poses, **kw):
    m = {"end": end, "keys": list(poses)}
    m.update(kw)
    return m


def blinks(group, frames, shut=1, open_=0, length=4):
    """a "tex" track: the group's drawing `shut` for `length` frames at each of `frames`"""
    pts = [(0, open_, "step")]
    for f in frames:
        pts += [(f, shut, "step"), (f + length, open_, "step")]
    return {group: {"tex": pts}}


def talk(group, end, every=6, talk_tex=2, rest_tex=0):
    """a "tex" track opening and closing the mouth through `end` frames"""
    pts, f, open_ = [(0, rest_tex, "step")], every, True
    while f < end:
        pts.append((f, talk_tex if open_ else rest_tex, "step"))
        open_ = not open_
        f += every
    return {group: {"tex": pts}}


def merge_tracks(*ts):
    out = {}
    for t in ts:
        for g, ch in t.items():
            out.setdefault(g, {}).update(ch)
    return out
