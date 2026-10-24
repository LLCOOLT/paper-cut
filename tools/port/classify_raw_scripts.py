#!/usr/bin/env python3
"""Find raw `s32 name[] = {...}` arrays whose words form an exact event script
(command headers (argc << 16 | opcode) with opcode <= 118, ending in
END_SCRIPT on the last word) and retype them, and all their declarations, to
EvtWord with (EvtWord) casts. Identical for MWCC; pointer-sized on PC.

usage: classify_raw_scripts.py [--dry-run]
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEF = re.compile(r"\bs32(\s*\(?\s*)(\w+)(\s*\)?\s*\[[^\]]*\]\s*=\s*\{)")

def split_words(body):
    words, cur, depth, i = [], "", 0, 0
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            cur += body[i:j + 1]
            i = j + 1
            continue
        if body.startswith("/*", i):
            j = body.index("*/", i) + 2
            cur += body[i:j]
            i = j
            continue
        if body.startswith("//", i):
            i = body.index("\n", i)
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            words.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    if cur.strip():
        words.append(cur.strip())
    return words

def as_int(w):
    w = re.sub(r"/\*.*?\*/", "", w).strip()
    try:
        return int(w, 0)
    except ValueError:
        return None

def is_script(words):
    i = 0
    while i < len(words):
        h = as_int(words[i])
        if h is None or h < 0:
            return False
        argc, op = h >> 16, h & 0xFFFF
        if op > 118 or argc > 64:
            return False
        if op == 1:
            return argc == 0 and i == len(words) - 1
        i += 1 + argc
    return False

def main():
    dry = "--dry-run" in sys.argv
    texts, scripts = {}, set()
    for d in ["src", "rels"]:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for f in fns:
                if f.endswith(".c"):
                    p = os.path.join(dp, f)
                    texts[p] = open(p, encoding="utf-8", errors="surrogateescape").read()
    found = {}
    for p, t in texts.items():
        for m in DEF.finditer(t):
            end = t.index("};", m.end())
            if is_script(split_words(t[m.end():end])):
                found.setdefault(p, set()).add(m.group(2))
    total = sum(len(v) for v in found.values())
    print(f"{total} raw script arrays in {len(found)} files")
    if dry:
        return
    for p, names in found.items():
        t = texts[p]
        decl = re.compile(r"\bs32(\s*\(?\s*)(" + "|".join(map(re.escape, names)) + r")(\s*\)?\s*\[)")
        t = decl.sub(lambda m: "EvtWord" + m.group(1) + m.group(2) + m.group(3), t)
        if "evt/evt_word.h" not in t:
            m = re.search(r"^#include[^\n]*\n", t, re.M)
            t = (t[:m.start()] if m else "") + '#include "evt/evt_word.h"\n' + (t[m.start():] if m else t)
        open(p, "w", encoding="utf-8", errors="surrogateescape").write(t)

if __name__ == "__main__":
    main()
