#!/usr/bin/env python3
"""Compare two saves field by field: the story (GSW(0)), every GSWF/GSW/LSWF/LSW, the map, the party and
Mario's pouch, by name (include/evt/evt_story.h, include/mario/mario_pouch.h).

A save is one card slot (0x4000 bytes: tests/port/full_run/checkpoints/), a .gci (the card file the port
writes: <user>/<region>/Card A/01-G8ME-mariost_save_file.gci; --slot picks the slot, default the last
saved) or a card folder. Play time, save counters, the date and what a scene leaves in the save's work
that the next room sets again are left out by default (--all keeps them).

  save_diff.py A B [--slot N] [--ignore NAME ...] [--all] [--quiet]
  save_diff.py --dump A              (every set value of one save)

Exit 0 when they match, 1 when they don't. Used by the test pass ("save_matches", tools/port/testpass.py).
"""
import argparse
import glob
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GCI_HEADER = 0x40
META = 0x2000
SLOT = 0x4000
G = 0x8  # GlobalWork in a slot (GameCube layout)
GW = {"gsw0": 0x174, "gswf": 0x178, "gsw": 0x578, "lswf": 0xD78, "lsw": 0xDB8, "map": 0x12C, "area": 0x13C,
      "saveCount": 0x11F0, "saveTime": 0x11E8, "savePos": 0x11D4, "party": 0x11E0, "name": 0x11BC}
P = 0x13E0  # PouchData in a slot
# Left out unless --all: the time and counters, and where Mario stood when saving.
DEFAULT_IGNORE = {"saveCount", "saveTime", "savePos", "playtime", "deadInfo"}


def enum_names(header, prefix):
    out, n = {}, 0
    text = open(os.path.join(ROOT, header), encoding="utf-8").read()
    for m in re.finditer(r"^\s*(" + prefix + r"\w+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,", text, re.M):
        n = int(m.group(2), 0) if m.group(2) else n
        out.setdefault(n, m.group(1))
        n += 1
    return out


def load(path, slot=None):
    """One slot's bytes from a slot file, a .gci or a card folder."""
    if os.path.isdir(path):
        found = glob.glob(os.path.join(path, "**", "*mariost_save_file*.gci"), recursive=True)
        if not found:
            sys.exit(f"save_diff: no save file under {path}")
        # the card's own file (<region>/Card A/), not a backup of an older one (Backups/)
        path = sorted(found, key=lambda p: ("Card A" not in p, -os.path.getmtime(p)))[0]
    data = open(path, "rb").read()
    if len(data) == SLOT:
        return data
    body = data[GCI_HEADER:]
    slots = [body[META + SLOT * i: META + SLOT * (i + 1)] for i in range(4)]
    if slot is None:  # the last saved: the card's lastSlot (metadata 0x1E44)
        slot = struct.unpack_from(">H", body, 0x1E44)[0]
        if not 0 <= slot < 4 or struct.unpack_from(">H", slots[slot], 0)[0] & 1:
            slot = next((i for i in range(4) if not struct.unpack_from(">H", slots[i], 0)[0] & 1), 0)
    return slots[slot]


