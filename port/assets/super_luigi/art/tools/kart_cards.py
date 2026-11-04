"""Big Green 01's cards from the style model's kart (after `python cutout.py cut_kart`): `python kart_cards.py`.

Takes kart/qwen/ (cut_kart.py's cards) into kart/, where sl_kart.py reads them:
- the near wheels, the steering wheel, the engine, as cut;
- wheel_far: the near front wheel a tone darker (the far side, in shade);
- kart_body, kart_nose (whole and wrecked) and kart_seat in each team's paint: the design's green tones (and the green
  line round them) taken through the paint's line, shadow, base and light by their lightness, so the shading and the
  airbrush stay; the white pods, the grey seat, the tyres and the scorch keep their own colours. Green is Big Green 01's
  own (Luigi's cap green), so every kart goes through the same ramp;
- parts.json: those cards' size, pivot and model point, the kart's other cards (the plates, flame, puff and smoke,
  kart.py's) as they were.
"""
import colorsys
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "..", "tools", "port"))
from ink import cmpr_round_trip, rgb, save  # noqa: E402

KART = os.path.join(HERE, "..", "kart")
QWEN = os.path.join(KART, "qwen")

# line, shadow, base, light (kart.py's PAINTS; green is Luigi's cap's)
PAINTS = {
    "green": ("#12380f", "#24822a", "#3caa39", "#7fd66c"),
    "red": ("#3e0a06", "#9c2018", "#d93a30", "#f2786a"),
    "blue": ("#0c1a44", "#22449c", "#3a6ad8", "#7aa0f0"),
    "yellow": ("#4a3606", "#b88a18", "#f0c43a", "#fae38a"),
    "purple": ("#2a0e3a", "#6a2c8a", "#9a4ac0", "#c486e0"),
    "orange": ("#4a2206", "#b0581a", "#ee8a2a", "#f8b872"),
    "white": ("#34323e", "#b6b4c0", "#e8e7e2", "#ffffff"),
    "black": ("#0c0c10", "#24242c", "#3c3c46", "#686876"),
}
COLOURS = ["red", "blue", "yellow", "purple", "orange", "white", "black"]
LUM = np.array([0.299, 0.587, 0.114], dtype=np.float32)


def green_mask(a):
    """the paint: green-hued texels with some colour (the line round it too), not the white, grey or scorch"""
    f = a[..., :3].astype(np.float32) / 255
    mx, mn = f.max(-1), f.min(-1)
    s = (mx - mn) / np.maximum(mx, 1e-3)
    g = (f[..., 1] >= f[..., 0]) & (f[..., 1] >= f[..., 2])
    hue = np.zeros(mx.shape, np.float32)
    d = np.maximum(mx - mn, 1e-3)
    hue[g] = (60 * ((f[..., 2] - f[..., 0]) / d + 2))[g]
    return (a[..., 3] > 0) & g & (s > 0.28) & (hue > 75) & (hue < 170)


def paint(a, colour, ref):
    """the green texels through the paint's ramp; ref = (line lum, base lum) of the design's green"""
    out = a.copy()
    m = green_mask(a)
    if not m.any():
        return out
    lum = a[..., :3].astype(np.float32) @ LUM
    lline, lbase = ref
    stops = [rgb(c) for c in PAINTS[colour]]
    # lightness to a place on the ramp: line 0, shadow 1, base 2, light 3
    t = np.where(lum <= lbase, 2.0 * (lum - lline) / max(1.0, lbase - lline), 2.0 + (lum - lbase) / max(1.0, 255 * 0.92 - lbase))
    t = np.clip(t, 0, 3)
    i = np.minimum(t.astype(int), 2)
    f = (t - i)[..., None]
    S = np.stack(stops)
    col = S[i] * (1 - f) + S[i + 1] * f
    out[..., :3][m] = np.clip(col[m] + 0.5, 0, 255).astype(np.uint8)
    return out


def ref_tones(a):
    m = green_mask(a)
    lum = a[..., :3].astype(np.float32) @ LUM
    v = lum[m]
    return float(np.percentile(v, 3)), float(np.percentile(v, 60))


