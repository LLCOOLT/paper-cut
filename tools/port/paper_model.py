"""New paper models of our own, in the game's format (docs/LUIGI_ANIMS.md,
"New models"). Pure Python, no dependencies.

A model here is a tree of parts, each a textured plane facing the camera (or
an empty mover), as the game's own NPC models are: every position is in the
model's space (y up, the character facing -x, front parts at +z), each part
turns and scales about its pivot. `build()` writes the model file (a/<name>)
and its textures (a/<name>-, a TPL of CMPR images, as the disc's), with any
animations (ttyd_anim.Anim) after it; ttyd_blender.build_model then edits it
like a disc model, and luigi_motions-style key poses animate it.

The drawings come from `Canvas`: flat colour shapes with a soft shade and a
thick dark outline, drawn from signed distances (circles, boxes, polygons,
unions), antialiased; `encode_cmpr` packs them.

  Part(name, parent, image=None, rect=(x0, y0, x1, y1), z=0.0, pivot=(x, y),
       binds=[more images], visible=True)
"""
import math
import struct
import time

import ttyd_anim as T

# --------------------------------------------------------------- drawing ----


def _clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def sd_circle(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def sd_ellipse(cx, cy, rx, ry):
    # an approximation good enough for drawing (exact on the axes)
    def f(x, y):
        dx, dy = (x - cx) / rx, (y - cy) / ry
        k = math.hypot(dx, dy)
        if k == 0:
            return -min(rx, ry)
        return (k - 1.0) * min(rx, ry) * (1.0 if k >= 1 else 1.0)
    return f


def sd_box(cx, cy, hw, hh, r=0.0):
    """a box centred at (cx, cy), half sizes hw hh, corners rounded by r"""
    def f(x, y):
        qx, qy = abs(x - cx) - hw + r, abs(y - cy) - hh + r
        return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r
    return f


def sd_poly(pts, r=0.0):
    """a polygon [(x, y), ...], corners rounded by r (the shape grows by r)"""
    n = len(pts)

    def f(x, y):
        d = float("inf")
        s = 1.0
        for i in range(n):
            ax, ay = pts[i]
            bx, by = pts[i - 1]
            ex, ey = bx - ax, by - ay
            wx, wy = x - ax, y - ay
            t = _clamp((wx * ex + wy * ey) / (ex * ex + ey * ey or 1.0))
            px, py = wx - ex * t, wy - ey * t
            d = min(d, px * px + py * py)
            c1 = y >= ay
            c2 = y < by
            c3 = ex * wy > ey * wx
            if (c1 and c2 and c3) or (not c1 and not c2 and not c3):
                s = -s
        return s * math.sqrt(d) - r
    return f


def sd_union(*fs):
    return lambda x, y: min(f(x, y) for f in fs)


def sd_cut(a, b):
    """a less b"""
    return lambda x, y: max(a(x, y), -b(x, y))


def sd_offset(f, dx, dy):
    return lambda x, y: f(x - dx, y - dy)


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


class Canvas:
    """an RGBA drawing, w x h texels, y down (row 0 the top), drawn shape by
    shape; coordinates in texels"""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[(0.0, 0.0, 0.0, 0.0) for _ in range(w)] for _ in range(h)]

    def _over(self, x, y, rgb, a):
        if a <= 0:
            return
        r, g, b, da = self.px[y][x]
        oa = a + da * (1 - a)
        if oa <= 0:
            return
        c = [(rgb[i] * a + (r, g, b)[i] * da * (1 - a)) / oa for i in range(3)]
        self.px[y][x] = (c[0], c[1], c[2], oa)

    def shape(self, f, fill, outline=None, width=3.0, shade=None, shade_off=(3.0, 3.0), light=None,
              light_off=(-2.5, -2.5), clip=None):
        """fill the shape f (inside where f < 0); a darker `shade` where the shape
        moved by shade_off doesn't cover it (the lower right edge), a `light`
        likewise on the upper left; then an outline `width` texels wide"""
        fill = hex_rgb(fill) if isinstance(fill, str) else fill
        outline = hex_rgb(outline) if isinstance(outline, str) else outline
        shade = hex_rgb(shade) if isinstance(shade, str) else shade
        light = hex_rgb(light) if isinstance(light, str) else light
        for y in range(self.h):
            for x in range(self.w):
                px, py = x + 0.5, y + 0.5
                d = f(px, py)
                if d > width + 1:
                    continue
                if clip is not None and clip(px, py) > 0.5:
                    continue
                inner = _clamp(0.5 - d)
                if inner > 0:
                    col = fill
                    if shade is not None:
                        ds = f(px - shade_off[0], py - shade_off[1])
                        k = _clamp(ds + 0.5)
                        col = tuple(col[i] * (1 - k) + shade[i] * k for i in range(3))
                    if light is not None:
                        dl = f(px - light_off[0], py - light_off[1])
                        k = _clamp(dl + 0.5) * 0.85
                        col = tuple(col[i] * (1 - k) + light[i] * k for i in range(3))
                    self._over(x, y, col, inner)
                if outline is not None:
                    # a band from the edge inward, `width` texels
                    self._over(x, y, outline, _clamp(0.5 - d) * _clamp(d + width + 0.5))

    def line(self, pts, color, width=2.0):
        """a stroke along the points (carved lines, cracks)"""
        segs = list(zip(pts, pts[1:]))

        def f(x, y):
            d = float("inf")
            for (ax, ay), (bx, by) in segs:
                ex, ey = bx - ax, by - ay
                t = _clamp(((x - ax) * ex + (y - ay) * ey) / (ex * ex + ey * ey or 1.0))
                d = min(d, math.hypot(x - ax - ex * t, y - ay - ey * t))
            return d - width / 2.0
        self.shape(f, color)

    def speckle(self, color, count, seed, size=(0.8, 1.6), inside=None):
        """small flecks (stone grain), only where `inside` < 0"""
        rnd = _Rand(seed)
        for _ in range(count):
            x, y = rnd.next() * self.w, rnd.next() * self.h
            if inside is not None and inside(x, y) > -2.5:
                continue
            r = size[0] + rnd.next() * (size[1] - size[0])
            f = sd_circle(x, y, r)
            x0, y0 = max(0, int(x - r - 2)), max(0, int(y - r - 2))
            x1, y1 = min(self.w, int(x + r + 3)), min(self.h, int(y + r + 3))
            col = hex_rgb(color) if isinstance(color, str) else color
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    a = _clamp(0.5 - f(xx + 0.5, yy + 0.5)) * 0.6
                    if self.px[yy][xx][3] > 0.5:
                        self._over(xx, yy, col, a)

    def rows(self):
        """RGBA rows of 0..255 ints, for encode_cmpr and ttyd_anim.write_png"""
        return [[(int(round(r)), int(round(g)), int(round(b)), int(round(a * 255))) for r, g, b, a in row]
                for row in self.px]


