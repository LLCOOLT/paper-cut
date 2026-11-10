"""Art recipes: images made from the player's own disc (RECIPES.md has the format).

The repo holds only the recipes (port/assets/luigi/art/recipes/*.recipe); the game runs them
on the player's disc when it first starts ("Preparing graphics", port/src/common/art_recipe.c) and
keeps the results in the user folder. This is the authoring and preview tool: the same format, the
same arithmetic, the same pixels (tests/port/art_recipe_check.py checks the two against each other).
Its output goes to a scratch folder, never into the repo.

Run: python recipes.py [--disc DIR] [--out DIR] [--only NAME] [--zoom K] FILE.recipe...
  --disc  the extracted disc (the folder holding files/ and sys/, or files/ itself); default the US
          disc under data/G8ME01 of this checkout or of the main one beside it, or $TTYD_DISC_DIR
  --out   where the images go, at their asset paths (default ./recipe_out)
  --zoom  also write <out>/preview.png: each image at xK beside its first source (default 4; 0: none)
"""
import argparse
import colorsys
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, *[".."] * 5))


class RecipeError(Exception):
    pass


class LineError(RecipeError):
    """a RecipeError that already names its line"""


class NotForDisc(RecipeError):
    """the image isn't made on this disc (`discs`)"""


class DrawnOnDisc(NotForDisc):
    """the image isn't made on this disc: its fallback stands in (`drawn`)"""


# ------------------------------------------------------------------ the disc --
BLOCKS = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16),
          6: (4, 4, 32), 8: (8, 8, 4), 9: (8, 4, 8), 10: (4, 4, 16), 14: (8, 8, 4)}


def rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, 255)


def rgb5a3(v):
    if v & 0x8000:
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255)
    a, r, g, b = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17, a << 5 | a << 2 | a >> 1)


def ia8(v):
    return (v & 0xFF, v & 0xFF, v & 0xFF, v >> 8)


