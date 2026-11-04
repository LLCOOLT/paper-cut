"""Chapter 3's texture swaps (Super Luigi; docs/super_luigi/ch3.md, New assets 8):
the art's racing posters and Circuit Break sign
(port/assets/super_luigi/art/ch3_posters/) as texture packs named as Dolphin
names the textures they replace (docs/MODDING.md, "textures/"), in
port/assets/super_luigi/textures/:

  ch3_posters/             tou_02's other fighter posters -> racing posters,
                           yuu_02's "Pianta Parlor" neon -> "Circuit Break"
  ch3_posters_revvo/       tou_02's Glitz Battle posters -> Revvo's (before the race)
  ch3_posters_revvo_after/ the same names -> Revvo's, the fourth now Big Green 01
                           (after the race: the story code turns this set on
                           in place of ch3_posters_revvo; the two give the
                           same names, so only one may be on)

  python tools/port/ch3_textures.py [DISC_FILES_DIR]
      copies the art under those names; with the disc's files folder, first
      checks each name against the disc's texture (XXH64 of its data, as
      Dolphin hashes it)
"""
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "..", "port", "assets", "super_luigi", "art", "ch3_posters")
OUT = os.path.join(HERE, "..", "..", "port", "assets", "super_luigi", "textures")

# (set, art, map, image in the map's t, Dolphin's name)
SWAPS = [
    ("ch3_posters", "posters_racing", "tou_02", 4, "tex1_128x128_eb72945d6523abbd_14"),
    ("ch3_posters", "posters_racing", "tou_02", 21, "tex1_128x128_91c6bc1d969a6b84_14"),
    ("ch3_posters", "sign_circuit_break", "yuu_02", 18, "tex1_320x192_3973193af700956a_14"),
    ("ch3_posters_revvo", "posters_revvo", "tou_02", 3, "tex1_128x128_e607934f6c2114a8_14"),
    ("ch3_posters_revvo", "posters_revvo", "tou_02", 18, "tex1_128x128_3caed24c58f64d58_14"),
    ("ch3_posters_revvo_after", "posters_revvo_after", "tou_02", 3, "tex1_128x128_e607934f6c2114a8_14"),
    ("ch3_posters_revvo_after", "posters_revvo_after", "tou_02", 18, "tex1_128x128_3caed24c58f64d58_14"),
]

_M = (1 << 64) - 1
_P = (11400714785074694791, 14029467366897019727, 1609587929392839161, 9650029242287828579, 2870177450012600261)


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _M


def _round(acc, v):
    return (_rotl((acc + v * _P[1]) & _M, 31) * _P[0]) & _M


def xxh64(d, seed=0):
    n, i = len(d), 0
    if n >= 32:
        v = [(seed + _P[0] + _P[1]) & _M, (seed + _P[1]) & _M, seed, (seed - _P[0]) & _M]
        while i + 32 <= n:
            for k in range(4):
                v[k] = _round(v[k], struct.unpack_from("<Q", d, i + 8 * k)[0])
            i += 32
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & _M
        for k in range(4):
            h = ((h ^ _round(0, v[k])) * _P[0] + _P[3]) & _M
    else:
        h = (seed + _P[4]) & _M
    h = (h + n) & _M
    while i + 8 <= n:
        h = (_rotl(h ^ _round(0, struct.unpack_from("<Q", d, i)[0]), 27) * _P[0] + _P[3]) & _M
        i += 8
    if i + 4 <= n:
        h = (_rotl(h ^ ((struct.unpack_from("<I", d, i)[0] * _P[0]) & _M), 23) * _P[1] + _P[2]) & _M
        i += 4
    while i < n:
        h = (_rotl(h ^ ((d[i] * _P[4]) & _M), 11) * _P[0]) & _M
        i += 1
    h ^= h >> 33
    h = (h * _P[1]) & _M
    h ^= h >> 29
    h = (h * _P[2]) & _M
    return h ^ (h >> 32)


def dolphin_name(tpl, i):
    """a CMPR texture's name as Dolphin gives it"""
    tab = struct.unpack_from(">I", tpl, 8)[0]
    th = struct.unpack_from(">I", tpl, tab + 8 * i)[0]
    h, w, fmt, off = struct.unpack_from(">HHII", tpl, th)
    return "tex1_%dx%d_%016x_%d" % (w, h, xxh64(tpl[off:off + w * h // 2]), fmt)


def main(argv):
    disc = argv[0] if argv else None
    for s, art, m, i, name in SWAPS:
        if disc:
            got = dolphin_name(open(os.path.join(disc, "m", m, "t"), "rb").read(), i)
            if got != name:
                print("%s image %d is %s, not %s" % (m, i, got, name))
                return 1
        os.makedirs(os.path.join(OUT, s), exist_ok=True)
        shutil.copyfile(os.path.join(ART, art + ".png"), os.path.join(OUT, s, name + ".png"))
        print("%-24s %s.png <- %s" % (s, name, art))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
