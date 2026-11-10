#!/usr/bin/env python3
"""Play as: every animation of Mario's has one of each pick's (docs/PLAY_AS.md).

Whoever Play as picks is drawn as a stand-in following Mario's own pose
(port/src/common/luigi_pc.c): each frame, Mario's current animation is looked
up for the pick, in the order charAnim() does it:

  own    an animation of ours of the same name in the pick's model (the packs,
         port/assets/luigi/*_anims.bin; "<name>@b" in battle), or in its
         back-facing model for Mario's R names. Ours take Mario's clock, so
         they have Mario's length and loop (checked here).
  disc   the pick's disc animation the map table (kLuigiMap, kPeachMap,
         kBowserMap) gives it: one deliberately chosen. standInClock() fits
         it to Mario's: where his ends, it is scaled to end on his last frame
         (a loop of its own in whole turns) and holds; where his loops, it
         loops on his clock. So a map can't give a one-shot for a loop.
  none   DELIBERATE below: Mario's model is drawn as it is.
  kind   none of those: the nearest plain move by the name's kind (Peach,
         Bowser), a stand-in nobody chose. A gap.
  mario  none of those for Luigi: Mario's own model in green. A gap.

Mario's scripts wait on Mario's pose, which always runs underneath, so a
stand-in never holds a scene up; the parity is that each pick really does
what Mario does, for as long as he does it.

  anim_parity.py check
      fails (exit 1) if any animation of Mario's has no own or disc
      counterpart for Luigi, Peach or Bowser, if a map names an animation the
      pick's model doesn't have, or if one of ours doesn't loop or end as
      Mario's does (CI; needs no disc)
  anim_parity.py report [--md FILE]
      the whole inventory: every animation of Mario's, where it's played,
      and each pick's counterpart
  anim_parity.py disc ISO [ISO ...] [--write]
      Mario's set and the picks' disc sets as the discs have them, against
      the copy here (anim_parity_set.py: names, loop and length only, no
      pixels or models); --write refreshes that copy

Mario's set is a_mario (the field), a_mario_r (from behind), e_mario (the
story's poses: every name evt_mario_set_pose plays that isn't in
a_mario_group) and b_mario (battle). a_mario_l/_w/_wl and their R and e_
files are the emblems' recolours with the same animations (checked by `disc`).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

LUIGI_PC = os.path.join(REPO, "port", "src", "common", "luigi_pc.c")
EVT_MARIO = os.path.join(REPO, "src", "event", "evt_mario.c")
PACKS = os.path.join(REPO, "port", "assets", "luigi")

MARIO_FILES = ["a_mario", "a_mario_r", "e_mario", "b_mario"]
EMBLEM_FILES = {"a_mario": ["a_mario_l", "a_mario_w", "a_mario_wl"],
                "a_mario_r": ["a_mario_lr", "a_mario_wr", "a_mario_wlr"],
                "e_mario": ["e_mario_l", "e_mario_w", "e_mario_wl"],
                "b_mario": ["b_mario_l", "b_mario_w", "b_mario_wl"]}
PICK_FILES = ["c_luigi", "c_peach", "c_peach_r", "c_koopa"]

# who, their front and back models (disc files, then our packs), as luigi_pc.c's kChars
PICKS = {
    "Luigi": {"map": "kLuigiMap", "front": ("c_luigi", ["luigi"]), "back": None, "always": False},
    "Peach": {"map": "kPeachMap", "front": ("c_peach", ["peach"]), "back": ("c_peach_r", ["peach_r"]),
              "always": True},
    # (his back view, c_koopa_r_pc, is made over c_koopa: his disc moves play on it)
    "Bowser": {"map": "kBowserMap", "front": ("c_koopa", ["bowser"]), "back": ("c_koopa", ["bowser_r"]),
               "always": True},
}


# Mario's animations deliberately without a counterpart of anyone's, and why
# (luigi_pc.c charAnim returns none for them)
DELIBERATE = {
    # battle: the hammer thrown (Hammer Throw's part 3): b_mario hides Mario's
    # whole body in it and draws only the flying hammer, so no pick has an
    # animation for it; luigi_pc.c (thrownDraw) draws the thrown thing as the
    # pick's instead: Luigi Mario's hammer, Peach her closed parasol on the
    # hammer's spin, Bowser nothing (his fireball flies, fire_breath_pc.c)
    "M_H_12": "the thrown hammer: Mario's, Peach's parasol, Bowser's fireball",
}


def is_back(name):
    """luigi_pc.c isBack: an a_mario_r name (M_S_1R, M_H_1R; not M_X_1)"""
    return len(name) > 4 and name[-1] == "R" and name[-2] != "_"


# ------------------------------------------------------------- sources ----

def _c_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def c_array(text, name):
    """the body of `static const ... name[] = { ... };`"""
    m = re.search(r"\b%s\[\]\s*=\s*\{(.*?)\n\};" % re.escape(name), text, re.S)
    if not m:
        raise SystemExit("anim_parity: %s not found in luigi_pc.c" % name)
    return re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)


def maps(text):
    """{pick: {mario: (to, back)}} from the map tables"""
    out = {}
    for who, p in PICKS.items():
        body = c_array(text, p["map"])
        out[who] = {m: (t, b == "1") for m, t, b in re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\d)\s*\}', body)}
    return out


def same_in_battle(text):
    return re.findall(r'"([^"]+)"', c_array(text, "kSameInBattle"))


def a_mario_group():
    """evt_mario.c's a_mario_group (the EU-and-later list, with M_I_3)"""
    body = c_array(_c_text(EVT_MARIO), "a_mario_group")
    body = re.sub(r"#else.*?#endif", "", body, flags=re.S)
    return re.findall(r'"([^"]+)"', body)