def tpl_image(data, n):
    """image n of a TPL as (w, h, [r, g, b, a] list), as tools/port/tpl2png.py decodes"""
    if len(data) < 12 or struct.unpack_from(">I", data, 0)[0] != 0x0020AF30:
        raise RecipeError("not a TPL")
    count, table = struct.unpack_from(">II", data, 4)
    if n < 0 or n >= count:
        raise RecipeError(f"no image {n} (it has {count})")
    img_off, pal_off = struct.unpack_from(">II", data, table + 8 * n)
    h, w, fmt, off = struct.unpack_from(">HHII", data, img_off)
    if fmt not in BLOCKS:
        raise RecipeError(f"image {n}: format {fmt} isn't supported")
    pal = None
    if fmt in (8, 9, 10):
        if not pal_off:
            raise RecipeError(f"image {n}: no palette")
        pcount, _, _, pfmt, pdata = struct.unpack_from(">HBBII", data, pal_off)
        conv = {0: ia8, 1: rgb565, 2: rgb5a3}[pfmt]
        pal = [conv(struct.unpack_from(">H", data, pdata + 2 * i)[0]) for i in range(pcount)]
    bw, bh, bpp = BLOCKS[fmt]
    px = [[0, 0, 0, 0] for _ in range(w * h)]
    p = off

    def put(x, y, c):
        if x < w and y < h:
            px[y * w + x] = list(c)

    for by in range((h + bh - 1) // bh):
        for bx in range((w + bw - 1) // bw):
            x0, y0 = bx * bw, by * bh
            if fmt == 14:
                for sb in range(4):
                    c0, c1 = struct.unpack_from(">HH", data, p)
                    p0, p1 = rgb565(c0), rgb565(c1)
                    if c0 > c1:
                        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0[:3], p1[:3])) + (255,)
                        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0[:3], p1[:3])) + (255,)
                    else:
                        p2 = tuple((a + b) // 2 for a, b in zip(p0[:3], p1[:3])) + (255,)
                        p3 = (0, 0, 0, 0)
                    bits = struct.unpack_from(">I", data, p + 4)[0]
                    p += 8
                    sx, sy = x0 + (sb & 1) * 4, y0 + (sb >> 1) * 4
                    for i in range(16):
                        put(sx + i % 4, sy + i // 4, (p0, p1, p2, p3)[(bits >> (30 - 2 * i)) & 3])
                continue
            if fmt == 6:
                for i in range(16):
                    put(x0 + i % 4, y0 + i // 4, (data[p + 2 * i + 1], data[p + 32 + 2 * i],
                                                  data[p + 33 + 2 * i], data[p + 2 * i]))
                p += 64
                continue
            for i in range(bw * bh):
                x, y = x0 + i % bw, y0 + i // bw
                if bpp == 4:
                    v = data[p + i // 2] >> (4 if i % 2 == 0 else 0) & 15
                    c = (v * 17, v * 17, v * 17, 255) if fmt == 0 else pal[v]
                elif bpp == 8:
                    v = data[p + i]
                    c = (v, v, v, 255) if fmt == 1 else ((v & 15) * 17,) * 3 + ((v >> 4) * 17,) if fmt == 2 \
                        else pal[v]
                else:
                    v = struct.unpack_from(">H", data, p + 2 * i)[0]
                    c = ia8(v) if fmt == 3 else rgb565(v) if fmt == 4 else rgb5a3(v) if fmt == 5 \
                        else pal[v & 0x3FFF]
                put(x, y, c)
            p += bw * bh * bpp // 8
    return w, h, px


def icon_ids(version):
    """ICON_xxx (named by the JP id) -> this version's id, from include/icon_ids.h"""
    out = {}
    with open(os.path.join(ROOT, "include", "icon_ids.h"), encoding="utf-8") as f:
        for m in re.finditer(r"#define (ICON_[0-9A-F]+) VERSION_PICK\((0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+)\)", f.read()):
            out[m.group(1)] = int(m.group(2) if version == "jp" else m.group(3), 16)
    return out


class Disc:
    def __init__(self, root):
        self.files = os.path.join(root, "files") if os.path.isdir(os.path.join(root, "files")) else root
        boot = os.path.join(os.path.dirname(self.files), "sys", "boot.bin")
        gid = open(boot, "rb").read(6).decode("ascii") if os.path.exists(boot) else "G8ME01"
        self.gid = gid
        self.version = "jp" if gid[3] == "J" else "us"
        self.icons = icon_ids(self.version)
        self.cache = {}

    def read(self, path):
        if path not in self.cache:
            full = os.path.join(self.files, *path.split("/"))
            if not os.path.exists(full) and "/us/" in path:  # the Japanese disc's folder for the language
                full = os.path.join(self.files, *path.replace("/us/", "/jp/", 1).split("/"))
            if not os.path.exists(full):
                raise RecipeError(f"{path} isn't on this disc")
            self.cache[path] = open(full, "rb").read()
        return self.cache[path]

    def icon(self, name, frame):
        iid = int(name, 16) if name.startswith("0x") else self.icons.get(name)
        if iid is None:
            raise RecipeError(f"{name}: no such icon")
        b = self.read("icon.bin")
        count = struct.unpack_from(">H", b, 0)[0]
        if iid >= count:
            raise RecipeError(f"{name}: icon {iid:#x} isn't on this disc")
        off = struct.unpack_from(">H", b, 2 + 2 * iid)[0]
        n = struct.unpack_from(">H", b, off + 6)[0]
        if frame >= n:
            raise RecipeError(f"{name}: no frame {frame}")
        return struct.unpack_from(">H", b, off + 8 + 4 * frame)[0]


# ------------------------------------------------------------- an image -----
class Img:
    def __init__(self, w, h, px=None, fill=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.px = px if px is not None else [list(fill) for _ in range(w * h)]

    def at(self, x, y):
        return self.px[y * self.w + x]

    def copy(self):
        return Img(self.w, self.h, [list(p) for p in self.px])


def rnd(x):
    """round half to even, as the C runner's"""
    return int(round(x))


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def lum(c):
    return 0.299 * float(c[0]) + 0.587 * float(c[1]) + 0.114 * float(c[2])


def hsv(c):
    h, s, v = colorsys.rgb_to_hsv(c[0] / 255, c[1] / 255, c[2] / 255)
    return h, s, v


def parse_colour(tok, names):
    if HEX.match(tok):
        v = [int(tok[i:i + 2], 16) for i in range(1, len(tok), 2)]
        return v + [255] if len(v) == 3 else v
    if tok in names:
        return list(names[tok])
    raise RecipeError(f"{tok}: not a colour (#rrggbb, #rrggbbaa or a name from `colours`)")


NUM = re.compile(r"^-?[0-9]+(\.[0-9]+)?$")
INT = re.compile(r"^(-?[0-9]+|0x[0-9A-Fa-f]+)$")
HEX = re.compile(r"^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$")


def num(tok):
    if not NUM.match(tok):
        raise RecipeError(f"{tok}: not a number")
    return float(tok)


def integer(tok):
    if not INT.match(tok):
        raise RecipeError(f"{tok}: not a whole number")
    return int(tok, 16) if tok.startswith("0x") else int(tok)


CLAUSE = re.compile(r"^([hsvlrgba])(<=|>=|<|>|=)(-?[0-9]+(?:\.[0-9]+)?)(?:\.\.(-?[0-9]+(?:\.[0-9]+)?))?$")


def parse_where(toks, i):
    """`where` clauses from toks[i]: [[(key, op, a, b)...] or ...], the next index"""
    alts, cur = [], []
    while i < len(toks):
        t = toks[i]
        if t == "or":
            if not cur:
                raise RecipeError("`or` without a clause before it")
            alts.append(cur)
            cur = []
            i += 1
            continue
        m = CLAUSE.match(t)
        if not m:
            break
        key, op, a, b = m.group(1), m.group(2), num(m.group(3)), m.group(4)
        if b is not None and (op != "=" or key != "h"):
            raise RecipeError(f"{t}: a range is h=A..B")
        cur.append((key, op, a, num(b) if b is not None else None))
        i += 1
    if not cur:
        raise RecipeError("`where` needs a clause")
    alts.append(cur)
    return alts, i


def selected(alts, c):
    if alts is None:
        return True
    h, s, v = hsv(c)
    vals = {"h": h * 360, "s": s, "v": v, "l": lum(c), "r": c[0], "g": c[1], "b": c[2], "a": c[3]}
    for alt in alts:
        ok = True
        for key, op, a, b in alt:
            x = vals[key]
            if b is not None:
                ok = (a <= x <= b) if a <= b else (x >= a or x <= b)
            elif op == "<":
                ok = x < a
            elif op == "<=":
                ok = x <= a
            elif op == ">":
                ok = x > a
            elif op == ">=":
                ok = x >= a
            else:
                ok = x == a
            if not ok:
                break
        if ok:
            return True
    return False


def inside(poly, x, y):
    n, hit = len(poly), False
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def ranges(tok):
    """'15-31' or '15' -> (15, 31)"""
    a, _, b = tok.partition("-")
    return integer(a), integer(b) if b else integer(a)


# ------------------------------------------------------------- map meshes --
# `mesh`: an object of a map drawn into the image (RECIPES.md). The map's `d` file is read as
# the game reads it (packed display lists; ver1.02's fixed-point positions and texture
# coordinates), each joint under the named one placed by its scale and translation (a turned
# joint is refused: none is needed, and it would need sines both runners agree on).
def map_mesh(d, joint, pole):
    """the triangles of joint's subtree in map file d: [(part, is_pole, [(x, y, z) * 3],
    [(u, v) * 3], [(r, g, b) * 3], texture name)]"""
    B = 0x20
    try:
        fs, dsz, rc, cc = struct.unpack_from(">IIII", d, 0)
        strtab = B + dsz + rc * 4 + cc * 8
        chunks = {}
        for i in range(cc):
            off, no = struct.unpack_from(">II", d, B + dsz + rc * 4 + i * 8)
            e = d.index(b"\0", strtab + no)
            chunks[d[strtab + no:e].decode("ascii", "replace")] = B + off
    except (struct.error, ValueError):
        raise RecipeError("not a map file")
    if "information" not in chunks:
        raise RecipeError("not a map file")
    u32 = lambda a: struct.unpack_from(">I", d, a)[0]
    s32 = lambda a: struct.unpack_from(">i", d, a)[0]
    P = lambda a: (B + u32(a)) if u32(a) else 0

    def cstr(a):
        return d[a:d.index(b"\0", a)].decode("ascii", "replace") if a else ""
    info = chunks["information"]
    v102 = cstr(P(info)).startswith("ver1.02")
    found = []

    def find(j):
        while j and not found:
            if cstr(P(j)) == joint:
                found.append(j)
                return
            if P(j + 0xC):
                find(P(j + 0xC))
            j = P(j + 0x10)
    find(P(info + 4))
    if not found:
        raise RecipeError(f"no object {joint} in the map")
    tris = []
    parts = []

    def walk(j, ox, oy, oz, kx, ky, kz, top):
        while j:
            sx, sy, sz = struct.unpack_from(">3f", d, j + 0x18)
            rx, ry, rz = struct.unpack_from(">3f", d, j + 0x24)
            tx, ty, tz = struct.unpack_from(">3f", d, j + 0x30)
            if top:  # the named object itself: where it stands in the map doesn't count
                sx = sy = sz = 1.0
                tx = ty = tz = 0.0
            elif rx != 0.0 or ry != 0.0 or rz != 0.0:
                raise RecipeError(f"{cstr(P(j))} is turned: not drawn")
            px, py, pz = ox + tx * kx, oy + ty * ky, oz + tz * kz
            qx, qy, qz = kx * sx, ky * sy, kz * sz
            name = cstr(P(j))
            for k in range(s32(j + 0x5C)):
                mat = P(j + 0x60 + 8 * k)
                mesh = P(j + 0x60 + 8 * k + 4)
                if not mesh:
                    continue
                tex = ""
                if mat and d[mat + 0xB]:
                    tex = cstr(P(P(P(mat + 0xC))))
                part = len(parts)
                parts.append(name)
                for corners in mesh_tris(d, mesh, v102, u32, s32, P):
                    tris.append((part, name == pole,
                                 [(x * qx + px, y * qy + py, z * qz + pz) for (x, y, z), _, _ in corners],
                                 [uv for _, uv, _ in corners], [c for _, _, c in corners], tex))
            if P(j + 0xC):
                walk(P(j + 0xC), px, py, pz, qx, qy, qz, False)
            if top:
                return
            j = P(j + 0x10)
    walk(found[0], 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, True)
    if pole not in parts:
        raise RecipeError(f"no pole {pole} under {joint}")
    names = []
    if "texture_table" in chunks:
        tt = chunks["texture_table"]
        names = [cstr(P(tt + 4 + 4 * k)) for k in range(u32(tt))]
    return tris, names


def mesh_tris(d, m, v102, u32, s32, P):
    """a packed mesh's triangles: [((x, y, z), (u, v), (r, g, b)) * 3]"""
    if not d[m + 3]:
        raise RecipeError("an unpacked mesh: not drawn")
    n, mask, vcd = s32(m + 4), u32(m + 8), P(m + 0xC)
    pos_a, col_a, tex_a = P(vcd), P(vcd + 0xC), P(vcd + 0x18)
    pos_k = 2.0 ** -s32(vcd + 0x44)
    tex_k = 2.0 ** -s32(vcd + 0x48)
    ncol = sum(1 for i in range(2) if mask & (1 << (i + 2)))
    ntex = 0
    for i in range(8):
        if not mask & (1 << (i + 4)):
            break
        ntex += 1
    stride = 2 + ncol + ntex

    def vert(idx):
        if v102:
            x, y, z = struct.unpack_from(">3h", d, pos_a + 4 + 6 * idx[0])
            p = (x * pos_k, y * pos_k, z * pos_k)
        else:
            p = struct.unpack_from(">3f", d, pos_a + 4 + 12 * idx[0])
        uv = (0.0, 0.0)
        if ntex and tex_a:
            if v102:
                s, t = struct.unpack_from(">2h", d, tex_a + 4 + 4 * idx[2 + ncol])
                uv = (s * tex_k, t * tex_k)
            else:
                uv = struct.unpack_from(">2f", d, tex_a + 4 + 8 * idx[2 + ncol])
        c = (255, 255, 255)
        if ncol and col_a:
            c = tuple(d[col_a + 4 + 4 * idx[2]:col_a + 7 + 4 * idx[2]])
        return p, uv, c
    tris = []
    for li in range(n):
        o = P(m + 0x10 + 8 * li)
        end = o + u32(m + 0x10 + 8 * li + 4)
        while o < end:
            cmd = d[o]
            if cmd == 0:
                o += 1
                continue
            cnt = struct.unpack_from(">H", d, o + 1)[0]
            o += 3
            vs = []
            for k in range(cnt):
                vs.append(vert(struct.unpack_from(">%dH" % stride, d, o)))
                o += 2 * stride
            prim = cmd & 0xF8
            if prim == 0x98:
                for k in range(cnt - 2):
                    tris.append((vs[k], vs[k + 1], vs[k + 2]) if k % 2 == 0 else (vs[k + 1], vs[k], vs[k + 2]))
            elif prim == 0x90:
                for k in range(0, cnt - 2, 3):
                    tris.append((vs[k], vs[k + 1], vs[k + 2]))
            elif prim == 0xA0:
                for k in range(1, cnt - 1):
                    tris.append((vs[0], vs[k], vs[k + 1]))
            else:
                raise RecipeError(f"display list command {cmd:#x}: not drawn")
    return tris


def unit_turn(deg):
    """cos and sin of whole degrees, on 1/65536ths (so both runners agree)"""
    a = math.radians(deg)
    return math.floor(math.cos(a) * 65536 + 0.5) / 65536, math.floor(math.sin(a) * 65536 + 0.5) / 65536


def norm3(x, y, z):
    n = math.sqrt(x * x + y * y + z * z)
    return (x / n, y / n, z / n) if n > 0.0 else (0.0, 0.0, 0.0)


# the light (from the front, above, the left) and the shading bands: flat per panel, as drawn
MESH_LIGHT = norm3(-0.5, 0.7, -0.5)


def mesh_shade(d, inside):
    f = 1.0 if d >= 0.45 else 0.86 if d >= 0.0 else 0.72
    return f * 0.8 if inside else f


def draw_mesh(img, tris, textures, closed, turn, pitch, size, ax, ay, line):
    """the triangles straightened (the pole upright, its foot at the origin), the canopy folded
    down the pole if closed, turned `turn` degrees about it, seen from the front `pitch` degrees
    from above, `size` texels a unit, the foot at (ax, ay); flat colours per triangle (its
    vertices' colour times its texture's under its middle, shaded by its facing), a z-buffer, and
    with `line` a 1-texel line where a part meets another in front of it"""
    pole = [p for t in tris if t[1] for p in t[2]]
    n = float(len(pole))
    cx = cy = cz = 0.0
    for x, y, z in pole:
        cx += x
        cy += y
        cz += z
    cx, cy, cz = cx / n, cy / n, cz / n
    far, fd = pole[0], -1.0
    for p in pole:
        dd = (p[0] - cx) * (p[0] - cx) + (p[1] - cy) * (p[1] - cy) + (p[2] - cz) * (p[2] - cz)
        if dd > fd:
            far, fd = p, dd
    ex, ey, ez = far[0] - cx, far[1] - cy, far[2] - cz
    sa = [0.0, 0.0, 0.0, 0.0]
    sb = [0.0, 0.0, 0.0, 0.0]
    for x, y, z in pole:
        s = sa if (x - cx) * ex + (y - cy) * ey + (z - cz) * ez > 0.0 else sb
        s[0] += x
        s[1] += y
        s[2] += z
        s[3] += 1.0
    axx, axy, axz = norm3(sa[0] / sa[3] - sb[0] / sb[3], sa[1] / sa[3] - sb[1] / sb[3], sa[2] / sa[3] - sb[2] / sb[3])
    # the pole points up the canopy: toward the side the other parts are on
    side = 0.0
    for t in tris:
        if not t[1]:
            for x, y, z in t[2]:
                side += (x - cx) * axx + (y - cy) * axy + (z - cz) * axz
    if side < 0.0:
        axx, axy, axz = -axx, -axy, -axz
    # turned onto +y (Rodrigues, about axis x (0, 1, 0))
    s = math.sqrt(axz * axz + axx * axx)
    c = axy
    if s < 1e-9:
        R = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]] if c > 0.0 else \
            [[1.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.0, 0.0, -1.0]]
    else:
        kx, kz = -axz / s, axx / s
        t = 1.0 - c
        R = [[c + kx * kx * t, -kz * s, kx * kz * t],
             [kz * s, c, -kx * s],
             [kx * kz * t, kx * s, c + kz * kz * t]]

    def rot(p):
        return (R[0][0] * p[0] + R[0][1] * p[1] + R[0][2] * p[2],
                R[1][0] * p[0] + R[1][1] * p[1] + R[1][2] * p[2],
                R[2][0] * p[0] + R[2][1] * p[1] + R[2][2] * p[2])
    tris = [(t[0], t[1], [rot(p) for p in t[2]], t[3], t[4], t[5]) for t in tris]
    bx = bz = 0.0
    by = None
    for t in tris:
        if t[1]:
            for x, y, z in t[2]:
                bx += x
                bz += z
                by = y if by is None or y < by else by
    bx, bz = bx / n, bz / n
    tris = [(t[0], t[1], [(x - bx, y - by, z - bz) for x, y, z in t[2]], t[3], t[4], t[5]) for t in tris]
    # outward faces: the canopy's away from a point under its tip, the pole's away from its line
    rmax, tip = 0.0, None
    for t in tris:
        if not t[1]:
            for x, y, z in t[2]:
                r = math.sqrt(x * x + z * z)
                rmax = r if r > rmax else rmax
                tip = y if tip is None or y > tip else tip
    if tip is None:
        tip = 0.0

    def normal(v):
        ux, uy, uz = v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]
        wx, wy, wz = v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2]
        return norm3(uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx)
    flips = []
    for t in tris:
        nx, ny, nz = normal(t[2])
        mx = (t[2][0][0] + t[2][1][0] + t[2][2][0]) / 3.0
        my = (t[2][0][1] + t[2][1][1] + t[2][2][1]) / 3.0
        mz = (t[2][0][2] + t[2][1][2] + t[2][2][2]) / 3.0
        out = nx * mx + nz * mz if t[1] else nx * mx + ny * (my - (tip - rmax)) + nz * mz
        flips.append(out < 0.0)
    if closed and rmax > 0.0:
        def fold(p):
            x, y, z = p
            r = math.sqrt(x * x + z * z)
            if r <= 1e-6:
                return p
            k = r / rmax
            u = 0.15 + 0.85 * k
            u = 1.0 if u > 1.0 else u
            nr = rmax * (0.05 + 0.2 * math.sqrt(4.0 * u * (1.0 - u)))
            return (x * (nr / r), tip - 0.3 - k * rmax * 1.45, z * (nr / r))
        tris = [(t[0], t[1], t[2] if t[1] else [fold(p) for p in t[2]], t[3], t[4], t[5]) for t in tris]
    tc, ts = unit_turn(turn)
    pc, ps = unit_turn(pitch)
    zb = [0.0] * (img.w * img.h)
    ids = [-1] * (img.w * img.h)
    for i, t in enumerate(tris):
        v = [(x * tc - z * ts, y, x * ts + z * tc) for x, y, z in t[2]]
        nx, ny, nz = normal(v)
        if flips[i]:
            nx, ny, nz = -nx, -ny, -nz
        inside = ny * ps - nz * pc < 0.0
        if inside:
            nx, ny, nz = -nx, -ny, -nz
        shade = mesh_shade(nx * MESH_LIGHT[0] + ny * MESH_LIGHT[1] + nz * MESH_LIGHT[2], inside)
        cs = t[4]
        # the panel's own colour: its lightest vertex's (the disc's baked shading off)
        base = cs[0]
        for cc in cs[1:]:
            if 299 * cc[0] + 587 * cc[1] + 114 * cc[2] > 299 * base[0] + 587 * base[1] + 114 * base[2]:
                base = cc
        tex = textures.get(t[5])
        tcol = (255, 255, 255)
        if tex is not None:
            tw, th, tpx = tex
            u = (t[3][0][0] + t[3][1][0] + t[3][2][0]) / 3.0
            vv = (t[3][0][1] + t[3][1][1] + t[3][2][1]) / 3.0
            sx = int(math.floor((u % 1.0) * tw))
            sy = int(math.floor((vv % 1.0) * th))
            sx = tw - 1 if sx > tw - 1 else sx
            sy = th - 1 if sy > th - 1 else sy
            tcol = tpx[sy * tw + sx][:3]
        col = [clamp(int(rnd(base[k] * tcol[k] / 255.0 * shade))) for k in range(3)] + [255]
        pid = t[0] * 2 + (1 if inside else 0)
        pts = [(ax + x * size, ay - (y * pc + z * ps) * size, z * pc - y * ps) for x, y, z in v]
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts
        area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
        if area == 0.0:
            continue
        lo_x = max(0, int(math.floor(min(x0, x1, x2))))
        hi_x = min(img.w - 1, int(math.floor(max(x0, x1, x2))))
        lo_y = max(0, int(math.floor(min(y0, y1, y2))))
        hi_y = min(img.h - 1, int(math.floor(max(y0, y1, y2))))
        for py in range(lo_y, hi_y + 1):
            qy = py + 0.5
            for px in range(lo_x, hi_x + 1):
                qx = px + 0.5
                w0 = (x1 - qx) * (y2 - qy) - (y1 - qy) * (x2 - qx)
                w1 = (x2 - qx) * (y0 - qy) - (y2 - qy) * (x0 - qx)
                w2 = (x0 - qx) * (y1 - qy) - (y0 - qy) * (x1 - qx)
                if area > 0.0:
                    if w0 < 0.0 or w1 < 0.0 or w2 < 0.0:
                        continue
                elif w0 > 0.0 or w1 > 0.0 or w2 > 0.0:
                    continue
                dep = (w0 * z0 + w1 * z1 + w2 * z2) / area
                k = py * img.w + px
                if ids[k] < 0 or dep < zb[k]:
                    zb[k] = dep
                    ids[k] = pid
                    img.px[k] = list(col)
    if line is not None:
        marks = []
        for py in range(img.h):
            for px in range(img.w):
                k = py * img.w + px
                if ids[k] < 0:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    qx, qy = px + dx, py + dy
                    if 0 <= qx < img.w and 0 <= qy < img.h:
                        q = qy * img.w + qx
                        if ids[q] >= 0 and ids[q] != ids[k] and zb[q] < zb[k]:
                            marks.append(k)
                            break
        for k in marks:
            img.px[k] = list(line)


