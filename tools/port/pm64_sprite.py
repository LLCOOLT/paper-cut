#!/usr/bin/env python3
"""Paper Mario 64's NPC sprites, read from the player's own ROM for our tools (pm64_poses.py sheet): the rasters,
palettes and animations, played as the port plays them (port/src/common/pm64_pc.c portPm64AnimStep, one step a
60 fps frame). Nothing of the ROM is written or kept anywhere: everything stays in memory.

The ROM: $TTYD_PM64_ROM, --rom, else the first *.z64 beside this checkout.

  pm64_sprite.py info SPRITE [--rom ROM]        rasters, palettes, animations and each one's loop
Sprites by the ROM's NPC sprite number: 0..7 the field partners (Goombario, Kooper, Bombette, Parakarry, Bow,
Watt, Sushie, Lakilester), 8..15 their battle sprites (pm64_partner_pc.c kWho).
"""
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SPRITES_AT = 0x1943000  # (the ROM's sprite section)


def _be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def _be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def _sx12(v):
    v &= 0xFFF
    return v - 0x1000 if v & 0x800 else v


def _sx16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def find_rom(path=None):
    for p in (path, os.environ.get("TTYD_PM64_ROM")):
        if p and os.path.isfile(p):
            return p
    for root in (os.path.dirname(REPO), REPO):
        hits = sorted(glob.glob(os.path.join(root, "*.z64")))
        if hits:
            return hits[0]
    return None


def yay0(s):
    if s[:4] != b"Yay0":
        raise ValueError("not Yay0")
    total, link, chunk = _be32(s, 4), _be32(s, 8), _be32(s, 12)
    d = bytearray()
    mp, bits, mask = 16, 0, 0
    while len(d) < total:
        if bits == 0:
            mask, mp, bits = _be32(s, mp), mp + 4, 32
        if mask & 0x80000000:
            d.append(s[chunk])
            chunk += 1
        else:
            v = _be16(s, link)
            link += 2
            dist = (v & 0xFFF) + 1
            n = v >> 12
            if n == 0:
                n = s[chunk] + 0x12
                chunk += 1
            else:
                n += 2
            for _ in range(n):
                if len(d) >= total:
                    break
                d.append(d[-dist])
        mask = (mask << 1) & 0xFFFFFFFF
        bits -= 1
    return bytes(d)


def _list(d, at, most=256):
    out = []
    while at + 4 <= len(d) and len(out) < most:
        v = _be32(d, at)
        if v == 0xFFFFFFFF:
            break
        out.append(v)
        at += 4
    return out


_ROMS = {}


