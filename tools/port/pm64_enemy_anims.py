#!/usr/bin/env python3
"""Paper Mario 64 enemies: every battle state has an animation of the enemy's own.

A PM64 enemy in battle (port/src/common/pm64_enemy_pc.c) stands in for a TTYD
host unit and plays, for each of the host's poses, the animation of its PM64
sprite that the pose's state calls for (stateOf: walking, hurt, asleep,
stopped, dizzy, burnt, knocked out, on its back ...). The states and their
animations are kEnemies' idle, walk and hurt and kRows' every other column;
UP in a flipped column deliberately plays the upright row (a sprite with no
frames on its back).

  pm64_enemy_anims.py check [--pm64 DIR]
      fails (exit 1) if an enemy's sprite has no kRows row, a row leaves a
      state out (fewer columns, or a -1), UP stands anywhere but a flipped
      column, a host that flips (STATUS_FLAG_FLIPPED in its unit scripts) has
      only UP there for the enemy that lies on its back (itself, or its
      grounded cousin in kGrounded: a Paratroopa flips as a Koopa Troopa), a
      kGrounded name isn't in kEnemies, or a host's script sets a pose by name
      whose letter kPoseLetter doesn't list (CI; needs no ROM). With --pm64,
      the PM64 decomp checkout (read only): each animation number must be one
      of the sprite's (tools/splat_ext/npc_sprite_names.yaml).
  pm64_enemy_anims.py report [--pm64 DIR]
      every enemy and state with the animation it plays (named with --pm64),
      and every host's poses
"""
import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ENEMY_PC = os.path.join(REPO, "port", "src", "common", "pm64_enemy_pc.c")
UNIT_H = os.path.join(REPO, "include", "battle", "battle_unit.h")
UNITS = os.path.join(REPO, "rels", "*", "src", "unit", "unit_*.c")

STATES = ["run", "jump", "attack", "talk", "sleep", "still", "dizzy", "burnt", "burn hurt", "dead",
          "flipped", "flipped hurt", "flipped asleep", "flipped still", "flipped dizzy", "flipped struggle"]
FLIP_FIRST = STATES.index("flipped")
UP = "UP"