def fields(s):
    """{name: value} of everything compared."""
    gswf_names = enum_names("include/evt/evt_story.h", "GSWF_")
    gsw_names = enum_names("include/evt/evt_story.h", "GSW_")
    items = enum_names("include/mario/mario_pouch.h", "ITEM_")
    out = {}
    out["story GSW(0)"] = struct.unpack_from(">i", s, G + GW["gsw0"])[0]
    for i in range(0x100 * 32):
        if s[G + GW["gswf"] + (i // 32) * 4 + 3 - (i % 32) // 8] >> (i % 8) & 1:
            out[f"GSWF({i}) {gswf_names.get(i, '')}".rstrip()] = 1
    for i in range(0x800):
        v = s[G + GW["gsw"] + i]
        if v and i != 0:
            out[f"GSW({i}) {gsw_names.get(i, '')}".rstrip()] = v
    for i in range(0x10 * 32):
        if s[G + GW["lswf"] + (i // 32) * 4 + 3 - (i % 32) // 8] >> (i % 8) & 1:
            out[f"LSWF({i})"] = 1
    for i in range(0x400):
        if s[G + GW["lsw"] + i]:
            out[f"LSW({i})"] = s[G + GW["lsw"] + i]
    out["map"] = s[G + GW["map"]: G + GW["map"] + 16].split(b"\0")[0].decode("ascii", "replace")
    out["area"] = s[G + GW["area"]: G + GW["area"] + 16].split(b"\0")[0].decode("ascii", "replace")
    out["partner out"] = struct.unpack_from(">ii", s, G + GW["party"])
    out["saveCount"] = struct.unpack_from(">i", s, G + GW["saveCount"])[0]
    out["saveTime"] = struct.unpack_from(">q", s, G + GW["saveTime"])[0]
    out["savePos"] = tuple(round(v, 1) for v in struct.unpack_from(">fff", s, G + GW["savePos"]))
    for i in range(8):
        f = struct.unpack_from(">Hhhhhhh", s, P + i * 14)
        if f[0]:
            out[f"partner {i}"] = f
    names = ["currentHP", "maximumHP", "currentFP", "maximumFP", "coins", "currentSP", "maximumSP"]
    for k, v in zip(names, struct.unpack_from(">7h", s, P + 0x70)):
        out["pouch." + k] = v
    out["pouch.audience"] = round(struct.unpack_from(">f", s, P + 0x84)[0], 2)
    for k, off, fmt in [("rank", 0x88, ">h"), ("level", 0x8A, ">h"), ("starPowers", 0x8C, ">H"),
                        ("baseMaxHP", 0x8E, ">h"), ("baseMaxFP", 0x90, ">h"), ("availableBP", 0x92, ">h"),
                        ("maximumBP", 0x94, ">h"), ("starPoints", 0x96, ">h"), ("jumpLevel", 0x98, ">b"),
                        ("hammerLevel", 0x99, ">b"), ("starPieces", 0x9A, ">h"), ("shineSprites", 0x9C, ">h"),
                        ("shopPoints", 0x5B0, ">h")]:
        out["pouch." + k] = struct.unpack_from(fmt, s, P + off)[0]
    for k, off, n in [("keyItems", 0xA0, 121), ("heldItems", 0x192, 20), ("storedItems", 0x1BA, 32),
                      ("badges", 0x1FA, 200), ("equipped", 0x38A, 200)]:
        v = [x for x in struct.unpack_from(f">{n}h", s, P + off) if x]
        out["pouch." + k] = [items.get(x, x) for x in v]
    out["pouch.emails"] = list(s[P + 0x51A: P + 0x51A + 100].rstrip(b"\0"))
    out["pouch.emailFlags"] = struct.unpack_from(">8I", s, P + 0x580)
    out["deadInfo"] = [(s[o:o + 16].split(b"\0")[0].decode("ascii", "replace"), struct.unpack_from(">i", s, o + 16)[0])
                       for o in range(0x19B4, 0x19B4 + 64 * 20, 20) if s[o]]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--slot", type=int)
    ap.add_argument("--slot-b", type=int)
    ap.add_argument("--ignore", nargs="*", default=[], help="field names (or their start) left out")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    fa = fields(load(a.a, a.slot))
    if a.dump or not a.b:
        for k, v in fa.items():
            print(f"{k}: {v}")
        return 0
    fb = fields(load(a.b, a.slot_b))
    ignore = set(a.ignore) | (set() if a.all else DEFAULT_IGNORE)
    diff = []
    for k in list(fa) + [k for k in fb if k not in fa]:
        if any(k == i or k.startswith(i + " ") or k.startswith(i + "(") or k.startswith(i) and i.endswith(".")
               for i in ignore):
            continue
        if fa.get(k) != fb.get(k):
            diff.append(f"{k}: {fa.get(k)!r} | {fb.get(k)!r}")
    if not a.quiet:
        print("\n".join(diff) if diff else "save_diff: the same")
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