# ---------------------------------------------------------------- the runner --
class Runner:
    def __init__(self, disc):
        self.disc = disc

    # --- parsing ---
    @staticmethod
    def lines(text):
        """(line number, tokens, row) per line: comments ('#' then a space or the line's end) dropped;
        a row ('|' lines) kept whole"""
        out = []
        for n, raw in enumerate(text.splitlines(), 1):
            s = raw.strip()
            if s.startswith("|"):
                out.append((n, None, s[1:].strip()))
                continue
            cut = len(s)
            for i, ch in enumerate(s):
                if ch == "#" and (i + 1 == len(s) or s[i + 1] in " \t#"):
                    cut = i
                    break
            toks = s[:cut].split()
            if toks:
                out.append((n, toks, None))
        return out

    def images(self, text):
        """the names of the images a recipe file makes, in order"""
        return [t[1] for _, t, _ in self.lines(text) if t and t[0] == "image" and len(t) > 1]

    def fallback(self, text, name):
        on = False
        for _, t, _ in self.lines(text):
            if t and t[0] == "image":
                on = len(t) > 1 and t[1] == name
            elif on and t and t[0] == "fallback" and len(t) > 1:
                return t[1]
        return None

    def make(self, text, name):
        """run image `name` of a recipe file: an Img (raises RecipeError, 'line N: why')"""
        ls = self.lines(text)
        self.names, self.grads, self.masks, self.pieces = {}, {}, {}, {}
        self.steps = {}  # name -> [(line number, tokens, rows)]: a `steps NAME` ... `end` block
        self.canvas = None
        # out (the shared lines between images), this (the image made), other, steps (a block's lines)
        state = "out"
        block = None
        k = 0
        while k < len(ls):
            n, toks, _ = ls[k]
            k += 1
            rows = []
            while k < len(ls) and ls[k][1] is None:
                rows.append(ls[k][2])
                k += 1
            try:
                if toks is None:
                    raise RecipeError("a row ('|') without a `paint` or `mask ... rows` before it")
                if toks[0] == "image":
                    if state != "out":
                        raise RecipeError("`image` before the last one's `end`")
                    if len(toks) != 2:
                        raise RecipeError("image PATH")
                    state = "this" if toks[1] == name else "other"
                    continue
                if toks[0] == "steps":
                    if state != "out":
                        raise RecipeError("`steps` before the last one's `end`")
                    if len(toks) != 2:
                        raise RecipeError("steps NAME")
                    if len(toks[1]) >= 32:
                        raise RecipeError(f"{toks[1]}: too long a name")
                    state, block = "steps", []
                    self.steps[toks[1]] = block
                    continue
                if toks[0] == "end":
                    if state == "out":
                        raise RecipeError("`end` without an `image`")
                    if state == "this":
                        if self.canvas is None:
                            raise RecipeError("the image has no canvas (`load` or `canvas`)")
                        return self.canvas
                    state = "out"
                    continue
                if state == "steps":
                    block.append((n, toks, rows))
                    continue
                if state == "other":
                    continue
                self.run_line(toks, rows, state == "this", 0)
            except NotForDisc:
                raise
            except LineError:
                raise
            except RecipeError as e:
                raise LineError(f"line {n}: {e}")
        raise RecipeError(f"no image {name}" if state == "out" else "no `end`")

    def run_line(self, toks, rows, inside_image, depth):
        """one line of an image, or of a `steps` block it uses (`use NAME`: the block's lines run
        here, as if written in its place; an error names the block's line)"""
        if rows and toks[0] not in ("paint", "mask") and not toks[0].startswith("@"):
            raise RecipeError(f"`{toks[0]}` takes no rows")
        if toks[0] != "use":
            self.line(toks, rows, inside_image)
            return
        if not inside_image or len(toks) != 2:
            raise RecipeError("use NAME (in an image)")
        if toks[1] not in self.steps:
            raise RecipeError(f"no steps {toks[1]}")
        if depth >= 8:
            raise RecipeError(f"use {toks[1]}: steps used 8 deep")
        for bn, bt, br in self.steps[toks[1]]:
            try:
                self.run_line(bt, br, True, depth + 1)
            except (NotForDisc, LineError):
                raise
            except RecipeError as e:
                raise LineError(f"line {bn}: {e}")

    # --- one line ---
    def target(self, toks):
        if toks[0].startswith("@"):
            name = toks[0][1:]
            if name not in self.pieces:
                raise RecipeError(f"no piece {name}")
            return name, toks[1:]
        return None, toks

    def get(self, which):
        img = self.pieces[which] if which else self.canvas
        if img is None:
            raise RecipeError("no canvas yet (`load` or `canvas` first)")
        return img

    def put(self, which, img):
        if which:
            self.pieces[which] = img
        else:
            self.canvas = img

    def load(self, toks):
        """a source: tpl PATH N | model NAME N | icon ICON_xxx [frame N]; (Img, rest of toks)"""
        if not toks:
            raise RecipeError("a source: tpl PATH N, model NAME N or icon ICON_xxx")
        kind = toks[0]
        if kind == "tpl" and len(toks) >= 3:
            path, n, rest = toks[1], integer(toks[2]), toks[3:]
        elif kind == "model" and len(toks) >= 3:
            path, n, rest = "a/" + toks[1] + "-", integer(toks[2]), toks[3:]
        elif kind == "icon" and len(toks) >= 2:
            frame, rest = 0, toks[2:]
            if len(rest) >= 2 and rest[0] == "frame":
                frame, rest = integer(rest[1]), rest[2:]
            path, n = "icon.tpl", self.disc.icon(toks[1], frame)
        else:
            raise RecipeError(f"{' '.join(toks)}: a source is tpl PATH N, model NAME N or icon ICON_xxx")
        try:
            w, h, px = tpl_image(self.disc.read(path), n)
        except RecipeError as e:
            raise RecipeError(f"{path}: {e}")
        except struct.error:
            raise RecipeError(f"{path}: cut short")
        return Img(w, h, px), rest

    def line(self, toks, rows, inside_image):
        which, toks = self.target(toks)
        op, args = toks[0], toks[1:]
        top_level = {"gradient", "colours", "mask", "fallback"}
        if not inside_image and op not in top_level:
            raise RecipeError(f"`{op}` outside an image")
        if op == "fallback":
            return
        if op == "discs":
            if not inside_image or not args:
                raise RecipeError("discs ID... (in an image)")
            if self.disc.gid not in args:
                raise NotForDisc(f"not made on {self.disc.gid}")
            return
        if op == "drawn":
            if not inside_image or not args:
                raise RecipeError("drawn ID... (in an image)")
            if self.disc.gid in args:
                raise DrawnOnDisc(f"drawn on {self.disc.gid}")
            return
        if op == "gradient":
            if len(args) < 5 or len(args) % 2 == 0:
                raise RecipeError("gradient NAME T COLOUR T COLOUR ...")
            stops = [(num(args[i]), parse_colour(args[i + 1], self.names)) for i in range(1, len(args), 2)]
            if any(b[0] <= a[0] for a, b in zip(stops, stops[1:])):
                raise RecipeError("a gradient's stops go up")
            self.grads[args[0]] = stops
            return
        if op == "colours":
            i = 0
            while i < len(args):
                if i + 1 >= len(args):
                    raise RecipeError("colours NAME COLOUR ...")
                nm = args[i]
                if args[i + 1] == "sample":
                    if i + 3 >= len(args):
                        raise RecipeError("sample X Y")
                    img = self.get(which)
                    x, y = integer(args[i + 2]), integer(args[i + 3])
                    if not (0 <= x < img.w and 0 <= y < img.h):
                        raise RecipeError(f"sample {x} {y}: outside the image")
                    self.names[nm] = list(img.at(x, y))
                    i += 4
                else:
                    self.names[nm] = parse_colour(args[i + 1], self.names)
                    i += 2
            return
        if op == "mask":
            self.mask_line(args, rows)
            return
        if op == "load":
            if which:
                raise RecipeError("`load` is the canvas's; a piece is `piece NAME SOURCE`")
            img, rest = self.load(args)
            self.extra(rest)
            self.canvas = img
            return
        if op == "canvas":
            if which:
                raise RecipeError("`canvas` is the canvas's; a piece is `piece NAME canvas W H`")
            self.canvas = self.blank(args)
            return
        if op == "piece":
            if len(args) < 2:
                raise RecipeError("piece NAME SOURCE | piece NAME canvas W H [COLOUR] | piece NAME copy")
            nm = args[0]
            if args[1] == "canvas":
                self.pieces[nm] = self.blank(args[2:])
            elif args[1] == "copy":
                self.extra(args[2:])
                self.pieces[nm] = self.get(None).copy()
            else:
                img, rest = self.load(args[1:])
                self.extra(rest)
                self.pieces[nm] = img
            return
        img = self.get(which)
        fn = getattr(self, "op_" + op, None)
        if fn is None:
            raise RecipeError(f"`{op}`: no such step")
        out = fn(img, args, rows)
        if out is not None:
            self.put(which, out)

    @staticmethod
    def extra(rest):
        if rest:
            raise RecipeError(f"{' '.join(rest)}: not understood")

    def blank(self, args):
        if len(args) not in (2, 3):
            raise RecipeError("canvas W H [COLOUR]")
        w, h = integer(args[0]), integer(args[1])
        if not (0 < w <= 1024 and 0 < h <= 1024):
            raise RecipeError("a canvas is 1 to 1024 texels a side")
        return Img(w, h, fill=parse_colour(args[2], self.names) if len(args) == 3 else (0, 0, 0, 0))

    # --- masks ---
    def mask_line(self, args, rows):
        if len(args) < 2:
            raise RecipeError("mask NAME [+ | - | &] SHAPE")
        nm, rest = args[0], args[1:]
        mode = "="
        if rest[0] in ("+", "-", "&"):
            mode, rest = rest[0], rest[1:]
            if nm not in self.masks:
                raise RecipeError(f"no mask {nm} to change")
        if not rest:
            raise RecipeError("a shape: all, rect, circle, poly, spans, rows, alpha or mask NAME")
        shape = self.shape(rest, rows)
        if mode == "=":
            self.masks[nm] = [("+", shape)]
        else:
            self.masks[nm] = self.masks[nm] + [(mode, shape)]

    def shape(self, t, rows):
        kind, a = t[0], t[1:]
        if kind == "all" and not a:
            return ("all",)
        if kind == "alpha" and not a:
            return ("alpha",)
        if kind == "rect" and len(a) == 4:
            return ("rect",) + tuple(integer(v) for v in a)
        if kind == "circle" and len(a) == 3:
            return ("circle", num(a[0]), num(a[1]), num(a[2]))
        if kind == "poly" and len(a) >= 3:
            pts = []
            for p in a:
                xs, _, ys = p.partition(",")
                pts.append((num(xs), num(ys)))
            return ("poly", pts)
        if kind == "spans" and a:
            sp = {}
            for p in a:
                xs, _, ys = p.partition(":")
                x0, x1 = ranges(xs)
                y0, y1 = ranges(ys)
                for x in range(x0, x1 + 1):
                    sp[x] = (y0, y1)
            return ("spans", sp)
        if kind == "rows" and len(a) == 2:
            if not rows:
                raise RecipeError("mask ... rows X Y: the rows ('|' lines, x in, . out) follow")
            for r in rows:
                if any(ch not in "x." for ch in r):
                    raise RecipeError(f"| {r}: a mask's rows are x (in) and . (out)")
            return ("rows", integer(a[0]), integer(a[1]), rows)
        if kind == "mask" and len(a) == 1:
            if a[0] not in self.masks:
                raise RecipeError(f"no mask {a[0]}")
            return ("mask", list(self.masks[a[0]]))
        raise RecipeError(f"{' '.join(t)}: not a shape")

    def raster(self, parts, img):
        w, h = img.w, img.h
        m = [False] * (w * h)
        for mode, shape in parts:
            s = self.shape_raster(shape, img)
            for i in range(w * h):
                m[i] = (m[i] or s[i]) if mode == "+" else (m[i] and not s[i]) if mode == "-" else (m[i] and s[i])
        return m

    def shape_raster(self, shape, img):
        w, h = img.w, img.h
        kind = shape[0]
        if kind == "all":
            return [True] * (w * h)
        if kind == "alpha":
            return [p[3] >= 128 for p in img.px]
        if kind == "mask":
            return self.raster(shape[1], img)
        out = [False] * (w * h)
        if kind == "rect":
            x0, y0, x1, y1 = shape[1:]
            for y in range(max(0, y0), min(h - 1, y1) + 1):
                for x in range(max(0, x0), min(w - 1, x1) + 1):
                    out[y * w + x] = True
        elif kind == "poly":
            for y in range(h):
                for x in range(w):
                    out[y * w + x] = inside(shape[1], x + 0.5, y + 0.5)
        elif kind == "circle":
            _, cx, cy, r = shape
            for y in range(h):
                for x in range(w):
                    dx, dy = x + 0.5 - cx, y + 0.5 - cy
                    out[y * w + x] = dx * dx + dy * dy <= r * r
        elif kind == "spans":
            for x, (y0, y1) in shape[1].items():
                if 0 <= x < w:
                    for y in range(max(0, y0), min(h - 1, y1) + 1):
                        out[y * w + x] = True
        elif kind == "rows":
            _, x0, y0, rows = shape
            for j, r in enumerate(rows):
                for i, ch in enumerate(r):
                    if ch == "x" and 0 <= x0 + i < w and 0 <= y0 + j < h:
                        out[(y0 + j) * w + x0 + i] = True
        return out

    def select(self, args, img, keywords):
        """[in MASK] [where ...] at the front of args: (mask list or None, alts or None, rest)"""
        mask, alts, i = None, None, 0
        while i < len(args):
            if args[i] == "in" and i + 1 < len(args):
                if args[i + 1] not in self.masks:
                    raise RecipeError(f"no mask {args[i + 1]}")
                mask = self.raster(self.masks[args[i + 1]], img)
                i += 2
            elif args[i] == "where":
                alts, i = parse_where(args, i + 1)
            else:
                break
        if i < len(args) and args[i] == ":":   # an optional stop after the selection
            i += 1
        return mask, alts, args[i:]

    # --- the steps ---
    def each(self, img, mask, alts, skip_clear=True):
        for y in range(img.h):
            for x in range(img.w):
                c = img.px[y * img.w + x]
                if skip_clear and not c[3]:
                    continue
                if mask is not None and not mask[y * img.w + x]:
                    continue
                if not selected(alts, c):
                    continue
                yield x, y, c

    def op_ramp(self, img, args, rows):
        mask, alts, rest = self.select(args, img, ())
        if len(rest) != 4 or rest[0] != "base" or rest[2] != "to":
            raise RecipeError("ramp [in MASK] [where ...] base COLOUR to GRADIENT")
        base = lum(parse_colour(rest[1], self.names))
        if rest[3] not in self.grads:
            raise RecipeError(f"no gradient {rest[3]}")
        stops = self.grads[rest[3]]
        if base <= 0:
            raise RecipeError("the base colour is black")
        for x, y, c in list(self.each(img, mask, alts)):
            out = ramp_colour(stops, lum(c) / base)
            c[0], c[1], c[2] = out

    def op_map(self, img, args, rows):
        mask, alts, rest = self.select(args, img, ())
        pairs = []
        if not rest or len(rest) % 3:
            raise RecipeError("map [in MASK] [where ...] FROM -> TO ...")
        for i in range(0, len(rest), 3):
            if rest[i + 1] != "->":
                raise RecipeError("map [in MASK] [where ...] FROM -> TO ...")
            pairs.append((parse_colour(rest[i], self.names), parse_colour(rest[i + 2], self.names)))
        for x, y, c in list(self.each(img, mask, alts)):
            best, src, dst = -1, None, None
            for s, d in pairs:
                dd = (s[0] - c[0]) ** 2 + (s[1] - c[1]) ** 2 + (s[2] - c[2]) ** 2
                if best < 0 or dd < best:
                    best, src, dst = dd, s, d
            ls = lum(src)
            k = lum(c) / ls if ls > 0 else 1.0
            for j in range(3):
                c[j] = clamp(rnd(float(dst[j]) * k))

    def op_hsv(self, img, args, rows):
        mask, alts, rest = self.select(args, img, ())
        hue = sat = val = None
        i = 0
        while i < len(rest):
            if i + 1 >= len(rest) or rest[i] not in ("hue", "sat", "val"):
                raise RecipeError("hsv [in MASK] [where ...] [hue =D|+D|-D] [sat =K|*K] [val =K|*K|+K|-K]")
            ch = rest[i + 1]
            if ch[0] not in "=*+-" or len(ch) < 2:
                raise RecipeError(f"{rest[i]} {ch}: =N, *N, +N or -N")
            change = (ch[0], num(ch[1:]))
            if rest[i] == "hue":
                if change[0] == "*":
                    raise RecipeError("hue =D, +D or -D (degrees)")
                hue = change
            elif rest[i] == "sat":
                if change[0] in "+-":
                    raise RecipeError("sat =K or *K")
                sat = change
            else:
                val = change
            i += 2
        for x, y, c in list(self.each(img, mask, alts)):
            h, s, v = hsv(c)
            if hue:
                h = hue[1] / 360.0 if hue[0] == "=" else (h + (hue[1] if hue[0] == "+" else -hue[1]) / 360.0) % 1.0
            if sat:
                s = sat[1] if sat[0] == "=" else s * sat[1]
                s = min(1.0, max(0.0, s))
            if val:
                v = val[1] if val[0] == "=" else v * val[1] if val[0] == "*" else v + val[1] if val[0] == "+" \
                    else v - val[1]
                v = min(1.0, max(0.0, v))
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            c[0], c[1], c[2] = clamp(rnd(r * 255)), clamp(rnd(g * 255)), clamp(rnd(b * 255))

    def op_fill(self, img, args, rows):
        mask, alts, rest = self.select(args, img, ())
        if len(rest) != 1:
            raise RecipeError("fill [in MASK] [where ...] COLOUR")
        col = parse_colour(rest[0], self.names)
        for x, y, c in list(self.each(img, mask, alts, skip_clear=False)):
            c[:] = col

    def op_paint(self, img, args, rows):
        if len(args) != 2 or not rows:
            raise RecipeError("paint X Y, then the rows ('|' lines: a colour name a texel, . keeps, _ clears)")
        x0, y0 = integer(args[0]), integer(args[1])
        for j, r in enumerate(rows):
            for i, ch in enumerate(r):
                if ch == ".":
                    continue
                x, y = x0 + i, y0 + j
                if not (0 <= x < img.w and 0 <= y < img.h):
                    raise RecipeError(f"paint: texel {x},{y} is outside the image")
                img.px[y * img.w + x] = [0, 0, 0, 0] if ch == "_" else parse_colour(ch, self.names)

    def op_stamp(self, img, args, rows):
        if not args or args[0] not in self.masks:
            raise RecipeError("stamp MASK COLOUR | stamp MASK [edge C] [top C] [bottom C] [fill C]")
        m = self.raster(self.masks[args[0]], img)
        rest = args[1:]
        tones = {}
        if len(rest) == 1:
            tones = dict.fromkeys(("edge", "top", "bottom", "fill"), parse_colour(rest[0], self.names))
        else:
            if not rest or len(rest) % 2:
                raise RecipeError("stamp MASK COLOUR | stamp MASK [edge C] [top C] [bottom C] [fill C]")
            for i in range(0, len(rest), 2):
                if rest[i] not in ("edge", "top", "bottom", "fill"):
                    raise RecipeError(f"{rest[i]}: edge, top, bottom or fill")
                tones[rest[i]] = parse_colour(rest[i + 1], self.names)
            if "fill" not in tones:
                raise RecipeError("stamp: a fill colour")
            for t in ("edge", "top", "bottom"):
                tones.setdefault(t, tones["fill"])
        w, h = img.w, img.h

        def on(x, y):
            return 0 <= x < w and 0 <= y < h and m[y * w + x]
        for y in range(h):
            for x in range(w):
                if not m[y * w + x]:
                    continue
                if not (on(x - 1, y) and on(x + 1, y) and on(x, y - 1) and on(x, y + 1)):
                    t = "edge"
                elif not on(x, y - 2):
                    t = "top"
                elif not on(x, y + 2):
                    t = "bottom"
                else:
                    t = "fill"
                img.px[y * w + x] = list(tones[t])

    def op_slide(self, img, args, rows):
        if len(args) < 3 or args[0] not in ("down", "up", "right", "left"):
            raise RecipeError("slide down|up|right|left AT FROM-TO:BY ...")
        d, at = args[0], integer(args[1])
        w, h = img.w, img.h
        long_ = h if d in ("down", "up") else w
        across = w if d in ("down", "up") else h
        if not (0 < at < long_ - 1):
            raise RecipeError(f"slide: {at} isn't inside the image")
        for tok in args[2:]:
            rs, _, bs = tok.partition(":")
            if not bs:
                raise RecipeError(f"{tok}: FROM-TO:BY")
            a0, a1 = ranges(rs)
            by = integer(bs)
            if by < 0 or by >= long_:
                raise RecipeError(f"{tok}: BY is 0 to the image's size")
            for a in range(a0, a1 + 1):
                if not (0 <= a < across):
                    raise RecipeError(f"slide: {a} is outside the image")
                idx = (lambda t, a=a: t * w + a) if d in ("down", "up") else (lambda t, a=a: a * w + t)
                line = [list(img.px[idx(t)]) for t in range(long_)]
                if d in ("down", "right"):
                    for t in range(long_):
                        if t >= at + by:
                            img.px[idx(t)] = list(line[t - by])
                        elif t >= at:
                            img.px[idx(t)] = list(line[at - 1])
                else:
                    for t in range(long_):
                        if t <= at - by:
                            img.px[idx(t)] = list(line[t + by])
                        elif t <= at:
                            img.px[idx(t)] = list(line[at + 1])

    def op_crop(self, img, args, rows):
        if len(args) != 4:
            raise RecipeError("crop X Y W H")
        x0, y0, w, h = (integer(a) for a in args)
        if not (0 < w <= 1024 and 0 < h <= 1024):
            raise RecipeError("crop: 1 to 1024 texels a side")
        out = Img(w, h)
        for y in range(h):
            for x in range(w):
                if 0 <= x0 + x < img.w and 0 <= y0 + y < img.h:
                    out.px[y * w + x] = list(img.at(x0 + x, y0 + y))
        return out

    def op_flip(self, img, args, rows):
        if args not in (["x"], ["y"]):
            raise RecipeError("flip x | flip y")
        out = Img(img.w, img.h)
        for y in range(img.h):
            for x in range(img.w):
                sx, sy = (img.w - 1 - x, y) if args[0] == "x" else (x, img.h - 1 - y)
                out.px[y * img.w + x] = list(img.at(sx, sy))
        return out

    def op_rotate(self, img, args, rows):
        """rotate D: turned D degrees clockwise about the middle, each texel the source texel under its
        centre (whole-number sums on 1/4096ths, so both runners agree), the size kept"""
        if len(args) != 1:
            raise RecipeError("rotate D")
        d = math.radians(integer(args[0]))
        c, s = int(math.floor(math.cos(d) * 4096 + 0.5)), int(math.floor(math.sin(d) * 4096 + 0.5))
        out = Img(img.w, img.h)
        for y in range(img.h):
            dy = 2 * y + 1 - img.h
            for x in range(img.w):
                dx = 2 * x + 1 - img.w
                sx = (c * dx + s * dy + img.w * 4096) // 8192
                sy = (-s * dx + c * dy + img.h * 4096) // 8192
                if 0 <= sx < img.w and 0 <= sy < img.h:
                    out.px[y * img.w + x] = list(img.at(sx, sy))
        return out

    def op_grow(self, img, args, rows):
        """grow R COLOUR: a border round the opaque shape, R texels wide (a round brush: dx^2 + dy^2 <=
        R^2 + R), in the colour; only clear texels change"""
        if len(args) != 2:
            raise RecipeError("grow R COLOUR")
        rad = integer(args[0])
        if not (1 <= rad <= 16):
            raise RecipeError("grow: R is 1 to 16")
        col = parse_colour(args[1], self.names)
        out = img.copy()
        w, h = img.w, img.h
        offs = [(dx, dy) for dy in range(-rad, rad + 1) for dx in range(-rad, rad + 1)
                if dx * dx + dy * dy <= rad * rad + rad]
        for y in range(h):
            for x in range(w):
                if img.px[y * w + x][3] >= 128:
                    continue
                for dx, dy in offs:
                    sx, sy = x + dx, y + dy
                    if 0 <= sx < w and 0 <= sy < h and img.px[sy * w + sx][3] >= 128:
                        out.px[y * w + x] = list(col)
                        break
        return out

    def op_scale(self, img, args, rows):
        if len(args) != 3 or args[2] not in ("nearest", "texel", "smooth"):
            raise RecipeError("scale W H nearest|texel|smooth")
        dw, dh = integer(args[0]), integer(args[1])
        if not (0 < dw <= 1024 and 0 < dh <= 1024):
            raise RecipeError("scale: 1 to 1024 texels a side")
        sw, sh = img.w, img.h
        out = Img(dw, dh)
        for y in range(dh):
            for x in range(dw):
                if args[2] == "nearest":
                    out.px[y * dw + x] = list(img.at((2 * x + 1) * sw // (2 * dw), (2 * y + 1) * sh // (2 * dh)))
                    continue
                if args[2] == "smooth":
                    # bilinear between the four source texels round the texel's centre, by alpha (premultiplied), in
                    # exact integers: positions in 1/(2 dw), 1/(2 dh) of a source texel; in where half covered
                    den_x, den_y = 2 * dw, 2 * dh
                    nx, ny = max(0, (2 * x + 1) * sw - dw), max(0, (2 * y + 1) * sh - dh)
                    x0, y0 = nx // den_x, ny // den_y
                    fx, fy = nx - x0 * den_x, ny - y0 * den_y
                    if x0 >= sw - 1:
                        x0, fx = sw - 1, 0
                    if y0 >= sh - 1:
                        y0, fy = sh - 1, 0
                    xs, ys = (x0, min(x0 + 1, sw - 1)), (y0, min(y0 + 1, sh - 1))
                    wxs, wys = (den_x - fx, fx), (den_y - fy, fy)
                    tw, ta, tc = den_x * den_y, 0, [0, 0, 0]
                    for j in range(2):
                        for i in range(2):
                            c = img.px[ys[j] * sw + xs[i]]
                            wgt = wxs[i] * wys[j]
                            ta += wgt * c[3]
                            for k in range(3):
                                tc[k] += wgt * c[3] * c[k]
                    if ta == 0 or 2 * ta < 255 * tw:
                        continue
                    out.px[y * dw + x] = [(tc[k] + ta // 2) // ta for k in range(3)] + [255]
                    continue
                # the box under the texel, each source texel weighted by its overlap (in 1/dw, 1/dh units)
                # and its alpha; alpha kept 1-bit, as the game's: in where half the box is covered
                tw = ta = 0
                tc = [0, 0, 0]
                for j in range(y * sh // dh, min(sh, ((y + 1) * sh + dh - 1) // dh)):
                    wy = min((j + 1) * dh, (y + 1) * sh) - max(j * dh, y * sh)
                    if wy <= 0:
                        continue
                    for i in range(x * sw // dw, min(sw, ((x + 1) * sw + dw - 1) // dw)):
                        wx = min((i + 1) * dw, (x + 1) * sw) - max(i * dw, x * sw)
                        if wx <= 0:
                            continue
                        c = img.at(i, j)
                        wgt = wx * wy
                        tw += wgt
                        ta += wgt * c[3]
                        for k in range(3):
                            tc[k] += wgt * c[3] * c[k]
                if ta == 0 or 2 * ta < 255 * tw:
                    continue
                out.px[y * dw + x] = [(tc[k] + ta // 2) // ta for k in range(3)] + [255]
        return out

    def op_paste(self, img, args, rows):
        if len(args) not in (3, 4) or (len(args) == 4 and args[3] not in ("over", "copy")):
            raise RecipeError("paste PIECE X Y [over|copy]")
        if args[0] not in self.pieces:
            raise RecipeError(f"no piece {args[0]}")
        p = self.pieces[args[0]]
        x0, y0 = integer(args[1]), integer(args[2])
        copy = len(args) == 4 and args[3] == "copy"
        for y in range(p.h):
            for x in range(p.w):
                tx, ty = x0 + x, y0 + y
                if not (0 <= tx < img.w and 0 <= ty < img.h):
                    continue
                s, d = p.at(x, y), img.px[ty * img.w + tx]
                if copy or s[3] == 255:
                    img.px[ty * img.w + tx] = list(s)
                elif s[3]:
                    a = s[3]
                    img.px[ty * img.w + tx] = [(s[k] * a + d[k] * (255 - a) + 127) // 255 for k in range(3)] + \
                        [a + (d[3] * (255 - a) + 127) // 255]

    def op_alpha(self, img, args, rows):
        if len(args) == 2 and args[0] == "cut":
            t = integer(args[1])
            for c in img.px:
                c[3] = 255 if c[3] >= t else 0
            return
        if len(args) >= 2 and args[0] == "set":
            mask, alts, rest = self.select(args[2:], img, ())
            a = integer(args[1])
            if rest or not (0 <= a <= 255):
                raise RecipeError("alpha set A [in MASK] [where ...]")
            for x, y, c in list(self.each(img, mask, alts, skip_clear=False)):
                c[3] = a
            return
        raise RecipeError("alpha cut T | alpha set A [in MASK] [where ...]")

    def op_mesh(self, img, args, rows):
        """mesh MAP OBJECT pole PART [closed] [turn D] [pitch D] size K at X Y [line COLOUR]:
        a map's object drawn in (draw_mesh), from MAP/d and its textures MAP/t"""
        usage = "mesh MAP OBJECT pole PART [closed] [turn D] [pitch D] size K at X Y [line COLOUR]"
        if len(args) < 2:
            raise RecipeError(usage)
        mp, obj = args[0], args[1]
        pole, closed, turn, pitch, size, at, line = None, False, 0, 0, None, None, None
        i = 2
        while i < len(args):
            k = args[i]
            if k == "closed":
                closed, i = True, i + 1
            elif k in ("pole", "turn", "pitch", "size", "line") and i + 1 < len(args):
                v = args[i + 1]
                if k == "pole":
                    pole = v
                elif k == "turn":
                    turn = integer(v)
                elif k == "pitch":
                    pitch = integer(v)
                elif k == "size":
                    size = num(v)
                else:
                    line = parse_colour(v, self.names)
                i += 2
            elif k == "at" and i + 2 < len(args):
                at, i = (num(args[i + 1]), num(args[i + 2])), i + 3
            else:
                raise RecipeError(f"{k}: not understood ({usage})")
        if pole is None or size is None or at is None or not (0.0 < size <= 256.0):
            raise RecipeError(usage)
        d = self.disc.read(mp + "/d")
        try:
            tris, names = map_mesh(d, obj, pole)
        except (struct.error, IndexError, ValueError):
            raise RecipeError(f"{mp}/d: cut short")
        textures = {}
        for t in tris:
            if t[5] and t[5] not in textures and t[5] in names:
                try:
                    textures[t[5]] = tpl_image(self.disc.read(mp + "/t"), names.index(t[5]))
                except struct.error:
                    raise RecipeError(f"{mp}/t: cut short")
        draw_mesh(img, tris, textures, closed, turn, pitch, size, at[0], at[1], line)


def ramp_colour(stops, t):
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = max(0.0, (t - t0) / (t1 - t0))
            return [clamp(rnd(float(c0[k]) * (1 - f) + float(c1[k]) * f)) for k in range(3)]
    return list(stops[-1][1][:3])


# ------------------------------------------------------------------- output --
def save(img, path):
    from PIL import Image
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    im = Image.new("RGBA", (img.w, img.h))
    im.putdata([tuple(p) for p in img.px])
    im.save(path)
    return im


def find_disc():
    if os.environ.get("TTYD_DISC_DIR"):
        return os.environ["TTYD_DISC_DIR"]
    for base in (ROOT, os.path.join(os.path.dirname(ROOT), "ttyd-port")):
        path = os.path.join(base, "data", "G8ME01")
        if os.path.isdir(path):
            return path
    sys.exit("no disc: pass --disc (the extracted disc's folder) or set TTYD_DISC_DIR")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recipes", nargs="+")
    ap.add_argument("--disc")
    ap.add_argument("--out", default="recipe_out")
    ap.add_argument("--only")
    ap.add_argument("--zoom", type=int, default=4)
    a = ap.parse_args()
    disc = Disc(a.disc or find_disc())
    run = Runner(disc)
    made, bad = [], 0
    for path in a.recipes:
        text = open(path, encoding="utf-8").read()
        for name in run.images(text):
            if a.only and a.only != name:
                continue
            try:
                img = run.make(text, name)
            except DrawnOnDisc:
                print(f"{name}: drawn on this disc")
                continue
            except NotForDisc:
                print(f"{name}: not made on this disc")
                continue
            except RecipeError as e:
                print(f"{os.path.basename(path)}: {name}: {e}")
                bad += 1
                continue
            save(img, os.path.join(a.out, *name.split("/")))
            made.append((name, img, None))
            print(f"{name}: {img.w}x{img.h}")
    if a.zoom and made:
        from PIL import Image
        k = a.zoom
        wid = sum(im.w * k + 8 for _, im, _ in made) + 8
        hei = max(im.h for _, im, _ in made) * k + 16
        sheet = Image.new("RGBA", (wid, hei), (120, 116, 130, 255))
        x = 8
        for name, im, _ in made:
            pil = Image.new("RGBA", (im.w, im.h))
            pil.putdata([tuple(p) for p in im.px])
            sheet.alpha_composite(pil.resize((im.w * k, im.h * k), Image.NEAREST), (x, 8))
            x += im.w * k + 8
        sheet.convert("RGB").save(os.path.join(a.out, "preview.png"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
