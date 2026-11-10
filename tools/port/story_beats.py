#!/usr/bin/env python3
"""Derive the story beats of the game from the decomp: tests/port/story_beats.json.

The story is one number, GSW(GSW_STORY_PROGRESS) (include/evt/evt_story.h's
StoryProgress values). This lists, from the sources and not by hand:

- every place a script sets it (SET_VALUE(GSW(GSW_STORY_PROGRESS), X) in
  rels/*/src): the value, the REL and map, the script, the story guard it sits
  under (the CASE/IF on the story just above it, when there is one), whether
  a battle is fought on the way (evt_npc_battle_start in the script, its
  callers or what they run), and what starts it: the "triggers", the scripts
  at the top of its callers (SPAWN_EVENT / CALL_CHILD / a PTR() in a SET or a
  CALL followed up), each with its kind:
    init      run by the map's init script (relSetEvtAddr): plays on arrival
    enter     a bero's enterEvent (plays on arrival through that entrance)
    exit      a bero's exitEvent (leaving through it)
    talk      an NPC's talkEvent (A in front of it)
    npc       another NPC event (init, regular, dead, find, ...) or one an
              NPC is given by a script (evt_npc_set_*, evt_npc_change_interrupt)
    case      a hit-object case (evt_run_case_evt: A in front, touched, ...)
    mobj      a map object's event (a block, a switch, a chest)
    item      an item's pick-up or use (evt_item_entry, evt_win_item_select)
    other     another user function's callback
- the game's own event table (src/event.c, event_dt: what its debug scenario
  select warps to, as seq_e3.c does): for each row the id, name, map, entrance
  (a bero, or a sequence name such as "sekai_yami1" that the map's scripts
  play), partners and set-up function.

tools/port/story_sweep.py reads the JSON: each row of the event table is one
beat, entered as the scenario select does and played until the story moves
on; the set sites tell it which script to start if nothing plays by itself.

usage: story_beats.py [--out tests/port/story_beats.json] [--check]
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def story_enum():
    """StoryProgress name -> value."""
    t = open(os.path.join(ROOT, "include", "evt", "evt_story.h"), encoding="utf-8").read()
    body = t[t.index("typedef enum StoryProgress"):t.index("} StoryProgress;")]
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"\b(STORY_\w+)\s*=\s*(\d+)", body)}


def value_of(text, enum):
    text = text.strip()
    if re.fullmatch(r"-?\d+", text):
        return int(text)
    return enum.get(text)


EVT_BEGIN = re.compile(r"^(static\s+)?EVT_BEGIN\((\w+)\)", re.M)
SET_STORY = re.compile(r"SET_VALUE\(GSW\(GSW_STORY_PROGRESS\),\s*(\w+)\)")
GUARD = re.compile(r"(?:CASE_[A-Z_]+|IF_INT_[A-Z_]+\(GSW\(GSW_STORY_PROGRESS\),)\s*\(?\s*(STORY_\w+|\d+)")


class Source:
    """One REL source: its scripts (name -> (start, end) line), lines, statics."""

    def __init__(self, path):
        self.path = path
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        self.relpath = rel
        self.rel = rel.split("/")[1]
        self.map = os.path.splitext(os.path.basename(path))[0]
        self.lines = open(path, encoding="utf-8", errors="replace").read().replace("\r\n", "\n").split("\n")
        self.events = {}   # name -> list of (start, end) (a script may be defined per version)
        self.static = set()
        cur = None
        for i, line in enumerate(self.lines):
            m = EVT_BEGIN.match(line)
            if m:
                cur = (m.group(2), i)
                if m.group(1):
                    self.static.add(m.group(2))
                continue
            if cur and line.startswith("EVT_END()"):
                self.events.setdefault(cur[0], []).append((cur[1], i))
                cur = None

    def event_at(self, line):
        for name, spans in self.events.items():
            for s, e in spans:
                if s <= line <= e:
                    return name
        return None

    def body(self, name):
        out = []
        for s, e in self.events.get(name, []):
            out += self.lines[s:e + 1]
        return out


def ref_kind(src, i, name):
    """What the line i of src does with the script `name`: (kind, detail), or
    ("call", parent script) for a script run from another script."""
    line = src.lines[i]
    parent = src.event_at(i)
    if re.search(r"relSetEvtAddr\(", line):
        return "init", None
    m = re.search(r"//\s*(\w+Event)\b", line)
    if m and parent is None:
        field = m.group(1)
        if field == "enterEvent":
            return "enter", bero_name(src, i)
        if field == "exitEvent":
            return "exit", bero_name(src, i)
        if field == "talkEvent":
            return "talk", npc_name(src, i)
        return "npc", f"{npc_name(src, i)}.{field}"
    if parent is None and re.search(r"//\s*evt(Open|Close)\b", line):
        # a door's open or close script: the door opens from its outside hit (A in front, its case)
        for j in range(i, max(0, i - 20), -1):
            m = re.search(r'"(\w+)",\s*//\s*hitOut', src.lines[j])
            if m:
                return "door", m.group(1)
        return "door", None
    if parent is None and re.search(r"\bevtEntry\(", line):
        return "c", line.strip()[:60]  # C code (a user function) starts it
    if parent is None:
        return "other", line.strip()[:60]
    if re.search(r"\b(SPAWN_EVENT|SPAWN_EVENT_GET_ID|CALL_CHILD|SPAWN_CHILD_EVENT)\(", line):
        return "call", parent
    m = re.search(r"CALL\((\w+)", line)
    if m:
        fn = m.group(1)
        if fn in ("evt_run_case_evt", "evt_run_case_evt_bero"):
            h = re.search(r'STRING\("(\w+)"\)', line)
            return "case", h.group(1) if h else None
        if fn.startswith("evt_mobj_"):
            h = re.search(r'STRING\("(\w+)"\)', line)
            return "mobj", h.group(1) if h else fn
        if fn.startswith(("evt_npc_", "evt_nannpc_")):
            h = re.search(r'STRING\("([^"]+)"\)', line)
            return "npc", f"{h.group(1) if h else '?'}.{fn}"
        if fn.startswith(("evt_item_", "evt_win_item")):
            return "item", fn
        if fn.startswith("evt_bero_"):
            return "exit", fn
        if fn in ("evt_set_dead_evt",):
            return "npc", fn
        return "other", fn
    if re.search(r"\bSET_VALUE\(|\bSET_INT_READ_ADDR\(", line):
        return "call", parent  # a pointer kept in a variable, run from there
    return "call", parent


def npc_name(src, i):
    for j in range(i, max(0, i - 12), -1):
        m = re.search(r'"([^"]*)",\s*//\s*name', src.lines[j])
        if m:
            return m.group(1)
    return "?"


def bero_name(src, i):
    for j in range(i, max(0, i - 16), -1):
        m = re.search(r'"(\w+)",\s*//\s*hitName', src.lines[j])
        if m:
            return m.group(1)
    return "?"


def refs(src, name):
    pat = re.compile(r"(?<![\w])" + re.escape(name) + r"(?![\w])")
    out = []
    spans = src.events.get(name, [])
    for i, line in enumerate(src.lines):
        if any(s <= i <= e for s, e in spans) and not line.lstrip().startswith(("SPAWN", "CALL_CHILD")):
            continue  # inside its own body (a script that runs itself again still counts)
        code = line.split("//")[0] if not re.search(r"//\s*\w+Event\b", line) else line
        if re.match(r"\s*(static\s+)?(const\s+)?EvtWord\s+\w+\[\d*\];", line) or line.startswith("#"):
            continue  # a forward declaration
        if pat.search(code) and not EVT_BEGIN.match(line) and not line.lstrip().startswith(("*", "/*", "//", "extern ")):
            out.append(i)
    return out


def triggers(src, name, init_names):
    """The scripts that start `name` and how: [(kind, root script, detail)]."""
    seen, out, todo = set(), [], [name]
    while todo:
        n = todo.pop()
        if n in seen:
            continue
        seen.add(n)
        if n in init_names:
            out.append(("init", n, None))
            continue
        found = False
        for i in refs(src, n):
            kind, detail = ref_kind(src, i, n)
            if kind == "call":
                found = True
                todo.append(detail)
            elif kind != "other" or detail not in (None, ""):
                found = True
                out.append((kind, n, detail))
        if not found:
            out.append(("unknown", n, None))
    return sorted(set(out), key=lambda t: (t[0], t[1], str(t[2]))), seen


def descendants(src, name):
    seen, todo = set(), [name]
    while todo:
        n = todo.pop()
        if n in seen or n not in src.events:
            continue
        seen.add(n)
        for line in src.body(n):
            for m in re.finditer(r"PTR\((\w+)\)", line):
                todo.append(m.group(1))
    return seen


def fights(src, names):
    for n in names:
        for d in descendants(src, n):
            if any("evt_npc_battle_start" in l for l in src.body(d)):
                return True
    return False


def asks(src, names):
    """A choice on the way (a yes/no or list window)."""
    for n in names:
        for d in descendants(src, n):
            if any(re.search(r"evt_win_(?:item_)?select|evt_msg_select|evt_win_other_select", l) for l in src.body(d)):
                return True
    return False


def guard_of(src, i, enum):
    """The story value the set sits under (the closest CASE/IF on the story above it, in its script)."""
    name = src.event_at(i)
    start = min(s for s, e in src.events[name] if s <= i <= e)
    for j in range(i - 1, start, -1):
        l = src.lines[j]
        if "SWITCH(GSW(GSW_STORY_PROGRESS))" in l:
            break
        m = GUARD.search(l)
        if m and ("CASE_" in l or "GSW_STORY_PROGRESS" in l):
            v = value_of(m.group(1), enum)
            op = re.search(r"(CASE_[A-Z_]+|IF_INT_[A-Z_]+)", l).group(1)
            return {"op": op, "value": v}
    return None


def init_scripts():
    """Each map's init script name (relSetEvtAddr in each REL's own source)."""
    out = {}
    for f in glob.glob(os.path.join(ROOT, "rels", "*", "src", "*.c")):
        for m in re.finditer(r'relSetEvtAddr\("(\w+)",\s*(\w+)\)', open(f, encoding="utf-8", errors="replace").read()):
            out[m.group(1)] = m.group(2)
    return out


def symbol(src, name):
    """The executable's name for a script (tools/port/rel_rename.py: rel_<area>__<name>;
    a static through its export pointer, CMakeLists.txt's ttyd_static_exports)."""
    if name in src.static:
        mod = re.sub(r"[^A-Za-z0-9]", "_", src.relpath)[:-2]
        return "*rel_%s__port_static__%s__%s" % (src.rel, mod, name)
    return "rel_%s__%s" % (src.rel, name)