def read_png(path):
    """RGBA rows (0..255) of an 8-bit PNG (RGBA, RGB, grey or grey+alpha; not
    interlaced, no palette): drawings made elsewhere, e.g. an artist's parts"""
    import zlib
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s isn't a PNG" % path)
    at, idat = 8, b""
    while at < len(data):
        n, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _c, _f, inter = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        at += 12 + n
    if depth != 8 or inter or ctype not in (0, 2, 4, 6):
        raise ValueError("%s: only 8-bit, non-interlaced, non-palette PNGs" % path)
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, at = [], bytearray(stride), 0
    for _y in range(h):
        ft, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        px = []
        for x in range(w):
            v = line[x * bpp:(x + 1) * bpp]
            px.append((v[0], v[0], v[0], 255) if ctype == 0 else (v[0], v[0], v[0], v[1]) if ctype == 4
                      else (v[0], v[1], v[2], 255) if ctype == 2 else tuple(v))
        rows.append(px)
    return rows


def pad8(rows):
    """an image grown to multiples of 8 texels (transparent at the right and the
    top, so its bottom left corner stays put), as CMPR needs"""
    h, w = len(rows), len(rows[0])
    W, H = w + (-w % 8), h + (-h % 8)
    return [[(0, 0, 0, 0)] * W for _ in range(H - h)] + [list(r) + [(0, 0, 0, 0)] * (W - w) for r in rows]


def png_part(name, parent, path, origin, px_per_unit=8.0, z=0.0, pivot=None, binds=(), visible=True):
    """a Part from a drawing in a PNG: `origin` is where the image's bottom left
    corner sits in the model (x, y), at px_per_unit texels a unit; pivot in the
    model's units (default the image's bottom middle); binds: more PNG paths"""
    rows = pad8(read_png(path))
    h, w = len(rows), len(rows[0])
    x0, y0 = origin
    rect = (x0, y0, x0 + w / px_per_unit, y0 + h / px_per_unit)
    if pivot is None:
        pivot = ((rect[0] + rect[2]) / 2.0, y0)
    return Part(name, parent, rows, rect, z=z, pivot=pivot, binds=[pad8(read_png(b)) for b in binds],
                visible=visible)


class _Rand:
    def __init__(self, seed):
        self.s = (seed * 2654435761) & 0xFFFFFFFF or 1

    def next(self):
        self.s ^= (self.s << 13) & 0xFFFFFFFF
        self.s ^= self.s >> 17
        self.s ^= (self.s << 5) & 0xFFFFFFFF
        return (self.s & 0xFFFFFF) / float(0x1000000)


