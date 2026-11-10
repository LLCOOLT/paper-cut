#!/usr/bin/env python3
"""How the story advances: for each value the scripts set the story to
(SET_VALUE(GSW(0), N)), the script that sets it and what starts that script.

The story counter GSW(0) runs from 0 to about 405; every map's scripts branch
on it. To play the story with input scripts (port/src/common/input_pc.c) one
needs, at each step, the scene's trigger: walking somewhere (a loop on Mario's
position, a hit object's case event), talking to an NPC, entering a map, a
battle's end. This lists them from the decomp's scripts:

  N   map/file   script            started by                      conditions
  4   gor_00     christine_osoware  SPAWN_EVENT in gor_00_init_evt  Mario z <= -100

usage: story_steps.py [--from N] [--to N] [--file SUBSTRING]
"""
import argparse
import collections
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EVT = re.compile(r"EVT_BEGIN\((\w+)\)(.*?)EVT_END\(\)", re.S)
REF = re.compile(r"PTR\((\w+)\)")


def load():
    """{name: [(file, body)]} of every script, and the raw text of every file."""
    scripts = collections.defaultdict(list)
    texts = {}
    for f in glob.glob(os.path.join(ROOT, "rels", "*", "src", "**", "*.c"), recursive=True) + \
            glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True):
        t = open(f, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
        rel = os.path.relpath(f, ROOT).replace(os.sep, "/")
        texts[rel] = t
        for m in EVT.finditer(t):
            scripts[m.group(1)].append((rel, m.group(2)))
    return scripts, texts


def starters(name, scripts, texts, file_hint):
    """How script `name` is started: other scripts referring to it (with the
    command), and data tables naming it (NPC setups, entrances)."""
    out = []
    # PTR(name), or the bare name as a command's argument (SPAWN_EVENT(name), RUN_EVT(name), ...)
    pat = re.compile(r"^.*(?:PTR\(|[A-Z_]+\(|, )" + re.escape(name) + r"\b.*$", re.M)
    for other, defs in scripts.items():
        for rel, body in defs:
            for line in pat.findall(body):
                cmd = line.strip().split("(")[0]
                hit = re.search(r'evt_run_case_evt, \d+, \d+, STRING\("(\w+)"\)', line)
                out.append(f"{cmd}{' on hit ' + hit.group(1) if hit else ''} in {other}")
    for rel, t in texts.items():
        if file_hint and file_hint.split("/")[1] != rel.split("/")[1]:
            continue  # data tables: same area only
        for m in re.finditer(r"^\s*" + re.escape(name) + r",\s*//\s*(\w+)", t, re.M):
            out.append(f"{m.group(1)} of a table in {os.path.basename(rel)}")
    return sorted(set(out))


def conditions(body):
    """Position checks in a script (the walking triggers)."""
    conds = []
    for m in re.finditer(r"IF_[A-Z_]+\(LW\(\d\), (-?\d+)\)", body):
        line = body[body.rfind("\n", 0, m.start()) + 1: body.find("\n", m.end())].strip()
        if "evt_mario_get_pos" in body:
            conds.append(line)
    return conds[:3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=999)
    ap.add_argument("--file", default="")
    a = ap.parse_args()
    scripts, texts = load()
    # the story's values by name (include/evt/evt_story.h: STORY_STG1_00_PETAL_MEADOWS = 23)
    with open(os.path.join(ROOT, "include", "evt", "evt_story.h"), encoding="utf-8") as f:
        names = dict(re.findall(r"^\s*(STORY_\w+) = (\d+),", f.read(), re.M))
    steps = []
    for name, defs in scripts.items():
        for rel, body in defs:
            if a.file and a.file not in rel:
                continue
            for v in re.findall(r"SET_VALUE\(GSW\((?:0|GSW_STORY_PROGRESS)\), (\w+)\)", body):
                v = names.get(v, v)
                if v.isdigit() and a.lo <= int(v) <= a.hi:
                    steps.append((int(v), rel, name, body))
    for v, rel, name, body in sorted(steps):
        print(f"{v:4d}  {os.path.basename(rel):24s} {name}")
        for s in starters(name, scripts, texts, rel):
            print(f"        started by: {s}")
        for c in conditions(body):
            print(f"        condition:  {c}")


if __name__ == "__main__":
    main()