def pack_anims(key):
    """{name: (loop, end)} of a committed pack"""
    import luigi_pack
    import ttyd_anim as T
    with open(os.path.join(PACKS, "%s_anims.bin" % key), "rb") as f:
        p = luigi_pack.read_pack(f.read())
    out = {}
    for name, blob in p["anims"]:
        a = T.Anim.parse(name, blob)
        out[name] = (a.loop, a.end)
    return out


def disc_set():
    import anim_parity_set as S
    return {f: {n: (lp, e) for n, lp, e in v} for f, v in S.FILES.items()}, S.REGION_ONLY


# -------------------------------------------------------------- inventory ----

def mario_set(files, group):
    """[(context, name, (loop, end), file)]: every animation Mario plays: in
    the field (a_mario's names; the rest of e_mario's, the second set, which
    evt_mario_set_pose plays for names outside a_mario_group), from behind,
    and in battle"""
    out = []
    seen = set()
    for f in ("a_mario", "a_mario_r", "e_mario"):
        for n, t in files[f].items():
            if f == "e_mario" and n in group:
                continue  # (a_mario_group's names play from a_mario)
            if n not in seen:
                seen.add(n)
                out.append(("field", n, t, f))
    for n, t in files["b_mario"].items():
        out.append(("battle", n, t, "b_mario"))
    return out


def resolve(who, ctx, name, files, packs, cmaps, sib):
    """(class, animation, model, (loop, end) or None) as charAnim() finds it"""
    p = PICKS[who]

    def model(spec):
        disc, keys = spec
        anims = dict(files[disc])
        for k in keys:
            anims.update(packs[k])  # (a pack's animation replaces the disc's of its name)
        return anims

    front = model(p["front"])
    if p["back"] is not None and is_back(name):
        back = model(p["back"])
        if name in back:
            return ("own", name, "back", back[name])
    if ctx == "battle":
        if name + "@b" in front:
            return ("own", name + "@b", "front", front[name + "@b"])
        if name in sib and name in front:
            return ("own", name, "front", front[name])
    elif name in front:
        return ("own", name, "front", front[name])
    if name in cmaps[who]:
        to, back = cmaps[who][name]
        spec = p["back"] if back and p["back"] is not None else p["front"]
        anims = files[spec[0]]
        return ("disc", to, "back" if spec is p["back"] else "front", anims.get(to))
    if name in DELIBERATE:
        return ("none", None, None, None)
    if not p["always"]:
        return ("mario", None, None, None)
    return ("kind", None, None, None)


