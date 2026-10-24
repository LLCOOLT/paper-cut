#!/usr/bin/env python3
"""Retype s32/u32 locals that receive script words to EvtWord.

-Wshorten-64-to-32 (audit build) flags every 'EvtWord' -> 's32' narrowing.
In code that moves script words around without knowing what they hold (the
script interpreter), each such local must be an EvtWord, or pointers stored
in script variables are cut to 32 bits on PC. EvtWord is s32 on GameCube, so
the change is matching-neutral.

For each warning in FILE whose code is `name = ...` or `T name = ...`, the
declaration of `name` in the enclosing function is retyped.

usage: widen_evtword.py AUDIT_LOG FILE [--apply]
"""
import re
import sys

WARN = re.compile(r"^(.*?):(\d+):(\d+): warning: implicit conversion loses integer precision: "
                  r"'(?:EvtWord|intptr_t)'.*\[-Wshorten-64-to-32\]")


def main():
    log, target = sys.argv[1], sys.argv[2].replace("\\", "/")
    apply = "--apply" in sys.argv
    lines = open(target, encoding="utf-8").read().split("\n")
    hits = set()
    for line in open(log, encoding="utf-8", errors="replace"):
        m = WARN.match(line.strip())
        if m and m.group(1).replace("\\", "/").endswith(target):
            hits.add(int(m.group(2)))
    changed = {}
    for ln in sorted(hits):
        code = lines[ln - 1]
        m = re.match(r"^\s*(?:(?:s32|u32|int)\s+)?(\w+)\s*(?:\[[^\]]*\])?\s*[-+*/|&]?=", code)
        if not m:
            print(f"{target}:{ln}: not an assignment, skipped: {code.strip()}")
            continue
        name = m.group(1)
        decl = re.compile(rf"^(\s*)(s32|u32|int)(\s+)((?:\w+\s*,\s*)*){name}\b(?!\s*\()")
        for k in range(ln - 1, -1, -1):  # search back to the function's start
            if decl.match(lines[k]):
                if k not in changed:
                    changed[k] = name
                break
            if re.match(r"^\S.*\)\s*\{?\s*(//.*)?$", lines[k]) and not lines[k].startswith(("#", "}")):
                print(f"{target}:{ln}: no s32 declaration of '{name}' in the function")
                break
    for k, name in sorted(changed.items()):
        old = lines[k]
        d = re.match(r"^(\s*)(s32|u32|int)(\s+)(.*)$", old)
        if "," in d.group(4).split(";")[0] and "=" not in d.group(4):
            # 's32 a, b;' -> split out the widened name
            names = [n.strip() for n in d.group(4).split(";")[0].split(",")]
            rest = [n for n in names if re.match(rf"{name}\b", n) is None]
            new = f"{d.group(1)}EvtWord {name};" + (f" {d.group(2)} {', '.join(rest)};" if rest else "")
        else:
            new = f"{d.group(1)}EvtWord{d.group(3)}{d.group(4)}"
        print(f"{target}:{k + 1}: {old.strip()}  ->  {new.strip()}")
        lines[k] = new
    if apply:
        open(target, "w", encoding="utf-8", newline="\n").write("\n".join(lines))


if __name__ == "__main__":
    main()
