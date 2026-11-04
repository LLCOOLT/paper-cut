"""The race posters' kart panels (ART_PLAN wave 3 row 4): Big Green 01's own cards, small, in place of the Bill Blaster.

  python poster_karts.py            rewrites the kart pieces of recipes/model_letters.recipe's poster images

The posters stay the recipes' (the Glitz Pit's own posters, recomposited from the disc). Only each `piece cN model
c_killtai 1` block that drew the kart (a glossy barrel turned red or green, scaled to 52 x 28) becomes a `paint` of the
kart as the game draws it: its cards (art/kart/, sl_kart.py's card list and draw order) laid together in that kart's
paint and plate, taken down to the panel's 52 x 28 with 1-bit alpha, in at most 24 colours, and given a one-texel line
round its outline (at a sixth of the size, the cards' own line is thinner than a texel). Nothing here reads the disc.
"""
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools", "port"))
import sl_kart as K  # noqa: E402

RECIPE = os.path.join(HERE, "..", "recipes", "model_letters.recipe")
W, H = 52, 28
NAMES = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
# the poster's hue turn of the barrel -> the kart it now shows (paint, plate)
KARTS = {"hue =0 sat =0.8": ("red", "13"), "hue =115 sat =0.7": (None, "01")}
LINES = {"red": (62, 10, 6), None: (18, 56, 15)}


def assemble(colour, wrecked=False, tpu=8):
    """the kart's cards laid together at tpu texels a unit (8: the game's), RGBA (wrecked: the crash's body and nose, no
    number); a card cut denser is taken down by the box of its texels (premultiplied), its alpha cut at half"""
    meta = K._meta()
    x0s, y0s, x1s, y1s = [], [], [], []
    rects = []
    for name, card, z, at, vis in sorted(K.CARDS, key=lambda r: r[2]):
        if not vis:
            continue
        src = card
        if colour and card in ("kart_body", "kart_nose", "kart_seat"):
            src = "%s_%s" % (card, colour)
        if wrecked and card in ("kart_body", "kart_nose"):
            src = card + "_wrecked" + ("_" + colour if colour else "")
        if card == "plate_01":
            src = "plate_blank" if wrecked else "plate_" + K.PLATE_NUMBER.get(colour, "01")
        (x0, y0, x1, y1), _ = K.card_rect(meta, card, at)
        rects.append((src, x0, y0, x1, y1))
        x0s.append(x0), y0s.append(y0), x1s.append(x1), y1s.append(y1)
    X0, Y1 = min(x0s), max(y1s)
    out = Image.new("RGBA", (round((max(x1s) - X0) * tpu), round((Y1 - min(y0s)) * tpu)), (0, 0, 0, 0))
    for src, x0, y0, x1, y1 in rects:
        im = Image.open(os.path.join(K.ART, src + ".png")).convert("RGBA")
        size = (round((x1 - x0) * tpu), round((y1 - y0) * tpu))
        if size[0] < im.width:
            im = im.convert("RGBa").resize(size, Image.BOX).convert("RGBA")
            im.putalpha(im.getchannel("A").point(lambda v: 255 if v >= 128 else 0))
        else:
            im = im.resize(size, Image.NEAREST)
        out.alpha_composite(im, (round((x0 - X0) * tpu), round((Y1 - y1) * tpu)))
    return out


def small(colour):
    """the kart at the panel's size: (RGB array, opaque mask), its line round it"""
    big = assemble(colour)
    bb = big.getchannel("A").getbbox()
    big = big.crop(bb)
    k = min((W - 2) / big.width, (H - 2) / big.height)
    w, h = max(1, round(big.width * k)), max(1, round(big.height * k))
    a = np.array(big).astype(np.float32)
    al = a[..., 3:4] / 255
    prem = Image.fromarray(np.concatenate([a[..., :3] * al, a[..., 3:4]], -1).astype(np.uint8), "RGBA")
    s = np.array(prem.resize((w, h), Image.BOX)).astype(np.float32)
    op = s[..., 3] >= 128
    col = s[..., :3] / np.maximum(s[..., 3:4] / 255, 1e-3)
    can = np.zeros((H, W, 3), np.float32)
    m = np.zeros((H, W), bool)
    ox, oy = (W - w) // 2, H - 1 - h  # sat on the panel's floor, a texel up
    can[oy:oy + h, ox:ox + w] = col
    m[oy:oy + h, ox:ox + w] = op
    p = np.pad(m, 1)
    rim = m & ~(p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:])
    can[rim] = LINES[colour]
    return np.clip(can + 0.5, 0, 255).astype(np.uint8), m


def paint_lines(piece, colour):
    rgb, m = small(colour)
    q = Image.fromarray(rgb).quantize(24, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = np.array(q)
    pal = q.getpalette()[:72]
    used = sorted(set(idx[m].tolist()))
    name = {u: NAMES[i] for i, u in enumerate(used)}
    cols = " ".join("%s #%02x%02x%02x" % (name[u], pal[3 * u], pal[3 * u + 1], pal[3 * u + 2]) for u in used)
    rows = ["  | " + "".join(name[idx[y, x]] if m[y, x] else "_" for x in range(W)) for y in range(H)]
    return ["  piece %s canvas %d %d" % (piece, W, H), "  colours " + cols, "  @%s paint 0 0" % piece] + rows


def main():
    text = open(RECIPE).read().split("\n")
    out, i, n = [], 0, 0
    pat = re.compile(r"^  piece (c\d+) model c_killtai 1$")
    # a panel this script already painted: its note, then the piece; painted again from the kart's cards as they are now
    own = re.compile(r"^  # Big Green 01's cards \(art/tools/poster_karts\.py\): the (\w+) kart$")
    while i < len(text):
        mt = pat.match(text[i])
        mo = own.match(text[i]) if not mt else None
        if not mt and not mo:
            out.append(text[i])
            i += 1
            continue
        if mo:
            piece = re.match(r"^  piece (c\d+) ", text[i + 1]).group(1)
            kart = next(v for v in KARTS.values() if (v[0] or "green") == mo.group(1))
        j = i
        if mt:
            piece = mt.group(1)
        while not text[j].startswith("  paste %s " % piece):
            j += 1
        if mt:
            block = "\n".join(text[i:j])
            kart = next(v for key, v in KARTS.items() if key in block)
        out += ["  # Big Green 01's cards (art/tools/poster_karts.py): the %s kart" % (kart[0] or "green")]
        out += paint_lines(piece, kart[0])
        i = j  # the paste stays
        n += 1
    open(RECIPE, "w").write("\n".join(out))
    for c in KARTS.values():
        rgb, m = small(c[0])
        im = np.concatenate([rgb, (m * 255).astype(np.uint8)[..., None]], -1)
        Image.fromarray(im, "RGBA").resize((W * 8, H * 8), Image.NEAREST).save(
            os.path.join(ROOT, "build-art", "poster_kart_%s.png" % (c[0] or "green")))
    print("posters: %d kart panels" % n)


if __name__ == "__main__":
    main()
