#!/usr/bin/env python3
"""The rooms of each area and the exits between them, from the decomp's own
room data: every BeroEntry table in rels/<area>/src (hitName, nextMapName,
nextBeroName), as tables for the area maps (port/src/common/area_maps_gen.c,
read through port/include/port/area_maps.h).

A room's place on the drawn map comes from its exits' directions: an exit's
hit name starts with the way Mario walks through it ("n_bero", "sw_bero"),
as BERO_DIR_AUTO reads it, so the room it leads to is drawn on that side.
Pipes, doors and the rest (no direction) take the nearest free place.

A map's variants (rsh_03_a, rsh_03_b) are one room: the first six letters.

    python tools/port/gen_area_maps.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RELS = os.path.join(ROOT, "rels")
OUT = os.path.join(ROOT, "port", "src", "common", "area_maps_gen.c")

# the areas with rooms to walk (not the title, demos, tests, minigames, the Pit's floors)
AREAS = ["gor", "tik", "hei", "nok", "gon", "win", "mri", "tou", "usu", "jin", "muj", "dou", "hom", "rsh", "eki",
         "pik", "bom", "moo", "aji", "las", "gra"]

DIRS = {"n": (0, -1), "ne": (1, -1), "e": (1, 0), "se": (1, 1), "s": (0, 1), "sw": (-1, 1), "w": (-1, 0),
        "nw": (-1, -1)}
DIR_CODE = {"n": 1, "ne": 2, "e": 3, "se": 4, "s": 5, "sw": 6, "w": 7, "nw": 8}

TABLE = re.compile(r"BeroEntry\s+(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\n\};", re.S)
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')


def room(name):
    return name[:6]


def split_entries(body):
    """The table's entries: the text inside each top-level { }."""
    out, depth, start = [], 0, None
    for i, ch in enumerate(body):
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                out.append(body[start:i])
                start = None
    return out


def fields(entry):
    """An entry's fields in order, comments and preprocessor lines left out."""
    lines = []
    for line in entry.split("\n"):
        if line.strip().startswith("#"):
            continue
        line = re.sub(r"//.*", "", line)
        lines.append(line)
    text = " ".join(lines)
    text = re.sub(r"/\*.*?\*/", "", text)
    parts, depth, cur, in_str = [], 0, "", False
    for ch in text:
        if ch == '"':
            in_str = not in_str
        if not in_str and ch == "(":
            depth += 1
        if not in_str and ch == ")":
            depth -= 1
        if ch == "," and depth == 0 and not in_str:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


def string_of(field):
    m = STRING.fullmatch(field.strip())
    return m.group(1) if m else None


def read_exits():
    """(from room, hit name, to room or None) for every bero, by area."""
    exits = []
    rooms = set()
    for area in AREAS:
        src = os.path.join(RELS, area, "src")
        if not os.path.isdir(src):
            continue
        for dirpath, dirnames, filenames in os.walk(src):
            dirnames[:] = [d for d in dirnames if d != "unit"]
            for fn in sorted(filenames):
                if not fn.endswith(".c"):
                    continue
                stem = fn[:-2]
                if re.fullmatch(r"[a-z]{3}_\d\d\w*", stem):
                    rooms.add(room(stem))
                text = open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace").read()
                for m in TABLE.finditer(text):
                    table, body = m.group(1), m.group(2)
                    owner = re.match(r"([a-z]{3}_\d\d)", table)
                    owner = owner.group(1) if owner else stem
                    if not re.fullmatch(r"[a-z]{3}_\d\d\w*", owner):
                        continue
                    for e in split_entries(body):
                        f = fields(e)
                        if len(f) < 13:
                            continue
                        hit = string_of(f[0])
                        nxt = string_of(f[11])
                        if not hit:
                            continue
                        exits.append((room(owner), hit, room(nxt) if nxt else None))
                        rooms.add(room(owner))
    return sorted(rooms), exits