def whole_tyre(img, pivot):
    """the tyre's top, which the rail hides in the design (the body's green and white there), from its own bottom:
    each such texel of the outer ring above the axle takes the texel mirrored below it"""
    h, w = img.shape[:2]
    cx, cy = pivot
    y, x = np.mgrid[:h, :w]
    r = np.hypot(x + 0.5 - cx, y + 0.5 - cy)
    R = r[img[..., 3] > 0].max()
    f = img[..., :3].astype(np.float32)
    lum = f @ LUM
    sat = (f.max(-1) - f.min(-1)) / np.maximum(f.max(-1), 1)
    body = green_mask(img) | ((lum > 170) & (sat < 0.15))
    fix = body & (r > 0.62 * R) & (y + 0.5 < cy) & (img[..., 3] > 0)
    my = np.clip(np.round(2 * cy - 1 - y).astype(int), 0, h - 1)
    out = img.copy()
    out[fix] = img[my[fix], x[fix]]
    # past the tyre's own line at its sides: the body's green, cut away
    nub = green_mask(out) & (r > 0.9 * R) & ((out[..., :3].astype(np.float32) @ LUM) > 45)
    out[nub] = 0
    return out


def load(n, qdir=QWEN):
    return np.array(Image.open(os.path.join(qdir, n + ".png")).convert("RGBA"))


LINE = "#12280f"  # cut_kart.py's
SOOT = "#2a2622"


def _rim(img, k=1):
    """a line again round the outline, where a bite took it away (k texels wide: one game texel at the density)"""
    op = img[..., 3] > 0
    inner = op.copy()
    for _ in range(k):
        p = np.pad(inner, 1)
        inner = inner & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    rim = op & ~inner
    lum = img[..., :3].astype(np.float32) @ LUM
    img[rim & (lum > 60), :3] = rgb(LINE).astype(np.uint8)


def wreck(img, pivot, design_y, bites, soot, cracks, seed, bend=None, scorch=()):
    """the crash: bites out of the top edge (dents), soot splats, cracks in the line's colour, the fender bent forward.
    Points are in the design's pixels (cut_kart.py's), taken to the card by its pivot: (x, y) design -> texels. Sizes
    are in the game's texels, k = the cutter's DENSITY texels each, so the crash looks the same at any density."""
    import cut_kart as C
    from cutout import DENSITY as k
    rng = np.random.default_rng(seed)
    px, py = pivot
    to = lambda p: ((p[0] - design_y[0]) / C.PX * k + px, (p[1] - design_y[1]) / C.PX * k + py)
    out = img.copy()
    h, w = out.shape[:2]
    yy, xx = np.mgrid[:h, :w]
    if bend:  # rows above the hinge lean forward (-x), more the higher they are
        hy = to((0, bend[0]))[1]
        src = out.copy()
        out[:] = 0
        for y in range(h):
            dx = int(round(bend[1] * max(0.0, hy - y)))
            if dx:
                out[y, :w - dx] = src[y, dx:]
            else:
                out[y] = src[y]
    for x, ry in bites:  # a dent: a bite out of the edge, from above
        col = int(round(to((x, 0))[0]))
        rows = np.nonzero(out[:, max(0, col), 3] > 0)[0]
        if not len(rows):
            continue
        top = rows[0]
        out[((xx - col) / (3.2 * k)) ** 2 + ((yy - (top - k)) / (ry * k)) ** 2 < 1] = 0
    op = out[..., 3] > 0
    f = out[..., :3].astype(np.float32)
    for x, y, r in scorch:  # broad burns: the paint darkened round each, most at the middle (r in texels)
        cx, cy = to((x, y))
        r *= k
        f *= 1.0 - 0.6 * np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (r * r))[..., None]
    out[..., :3] = f.astype(np.uint8)
    for x, y, r in soot:  # a splat: a blob of circles and a few drops round it, the paint dimmed under it
        cx, cy = to((x, y))
        r *= k
        m = np.zeros((h, w), bool)
        for _ in range(7):
            ox, oy = rng.normal(0, r * 0.45, 2)
            rr = r * rng.uniform(0.35, 0.7)
            m |= (xx - cx - ox) ** 2 + (yy - cy - oy) ** 2 < rr * rr
        for _ in range(5):
            a, d = rng.uniform(0, 2 * np.pi), r * rng.uniform(1.1, 1.8)
            m |= (xx - cx - d * np.cos(a)) ** 2 + (yy - cy - d * np.sin(a)) ** 2 < (rng.uniform(0.6, 1.5) * k) ** 2
        m &= op
        f = out[..., :3].astype(np.float32)
        f[m] = rgb(SOOT) * rng.uniform(0.85, 1.15, (int(m.sum()), 1))  # neutral, so no paint's ramp takes it
        out[..., :3] = f.astype(np.uint8)
    for pts in cracks:  # a crack: a jagged stroke, one game texel (k texels) wide
        q = [to(p) for p in pts]
        for (x0, y0), (x1, y1) in zip(q, q[1:]):
            n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
            for t in np.linspace(0, 1, n + 1):
                x, y = int(round(x0 + (x1 - x0) * t - k / 2 + 0.5)), int(round(y0 + (y1 - y0) * t - k / 2 + 0.5))
                for yy0 in range(max(0, y), min(h, y + k)):
                    for xx0 in range(max(0, x), min(w, x + k)):
                        if out[yy0, xx0, 3]:
                            out[yy0, xx0, :3] = rgb(LINE).astype(np.uint8)
    _rim(out, k)
    return out


