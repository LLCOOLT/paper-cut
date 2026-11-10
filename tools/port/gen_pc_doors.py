#!/usr/bin/env python3
"""Lists every map's entrances (bero) from the decomp's map sources.

Reads the BeroEntry tables in rels/<area>/src/<map>.c (hitName, type,
nextMapName, nextBeroName) and prints, per map, each entrance with where it
leads and whether the room on the other side leads back (a "pair"). Paper
Cut's room shuffle (port/archipelago/pc_doors.h) is written from this list;
run it to check that header's doors against the decomp:

    python tools/port/gen_pc_doors.py            # every map
    python tools/port/gen_pc_doors.py gor tik    # these areas
    python tools/port/gen_pc_doors.py --check    # pc_doors.h's pairs exist both ways

Only reads the decomp; writes nothing.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TABLE = re.compile(r"BeroEntry\s+\w+\[\]\s*=\s*\{")
TYPES = {0: "road", 1: "door", 2: "pipe", 3: "gate", 0xF00: "none"}


def _split(body):
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    out, cur, depth, quoted = [], "", 0, False
    for ch in body:
        if ch == '"':
            quoted = not quoted
        if not quoted and ch in "({":
            depth += 1
        if not quoted and ch in ")}":
            depth -= 1
        if ch == "," and not quoted and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def _entries(text):
    for t in TABLE.finditer(text):
        i, depth, start = t.end(), 1, None
        while i < len(text) and depth > 0:
            ch = text[i]
            if ch == "{":
                depth += 1
                if depth == 2:
                    start = i + 1
            elif ch == "}":
                depth -= 1
                if depth == 1 and start is not None:
                    yield _split(text[start:i])
                    start = None
            i += 1


def _str(v):
    v = v.strip()
    while v.startswith("(") and v.endswith(")"):
        v = v[1:-1].strip()
    return v[1:-1] if v.startswith('"') else ""


def entrances():
    """{map: {hit: set((type, nextMap, nextBero))}}"""
    out = {}
    rels = os.path.join(ROOT, "rels")
    for area in sorted(os.listdir(rels)):
        src = os.path.join(rels, area, "src")
        if not os.path.isdir(src):
            continue
        files = []
        for top, _, names in os.walk(src):
            files += [(n, os.path.join(top, n)) for n in names]
        for name, path in sorted(files):
            m = re.fullmatch(r"([a-z0-9]{3}_\d\d)\.c", name)
            if not m:
                continue
            text = open(path, encoding="utf-8", errors="replace").read()
            for f in _entries(text):
                if len(f) < 13 or not _str(f[0]):
                    continue
                hit, nmap, nbero = _str(f[0]), _str(f[11]), _str(f[12])
                try:
                    kind = TYPES.get(int(f[1], 0) & 0xFFF, f[1])
                except ValueError:
                    kind = f[1]
                out.setdefault(m.group(1), {}).setdefault(hit, set()).add((kind, nmap or m.group(1), nbero))
    return out


def main(argv):
    doors = entrances()
    if "--check" in argv:
        header = open(os.path.join(ROOT, "port", "archipelago", "pc_doors.h"), encoding="utf-8").read()
        bad = 0
        for a, ab, b, bb in re.findall(r'\{\{"(\w+)", "(\w+)"\}, \{"(\w+)", "(\w+)"\}', header):
            there = any(n == b and nb == bb for _, n, nb in doors.get(a, {}).get(ab, ()))
            back = any(n == a and nb == ab for _, n, nb in doors.get(b, {}).get(bb, ()))
            if not (there and back):
                print(f"not a pair: {a}.{ab} <-> {b}.{bb} (there {there}, back {back})")
                bad += 1
        print(f"{bad} bad")
        return 1 if bad else 0
    areas = [a for a in argv if not a.startswith("-")]
    for m in sorted(doors):
        if areas and m[:3] not in areas:
            continue
        for hit in sorted(doors[m]):
            for t, nmap, nbero in sorted(doors[m][hit]):
                back = any(n == m and nb == hit for _, n, nb in doors.get(nmap, {}).get(nbero, ()))
                print(f"{m}.{hit} [{t}] -> {nmap}.{nbero}{'' if back else '  (one way)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
