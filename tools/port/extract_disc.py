#!/usr/bin/env python3
"""Extract a GameCube disc image (.iso/.gcm) the way Dolphin does: files/
(every file of the disc's file system) and sys/ (boot.bin, bi2.bin,
apploader.img, main.dol, fst.bin). The headless build and the tests that play
the game read this ($TTYD_DATA_DIR, data/<version>; docs/PC_PORT.md).

  extract_disc.py DISC.iso OUT_DIR

Only plain images: a compressed one (.rvz, .ciso, .gcz, .wia) is converted
to .iso first (Dolphin: right-click > Convert File). Files already there with
the right size are left as they are, so a second run finishes quickly.
"""
import os
import struct
import sys


def u32(b, at):
    return struct.unpack_from(">I", b, at)[0]


def copy(disc, offset, size, path):
    """size bytes at offset to path (kept if it's there with that size)."""
    if os.path.isfile(path) and os.path.getsize(path) == size:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    disc.seek(offset)
    with open(path + ".part", "wb") as out:
        left = size
        while left:
            chunk = disc.read(min(left, 1 << 20))
            if not chunk:
                sys.exit(f"extract_disc: the image ends early ({path})")
            out.write(chunk)
            left -= len(chunk)
    os.replace(path + ".part", path)


def dol_size(head):
    """A DOL's size: the end of its furthest text or data section."""
    end = 0x100
    for i in range(18):  # 7 text sections, then 11 data sections
        off, size = u32(head, i * 4), u32(head, 0x90 + i * 4)
        if size:
            end = max(end, off + size)
    return end


def extract(image, out):
    with open(image, "rb") as disc:
        boot = disc.read(0x440)
        if u32(boot, 0x1C) != 0xC2339F3D:
            sys.exit(f"extract_disc: {image} isn't a plain GameCube image (convert an .rvz/.ciso to .iso first)")
        dol_off, fst_off, fst_size = u32(boot, 0x420), u32(boot, 0x424), u32(boot, 0x428)
        disc.seek(0x2440)
        apploader = disc.read(0x20)
        disc.seek(dol_off)
        dol = disc.read(0x100)
        disc.seek(fst_off)
        fst = disc.read(fst_size)

        sysdir = os.path.join(out, "sys")
        copy(disc, 0, 0x440, os.path.join(sysdir, "boot.bin"))
        copy(disc, 0x440, 0x2000, os.path.join(sysdir, "bi2.bin"))
        copy(disc, 0x2440, 0x20 + u32(apploader, 0x14) + u32(apploader, 0x18), os.path.join(sysdir, "apploader.img"))
        copy(disc, dol_off, dol_size(dol), os.path.join(sysdir, "main.dol"))
        copy(disc, fst_off, fst_size, os.path.join(sysdir, "fst.bin"))

        # the file system table: 12-byte entries, then their names. A
        # directory's entry holds its parent and the index past its last
        # entry; a file's, its offset and size.
        count = u32(fst, 8)
        names = count * 12
        def name(i):
            at = names + (u32(fst, i * 12) & 0xFFFFFF)
            return fst[at:fst.index(b"\0", at)].decode("shift_jis", errors="replace")
        files = 0
        stack = [(count, os.path.join(out, "files"))]  # (the index a directory ends at, its path)
        os.makedirs(stack[0][1], exist_ok=True)
        for i in range(1, count):
            while i >= stack[-1][0]:
                stack.pop()
            path = os.path.join(stack[-1][1], name(i))
            if fst[i * 12] & 1:
                os.makedirs(path, exist_ok=True)
                stack.append((u32(fst, i * 12 + 8), path))
            else:
                copy(disc, u32(fst, i * 12 + 4), u32(fst, i * 12 + 8), path)
                files += 1
    print(f"extract_disc: {boot[:6].decode('ascii', errors='replace')}: {files} files to {out}")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    extract(sys.argv[1], sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