def inventory():
    text = _c_text(LUIGI_PC)
    files, region_only = disc_set()
    packs = {k: pack_anims(k) for p in PICKS.values() for spec in (p["front"], p["back"]) if spec for k in spec[1]}
    cmaps = maps(text)
    sib = same_in_battle(text)
    rows = []
    for ctx, name, timing, f in mario_set(files, a_mario_group()):
        res = {who: resolve(who, ctx, name, files, packs, cmaps, sib) for who in PICKS}
        rows.append((ctx, name, timing, f, region_only.get((f, name)), res))
    return rows, cmaps, files


def problems(rows, cmaps, files):
    """[(who, context, name, what)]"""
    out = []
    for ctx, name, (loop, end), f, _only, res in rows:
        for who, (cls, anim, _m, t) in res.items():
            if cls in ("kind", "mario"):
                out.append((who, ctx, name, "no counterpart (%s)" % ("plain move by kind" if cls == "kind"
                                                                     else "Mario's model in green")))
            elif cls == "disc" and t is None:
                out.append((who, ctx, name, "map names %s, which the model doesn't have" % anim))
            elif cls == "disc" and loop and not t[0]:
                # (standInClock fits a disc animation to Mario's one-shots; a
                # one-shot can't be made to loop as his does)
                out.append((who, ctx, name, "%s ends at %g where Mario's loops" % (anim, t[1])))
            elif cls == "own" and t != (loop, end):
                out.append((who, ctx, name, "ours %s %s, Mario's %s %s" % (
                    "loops" if t[0] else "ends", "%g" % t[1], "loops" if loop else "ends", "%g" % end)))
    # a map entry for a name Mario doesn't have is a typo
    known = {n for _c, n, _t, _f, _o, _r in rows}
    for who, m in cmaps.items():
        for n in m:
            if n not in known:
                out.append((who, "-", n, "map entry for a name Mario doesn't have"))
    return out


def _fmt(t):
    return "-" if t is None else ("loop %g" % t[1] if t[0] else "%g" % t[1])


def cell(r):
    cls, anim, model, t = r
    if cls == "own":
        return "own `%s`%s" % (anim, " (back)" if model == "back" else "")
    if cls == "disc":
        return "disc `%s`%s (%s)" % (anim, " (back)" if model == "back" else "", _fmt(t))
    return {"kind": "**MISSING** (by kind)", "mario": "**MISSING** (Mario)", "none": "hammer only (as Mario)"}[cls]


def cmd_report(a):
    rows, cmaps, files = inventory()
    lines = ["| Where | Mario's | File | Length | " + " | ".join(PICKS) + " |",
             "|---|---|---|---|" + "---|" * len(PICKS)]
    for ctx, name, timing, f, only, res in rows:
        note = " (%s only)" % only if only else ""
        lines.append("| %s | `%s`%s | %s | %s | %s |" % (ctx, name, note, f, _fmt(timing),
                                                        " | ".join(cell(res[w]) for w in PICKS)))
    counts = {w: {} for w in PICKS}
    for row in rows:
        for w, r in row[5].items():
            counts[w][r[0]] = counts[w].get(r[0], 0) + 1
    summary = ["%s: %s" % (w, ", ".join("%s %d" % (k, v) for k, v in sorted(c.items()))) for w, c in counts.items()]
    probs = problems(rows, cmaps, files)
    text = "\n".join(lines)
    out = "%d animations of Mario's (%d field, %d battle)\n%s\n%d problems\n" % (
        len(rows), sum(r[0] == "field" for r in rows), sum(r[0] == "battle" for r in rows),
        "\n".join(summary), len(probs))
    for p in probs:
        out += "  %s %s %s: %s\n" % p
    if a.md:
        with open(a.md, "w", encoding="utf-8", newline="\n") as f:
            f.write(text + "\n")
    else:
        print(text)
    print(out, end="")
    return 0