# the wrecks (design pixels, as cut_kart.py's points): the side pod dented along its rim and scorched, a crack across
# the white; the nose's fender bent forward, its rail scorched and cracked
BODY_WRECK = dict(bites=[(500, 7.0), (590, 6.0), (680, 8.0), (760, 7.5), (860, 6.0), (930, 8.5)],
                  soot=[(800, 560, 12.0), (940, 590, 10.0), (520, 660, 9.0), (660, 520, 8.0), (600, 620, 7.0),
                        (880, 660, 8.0), (720, 600, 6.0)],
                  cracks=[[(700, 520), (720, 548), (706, 566), (730, 600)], [(820, 640), (850, 660), (842, 680)],
                          [(560, 560), (590, 590), (575, 620), (600, 650)]], seed=13,
                  scorch=[(820, 580, 22.0), (560, 640, 18.0)])
NOSE_WRECK = dict(bites=[(260, 5.0), (300, 6.0), (380, 6.0)],
                  soot=[(120, 630, 11.0), (360, 600, 9.0), (240, 430, 7.0), (200, 560, 7.0), (300, 660, 6.0)],
                  cracks=[[(90, 600), (120, 620), (110, 650)], [(220, 470), (250, 492)], [(320, 560), (340, 590)]],
                  seed=14, bend=(530, 0.12), scorch=[(150, 610, 20.0), (330, 520, 14.0)])


# the rival karts and the wrecks (seen at race distance) at half the cutter's density, their cards named <card>_lo;
# Luigi's own green kart (c_lp_kart, c_luigi_kart, its crash binds) at the full density (main's call, wave 3)
LO_DENSITY = 2
QWEN_LO = os.path.join(KART, "qwen_lo")


def cut_lo():
    """cut_kart.py's cards again at LO_DENSITY, into kart/qwen_lo/"""
    import cutout
    import cut_kart as C
    keep = (cutout.DENSITY, cutout.TEXELS_PER_UNIT, C.OUT)
    cutout.DENSITY, cutout.TEXELS_PER_UNIT, C.OUT = LO_DENSITY, 8 * LO_DENSITY, "kart/qwen_lo"
    try:
        cutout.main("cut_kart")
    finally:
        cutout.DENSITY, cutout.TEXELS_PER_UNIT, C.OUT = keep


