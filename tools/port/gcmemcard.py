#!/usr/bin/env python3
"""Files out of raw GameCube memory card images (.raw, as Dolphin keeps them)
into .gci files (the 64-byte directory entry, then the file's blocks): the
form the port's card (Aurora's, and the headless one) reads from
<user>/<region>/Card A/.

Card layout: 0x2000-byte blocks; block 0 the header, blocks 1-2 the
directory and its backup, blocks 3-4 the block allocation table and its
backup (the copy with the higher update counter is current); files start at
block 5. A directory entry is 64 bytes (game code, maker, file name, ...,
first block at 0x36, block count at 0x38); the table chains a file's blocks.

usage: gcmemcard.py list CARD.raw
       gcmemcard.py extract CARD.raw OUT.gci [--game G8MJ]
       gcmemcard.py tree RAW_DIR GCI_DIR [--game G8MJ]   (every .raw below RAW_DIR)
"""
import os
import struct
import sys

BLOCK = 0x2000


def current(img, a, b, counter_at):
    """The current one of two copies (blocks a and b): the higher update counter."""
    ca = struct.unpack_from(">h", img, a * BLOCK + counter_at)[0]
    cb = struct.unpack_from(">h", img, b * BLOCK + counter_at)[0]
    return a if ca >= cb else b


def entries(img):
    d = current(img, 1, 2, 0x1FFA) * BLOCK
    for i in range(127):
        e = img[d + i * 64: d + i * 64 + 64]
        if e[:4] == b"\xff\xff\xff\xff":
            continue
        name = e[8:40].split(b"\0")[0].decode("ascii", "replace")
        first, count = struct.unpack_from(">HH", e, 0x36)
        yield e, e[:4].decode("ascii", "replace"), name, first, count


def blocks(img, first, count):
    bat = current(img, 3, 4, 0x04) * BLOCK
    out, b = [], first
    for _ in range(count):
        if b < 5 or b * BLOCK >= len(img):
            raise ValueError(f"bad block {b}")
        out.append(img[b * BLOCK:(b + 1) * BLOCK])
        b = struct.unpack_from(">H", img, bat + 0x0A + (b - 5) * 2)[0]
    return b"".join(out)


def extract(raw, out, game=None):
    img = open(raw, "rb").read()
    for e, code, name, first, count in entries(img):
        if game and code != game:
            continue
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, "wb") as f:
            f.write(e + blocks(img, first, count))
        return code, name, count
    return None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    game = sys.argv[sys.argv.index("--game") + 1] if "--game" in sys.argv else None
    if sys.argv[1] == "list":
        for e, code, name, first, count in entries(open(sys.argv[2], "rb").read()):
            print(f"{code} {name:32s} blocks {count} at {first}")
    elif sys.argv[1] == "extract":
        print(extract(sys.argv[2], sys.argv[3], game))
    elif sys.argv[1] == "tree":
        n = bad = 0
        for root, _, files in os.walk(sys.argv[2]):
            for fn in sorted(files):
                if not fn.lower().endswith(".raw"):
                    continue
                rel = os.path.relpath(os.path.join(root, fn), sys.argv[2])
                out = os.path.join(sys.argv[3], os.path.splitext(rel)[0] + ".gci")
                try:
                    r = extract(os.path.join(root, fn), out, game)
                except (ValueError, struct.error) as ex:
                    r, why = None, str(ex)
                else:
                    why = "no such file on the card"
                if r:
                    n += 1
                else:
                    bad += 1
                    print(f"skipped {rel}: {why}")
        print(f"gcmemcard: {n} files extracted, {bad} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
