#!/usr/bin/env python3
"""Calls through a no-prototype declaration with the wrong number of arguments.

The decomp declares some functions as `void f();` (no prototype) in the file
that calls them, and some calls pass fewer arguments than the definition
takes: `L_partyVivianTailStart()` for `L_partyVivianTailStart(PartyEntry*)`.
On PowerPC the missing argument is whatever r3 still holds (often the right
value: MWCC's code matched); on PC it is whatever the argument register holds
(rdi on x86-64: a crash at moo_00; x0 on arm64 happened to be right). No
compiler sees the definition from the calling file, so this lists them:
every call, in a file with an `f()` declaration, passing fewer arguments
than f's definition takes (extra arguments are ignored on every ABI). Lines the PC build does not compile are skipped.

usage: noproto_calls.py        (exit 1 if any call passes too few)
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from proto_audit import ROOT, pc_only, strip_comments  # noqa: E402

# Calls the PC build does not compile although pc_only keeps them.
KNOWN = {
    ("snd_init.c", "dataInitStack"),  # the MUSY_VERSION < 2.0.1 branch
}
NOPROTO = re.compile(r"^[ \t]*(?:extern\s+)?[\w\s\*]+?\b(\w+)\s*\(\s*\)\s*;", re.M)
DEF = re.compile(r"^[ \t]*(?:static\s+|inline\s+)*[\w\s\*]+?\b(\w+)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*\{", re.M)


def nparams(params):
    p = params.strip()
    if p in ("", "void"):
        return 0
    if "..." in p:
        return None  # variadic: any count
    depth, n = 0, 1
    for ch in p:
        depth += ch == "("
        depth -= ch == ")"
        n += ch == "," and depth == 0
    return n


def call_args(text, start):
    """Argument count of the call whose '(' is at text[start]."""
    depth, n, empty, i = 0, 1, True, start
    while i < len(text):
        ch = text[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                return 0 if empty else n
        elif ch == "," and depth == 1:
            n += 1
        elif depth >= 1 and not ch.isspace():
            empty = False
        i += 1
    return None


def main():
    files = [f for d in ("src", "rels", "libs/musyx") for f in glob.glob(os.path.join(ROOT, d, "**", "*.c"), recursive=True)]
    texts = {f: pc_only(strip_comments(open(f, encoding="utf-8", errors="replace").read())) for f in files}
    defs = {}
    for f, t in texts.items():
        for m in DEF.finditer(t):
            if m.group(1) not in ("if", "while", "for", "switch", "USER_FUNC"):
                defs.setdefault(m.group(1), set()).add(nparams(m.group(2)))
    found = extra = 0
    for f, t in sorted(texts.items()):
        for name in sorted(set(NOPROTO.findall(t))):
            if re.search(r"\(\s*\*\s*%s\s*\)|\b%s\s*[;=]" % (name, name), t) or (os.path.basename(f), name) in KNOWN:
                continue  # a function-pointer variable of that name, or KNOWN
            counts = defs.get(name)
            if not counts or None in counts or len(counts) != 1:
                continue
            want = next(iter(counts))
            for m in re.finditer(r"\b%s\s*\(" % re.escape(name), t):
                line_start = t.rfind("\n", 0, m.start()) + 1
                before = t[line_start:m.start()]
                words = re.findall(r"\w+", before)
                if re.fullmatch(r"[\w\s\*]*", before) and words and not set(words) & {"return", "else", "case", "do"}:
                    continue  # a declaration or definition, not a call
                got = call_args(t, m.end() - 1)
                if got is None or got == want:
                    continue
                if got > want:
                    extra += 1  # ignored by the callee on every ABI
                    continue
                line = t.count("\n", 0, m.start()) + 1
                print(f"{os.path.relpath(f, ROOT)}:{line}: {name}: {got} argument(s), definition takes {want}")
                found += 1
    print(f"noproto_calls: {found} call(s) with too few arguments ({extra} with extra ones, harmless)")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
