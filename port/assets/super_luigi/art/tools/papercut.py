"""Paper Cut's TTYD-style texture kit: builds a part as an SVG (the editable
source) and exports the PNG the models use, following ../STYLE.md.

A part is a fixed-size texture (both sides multiples of 8, like the game's
CMPR textures). Shapes are closed point lists, smoothed (Catmull-Rom) with a
small seeded jitter for the game's hand-cut edge, and drawn in layers:

  outline   the shape grown by `grow` px and again shifted by `bias` (light
            from the top left, so the line is heavier on the bottom right),
  fill      base colour, with a hard light crescent on the top left and a
            hard shadow crescent on the bottom right (cel bands, no blur),
  zones     regions of the shape in another colour scheme (a belly), whose
            outline is that scheme's line colour too,
  details   flat marks and interior lines, clipped to the shape,
  inner     the line's inner half, `inner` px into the shape.

Export: rendered by resvg, then alpha made 1-bit (CMPR keeps no partial
alpha) with colour un-premultiplied, so edges are hard and the anti-aliasing
lives between the line and the fill, as in the game's textures.
"""
import io
import math
import random

import resvg_py
from PIL import Image

# STYLE R6b highlight: white overlay strength, gap from the line (px), edge softness (blur px)
HL_ALPHA = 0.30
HL_GAP = 2.5
HL_EDGE = 0.7
# a default sheen window (bbox fractions): upper left of centre, as measured on Luigi's cap / Peach
HL_DEFAULT = (0.42, 0.26, 0.26, 0.17)

# ---------------------------------------------------------------- geometry --


def path_bbox(d):
    import re
    v = [float(t) for t in re.findall(r"-?\d+(?:\.\d+)?", d)]
    xs, ys = v[0::2], v[1::2]
    return min(xs), min(ys), max(xs), max(ys)


def smooth_path(pts, closed=True, jitter=0.0, seed=0, tension=1.0):
    """Catmull-Rom through pts as cubic Beziers (SVG path d)."""
    rnd = random.Random(seed)
    p = [(x + rnd.uniform(-jitter, jitter), y + rnd.uniform(-jitter, jitter)) for x, y in pts]
    n = len(p)
    if n < 3:
        return "M%.2f %.2fL%.2f %.2f" % (*p[0], *p[1])
    d = ["M%.2f %.2f" % p[0]]
    rng = range(n) if closed else range(n - 1)
    for i in rng:
        p0 = p[(i - 1) % n] if (closed or i > 0) else p[i]
        p1 = p[i]
        p2 = p[(i + 1) % n]
        p3 = p[(i + 2) % n] if (closed or i + 2 < n) else p2
        t = tension / 6.0
        c1 = (p1[0] + (p2[0] - p0[0]) * t, p1[1] + (p2[1] - p0[1]) * t)
        c2 = (p2[0] - (p3[0] - p1[0]) * t, p2[1] - (p3[1] - p1[1]) * t)
        d.append("C%.2f %.2f %.2f %.2f %.2f %.2f" % (*c1, *c2, *p2))
    if closed:
        d.append("Z")
    return "".join(d)


def poly_path(pts, closed=True):
    d = "M" + "L".join("%.2f %.2f" % pt for pt in pts)
    return d + ("Z" if closed else "")


def ellipse_pts(cx, cy, rx, ry, n=16, rot=0.0, wobble=0.0, seed=0):
    rnd = random.Random(seed)
    out = []
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    for i in range(n):
        a = 2 * math.pi * i / n
        k = 1 + rnd.uniform(-wobble, wobble)
        x, y = rx * math.cos(a) * k, ry * math.sin(a) * k
        out.append((cx + x * c - y * s, cy + x * s + y * c))
    return out


def star_pts(cx, cy, r_out, r_in, n=5, rot=-90):
    out = []
    for i in range(2 * n):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(rot + 180 * i / n)
        out.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return out


# ------------------------------------------------------------------- parts --