CMP = re.compile(r"IF_INT_(LESS_OR_EQUAL|GREATER_OR_EQUAL|LESS|GREATER)\(LW\((\d+)\),\s*(-?\d+)\)")


def walk_to(src, name, set_line=None):
    """Where Mario has to walk for the script to go on, when it waits on his
    position: evt_mario_get_pos into LW(x), LW(y), LW(z), then comparisons of
    x or z with numbers (before the story is set, in the setting script).
    "x:z" ("_" for an axis it doesn't test), or None."""
    if set_line is None:
        lines = src.body(name)
    else:
        start = min(s for s, e in src.events[name] if s <= set_line <= e)
        lines = src.lines[start:set_line]
    bounds = {"x": [None, None], "z": [None, None]}
    found = False
    num = r"(?:FLOAT\()?(-?\d+(?:\.\d+)?)f?\)?"
    for line in lines:
        # a line Mario has to cross (evt_sub_check_intersect: his last and new position against a segment):
        # its middle
        m = re.search(r"CALL\(evt_sub_check_intersect,(?:\s*LW\(\d+\),){4}\s*" + r",\s*".join([num] * 4), line)
        if m:
            x1, z1, x2, z2 = (float(v) for v in m.groups())
            return "%d:%d" % (round((x1 + x2) / 2), round((z1 + z2) / 2))
    for i, line in enumerate(lines):
        m = re.search(r"CALL\(evt_mario_get_pos,\s*0,\s*LW\((\d+)\),\s*LW\((\d+)\),\s*LW\((\d+)\)\)", line)
        if not m:
            continue
        axis = {m.group(1): "x", m.group(3): "z"}
        for k, l in enumerate(lines[i + 1:i + 12], i + 1):
            if "evt_mario_get_pos" in l:
                break
            # the IF's body: a wait (WAIT_FRAMES then GOTO back: "while x < v, wait") tests where Mario must NOT
            # be, so the bound goes the other way (hei_00's gonbaba_evt: x < -100 waits, so east of -100)
            body = []
            for b in lines[k + 1:k + 6]:
                if re.search(r"\b(END_IF|ELSE)\b", b):
                    break
                body.append(b)
            waits = any("WAIT_FRAMES" in b for b in body) and any("GOTO(" in b for b in body)
            for op, reg, v in CMP.findall(l):
                if reg not in axis:
                    continue
                v = int(v)
                lo_hi = bounds[axis[reg]]
                if op.startswith("GREATER") != waits:
                    lo_hi[0] = v if lo_hi[0] is None else max(lo_hi[0], v)
                else:
                    lo_hi[1] = v if lo_hi[1] is None else min(lo_hi[1], v)
                found = True
    if not found:
        return None

    def pick(lo, hi):
        if lo is not None and hi is not None:
            return str((lo + hi) // 2) if lo <= hi else "_"
        if lo is not None:
            return str(lo + 20)
        if hi is not None:
            return str(hi - 20)
        return "_"
    out = pick(*bounds["x"]) + ":" + pick(*bounds["z"])
    return None if out == "_:_" else out


def set_sites(enum):
    inits = init_scripts()
    init_names = set(inits.values())
    sites = []
    for path in sorted(glob.glob(os.path.join(ROOT, "rels", "*", "src", "*.c"))):
        src = Source(path)
        for i, line in enumerate(src.lines):
            m = SET_STORY.search(line)
            if not m:
                continue
            ev = src.event_at(i)
            if ev is None:
                continue
            trig, path = triggers(src, ev, init_names)
            # the walks: the setting script's, then those of the scripts that run it (a scene that waits for
            # Mario at the throne before the fight that leads to it)
            walks = [walk_to(src, ev, i)] + [walk_to(src, n) for n in sorted(path - {ev} - init_names)
                                             if n in src.events]
            walks = [w for k, w in enumerate(walks) if w and w not in walks[:k]]
            roots = sorted({t[1] for t in trig} | {ev})
            sites.append({
                "value": value_of(m.group(1), enum),
                "name": m.group(1),
                "rel": src.rel,
                "map": src.map,
                "file": src.relpath,
                "line": i + 1,
                "event": ev,
                "symbol": symbol(src, ev),
                "guard": guard_of(src, i, enum),
                "battle": fights(src, roots),
                "choice": asks(src, roots),
                "triggers": [{"kind": k, "event": n, "symbol": symbol(src, n), "detail": d} for k, n, d in trig],
                "walk": walks,
            })
    return sites


def event_table(enum):
    """src/event.c's event_dt rows, in order."""
    t = open(os.path.join(ROOT, "src", "event.c"), encoding="utf-8").read()
    # a row given per version (#if ... #else ... #endif): the first form only (the
    # same id, map and entrance; the game's own table is read at run time)
    t = re.sub(r"\n#else\n.*?\n#endif", "", t, flags=re.S)
    t = re.sub(r"\n#(?:if|endif)[^\n]*", "", t)
    rows = []
    for stage, m in enumerate(re.finditer(r"EventData stage(\d)_evt\[\] = \{(.*?)\n\};", t, re.S)):
        for r in re.finditer(r'\{(\d+), (\d+), (\d+), (\d+), "(\w+)", "[^"]*"(?: NAME_EN\("([^"]*)"\))?, '
                              r'"(\w+)", (?:"(\w*)"|NULL), (\w+)\}', m.group(2)):
            rows.append({
                "stage": int(m.group(1)),
                "motion": int(r.group(1)),
                "partners": [int(r.group(2)), int(r.group(3))],
                "id": int(r.group(4)),
                "text": r.group(5),
                "name": r.group(6) or "",
                "map": r.group(7),
                "bero": r.group(8) or None,
                "init": None if r.group(9) == "NULL" else r.group(9),
            })
    return rows


def build():
    enum = story_enum()
    sites = set_sites(enum)
    rows = event_table(enum)
    by_value = {}
    for s in sites:
        by_value.setdefault(s["value"], []).append(s)
    beats = []
    prev = 0
    for idx, r in enumerate(rows):
        here = by_value.get(r["id"], [])
        # the set sites on this row's map first: the scripts that move the story to this row
        here = sorted(here, key=lambda s: (s["map"] != r["map"], s["file"], s["line"]))
        on_map = [s for s in here if s["map"] == r["map"]]
        beat = dict(r, index=idx, prev=prev, sets=here)
        beat["battle"] = any(s["battle"] for s in (on_map or here))
        beat["choice"] = any(s["choice"] for s in (on_map or here))
        beat["kind"] = ("same" if r["id"] == prev and idx > 0 else
                        "battle" if beat["battle"] else
                        "cutscene" if on_map and all(any(t["kind"] in ("init", "enter") for t in s["triggers"]) for s in on_map)
                        else "action" if here else "unset")
        beats.append(beat)
        prev = r["id"]
    names = {v: k for k, v in enum.items()}
    return {
        "about": "Generated by tools/port/story_beats.py from the decomp (src/event.c, rels/*/src); do not edit.",
        "story_names": {str(v): names[v] for v in sorted(names)},
        "beats": beats,
        "sets": sites,
        "values_without_row": sorted({s["value"] for s in sites} - {r["id"] for r in rows}),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "tests", "port", "story_beats.json"))
    ap.add_argument("--check", action="store_true", help="fail if the file is stale")
    a = ap.parse_args()
    data = build()
    text = json.dumps(data, indent=1, ensure_ascii=False) + "\n"
    if a.check:
        old = open(a.out, encoding="utf-8").read() if os.path.exists(a.out) else ""
        if old != text:
            print(f"story_beats: {a.out} is stale; run tools/port/story_beats.py")
            return 1
        return 0
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    beats = data["beats"]
    kinds = {}
    for b in beats:
        kinds[b["kind"]] = kinds.get(b["kind"], 0) + 1
    print(f"story_beats: {len(beats)} beats (event table rows), {len(data['sets'])} set sites, "
          f"{len(data['values_without_row'])} set values with no row; kinds {kinds}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
