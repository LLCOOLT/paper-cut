"""Chapter 1's volcano: lava textures for Keelhaul Key's maps (muj_*), which
Super Luigi's Rumblebump Volcano borrows (docs/SUPER_LUIGI.md, docs/LUIGI_ANIMS.md
"Lava textures"). Our own drawings, made here from code (tileable noise and
cells), at the sizes of the textures they replace and named as Dolphin names
those (the texture pack format, docs/MODDING.md), so they drop into a mod's
textures/ folder. They apply wherever those textures are drawn: the story
code turns the set on only for the chapter's maps.

  python tools/port/lava_textures.py [OUT_DIR]
      (default port/assets/super_luigi/textures/ch1_lava)

Kinds:
  rock    volcanic rock: dark basalt slabs, lit from above, glowing cracks between
  cinder  loose cinders and ash, ember specks
  ash     the jungle floor burnt: ash and cooled crust, faint glowing seams
  lava    molten lava, bubbling: bright cells under a dark crust, bubbles
  sky     the sea backdrop: a smoky red sky over a lava sea
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ttyd_anim as T  # noqa: E402 (write_png)

# Dolphin's name of each Keelhaul Key texture (muj_*) and what it becomes
TEXTURES = [
    ("tex1_256x256_bcdcfd65604e746b_14", "rock", 1),     # cliff rock slabs
    ("tex1_256x256_c193138b2e0f1d18_14", "rock", 2),     # rock wall
    ("tex1_256x256_3436b9e79c6c123c_14", "rock", 3),     # rock with a mossy top
    ("tex1_256x256_e2874ba2237fc18d_14", "rock", 4),     # earth cliff with roots
    ("tex1_128x128_5b3c34d42ce8242e_14", "cinder", 5),   # gravel
    ("tex1_16x256_c5995b073f1f671d_14", "rock", 6),      # banded earth
    ("tex1_64x64_6ede679aa57dd8a3_14", "ash", 7),        # jungle floor
    ("tex1_128x128_343f0bb84725788e_14", "ash", 8),      # mossy ground
    ("tex1_64x64_37b7ec66fe27686f_14", "ash", 9),        # sand
    ("tex1_64x64_19ddb822a721f84d_14", "ash", 10),       # pale sand
    ("tex1_128x128_cdfc80efab9f37f0_14", "lava", 11),    # shallow water
    ("tex1_128x128_e44b6fe0f0c01df3_4", "lava", 12),     # sea, bubbles
    ("tex1_256x256_a4ad7da2b1f042a1_14", "lava", 13),    # sea, bubbles
    ("tex1_256x256_a5055043c65d336a_2", "lava", 15),     # the sea's light layer
    ("tex1_32x32_409f58a8bff298b5_5", "lava", 16),       # wave crests
    ("tex1_512x480_5a716df3deffa6bc_14", "sky", 14),     # the sea and sky backdrop
]


# -------------------------------------------------------------- noise ----

def _hash(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0x1000000)


def _vnoise(x, y, period, seed):
    """periodic value noise, x y in cells, repeating every `period` cells"""
    x0, y0 = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - x0, y - y0
    sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def v(ix, iy):
        return _hash(ix % period, iy % period, seed)
    a = v(x0, y0) + (v(x0 + 1, y0) - v(x0, y0)) * sx
    b = v(x0, y0 + 1) + (v(x0 + 1, y0 + 1) - v(x0, y0 + 1)) * sx
    return a + (b - a) * sy


def _fbm(u, v, base, seed, octaves=4):
    """fractal noise over the tile (u, v in 0..1), tiling"""
    t, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        p = base << o
        t += _vnoise(u * p, v * p, p, seed + o * 17) * amp
        tot += amp
        amp *= 0.5
    return t / tot


class _Cells:
    """periodic Voronoi cells over the tile: n x m jittered points"""

    def __init__(self, n, m, seed, jitter=0.8):
        self.n, self.m = n, m
        self.pts = {}
        for i in range(n):
            for j in range(m):
                self.pts[(i, j)] = ((i + 0.5 + (_hash(i, j, seed) - 0.5) * jitter) / n,
                                    (j + 0.5 + (_hash(i, j, seed + 1) - 0.5) * jitter) / m)

    def at(self, u, v):
        """(nearest distance, second distance, cell id) in tile units"""
        ci, cj = int(u * self.n), int(v * self.m)
        best = [(9.0, None), (9.0, None)]
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                i, j = ci + di, cj + dj
                px, py = self.pts[(i % self.n, j % self.m)]
                px += math.floor(i / self.n)
                py += math.floor(j / self.m)
                d = math.hypot((u - px) * self.n, (v - py) * self.m)
                if d < best[0][0]:
                    best = [(d, (i % self.n, j % self.m)), best[0]]
                elif d < best[1][0]:
                    best[1] = (d, (i % self.n, j % self.m))
        return best[0][0], best[1][0], best[0][1]


def _mix(a, b, t):
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def _smooth(e0, e1, x):
    t = (x - e0) / (e1 - e0)
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    return t * t * (3 - 2 * t)


# -------------------------------------------------------------- kinds ----

ROCK_DARK, ROCK_MID, ROCK_LIGHT = (38, 30, 36), (72, 58, 62), (112, 92, 88)
OUTLINE = (24, 18, 22)
GLOW_RIM, GLOW_CORE = (232, 72, 24), (255, 214, 92)
LAVA_DEEP, LAVA_MID, LAVA_HOT = (176, 32, 12), (240, 104, 20), (255, 220, 96)
CRUST = (52, 22, 18)


def rock(w, h, seed):
    """basalt slabs: each a flat-shaded cell lit from the top left, dark outlines,
    glowing cracks along a few of the seams"""
    cells = _Cells(max(2, w // 40), max(2, h // 40), seed)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            u, v = (x + 0.5) / w, (y + 0.5) / h
            d1, d2, cid = cells.at(u, v)
            edge = d2 - d1
            tone = _hash(cid[0], cid[1], seed + 5)
            base = _mix(ROCK_DARK, ROCK_MID, 0.4 + 0.6 * tone)
            # the slab's lit top left: sample the cell a little up-left
            d1b, d2b, cidb = cells.at((u - 0.022) % 1.0, (v - 0.022) % 1.0)
            if cidb != cid:
                base = _mix(base, ROCK_LIGHT, 0.7)
            else:
                _d, _e, cids = cells.at((u + 0.022) % 1.0, (v + 0.022) % 1.0)
                if cids != cid:
                    base = _mix(base, (18, 12, 16), 0.5)  # its shaded bottom right
            n = _fbm(u, v, 8, seed + 9, 3)
            base = _mix(base, ROCK_DARK, (n - 0.5) * 0.6)
            col = base
            if edge < 0.10:
                col = OUTLINE
                hot = _hash(min(cid, cidb or cid)[0], 7, seed) > 0.45 and _fbm(u, v, 4, seed + 3, 2) > 0.45
                if hot:
                    col = _mix(GLOW_RIM, GLOW_CORE, 1.0 - edge / 0.06) if edge < 0.06 else GLOW_RIM
            elif edge < 0.16:
                col = _mix(OUTLINE, base, (edge - 0.10) / 0.06)
            row.append(tuple(int(c) for c in col) + (255,))
        rows.append(row)
    return rows


def cinder(w, h, seed):
    rows = []
    cells = _Cells(max(2, w // 10), max(2, h // 10), seed, 0.9)
    for y in range(h):
        row = []
        for x in range(w):
            u, v = (x + 0.5) / w, (y + 0.5) / h
            d1, d2, cid = cells.at(u, v)
            tone = _hash(cid[0], cid[1], seed)
            col = _mix((34, 28, 30), (92, 74, 70), tone * (1.0 - _smooth(0.25, 0.55, d1)))
            if d2 - d1 < 0.12:
                col = _mix(OUTLINE, col, (d2 - d1) / 0.12)
            if tone > 0.93 and d1 < 0.3:
                col = _mix(GLOW_CORE, GLOW_RIM, d1 / 0.3)  # an ember
            row.append(tuple(int(c) for c in col) + (255,))
        rows.append(row)
    return rows


def ash(w, h, seed):
    cells = _Cells(max(2, w // 22), max(2, h // 22), seed, 0.9)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            u, v = (x + 0.5) / w, (y + 0.5) / h
            n = _fbm(u, v, 4, seed, 4)
            col = _mix((54, 44, 44), (104, 90, 84), n)
            d1, d2, cid = cells.at(u, v)
            e = d2 - d1
            if e < 0.07:
                col = _mix((30, 22, 22), col, e / 0.07)
                if _hash(cid[0], cid[1], seed + 3) > 0.7:
                    col = _mix(GLOW_RIM, col, e / 0.07)
            if _hash(x % w, y % h, seed + 11) > 0.996:
                col = GLOW_CORE  # a spark
            row.append(tuple(int(c) for c in col) + (255,))
        rows.append(row)
    return rows


def lava(w, h, seed):
    """molten: hot cells, a darker cooling crust along their seams, round bubbles"""
    cells = _Cells(max(2, w // 32), max(2, h // 32), seed, 0.9)
    bub = _Cells(max(2, w // 24), max(2, h // 24), seed + 50, 0.9)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            u, v = (x + 0.5) / w, (y + 0.5) / h
            n = _fbm(u, v, 4, seed, 4)
            d1, d2, cid = cells.at(u, v)
            heat = _smooth(0.0, 0.5, d2 - d1) * (0.6 + 0.4 * n)
            col = _mix(LAVA_DEEP, LAVA_MID, heat * 1.6)
            col = _mix(col, LAVA_HOT, (heat - 0.55) * 2.2)
            if d2 - d1 < 0.05:
                col = _mix(CRUST, LAVA_DEEP, (d2 - d1) / 0.05)
            b1, _b2, bid = bub.at(u, v)
            r = 0.18 + 0.2 * _hash(bid[0], bid[1], seed + 7)
            if _hash(bid[0], bid[1], seed + 8) > 0.55 and abs(b1 - r) < 0.06:
                col = _mix(LAVA_HOT, (255, 250, 210), 1.0 - abs(b1 - r) / 0.06)  # a bubble's rim
            row.append(tuple(int(c) for c in col) + (255,))
        rows.append(row)
    return rows


def sky(w, h, seed):
    """a smoky red sky above, the lava sea below the horizon (60% down)"""
    horizon = int(h * 0.6)
    sea = lava(w, h - horizon, seed)
    rows = []
    for y in range(horizon):
        t = y / float(horizon)
        row = []
        for x in range(w):
            u = (x + 0.5) / w
            n = _fbm(u, t * 0.5, 4, seed + 21, 4)
            col = _mix((60, 22, 30), (196, 74, 38), t * 0.9 + (n - 0.5) * 0.3)
            smoke = _smooth(0.55, 0.75, n) * (1.0 - t * 0.6)
            col = _mix(col, (70, 60, 64), smoke)
            if horizon - y < 4:
                col = _mix(col, LAVA_HOT, 1.0 - (horizon - y) / 4.0)
            row.append(tuple(int(c) for c in col) + (255,))
        rows.append(row)
    return rows + sea


KINDS = {"rock": rock, "cinder": cinder, "ash": ash, "lava": lava, "sky": sky}


def main(argv):
    out = argv[0] if argv else os.path.join(os.path.dirname(os.path.dirname(HERE)), "port", "assets", "super_luigi",
                                             "textures", "ch1_lava_drawn")
    os.makedirs(out, exist_ok=True)
    for name, kind, seed in TEXTURES:
        w, h = (int(v) for v in name.split("_")[1].split("x"))
        T.write_png(os.path.join(out, name + ".png"), KINDS[kind](w, h, seed))
        print(name, kind)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