class Part:
    def __init__(self, name, w, h, grow=1.5, inner=2.0, bias=(1.5, 1.5)):
        assert w % 8 == 0 and h % 8 == 0, f"{name}: texture sides must be multiples of 8"
        self.name, self.w, self.h = name, w, h
        self.grow, self.inner, self.bias = grow, inner, bias
        self.defs, self.body, self._id = [], [], 0
        self.notes = {}

    def _nid(self, base):
        self._id += 1
        return f"{base}{self._id}"

    def shape(self, d, fill, line, light=None, shadow=None, light_off=(4, 4), shadow_off=(5, 5),
              zones=(), details=(), grow=None, inner=None, bias=None, outline=True, soft=0,
              hl=None, hl_alpha=None):
        """One outlined piece. d: path data. zones: [(zone_d, fill, line, light, shadow)] where
        zone_d is a region (may extend beyond the shape). details: raw SVG strings drawn inside."""
        hl_alpha = HL_ALPHA if hl_alpha is None else hl_alpha
        g = self.grow if grow is None else grow
        inn = self.inner if inner is None else inner
        bx, by = self.bias if bias is None else bias
        cid = self._nid("clip")
        self.defs.append(f'<clipPath id="{cid}"><path d="{d}"/></clipPath>')
        out = []
        sw = 2 * g

        def outer(colour, clip=None):
            cp = f' clip-path="url(#{clip})"' if clip else ""
            return (f'<g{cp}><path d="{d}" transform="translate({bx} {by})" fill="{colour}" stroke="{colour}" '
                    f'stroke-width="{sw}" stroke-linejoin="round"/>'
                    f'<path d="{d}" fill="{colour}" stroke="{colour}" stroke-width="{sw}" stroke-linejoin="round"/></g>')

        blur = ""
        if soft:
            fid = self._nid("soft")
            self.defs.append(f'<filter id="{fid}" x="-50%" y="-50%" width="200%" height="200%">'
                             f'<feGaussianBlur stdDeviation="{soft}"/></filter>')
            blur = f' filter="url(#{fid})"'

        def crescents(fillc, lightc, shadowc):
            s = [f'<path d="{d}" fill="{fillc}"/>']
            if lightc:
                lx, ly = light_off
                s.append(f'<g{blur}><path d="{d} {self._shift(d, lx, ly)}" fill="{lightc}" fill-rule="evenodd" '
                         f'clip-path="url(#{cid})"/></g>')
            if shadowc:
                sx, sy = shadow_off
                s.append(f'<g{blur}><path d="{d} {self._shift(d, -sx, -sy)}" fill="{shadowc}" '
                         f'fill-rule="evenodd" clip-path="url(#{cid})"/></g>')
            return s

        if outline:
            out.append(outer(line))
        zone_clips = []
        for z in zones:
            zd, zf, zl = z[0], z[1], z[2]
            zcid = self._nid("zone")
            self.defs.append(f'<clipPath id="{zcid}"><path d="{zd}"/></clipPath>')
            zone_clips.append((zcid, z))
            if outline:
                out.append(outer(zl, zcid))
        inside = crescents(fill, light, shadow)
        for zcid, z in zone_clips:
            zf, zli, zsh = z[1], (z[3] if len(z) > 3 else None), (z[4] if len(z) > 4 else None)
            inside.append(f'<g clip-path="url(#{zcid})">' + "".join(crescents(zf, zli, zsh)) + "</g>")
        if hl:
            inside.append(self._highlight(d, hl, inn, hl_alpha))
        inside += list(details)
        if outline:
            inside.append(f'<path d="{d}" fill="none" stroke="{line}" stroke-width="{2 * inn}"/>')
            for zcid, z in zone_clips:
                inside.append(f'<path d="{d}" fill="none" stroke="{z[2]}" stroke-width="{2 * inn}" '
                              f'clip-path="url(#{zcid})"/>')
        out.append(f'<g clip-path="url(#{cid})">' + "".join(inside) + "</g>")
        self.body.append("".join(out))
        return self

    def _highlight(self, d, hl, inn, alpha):
        """STYLE R6b: a crisp lighter sheen, white at `alpha` over the fill, an oval window
        (fractions of the shape's bbox: cx, cy, rx, ry[, rot]) cut by the shape and kept
        HL_GAP px inside the line."""
        x0, y0, x1, y1 = path_bbox(d)
        W, H = x1 - x0, y1 - y0
        cx, cy, rx, ry = x0 + hl[0] * W, y0 + hl[1] * H, hl[2] * W, hl[3] * H
        rot = hl[4] if len(hl) > 4 else 0
        mid, fid = self._nid("hlmask"), self._nid("hlsoft")
        self.defs.append(f'<mask id="{mid}" maskUnits="userSpaceOnUse" x="0" y="0" width="{self.w}" '
                         f'height="{self.h}"><path d="{d}" fill="#fff" stroke="#000" '
                         f'stroke-width="{2 * (inn + HL_GAP)}"/></mask>')
        self.defs.append(f'<filter id="{fid}" x="-20%" y="-20%" width="140%" height="140%">'
                         f'<feGaussianBlur stdDeviation="{HL_EDGE}"/></filter>')
        e = smooth_path(ellipse_pts(cx, cy, rx, ry, 14, rot=rot, wobble=0.04, seed=int(cx + cy)), jitter=0.3)
        return (f'<g mask="url(#{mid})"><path d="{e}" fill="#ffffff" fill-opacity="{alpha}" '
                f'filter="url(#{fid})"/></g>')

    @staticmethod
    def _shift(d, dx, dy):
        # a translated copy of the path as a second subpath (for evenodd crescents)
        import re
        nums = re.findall(r"[A-Za-z]|-?\d+(?:\.\d+)?", d)
        out, axis = [], 0
        for t in nums:
            if t.isalpha():
                out.append(t); axis = 0
            else:
                v = float(t) + (dx if axis == 0 else dy)
                axis ^= 1
                out.append("%.2f" % v)
        return " ".join(out)

    def raw(self, svg):
        self.body.append(svg)
        return self

    def stroke(self, d, colour, width, cap="round"):
        return self.raw(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{width}" '
                        f'stroke-linecap="{cap}" stroke-linejoin="round"/>')

    def svg(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" shape-rendering="geometricPrecision">\n'
                f'<!-- Paper Cut / Super Luigi art: {self.name} ({self.w}x{self.h}); see STYLE.md -->\n'
                f'<defs>{"".join(self.defs)}</defs>\n' + "\n".join(self.body) + "\n</svg>\n")

    def render(self):
        png = resvg_py.svg_to_bytes(svg_string=self.svg())
        im = Image.open(io.BytesIO(bytes(png))).convert("RGBA")
        return harden(im)

    def save(self, folder):
        import os
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, self.name + ".svg"), "w", encoding="utf-8") as f:
            f.write(self.svg())
        im = self.render()
        im.save(os.path.join(folder, self.name + ".png"))
        return im


