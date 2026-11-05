#!/usr/bin/env python3
"""Generate port/src/common/tattle_cards_gen.c: each Tattle Log enemy's
numbers for Tattle cards and the quiz party (port/include/port/party.h), from
the decomp's own battle units (rels/*/src/unit/*.c, src/unit/*.c):

  - HP and level: the BattleUnitKind's maxHp and level;
  - DEF: its "defence" table's normal entry (the first);
  - ATK: the strongest of the file's weapons whose damage is a plain number
    (weaponGetPowerDefault's first word).

A kind found in several files takes the first outside the test and Pit
rooms (rels/tst, rels/jon), in story order of areas. #if blocks are read as
the US release builds them (VERSION_US); the numbers are the same on the
other releases for nearly every enemy.

  gen_tattle_cards.py           write the table
  gen_tattle_cards.py --check   fail if it is out of date
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "port", "src", "common", "tattle_cards_gen.c")

VERSIONS = {"VERSION_DEMO_US": 0, "VERSION_DEMO_JP": 1, "VERSION_JP": 2, "VERSION_US": 3,
            "VERSION_DEMO_US2": 4, "VERSION_DEMO_EU": 5, "VERSION_EU": 6}
AREA_ORDER = ["gor", "tik", "nok", "gon", "hei", "win", "mri", "tou", "tou2", "usu", "jin", "muj", "dou", "rsh",
              "eki", "pik", "bom", "moo", "aji", "las", "gra", "jon", "tst"]


def unit_enum():
    """BattleUnitType's names, numbered as the enum counts them"""
    text = open(os.path.join(ROOT, "include", "battle", "battle_unit.h"), encoding="utf-8").read()
    body = text[text.index("typedef enum BattleUnitType"):]
    body = body[body.index("{") + 1:body.index("}")]
    names, n = {}, 0
    for line in body.splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            name, value = [x.strip() for x in line.split("=", 1)]
            n = int(value, 0)
        else:
            name = line
        names[name] = n
        n += 1
    return names


def us_lines(text):
    """the file's lines as the US release builds them: #if blocks evaluated, other directives dropped"""
    out, stack = [], []  # stack of (taking, any branch taken)

    def cond(expr):
        expr = expr.replace("||", " or ").replace("&&", " and ").replace("!=", " != ").replace("!", " not ")
        expr = expr.replace(" not =", "!=")
        expr = re.sub(r"defined\s*\(?\s*(\w+)\s*\)?", lambda m: "1" if m.group(1) == "TARGET_PC" else "0", expr)
        for k, v in VERSIONS.items():
            expr = re.sub(r"\b" + k + r"\b", str(v), expr)
        expr = re.sub(r"\bVERSION\b", str(VERSIONS["VERSION_US"]), expr)
        expr = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: m.group(0) if m.group(0) in ("and", "or", "not") else "0", expr)
        try:
            return bool(eval(expr, {}, {}))
        except Exception:
            return False

    for line in text.splitlines():
        s = line.strip()
        taking = all(t for t, _ in stack)
        if s.startswith("#if"):
            if s.startswith("#ifdef"):
                c = s.split()[1] == "TARGET_PC"
            elif s.startswith("#ifndef"):
                c = s.split()[1] != "TARGET_PC"
            else:
                c = cond(s[3:])
            stack.append((c, c))
        elif s.startswith("#elif"):
            t, done = stack.pop()
            c = not done and cond(s[5:])
            stack.append((c, done or c))
        elif s.startswith("#else"):
            t, done = stack.pop()
            stack.append((not done, True))
        elif s.startswith("#endif"):
            stack.pop()
        elif s.startswith("#"):
            continue
        elif taking:
            out.append(line)
    return "\n".join(out)


def values(block):
    """the initializer's scalar words in order, comments and braces out"""
    block = re.sub(r"//[^\n]*", "", block)
    block = re.sub(r"/\*.*?\*/", "", block, flags=re.S)
    block = block.replace("{", ",").replace("}", ",")
    return [v.strip() for v in block.split(",") if v.strip()]


def number(word, enum):
    word = word.strip()
    if word in enum:
        return enum[word]
    try:
        return int(float(word.rstrip("fF")))
    except ValueError:
        return None


def scan(path, enum):
    text = us_lines(open(path, encoding="utf-8", errors="replace").read())
    attack = 0
    for m in re.finditer(r"BattleWeapon \w+ = \{(.*?)\n\};", text, re.S):
        body = m.group(1)
        d = re.search(r"weaponGetPowerDefault,\s*//\s*hpDamageFn\s*\n\s*\{\s*(-?\d+)", body)
        if d:
            attack = max(attack, int(d.group(1)))
    defense = None
    for m in re.finditer(r"BattleUnitDefense (\w+) = \{\{\s*(-?\d+)", text):
        if defense is None or m.group(1) == "defence":
            defense = int(m.group(2))
    found = []
    for m in re.finditer(r"\nBattleUnitKind (\w+) = \{(.*?)\n\};", text, re.S):
        v = values(m.group(2))
        if len(v) < 7:
            continue
        kind, hp, level = number(v[0], enum), number(v[2], enum), number(v[6], enum)
        if kind is None or hp is None or level is None or kind <= 0:
            continue
        found.append((kind, hp, attack, defense or 0, level))
    return found


def main():
    enum = unit_enum()
    files = []
    for area in sorted(os.listdir(os.path.join(ROOT, "rels"))):
        d = os.path.join(ROOT, "rels", area, "src", "unit")
        if os.path.isdir(d):
            files += [(area, os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".c")]
    d = os.path.join(ROOT, "src", "unit")
    files += [("dol", os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".c")]

    def rank(area):
        return AREA_ORDER.index(area) if area in AREA_ORDER else len(AREA_ORDER) - 2

    files.sort(key=lambda af: (rank(af[0]), af[1]))
    best = {}
    for area, path in files:
        for kind, hp, atk, df, level in scan(path, enum):
            if kind not in best:
                best[kind] = (hp, atk, df, level, os.path.relpath(path, ROOT).replace("\\", "/"))
    names = {v: k for k, v in enum.items()}
    lines = [
        "/* Generated by tools/port/gen_tattle_cards.py from the decomp's battle units: do not edit. */",
        "/* Each enemy kind's HP, ATK, DEF and level (port/include/port/party.h, PortCardStats). */",
        '#include "port/party.h"',
        "",
        "const PortCardStats gPortCardStats[] = {",
    ]
    for kind in sorted(best):
        hp, atk, df, level, src = best[kind]
        lines.append("    {%d, %d, %d, %d, %d}, /* %s: %s */" % (kind, hp, atk, df, level, names.get(kind, "?"), src))
    lines += ["};", "const int gPortCardStatsCount = (int)(sizeof(gPortCardStats) / sizeof(gPortCardStats[0]));", ""]
    text = "\n".join(lines)
    if "--check" in sys.argv:
        old = open(OUT, encoding="utf-8").read().replace("\r\n", "\n") if os.path.exists(OUT) else ""
        if old != text:
            print("tattle_cards_gen.c is out of date: run tools/port/gen_tattle_cards.py")
            return 1
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%d kinds" % len(best))
    return 0


if __name__ == "__main__":
    sys.exit(main())
