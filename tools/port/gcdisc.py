#!/usr/bin/env python3
"""Read single files out of a plain GameCube disc image (.iso/.gcm) by path,
without extracting the whole disc (extract_disc.py does that).

  gcdisc.py DISC.iso a/c_luigi [a/a_mario ...] -o OUT_DIR
  gcdisc.py DISC.iso --list a/        (paths under a directory)

As a module: Disc(path).read("a/c_luigi") -> bytes.
"""
import argparse
import os
import struct
import sys


def _u32(b, at):
    return struct.unpack_from(">I", b, at)[0]


class Disc:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            boot = f.read(0x440)
            if _u32(boot, 0x1C) != 0xC2339F3D:
                raise ValueError(f"{path} isn't a plain GameCube image (convert .rvz/.ciso to .iso first)")
            self.game_id = boot[0:6].decode("ascii", "replace")
            fst_off, fst_size = _u32(boot, 0x424), _u32(boot, 0x428)
            f.seek(fst_off)
            fst = f.read(fst_size)
        count = _u32(fst, 8)
        names = fst[count * 12:]
        self.files = {}  # "dir/name" -> (offset, size)
        # walk the FST: directory entries hold their parent and the index past their last child
        ends = [(count, "")]
        for i in range(1, count):
            while ends and i >= ends[-1][0]:
                ends.pop()
            word = _u32(fst, i * 12)
            is_dir = word >> 24
            name_off = word & 0xFFFFFF
            name = names[name_off:names.index(b"\0", name_off)].decode("ascii", "replace")
            prefix = ends[-1][1] if ends else ""
            full = prefix + name
            if is_dir:
                ends.append((_u32(fst, i * 12 + 8), full + "/"))
            else:
                self.files[full] = (_u32(fst, i * 12 + 4), _u32(fst, i * 12 + 8))

    def read(self, name):
        if name not in self.files:
            raise KeyError(f"{name} is not on {self.path}")
        off, size = self.files[name]
        with open(self.path, "rb") as f:
            f.seek(off)
            return f.read(size)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("paths", nargs="*")
    ap.add_argument("-o", "--out", default=".")
    ap.add_argument("--list", metavar="DIR")
    a = ap.parse_args()
    d = Disc(a.disc)
    if a.list is not None:
        for name in sorted(d.files):
            if name.startswith(a.list):
                print(name, d.files[name][1])
        return
    for p in a.paths:
        dst = os.path.join(a.out, p)
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        with open(dst, "wb") as f:
            f.write(d.read(p))
        print(dst)


if __name__ == "__main__":
    sys.exit(main())