def direction(hit):
    m = re.match(r"(ne|nw|se|sw|n|e|s|w)_", hit)
    return m.group(1) if m else None


def layout(area_rooms, exits):
    """Grid places (x east, y south) for one area's rooms."""
    place = {}
    taken = set()
    links = {r: [] for r in area_rooms}
    for frm, hit, to in exits:
        if frm in links and to in links and frm != to:
            d = direction(hit)
            links[frm].append((to, d))
            # the way back, for rooms only reached from their far side
            back = None
            if d:
                dx, dy = DIRS[d]
                back = [k for k, v in DIRS.items() if v == (-dx, -dy)][0]
            links[to].append((frm, back))

    def free_near(x, y):
        for r in range(0, 12):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if max(abs(dx), abs(dy)) != r:
                        continue
                    if (x + dx, y + dy) not in taken:
                        return x + dx, y + dy
        return x, y

    origin_x = 0
    for start in area_rooms:
        if start in place:
            continue
        x, y = free_near(origin_x, 0)
        place[start] = (x, y)
        taken.add((x, y))
        queue = [start]
        while queue:
            cur = queue.pop(0)
            cx, cy = place[cur]
            for to, d in sorted(links[cur], key=lambda t: (t[1] is None, t[0])):
                if to in place:
                    continue
                if d:
                    dx, dy = DIRS[d]
                    tx, ty = cx + dx, cy + dy
                    if (tx, ty) in taken:
                        tx, ty = cx + 2 * dx, cy + 2 * dy
                    if (tx, ty) in taken:
                        tx, ty = free_near(cx + dx, cy + dy)
                else:
                    tx, ty = free_near(cx + 1, cy)
                place[to] = (tx, ty)
                taken.add((tx, ty))
                queue.append(to)
        origin_x = max(px for px, _ in place.values()) + 2
    # from 0, 0
    mx = min(px for px, _ in place.values())
    my = min(py for _, py in place.values())
    return {r: (px - mx, py - my) for r, (px, py) in place.items()}


def main():
    rooms, exits = read_exits()
    # rooms with a way in or out (not the cutscene-only maps)
    linked = {frm for frm, _, to in exits if to} | {to for _, _, to in exits if to}
    rooms = [r for r in rooms if r in linked]
    out = ["/* GENERATED by tools/port/gen_area_maps.py from the rels' BeroEntry tables. Do not edit. */",
           '#include "port/area_maps.h"', ""]
    room_rows, index = [], {}
    for area in AREAS:
        area_rooms = [r for r in rooms if r.startswith(area + "_")]
        if not area_rooms:
            continue
        places = layout(area_rooms, exits)
        for r in area_rooms:
            index[r] = len(room_rows)
            room_rows.append((r, area, places[r][0], places[r][1]))
    out.append("const PortAreaRoom portAreaRooms[] = {")
    for r, area, x, y in room_rows:
        out.append('    {"%s", "%s", %d, %d},' % (r, area, x, y))
    out.append("};")
    out.append("const int portAreaRoomCount = %d;" % len(room_rows))
    out.append("")
    seen = set()
    exit_rows = []
    for frm, hit, to in exits:
        if frm not in index:
            continue
        key = (frm, hit)
        if key in seen:
            continue  # a table repeated under #if (the releases' versions)
        seen.add(key)
        d = direction(hit)
        exit_rows.append((index[frm], hit, to if to else "", index.get(to, -1) if to else -1, DIR_CODE.get(d, 0)))
    out.append("const PortAreaExit portAreaExits[] = {")
    for frm, hit, to, toi, d in exit_rows:
        out.append('    {%d, "%s", "%s", %d, %d},' % (frm, hit, to, toi, d))
    out.append("};")
    out.append("const int portAreaExitCount = %d;" % len(exit_rows))
    out.append("")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print("%d rooms, %d exits -> %s" % (len(room_rows), len(exit_rows), OUT))


if __name__ == "__main__":
    main()