# ------------------------------------------------------------- textures ----

def _to565(c):
    r, g, b = c
    return ((int(r) >> 3) << 11) | ((int(g) >> 2) << 5) | (int(b) >> 3)


def _cmpr_block(px):
    """8 bytes for 16 RGBA texels (row-major)"""
    opaque = [p[:3] for p in px if p[3] >= 128]
    has_alpha = len(opaque) < 16
    if not opaque:
        return struct.pack(">HHI", 0, 0, 0xFFFFFFFF)
    # the two end colours: the pair farthest apart (by luminance, then distance)
    lum = [0.299 * r + 0.587 * g + 0.114 * b for r, g, b in opaque]
    lo = opaque[lum.index(min(lum))]
    hi = opaque[lum.index(max(lum))]
    c0, c1 = _to565(hi), _to565(lo)
    if has_alpha:
        if c0 > c1:
            c0, c1 = c1, c0
        p0, p1 = T._rgb565(c0), T._rgb565(c1)
        pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1))]
    else:
        if c0 == c1:
            return struct.pack(">HHI", c0, c1, 0)
        if c0 < c1:
            c0, c1 = c1, c0
        p0, p1 = T._rgb565(c0), T._rgb565(c1)
        pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    bits = 0
    for j, p in enumerate(px):
        if has_alpha and p[3] < 128:
            sel = 3
        else:
            sel = min(range(len(pal)), key=lambda k: sum((pal[k][i] - p[i]) ** 2 for i in range(3)))
        bits |= sel << (30 - 2 * j)
    return struct.pack(">HHI", c0, c1, bits)


def encode_cmpr(rows):
    """RGBA rows (w a multiple of 8, h of 8) to GX CMPR"""
    h, w = len(rows), len(rows[0])
    out = bytearray()
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for sub in range(4):
                sx, sy = bx + (sub & 1) * 4, by + (sub >> 1) * 4
                blk = []
                for j in range(16):
                    x, y = sx + (j & 3), sy + (j >> 2)
                    blk.append(rows[y][x] if x < w and y < h else (0, 0, 0, 0))
                out += _cmpr_block(blk)
    return bytes(out)


def make_tpl(images):
    """a TPL (as the disc's) of RGBA row images, CMPR"""
    import luigi_pack
    ents = []
    for rows in images:
        h, w = len(rows), len(rows[0])
        hdr = bytearray(0x24)
        struct.pack_into(">HHIIIIII", hdr, 0, h, w, 14, 0, 0, 0, 1, 1)
        ents.append((bytes(hdr), encode_cmpr(rows)))
    return luigi_pack.layout_tpl(ents)


# ---------------------------------------------------------------- models ----

class Part:
    """a group of the model: a textured plane (image given) or an empty mover.
    rect: the plane's corners in model units (x0, y0) bottom left, (x1, y1) top
    right; z its depth; pivot: where it turns and scales (x, y[, z]); binds:
    other drawings its texture can switch to (the "tex" channel)"""

    def __init__(self, name, parent=None, image=None, rect=None, z=0.0, pivot=(0.0, 0.0), binds=(),
                 visible=True, cull=3):
        self.cull = cull  # the shape's cull mode (animdrv's: 0, 1 one side only; 3 both)
        self.name, self.parent, self.image, self.rect, self.z = name, parent, image, rect, z
        self.pivot = tuple(pivot) + (z,) * (3 - len(pivot))
        self.binds = list(binds)
        self.visible = visible