def cmd_check(_a):
    rows, cmaps, files = inventory()
    probs = problems(rows, cmaps, files)
    for p in probs:
        print("anim_parity: %s, %s %s: %s" % p)
    print("anim_parity: %d animations of Mario's x %d picks, %d problems" % (len(rows), len(PICKS), len(probs)))
    return 1 if probs else 0


def read_disc(iso):
    import gcdisc
    import ttyd_anim as T
    d = gcdisc.Disc(iso)
    out = {}
    for f in MARIO_FILES + PICK_FILES + [e for v in EMBLEM_FILES.values() for e in v]:
        p = T.PoseFile.parse(d.read("a/" + f))
        out[f] = [(an.name, an.loop, round(an.end, 3)) for an in p.anims]
    return d.game_id, out


def cmd_disc(a):
    discs = [read_disc(iso) for iso in a.iso]
    bad = 0
    for gid, d in discs:
        for f, emblems in EMBLEM_FILES.items():
            for e in emblems:
                if [x[0] for x in d[e]] != [x[0] for x in d[f]]:
                    print("%s: %s's animations aren't %s's" % (gid, e, f))
                    bad += 1
    # the union over the discs, in the first's order; names only some regions have
    files, only = {}, {}
    for f in MARIO_FILES + PICK_FILES:
        merged = []
        for gid, d in discs:
            for x in d[f]:
                if x[0] not in [m[0] for m in merged]:
                    merged.append(x)
        files[f] = merged
        for x in merged:
            have = [gid for gid, d in discs if x[0] in [y[0] for y in d[f]]]
            if len(have) != len(discs):
                only[(f, x[0])] = "/".join(have)
    if a.write:
        with open(os.path.join(HERE, "anim_parity_set.py"), "w", encoding="utf-8", newline="\n") as o:
            o.write('"""Mario\'s animations and the Play as picks\' disc animations, as the discs\n'
                    'have them: names, loop and length only (anim_parity.py disc --write; %s)."""\n\n'
                    % ", ".join(g for g, _ in discs))
            o.write("FILES = {\n")
            for f, v in files.items():
                o.write("    %r: [\n" % f)
                for n, lp, e in v:
                    o.write("        (%r, %r, %r),\n" % (n, lp, e))
                o.write("    ],\n")
            o.write("}\n\n# names only some discs have: (file, name) -> the discs that do\nREGION_ONLY = {\n")
            for k, v in only.items():
                o.write("    %r: %r,\n" % (k, v))
            o.write("}\n")
        print("anim_parity_set.py written from %s" % ", ".join(g for g, _ in discs))
        return 1 if bad else 0
    have, _only = disc_set()
    for f in MARIO_FILES + PICK_FILES:
        mine = {n: (lp, e) for n, lp, e in files[f]}
        if mine != have.get(f):
            print("%s differs from anim_parity_set.py (anim_parity.py disc --write)" % f)
            bad += 1
    print("%d discs, %s" % (len(discs), "the set here matches" if not bad else "%d differences" % bad))
    return 1 if bad else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    r = sub.add_parser("report")
    r.add_argument("--md")
    d = sub.add_parser("disc")
    d.add_argument("iso", nargs="+")
    d.add_argument("--write", action="store_true")
    a = ap.parse_args(argv)
    return {"check": cmd_check, "report": cmd_report, "disc": cmd_disc}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