def clean(folder):
    """Remove a generator's output (cards, parts.json, preview) before a rebuild, keeping the
    hand-written README.md and anything else that isn't generated."""
    import glob
    import os
    for pat in ("*.png", "*.svg", "parts.json"):
        for f in glob.glob(os.path.join(folder, pat)):
            os.remove(f)


def harden(im, threshold=128):
    """1-bit alpha (as CMPR), colour un-premultiplied; transparent texels black."""
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255) if a >= threshold else (0, 0, 0, 0)
    return im


def stroke_svg(d, colour, width, cap="round"):
    return (f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{width}" '
            f'stroke-linecap="{cap}" stroke-linejoin="round"/>')


def fill_svg(d, colour, rule="nonzero"):
    return f'<path d="{d}" fill="{colour}" fill-rule="{rule}"/>'


# ------------------------------------------------------- model-unit cards --

class Rig:
    """A character's cards drawn in model units (8 texels/unit, y up, facing -x); collects parts.json."""

    def __init__(self, model, out, unit=8):
        self.model, self.out, self.unit, self.meta = model, out, unit, {}

    def card(self, name, rect, **kw):
        return ModelCard(self, name, rect, **kw)

    def write_json(self, extra=None):
        import json
        import os
        os.makedirs(self.out, exist_ok=True)
        data = {"model": self.model, "texels_per_unit": self.unit, "faces": "-x",
                "pivot_note": "pivot: pixels from the PNG's top-left; model_xy: where the pivot sits at rest "
                              "(model units, feet at y 0)", "cards": self.meta}
        data.update(extra or {})
        with open(os.path.join(self.out, "parts.json"), "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1)


class ModelCard:
    def __init__(self, rig, name, rect, grow=1.8, inner=2.2, bias=(1.6, 1.8)):
        x0, y0, x1, y1 = rect
        U = rig.unit
        self.rig, self.name, self.x0, self.y1, self.U = rig, name, x0, y1, U
        self.w, self.h = round((x1 - x0) * U), round((y1 - y0) * U)
        assert self.w % 8 == 0 and self.h % 8 == 0, (name, self.w, self.h)
        self.p = Part(name, self.w, self.h, grow=grow, inner=inner, bias=bias)

    def u(self, x, y):
        return ((x - self.x0) * self.U, (self.y1 - y) * self.U)

    def pts(self, pts):
        return [self.u(x, y) for x, y in pts]

    def d(self, pts, tension=0.5, jitter=0.5, seed=1):
        return smooth_path(self.pts(pts), tension=tension, jitter=jitter, seed=seed)

    def line(self, pts):
        return "M" + " L".join("%.1f %.1f" % self.u(x, y) for x, y in pts)

    def curve(self, a, b, c):
        (ax, ay), (bx, by), (cx, cy) = self.u(*a), self.u(*b), self.u(*c)
        return f"M{ax:.1f} {ay:.1f} Q{bx:.1f} {by:.1f} {cx:.1f} {cy:.1f}"

    def ell(self, cx, cy, rx, ry, n=12, rot=0.0, wobble=0.0, seed=0):
        x, y = self.u(cx, cy)
        return smooth_path(ellipse_pts(x, y, rx * self.U, ry * self.U, n, rot=rot, wobble=wobble, seed=seed),
                           tension=0.5)

    def shape(self, pts, tone, details=(), hl=None, soft=3, tension=0.5, seed=1, jitter=0.5, lo=(6, 6), so=(8, 8),
              d=None, **kw):
        self.p.shape(d or self.d(pts, tension, jitter, seed), tone["fill"], tone["line"], tone.get("light"),
                     tone.get("shadow"), lo, so, details=details, soft=soft, hl=hl, **kw)

    def save(self, pivot_xy, desc):
        px, py = self.u(*pivot_xy)
        self.rig.meta[self.name] = {"size": [self.w, self.h], "pivot": [round(px, 1), round(py, 1)],
                                    "model_xy": [round(pivot_xy[0], 2), round(pivot_xy[1], 2)], "desc": desc}
        return self.p.save(self.rig.out)


def assemble(out, meta, order, size, origin, overrides=None, scale=1.0, bg=(236, 232, 222, 255)):
    """Paste cards at rest (their pivots at model_xy) onto a canvas; origin = canvas px of model (0, 0).
    overrides: {slot_name: card_name} to swap a card in place (same pivot spot)."""
    import os
    from PIL import Image
    c = Image.new("RGBA", size, bg)
    for name in order:
        file = (overrides or {}).get(name, name)
        im = Image.open(os.path.join(out, file + ".png")).convert("RGBA")
        px, py = meta[file]["pivot"]
        mx, my = meta[name]["model_xy"]
        if scale != 1.0:
            im = im.resize((round(im.width * scale), round(im.height * scale)), Image.NEAREST)
        x = origin[0] + mx * 8 * scale - px * scale
        y = origin[1] - my * 8 * scale - py * scale
        tmp = Image.new("RGBA", size, (0, 0, 0, 0))
        tmp.paste(im, (round(x), round(y)), im)
        c.alpha_composite(tmp)
    return c