def build(name, parts, anims=(), build_time=None, size=None):
    """(model file, TPL) for the parts (Part, parents before children; the first
    with no parent is the root) and animations (ttyd_anim.Anim).

    size: the header's (radius, height) words; by default from the rest pose's
    bounds. The game doubles each (animPoseGetRadius, animPoseGetHeight) and an
    NPC starts with width = 2 x height word, height = 2 x radius word
    (npcdrv.c, swapped as the game has it), so by default radius = half the
    model's height and height = half its width: an NPC of it starts its own
    size. (The disc's models all carry 5 and 5; their scripts set sizes.)"""
    p = T.PoseFile()
    p.tables = {t: [] for t, _s, _c in T.TABLES}
    t = p.tables
    images = []
    order = _tree_order(parts)       # groups in file order: children before parents, root last
    index = {pt.name: i for i, pt in enumerate(order)}
    lo, hi = [1e9] * 3, [-1e9] * 3
    for gi, pt in enumerate(order):
        shape = -1
        if pt.image is not None:
            shape = len(t["shapes"])
            pos_i, tex_i, nrm_i = len(t["pos"]), len(t["texVtx"]), len(t["nrm"])
            pid_i, call_i, draw_i, slot_i = len(t["posIdx"]), len(t["drawCalls"]), len(t["draws"]), len(t["texData"])
            sh = bytearray(0xA8)
            sh[0:64] = T._pad_name(pt.name + "Shape", 64)
            vals = [pos_i, 4, 0, 0, 0, 0, tex_i, 4] + [tex_i + 4, 0] * 7 + [draw_i, 1, 0, pt.cull]
            struct.pack_into(">26i", sh, 0x40, *vals)
            t["shapes"].append(bytes(sh))
            t["drawCalls"].append(struct.pack(">ii", 0, 4))
            x0, y0, x1, y1 = pt.rect
            for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
                t["pos"].append(struct.pack(">3f", x, y, pt.z))
                t["nrm"].append(struct.pack(">3f", 0.0, 0.0, 1.0))
                t["clr"].append(b"\xff\xff\xff\xff")
                for c, v in enumerate((x, y, pt.z)):
                    lo[c], hi[c] = min(lo[c], v), max(hi[c], v)
            for uv in ((0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0)):
                t["texVtx"].append(struct.pack(">2f", *uv))
            for k in (0, 1, 3, 2):
                t["posIdx"].append(struct.pack(">I", k))
                t["texIdx0"].append(struct.pack(">I", k))
            for k in range(4):
                t["nrmIdx"].append(struct.pack(">I", nrm_i + k))
                t["clrIdx"].append(struct.pack(">I", nrm_i + k))
            t["texAnim"].append(struct.pack(">B3x5f", 0, 0.0, 0.0, 1.0, 1.0, 0.0))
            t["texData"].append(struct.pack(">ii", len(t["texBinds"]), 0))
            for img in [pt.image] + pt.binds:
                b = bytearray(0x40)
                struct.pack_into(">Ii", b, 4, len(images), 0)
                t["texBinds"].append(bytes(b))
                images.append(img)
            dr = bytearray(0x6C)
            struct.pack_into(">i4xi4x8i", dr, 0, 1, 0, slot_i, -1, -1, -1, -1, -1, -1, -1)
            dr[0x4:0x8] = struct.pack(">i", 1)
            dr[0x30:0x38] = bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
            struct.pack_into(">2i11i", dr, 0x38, call_i, 1, pid_i, pid_i, pid_i, pid_i, 0, 0, 0, 0, 0, 0, 0)
            t["draws"].append(bytes(dr))
        vis_i = len(t["vis"])
        t["vis"].append(b"\x01" if pt.visible else b"\x00")
        node_i = len(t["node"])
        px, py, pz = pt.pivot
        node = [0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                px, py, pz, px, py, pz, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
        t["node"] += [struct.pack(">f", v) for v in node]
        kids = [c for c in order if c.parent == pt.name]
        brother = -1
        sibs = [c for c in order if c.parent == pt.parent and c is not pt] if pt.parent is not None else []
        later = [s for s in sibs if index[s.name] < gi]
        # brothers chain: each group's brother is the previous sibling in file order
        if later:
            brother = index[later[-1].name]
        child = index[kids[-1].name] if kids else -1
        g = bytearray(0x58)
        g[0:64] = T._pad_name(pt.name, 64)
        struct.pack_into(">6i", g, 0x40, brother, child, shape, vis_i, node_i, 0)
        t["groups"].append(bytes(g))
    hdr = bytearray(T.HEADER_SIZE)
    hdr[0x4:0x44] = T._pad_name(name, 64)
    hdr[0x44:0x84] = T._pad_name(name, 64)
    stamp = build_time or time.strftime("%a %b %d %H:%M:%S %Y", time.gmtime(0))
    hdr[0x84:0xC4] = T._pad_name(stamp, 64)
    if size is None:
        size = (max(1, int(round((hi[1] - min(lo[1], 0.0)) / 2.0))), max(1, int(round((hi[0] - lo[0]) / 2.0))))
    struct.pack_into(">Iii6f", hdr, 0xC4, 1, size[0], size[1], *lo, *hi)
    p.header = hdr
    p.anims = list(anims)
    return p.build(), make_tpl(images)


def _tree_order(parts):
    """file order: each group's children before it, the root last (as the disc's)"""
    by_parent = {}
    for pt in parts:
        by_parent.setdefault(pt.parent, []).append(pt)
    roots = by_parent.get(None, [])
    if len(roots) != 1:
        raise ValueError("a model needs one root part")
    out = []

    def walk(pt):
        for c in by_parent.get(pt.name, []):
            walk(c)
        out.append(pt)
    walk(roots[0])
    return out


def still(name="Z_1"):
    """a one-key animation, the model at rest"""
    a = T.Anim(name)
    a.keys = [T.Key(0.0)]
    return a