def text(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(s):
    return re.sub(r"/\*.*?\*/", "", s, flags=re.S)


def c_array(src, name):
    m = re.search(r"\b%s\[\]\s*=\s*\{(.*?)\n\};" % re.escape(name), src, re.S)
    if not m:
        raise SystemExit("pm64_enemy_anims: %s not found in pm64_enemy_pc.c" % name)
    return strip_comments(m.group(1))


def enemies(src):
    out = []
    for m in re.finditer(r'\{"([^"]+)",\s*(0x\w+),\s*(0x\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(UNIT_\w+)',
                         c_array(src, "kEnemies")):
        out.append({"name": m.group(1), "sprite": int(m.group(4)), "palette": int(m.group(5)),
                    "idle": int(m.group(6)), "walk": int(m.group(7)), "hurt": int(m.group(8)), "host": m.group(9)})
    return out


def rows(src):
    out, problems = {}, []
    for m in re.finditer(r"\{\s*(\d+)\s*,\s*\{([^}]*)\}\s*\}", c_array(src, "kRows")):
        sprite = int(m.group(1))
        vals = [v.strip() for v in m.group(2).split(",") if v.strip()]
        if sprite in out:
            problems.append("sprite %d: two kRows rows" % sprite)
        out[sprite] = vals
    return out, problems


def grounded(src):
    return re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', c_array(src, "kGrounded"))


def letters(src):
    return {c: st for c, st in re.findall(r"\{\s*'(.)'\s*,\s*(ST_\w+|-1)\s*\}", c_array(src, "kPoseLetter"))}


def unit_kinds():
    """{UNIT_name: number} from battle_unit.h's BattleUnitType"""
    m = re.search(r"typedef enum BattleUnitType[^{]*\{(.*?)\}", text(UNIT_H), re.S)
    kinds, n = {}, 0
    for line in m.group(1).split("\n"):
        mm = re.match(r"\s*(UNIT_\w+)\s*(?:=\s*(\w+))?\s*,", line)
        if mm:
            if mm.group(2):
                n = int(mm.group(2), 0)
            kinds[mm.group(1)] = n
            n += 1
    return kinds


def hosts(wanted):
    """{UNIT_name: {"table": set, "script": set, "flips": bool, "files": [...]}} for the hosts wanted"""
    kinds = unit_kinds()
    num = {kinds[h]: h for h in wanted if h in kinds}
    out = {h: {"table": set(), "script": set(), "flips": False, "files": []} for h in wanted}
    for f in sorted(glob.glob(UNITS)):
        t = text(f)
        m = re.search(r"BattleUnitKind unit_\w+ = \{\s*(\w+)\s*,", t)
        if not m:
            continue
        k = m.group(1)
        k = kinds.get(k) if k.startswith("UNIT_") else int(k, 0)
        if k not in num:
            continue
        h = out[num[k]]
        h["files"].append(os.path.relpath(f, REPO).replace("\\", "/"))
        body = strip_comments(t)
        h["table"].update(re.findall(r'\{\s*\w+\s*,\s*"(\w+)"\s*\}', body))
        h["script"].update(re.findall(r'AnimeChangePose\b[^\n]*?STRING\("(\w+)"\)', body))
        h["flips"] |= "STATUS_FLAG_FLIPPED" in body
    return out


def letter_of(pose):
    """pm64_enemy_pc.c letterState: the letter before the last '_' (or the first)"""
    u = pose.rfind("_")
    return pose[u - 1] if u > 0 else pose[0]


def pm64_sprites(root):
    """[{name, animations}] in archive order (the port's 0-based sprite index)"""
    path = os.path.join(root, "tools", "splat_ext", "npc_sprite_names.yaml")
    sprites, cur, sec = [], None, None
    for line in text(path).split("\n"):
        m = re.match(r"^([A-Za-z0-9_]+):\s*$", line)
        if m:
            cur = {"name": m.group(1), "animations": []}
            sprites.append(cur)
            sec = None
            continue
        m = re.match(r"^  (\w+):", line)
        if m and cur is not None:
            sec = m.group(1)
            continue
        m = re.match(r"^    - (.*)$", line)
        if m and cur is not None and sec == "animations":
            cur["animations"].append(m.group(1).strip())
    return sprites


def collect(pm64):
    src = text(ENEMY_PC)
    ens = enemies(src)
    rws, probs = rows(src)
    grd = grounded(src)
    lts = letters(src)
    hs = hosts(sorted({e["host"] for e in ens}))
    spr = pm64_sprites(pm64) if pm64 else None
    by_name = {e["name"]: e for e in ens}
    for w, g in grd:
        for n in (w, g):
            if n not in by_name:
                probs.append("kGrounded: %s is no enemy in kEnemies" % n)
    winged = dict(grd)
    for sprite, vals in sorted(rws.items()):
        if len(vals) != len(STATES):
            probs.append("sprite %d: %d states in its row, not %d" % (sprite, len(vals), len(STATES)))
        for i, v in enumerate(vals[:len(STATES)]):
            if v == UP:
                if i < FLIP_FIRST:
                    probs.append("sprite %d: UP for %s (only the flipped states may stand upright)" % (sprite, STATES[i]))
            elif not re.fullmatch(r"\d+", v):
                probs.append("sprite %d: no animation for %s (%s)" % (sprite, STATES[i], v))
    for e in ens:
        if e["sprite"] not in rws:
            probs.append("%s (sprite %d): no kRows row: every state but idle, walk and hurt falls back"
                         % (e["name"], e["sprite"]))
            continue
        h = hs.get(e["host"])
        if h is None or not h["files"]:
            probs.append("%s: its host %s has no unit file under rels/" % (e["name"], e["host"]))
            continue
        if h["flips"]:
            lies = by_name.get(winged.get(e["name"], ""), e)
            vals = rws.get(lies["sprite"], [])
            if any(v == UP for v in vals[FLIP_FIRST:]):
                probs.append("%s: %s flips over (STATUS_FLAG_FLIPPED), but %s's sprite %d has UP in a flipped state"
                             % (e["name"], e["host"], lies["name"], lies["sprite"]))
        for pose in sorted(h["script"] - h["table"]):
            if letter_of(pose) not in lts:
                probs.append("%s: its host's script pose %s: letter %r isn't in kPoseLetter"
                             % (e["name"], pose, letter_of(pose)))
    if spr is not None:
        for e in ens:
            if e["sprite"] >= len(spr):
                probs.append("%s: sprite %d isn't in the PM64 sprite list" % (e["name"], e["sprite"]))
                continue
            n = len(spr[e["sprite"]]["animations"])
            for what in ("idle", "walk", "hurt"):
                if e[what] >= n:
                    probs.append("%s: %s %d past sprite %d's %d animations" % (e["name"], what, e[what], e["sprite"], n))
        for sprite, vals in rws.items():
            n = len(spr[sprite]["animations"]) if sprite < len(spr) else 0
            for i, v in enumerate(vals[:len(STATES)]):
                if v != UP and re.fullmatch(r"\d+", v) and int(v) >= n:
                    probs.append("sprite %d: %s %s past its %d animations" % (sprite, STATES[i], v, n))
    return ens, rws, hs, spr, probs


def anim_name(spr, sprite, v):
    if spr is None or v == UP or sprite >= len(spr) or not re.fullmatch(r"\d+", v):
        return v
    a = spr[sprite]["animations"]
    return "%s %s" % (v, a[int(v)]) if int(v) < len(a) else v


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["check", "report"])
    ap.add_argument("--pm64", help="the PM64 decomp checkout (read only), for the animations' names and counts")
    a = ap.parse_args()
    ens, rws, hs, spr, probs = collect(a.pm64)
    if a.cmd == "report":
        for e in ens:
            vals = rws.get(e["sprite"], [])
            print("%s (sprite %d%s, host %s%s)" % (e["name"], e["sprite"],
                                                    " " + spr[e["sprite"]]["name"] if spr else "", e["host"],
                                                    ", flips" if hs[e["host"]]["flips"] else ""))
            for what in ("idle", "walk", "hurt"):
                print("    %-17s %s" % (what, anim_name(spr, e["sprite"], str(e[what]))))
            for i, st in enumerate(STATES):
                print("    %-17s %s" % (st, anim_name(spr, e["sprite"], vals[i]) if i < len(vals) else "(none)"))
        print()
        for h, d in sorted(hs.items()):
            print("%s: %s" % (h, ", ".join(d["files"])))
            print("    table:  %s" % " ".join(sorted(d["table"])))
            print("    script: %s" % " ".join(sorted(d["script"] - d["table"])))
    for p in probs:
        print("pm64_enemy_anims: %s" % p)
    print("pm64_enemy_anims: %d enemies, %d sprites x %d states, %d problems" % (len(ens), len(rws), len(STATES) + 3,
                                                                                  len(probs)))
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main())
