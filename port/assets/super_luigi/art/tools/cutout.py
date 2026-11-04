"""Cards cut from a style-model design (art/designs/<who>/), the model's own pixels kept: `python cutout.py SPEC`.

SPEC is a module beside this one (cut_crepe.py, ...) holding:
  SRC      the design image (art/designs/...)
  OUT      the folder the cards go in (art/<who>/gen)
  MODEL    the model's name
  PX       design pixels per texel (8 texels a unit)
  DENSITY  optional: the spec's own density (as SL_CUT_DENSITY; = PX keeps one texel a sheet pixel)
  ORIGIN   the design point that is the model's (0, 0): between the feet, on the ground
  LINE     the outline colour: the design's black is recoloured to it, keeping its own light and dark
  LINE_DARK optional: the value below which the design's dark is line (78; lower for a character whose fill is dark)
  SPECKS   optional: opaque islands smaller than this fraction of a card's largest are dropped (stray strokes)
  HOLES    optional: [(x, y)] points inside the figure's closed holes (a ring's middle), flooded as the ground
  COLOURS  optional: each card's fills held to this many colours (median cut)
  LINE_GROW optional: game texels the outline is thickened by, outward (a card may set its own `line_grow`)
  CARDS    {name: dict(poly=[(x, y), ...] in design pixels, the part's outline, cut where it meets others;
                       pivot=(x, y) where it turns; at=(x, y) where that pivot sits at rest (default the pivot);
                       rotate=degrees about the pivot (an arm drawn raised, hung down); flip=False;
                       patch=[(poly, colour), ...] painted over the design first (what another part covered);
                       erase=[poly, ...] cut away; air=0.35 the airbrush's strength; rim=False: no line where it
                       was cut (an overlay); src=, scale=: another design, sized to this one; desc="...")}
  BINDS    [[name, ...]]: cards that swap on one part, made one size with their pivots on one texel
  ANCHORS  {name: (x, y)}: design points the rig needs as model points (a waist locator, ...)
  EXTRA    optional: {name: function() -> RGBA array} cards painted in (what the design lacks), each with
           EXTRA_META[name] = dict(pivot=(texels), at=(design x, y), desc)

What it does to the design: the background goes (flooded from the edges); dark lines become the warm LINE; each card
is cut, patched, turned, taken down to texels with 1-bit alpha, given a line where it was cut, a soft airbrush toward
its lower right rim (the game's volume over the model's flat fills), and a CMPR round trip. Nothing reads the disc.
"""
import importlib
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "..", "tools", "port"))
from ink import cmpr_round_trip, rgb, save  # noqa: E402

ART = os.path.join(HERE, "..")
# Texel density, shared by every card cut from a design (characters and props): DENSITY times the game's 8 texels a
# unit. The disc's 8 is the GameCube's budget; on the PC the model's own sheet pixels are kept (4: about one sheet pixel
# a texel), so a card stays sharp at the window's full size. A spec's PX stays in the game's terms (sheet pixels per
# 1/8 unit); parts.json's texels_per_unit says what was cut, and sl_rig.py reads it.
DENSITY = int(os.environ.get("SL_CUT_DENSITY", "4"))
TEXELS_PER_UNIT = 8 * DENSITY
MAX_TEXELS = 1024