def main():
    pj = os.path.join(KART, "parts.json")
    meta = json.load(open(pj))
    cards = meta["cards"]
    make(cards, QWEN, "")
    import cutout
    keep = cutout.DENSITY
    cut_lo()
    cutout.DENSITY = LO_DENSITY  # the wreck's sizes, in game texels, at this density
    try:
        make(cards, QWEN_LO, "_lo", lo=True)
    finally:
        cutout.DENSITY = keep
    meta["source"] = "kart.py (plates, flame, puff, smoke); cut_kart.py and kart_cards.py (the rest)"
    json.dump(meta, open(pj, "w"), indent=1)
    preview()
    print("kart: %d cards" % len(cards))


def make(cards, qdir, sfx, lo=False):
    """every card from one cut (qdir), named <card><sfx>; lo: not Luigi's whole green body and nose"""
    qmeta = json.load(open(os.path.join(qdir, "parts.json")))
    q, tpu = qmeta["cards"], qmeta.get("texels_per_unit", 8)
    load_ = lambda n: load(n, qdir)

    def put(name, img, like, pivot=None):
        name += sfx
        img = cmpr_round_trip(img)
        save(img, os.path.join(KART, name + ".png"))
        c = q[like]
        # the folder's 8 is the drawn cards' (the plates, flame, puff, smoke); these say the cutter's own density
        cards[name] = {"size": [img.shape[1], img.shape[0]], "pivot": pivot or c["pivot"], "model_xy": c["model_xy"],
                       "texels_per_unit": tpu, "desc": c["desc"] + ("" if name == like else " (%s)" % name)}
        return cards[name]
        svg = os.path.join(KART, name + ".svg")
        if os.path.exists(svg):  # the drawn card it replaces
            os.remove(svg)

    for n in ("wheel_near_front", "wheel_near_back", "steering_wheel", "kart_engine"):
        img = load_(n)
        if n.startswith("wheel"):
            piv = q[n]["pivot"]
            img = whole_tyre(img, piv)
            put(n, img, n, piv)
            if n == "wheel_near_front":
                far = img.astype(np.float32)
                far[..., :3] *= 0.72
                put("wheel_far", far.astype(np.uint8), n, piv)
            continue
        put(n, img, n)
    cards["wheel_far" + sfx]["desc"] = "the far wheels: the near front wheel in shade"
    ref = ref_tones(load_("kart_body"))
    import cut_kart as C
    for base in ("kart_body", "kart_nose", "kart_seat"):
        src = load_(base)
        if not lo or base == "kart_seat":  # the wreck's seat is the green one
            put(base, paint(src, "green", ref), base)
        for col in COLOURS:
            put("%s_%s" % (base, col), paint(src, col, ref), base)
        if base == "kart_seat":
            continue
        spec = BODY_WRECK if base == "kart_body" else NOSE_WRECK
        bad = wreck(src, q[base]["pivot"], C.CARDS[base]["pivot"], **spec)
        put(base + "_wrecked", paint(bad, "green", ref), base)["desc"] = q[base]["desc"] + (
            "; wrecked: dented, scorched, cracked" + (", the fender bent" if base == "kart_nose" else ""))
        for col in COLOURS:
            put("%s_wrecked_%s" % (base, col), paint(bad, col, ref), base)


def preview():
    """preview.png: every team's kart as sl_kart.py lays its cards, then the green wreck and the red one"""
    import poster_karts as P
    rows = [P.assemble(c, tpu=16) for c in [None] + COLOURS] + [P.assemble(None, wrecked=True, tpu=16), P.assemble("red", wrecked=True, tpu=16)]
    w, h = max(r.width for r in rows), max(r.height for r in rows)
    sheet = Image.new("RGBA", (w * 2 + 24, (h + 8) * 5), (236, 232, 222, 255))
    for i, r in enumerate(rows):
        sheet.alpha_composite(r, ((i % 2) * (w + 24), (i // 2) * (h + 8)))
    sheet.convert("RGB").save(os.path.join(KART, "preview.png"))


if __name__ == "__main__":
    main()
