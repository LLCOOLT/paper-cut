#!/usr/bin/env python3
"""Controller parity of Paper Cut's actions (port/settings/features/bindings.cpp): everything a keyboard player
can do, a controller player can do too. testpass.py lint runs problems(); run alone it prints the table.

Each action in kActions needs a default controller binding, or a way to it in a menu (its kMenuWay line: the
settings page, the Start menu, a touch button). And the defaults must not clash: no two actions on one button or
combo, no action alone on one of the game's buttons (the face buttons, Start, the right shoulder (Z), the D-pad,
in the controllers' default layout), and no combo made of two of them (the game's would win: bindings.h)."""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BINDINGS = os.path.join(ROOT, "port", "settings", "features", "bindings.cpp")

# the game's buttons on a controller out of the box (Aurora's default mapping)
GAME = {"SOUTH", "EAST", "WEST", "NORTH", "START", "RIGHT_SHOULDER", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT",
        "DPAD_RIGHT"}
ROW = re.compile(r'^\s*\{"(\w+)", "([^"]+)", (k\w+), ([^,]+), (combo\([^)]*\)|[\w:]+)(?:, "\w+")?\},')
BUTTON = re.compile(r"SDL_GAMEPAD_BUTTON_(\w+)|\b(kBack)\b")


def _buttons(expr):
    out = []
    for m in BUTTON.finditer(expr):
        out.append("BACK" if m.group(2) else m.group(1))
    return out


def table():
    """[(id, name, key, pad expression, buttons, menu way)], or raises ValueError"""
    with open(BINDINGS, encoding="utf-8") as fh:
        src = fh.read()
    start = src.index("const ActionRow kActions[PORT_BIND_COUNT] = {")
    end = src.index("\n};", start)
    rows = []
    for line in src[start:end].splitlines()[1:]:
        m = ROW.match(line)
        if m:
            rows.append([m.group(1), m.group(2), m.group(4).strip(), m.group(5).strip()])
    start = src.index("const char* const kMenuWay[PORT_BIND_COUNT] = {")
    end = src.index("\n};", start)
    ways = re.findall(r'^\s*"((?:[^"\\]|\\.)*)",', src[start:end], re.M)
    if len(ways) != len(rows):
        raise ValueError(f"bindings.cpp: {len(rows)} actions in kActions but {len(ways)} kMenuWay lines")
    return [(r[0], r[1], r[2], r[3], _buttons(r[3]) if r[3] not in ("kPadNone",) else [], w)
            for r, w in zip(rows, ways)]


def problems():
    try:
        rows = table()
    except (OSError, ValueError) as e:
        return [str(e)]
    bad, seen = [], {}
    for aid, _name, _key, pad, buttons, way in rows:
        legacy = pad == "kPadLegacy"  # (the old setting's choice: rewind's Back + B, walk boost's right stick)
        if not buttons and not legacy and not way.strip():
            bad.append(f"{aid}: no controller binding by default and no menu way (kMenuWay): a controller or "
                       f"touch player can't do it")
        if len(buttons) == 1 and buttons[0] in GAME:
            bad.append(f"{aid}: its default button {buttons[0]} is one of the game's (it would never fire)")
        if len(buttons) == 2 and buttons[0] in GAME and buttons[1] in GAME:
            bad.append(f"{aid}: its default combo {buttons[0]} + {buttons[1]} is two of the game's buttons")
        if buttons:
            key = tuple(sorted(buttons))
            if key in seen:
                bad.append(f"{aid}: its default {' + '.join(buttons)} is {seen[key]}'s too")
            seen.setdefault(key, aid)
    return bad


def main():
    try:
        rows = table()
    except (OSError, ValueError) as e:
        print(e)
        return 1
    for aid, name, key, pad, buttons, way in rows:
        print(f"{aid:14} {key:24} {(' + '.join(buttons) if buttons else pad):22} {way}")
    bad = problems()
    for b in bad:
        print("problem:", b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