def figure(img, thresh=34, holes=()):
    """the design's figure as RGBA: the background flooded from the border (and from `holes`, points in its closed
    holes)"""
    im = img.convert("RGB")
    w, h = im.size
    probe = im.copy()
    marker = (255, 0, 255)
    for x, y in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1), (w // 2, 0), (w // 2, h - 1), (0, h // 2), (w - 1, h // 2)] + list(holes):
        if probe.getpixel((x, y)) != marker:
            ImageDraw.floodfill(probe, (x, y), marker, thresh=thresh)
    a = np.array(probe)
    bg = (a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 255)
    alpha = Image.fromarray(((~bg) * 255).astype(np.uint8))
    # a hair of erosion and a close: no halo of the grey ground, no pinholes
    alpha = alpha.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return out


def warm_line(rgba, line, dark=78):
    """the design's black outline and lines in the character's line colour, their own shading kept"""
    a = np.array(rgba).astype(np.float32)
    lum = a[..., :3] @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
    t = np.clip((dark - lum) / 30.0, 0, 1)[..., None]  # 1 for true black, fading out by `dark`
    tgt = rgb(line) * (0.75 + 0.5 * (lum[..., None] / max(1.0, dark)))
    a[..., :3] = a[..., :3] * (1 - t) + tgt * t
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")


def _poly_mask(size, pts):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon([tuple(p) for p in pts], fill=255)
    return m


def drop_specks(op, frac):
    """the opaque islands of a card smaller than frac of its largest dropped (stray strokes beside a piece)"""
    m = Image.fromarray((op * 255).astype(np.uint8)).copy()
    sizes, masks = [], []
    while True:
        a = np.asarray(m)
        i = int(np.argmax(a == 255))
        if a.flat[i] != 255:
            break
        ImageDraw.floodfill(m, (i % a.shape[1], i // a.shape[1]), 100)
        b = np.asarray(m) == 100
        sizes.append(int(b.sum()))
        masks.append(b)
        m = Image.fromarray(np.where(b, 50, np.asarray(m)).astype(np.uint8)).copy()
    keep = np.zeros_like(op)
    for n, b in zip(sizes, masks):
        if n >= frac * max(sizes):
            keep |= b
    return keep


def cut(fig, spec, px, line, air=0.35, line_grow=0, specks=0.0):
    """one card: (RGBA texels, pivot in texels)"""
    w, h = fig.size
    src = fig.copy()
    for poly, colour in spec.get("patch", []):
        m = _poly_mask((w, h), poly)
        layer = Image.new("RGBA", (w, h), tuple(int(v) for v in rgb(colour)) + (255,))
        src = Image.composite(layer, src, m)
    mask = _poly_mask((w, h), spec["poly"])
    for poly in spec.get("erase", []):
        mask = ImageChops.subtract(mask, _poly_mask((w, h), poly))
    a = ImageChops.multiply(mask, src.getchannel("A"))
    src.putalpha(a)
    px0, py0 = spec["pivot"]
    rot = spec.get("rotate", 0.0)
    if rot:  # on a canvas grown on every side, so nothing turned past the sheet's edge is lost
        m = max(w, h) // 2
        big = Image.new("RGBA", (w + 2 * m, h + 2 * m), (0, 0, 0, 0))
        big.paste(src, (m, m))
        src, px0, py0 = big, px0 + m, py0 + m
        src = src.rotate(rot, resample=Image.BICUBIC, center=(px0, py0))
    if spec.get("flip"):
        src = src.transpose(Image.FLIP_LEFT_RIGHT)
        px0 = src.size[0] - 1 - px0
    bb = src.getchannel("A").point(lambda v: 255 if v > 127 else 0).getbbox()
    k = DENSITY  # widths below were tuned in the game's texels
    pad = int(px * 2 * k)
    x0, y0, x1, y1 = bb[0] - pad, bb[1] - pad, bb[2] + pad, bb[3] + pad
    tw, th = math.ceil((x1 - x0) / px / 8) * 8, math.ceil((y1 - y0) / px / 8) * 8
    # centre the texel grid on the cut, then take it down with alpha-weighted averaging
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    x0, y0 = cx - tw * px / 2, cy - th * px / 2
    region = src.transform((int(round(tw * px)), int(round(th * px))), Image.AFFINE, (1, 0, x0, 0, 1, y0), resample=Image.BILINEAR)
    arr = np.array(region).astype(np.float32)
    al = arr[..., 3:4] / 255
    prem = Image.fromarray(np.clip(np.concatenate([arr[..., :3] * al, arr[..., 3:4]], -1), 0, 255).astype(np.uint8), "RGBA")
    small = np.array(prem.resize((tw, th), Image.BOX)).astype(np.float32)
    a = small[..., 3] / 255
    col = small[..., :3] / np.maximum(a[..., None], 1e-3)
    op = a >= 0.5
    if specks:
        op = drop_specks(op, specks)
    # the airbrush: darkening toward the lower right rim, as the game's volume falls
    if air:
        m = Image.fromarray((op * 255).astype(np.uint8))
        off = ImageChops.offset(m, -3 * k, -4 * k).filter(ImageFilter.GaussianBlur(5 * k))
        s = (1 - np.asarray(off, dtype=np.float32) / 255) * op
        col = col * (1 - air * s[..., None] * 0.6)
    # a line where it was cut: any rim texel that isn't dark already (one game texel wide, and a half-tone one inside)
    lum = col @ np.array([0.299, 0.587, 0.114], dtype=np.float32)

    def grow(m, n):
        for _ in range(n):
            p = np.pad(m, 1)
            m = m | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
        return m
    L = rgb(line)
    if line_grow:  # the outline thickened outward by whole game texels (a design whose line comes out thin at its PX)
        wide = grow(op, line_grow * k)
        col[wide & ~op] = L
        op = wide
        lum = col @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
    rim = op & grow(~op, k)
    rim2 = op & grow(rim, k) & ~rim
    if spec.get("rim", True):  # off for an overlay (a face's eyes or mouth) whose cut edge lies on the head
        col[rim & (lum > 70)] = L
        col[rim2 & (lum > 110)] = L * 0.5 + col[rim2 & (lum > 110)] * 0.5
    out = np.zeros((th, tw, 4), dtype=np.uint8)
    out[..., :3] = np.clip(col + 0.5, 0, 255).astype(np.uint8) * op[..., None]
    out[..., 3] = op * 255
    return out, ((px0 - x0) / px, (py0 - y0) / px)


def main(specname):
    global DENSITY, TEXELS_PER_UNIT
    S = importlib.import_module(specname)
    if getattr(S, "DENSITY", 0) and "SL_CUT_DENSITY" not in os.environ:  # a spec cut finer: one texel a sheet pixel at its PX
        DENSITY = int(S.DENSITY)
        TEXELS_PER_UNIT = 8 * DENSITY
    fig = warm_line(figure(Image.open(os.path.join(ART, S.SRC)), getattr(S, "THRESH", 34), getattr(S, "HOLES", ())), S.LINE, getattr(S, "LINE_DARK", 78))
    os.makedirs(os.path.join(ART, S.OUT), exist_ok=True)
    ox, oy = S.ORIGIN
    unit = S.PX * 8.0
    to_model = lambda p: [round((p[0] - ox) / unit, 3), round((oy - p[1]) / unit, 3)]
    cards = {}
    figs = {}

    def source(spec):
        """the card's design (SRC, or its own `src`), scaled by its `scale` to the main design's size"""
        key = (spec.get("src", S.SRC), spec.get("scale", 1.0))
        if key not in figs:
            f = fig if key[0] == S.SRC else warm_line(figure(Image.open(os.path.join(ART, key[0])), getattr(S, "THRESH", 34), getattr(S, "HOLES", ())), S.LINE,
                                                         getattr(S, "LINE_DARK", 78))
            if key[1] != 1.0:
                f = f.resize((round(f.size[0] * key[1]), round(f.size[1] * key[1])), Image.LANCZOS)
            figs[key] = f
        return figs[key]

    for name, spec in S.CARDS.items():
        k = spec.get("scale", 1.0)
        if k != 1.0:  # the card's own points are in its source's pixels
            spec = dict(spec, poly=[(x * k, y * k) for x, y in spec["poly"]], pivot=(spec["pivot"][0] * k, spec["pivot"][1] * k),
                        patch=[([(x * k, y * k) for x, y in p], c) for p, c in spec.get("patch", [])],
                        erase=[[(x * k, y * k) for x, y in p] for p in spec.get("erase", [])])
        img, piv = cut(source(spec), spec, S.PX / DENSITY, S.LINE, spec.get("air", 0.35), spec.get("line_grow", getattr(S, "LINE_GROW", 0)),
                       spec.get("specks", getattr(S, "SPECKS", 0.0)))
        cards[name] = {"img": img, "pivot": list(piv), "model_xy": to_model(spec.get("at", spec["pivot"])), "desc": spec.get("desc", "")}
    # a group's cards swap on one part (binds): one size, their pivots on the same texel
    for group in getattr(S, "BINDS", []):
        left = max(math.ceil(cards[n]["pivot"][0]) for n in group)
        top = max(math.ceil(cards[n]["pivot"][1]) for n in group)
        right = max(cards[n]["img"].shape[1] - math.floor(cards[n]["pivot"][0]) for n in group)
        bottom = max(cards[n]["img"].shape[0] - math.floor(cards[n]["pivot"][1]) for n in group)
        w, h = math.ceil((left + right) / 8) * 8, math.ceil((top + bottom) / 8) * 8
        for n in group:
            c = cards[n]
            dx, dy = left - math.ceil(c["pivot"][0]), top - math.ceil(c["pivot"][1])
            can = np.zeros((h, w, 4), dtype=np.uint8)
            ih, iw = c["img"].shape[:2]
            can[dy:dy + ih, dx:dx + iw] = c["img"][:h - dy, :w - dx]
            c["img"], c["pivot"] = can, [c["pivot"][0] + dx, c["pivot"][1] + dy]
    # the GameCube's largest texture is 1024 texels a side: a card (or a bound group) larger than that at the density
    # is cut at half of it (a giant's cards; its parts.json entry says its own texels_per_unit)
    grouped = {n for g in getattr(S, "BINDS", []) for n in g}
    for group in list(getattr(S, "BINDS", [])) + [[n] for n in cards if n not in grouped]:
        f = 1
        while max(max(cards[n]["img"].shape[:2]) for n in group) > MAX_TEXELS * f:
            f *= 2
        for n in group if f > 1 else ():
            c = cards[n]
            h, w = c["img"].shape[:2]
            im = Image.fromarray(c["img"]).resize((math.ceil(w / f), math.ceil(h / f)), Image.BOX)
            a = np.zeros((math.ceil(h / f / 8) * 8, math.ceil(w / f / 8) * 8, 4), dtype=np.uint8)
            a[:im.height, :im.width] = np.array(im)
            a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
            a[..., :3] *= (a[..., 3:4] > 0)
            c["img"], c["pivot"], c["texels_per_unit"] = a, [v / f for v in c["pivot"]], TEXELS_PER_UNIT // f
    for name, c in cards.items():
        img = c.pop("img")
        if getattr(S, "COLOURS", 0):  # fills held to a few colours, as the game's flat cards (its own colours kept)
            q = np.array(Image.fromarray(img[..., :3]).quantize(S.COLOURS, method=Image.MEDIANCUT).convert("RGB"))
            img = np.concatenate([q * (img[..., 3:4] > 0), img[..., 3:4]], -1).astype(np.uint8)
        img = cmpr_round_trip(img)
        save(img, os.path.join(ART, S.OUT, name + ".png"))
        c["size"] = [img.shape[1], img.shape[0]]
        c["pivot"] = [round(c["pivot"][0], 2), round(c["pivot"][1], 2)]
    for name, fn in getattr(S, "EXTRA", {}).items():
        meta = S.EXTRA_META[name]
        img = fn()  # painted at the game's 8 texels a unit: brought up to the density
        if DENSITY != 1:
            img = np.array(Image.fromarray(img).resize((img.shape[1] * DENSITY, img.shape[0] * DENSITY), Image.LANCZOS))
            img[..., 3] = np.where(img[..., 3] >= 128, 255, 0)
        img = cmpr_round_trip(img)
        save(img, os.path.join(ART, S.OUT, name + ".png"))
        cards[name] = {"size": [img.shape[1], img.shape[0]], "pivot": [v * DENSITY for v in meta["pivot"]], "model_xy": to_model(meta["at"]),
                       "desc": meta.get("desc", "")}
    anchors = {k: to_model(v) for k, v in getattr(S, "ANCHORS", {}).items()}
    json.dump({"model": S.MODEL, "texels_per_unit": TEXELS_PER_UNIT, "source": S.SRC, "cards": cards, "anchors": anchors},
              open(os.path.join(ART, S.OUT, "parts.json"), "w"), indent=1)
    fig.save(os.path.join(HERE, "..", "..", "..", "..", "..", "build-art", "fig_%s.png" % S.MODEL))
    print("%s: %d cards" % (S.MODEL, len(cards)))


if __name__ == "__main__":
    main(sys.argv[1])