class Sprite:
    def __init__(self, index, rom=None):
        path = find_rom(rom)
        if path is None:
            raise SystemExit("pm64_sprite: no PM64 ROM ($TTYD_PM64_ROM or --rom)")
        if path not in _ROMS:
            _ROMS[path] = open(path, "rb").read()
        spr = _ROMS[path][SPRITES_AT:]
        t = spr[_be32(spr, 0x18) + 0x10:]
        a, b = _be32(t, index * 4), _be32(t, index * 4 + 4)
        self.d = d = yay0(t[a:b])
        self.rasters = _list(d, _be32(d, 0))
        self.palettes = _list(d, _be32(d, 4))
        self.anims = _list(d, 0x10)

    def raster(self, i):
        """(pixel offset, width, height, its own palette)"""
        r = self.d[self.rasters[i]:]
        return _be32(r, 0), r[4], r[5], r[6]

    def colours(self, pal):
        po = self.palettes[pal]
        out = []
        for k in range(16):
            v = _be16(self.d, po + k * 2)
            out.append((((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31,
                        255 if v & 1 else 0))
        return out

    def indices(self, i):
        """(w, h, rows of palette indices)"""
        off, w, h, _p = self.raster(i)
        rows = []
        for y in range(h):
            row = []
            for x in range(w):
                at = y * w + x
                c = self.d[off + (at >> 1)]
                row.append(c & 15 if at & 1 else c >> 4)
            rows.append(row)
        return w, h, rows

    def rgba(self, i, pal=-1):
        """(w, h, rows of RGBA tuples) in palette pal (-1: its own)"""
        _off, _w, _h, own = self.raster(i)
        cols = self.colours(own if pal < 0 or pal >= len(self.palettes) else pal)
        w, h, idx = self.indices(i)
        return w, h, [[cols[c] for c in row] for row in idx]

    def cut(self, i, pal=-1, rect=None, idx=None, edge=None, not_near=None):
        """the rect of raster i (default all) with only idx's pixels (and edge's touching one, none of not_near's),
        as (w, h, rows of RGBA): pm64_pc.c portPm64RasterTextureCut, cropped"""
        w, h, ci = self.indices(i)
        _w, _h, px = self.rgba(i, pal)
        keep_all = not idx and not edge
        idx, edge, nn = set(idx or range(16)), set(edge or ()), set(not_near or ())
        x0, y0, rw, rh = rect if rect and rect[2] > 0 else (0, 0, w, h)
        out = []
        for y in range(y0, y0 + rh):
            row = []
            for x in range(x0, x0 + rw):
                if not (0 <= x < w and 0 <= y < h):
                    row.append((0, 0, 0, 0))
                    continue
                c = ci[y][x]
                keep = keep_all or c in idx
                if not keep and c in edge:
                    nb = [ci[y + dy][x + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                          if (dx or dy) and 0 <= x + dx < w and 0 <= y + dy < h]
                    keep = any(n in idx for n in nb) and not any(n in nn for n in nb)
                row.append(px[y][x] if keep else (0, 0, 0, 0))
            out.append(row)
        return rw, rh, out

    def comps(self, anim):
        out = []
        for c in _list(self.d, self.anims[anim], 32):
            at, nbytes = _be32(self.d, c), _be16(self.d, c + 4)
            base = (_sx16(_be16(self.d, c + 6)), _sx16(_be16(self.d, c + 8)), _sx16(_be16(self.d, c + 10)))
            out.append(([_be16(self.d, at + 2 * k) for k in range(nbytes // 2)], base))
        return out


class _Part:
    def __init__(self, cmds, base):
        self.cmds, self.base = cmds, base
        self.raster = self.palette = self.parent = -1
        self.pc, self.wait, self.loops = 0, 0.0, 0
        self.o, self.r, self.s = [0, 0, 0], [0, 0, 0], [1.0, 1.0, 1.0]
        self.c = (0, 0, 0)  # (its place with its parent's: compPos)

    def step(self, frames):
        cmd, n = self.cmds, len(self.cmds)
        if n <= 0:
            return
        self.wait -= frames
        goto_at, guard, changed = -1, 0, 0
        P = R = S = None
        while self.wait <= 0 and guard < 1024:
            guard += 1
            if self.pc < 0 or self.pc >= n:
                self.pc = 0
                break
            v = cmd[self.pc]
            op = v & 0xF000
            if op == 0:
                self.pc += 1
                self.wait = float(v & 0xFFF) or 4095.0
                self.o, self.r, self.s = [0, 0, 0], [0, 0, 0], [1.0, 1.0, 1.0]
            elif op == 0x1000:
                self.pc += 1
                self.raster = -1 if (v & 0xFFF) == 0xFFF else v & 0xFFF
                self.palette = -1
            elif op == 0x2000:
                self.pc = _sx12(v)
                if self.pc == goto_at:
                    self.pc, self.wait = 0, 1.0
                goto_at = self.pc
            elif op == 0x3000:
                self.pc += 1
                if (v & 0xF) <= 1 and self.pc + 3 <= n:
                    P = [_sx16(cmd[self.pc]), _sx16(cmd[self.pc + 1]), _sx16(cmd[self.pc + 2])]
                    self.pc += 3
                    changed |= 1
            elif op == 0x4000:
                if self.pc + 3 > n:
                    self.pc = n
                    break
                R = [_sx12(v), _sx16(cmd[self.pc + 1]), _sx16(cmd[self.pc + 2])]
                self.pc += 3
                changed |= 2
            elif op == 0x5000:
                self.pc += 1
                if self.pc < n:
                    k = cmd[self.pc] / 100.0
                    S = list(self.s) if S is None else S
                    m = v & 0xF
                    if m == 0:
                        S = [k, k, k]
                    elif m <= 3:
                        S[m - 1] = k
                    self.pc += 1
                    changed |= 4
            elif op == 0x6000:
                self.pc += 1
                self.palette = -1 if (v & 0xFFF) == 0xFFF else v & 0xFFF
            elif op == 0x7000:
                if self.loops != 0:
                    self.loops -= 1
                    if self.loops == 0:
                        self.pc += 2
                        continue
                elif self.pc + 1 < n:
                    self.loops = cmd[self.pc + 1]
                self.pc = _sx12(v)
            elif op == 0x8000:
                self.pc += 1
                if (v & 0xF00) == 0x100:
                    self.parent = v & 0xFF
            else:
                self.pc, self.wait = 0, 1.0
                break
        if changed & 1:
            self.o = P
        if changed & 2:
            self.r = R
        if changed & 4:
            self.s = [1.0, 1.0, 1.0] if S is None else S


def play(spr, anim, ticks):
    """[tick][component] = (raster, palette, x, y, z, (rx, ry, rz), (sx, sy, sz)), as the port plays the row"""
    parts = [_Part(c, b) for c, b in spr.comps(anim)]
    out = []
    for t in range(ticks):
        for p in parts:
            p.step(1.0 if t else 0.0)
        st = []
        for i, p in enumerate(parts):
            # (PM64's spr_component_update_finish, as pm64_pc.c: in component order, a parent before its child
            #  gives this step's place, one after it the last step's)
            x, y, z = p.o
            if 0 <= p.parent < len(parts):
                q = parts[p.parent].c
                x, y, z = x + q[0], y + q[1], z + q[2]
            p.c = (x, y, z)
            st.append((p.raster, p.palette, x + p.base[0], y + p.base[1], z + p.base[2], tuple(p.r),
                       tuple(round(v, 2) for v in p.s)))
        out.append(st)
    return out


def period(states, start=90):
    n = len(states)
    for per in range(1, (n - start) // 2 + 1):
        if all(states[t] == states[t + per] for t in range(start, n - per)):
            return per
    return -1


def main(argv):
    if len(argv) < 2 or argv[0] != "info":
        print(__doc__)
        return 2
    rom = argv[argv.index("--rom") + 1] if "--rom" in argv else None
    s = Sprite(int(argv[1]), rom)
    print("rasters %d, palettes %d, animations %d" % (len(s.rasters), len(s.palettes), len(s.anims)))
    for i in range(len(s.rasters)):
        _o, w, h, p = s.raster(i)
        print("  raster %d: %dx%d, palette %d" % (i, w, h, p))
    for a in range(len(s.anims)):
        st = play(s, a, 420)
        print("  anim %d: %d components, loop %d" % (a, len(s.comps(a)), period(st)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
