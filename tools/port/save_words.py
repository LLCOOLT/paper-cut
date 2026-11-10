#!/usr/bin/env python3
"""Paper Cut's own save words (port/include/port/save_words.h): no two features share one.

  save_words.py            print the table
  save_words.py --check    exit 1 when two ranges of a kind overlap, a range leaves its kind's bounds (or
                           starts inside the game's own words), or a save-word access in the port's code
                           (port/, and src/ and rels/ lines under TARGET_PC) names a number in Paper Cut's
                           part of a kind with a literal instead of the table's constants

Run by tools/port/local_ci.py (audits).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "port", "include", "port", "save_words.h")
ROW = re.compile(r'X\(\s*(GSWF|GSW|LSWF|LSW)\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*\)')
COUNT = {"GSW": 2048, "GSWF": 8192, "LSW": 1024, "LSWF": 512}


def header_value(name):
    m = re.search(r"#define\s+" + name + r"\s+(\d+)", open(HEADER, encoding="utf-8").read())
    return int(m.group(1)) if m else None


def rows():
    text = open(HEADER, encoding="utf-8").read()
    body = text[text.index("#define PORT_SAVE_WORDS(X)"):text.index("#define PORT_GAME_GSW_LAST")]
    return [(k, n, int(a), int(b), w) for k, n, a, b, w in ROW.findall(body)]


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


# an access to a save word whose index is a literal (or a constant set to one): gsw[2011], swByteGet(2040),
# swSet(8100), GSW(2030), portApGswGet(1792), a #define / enum / constexpr of a name with GSW or MARK in it
ACCESS = re.compile(
    r"(?:\bgswf?\s*\[|\b_?sw(?:Byte)?(?:Set|Get|Clear)\s*\(|\b(?:GSWF?|LSWF?)\s*\(|\bportApGswf?(?:Get|Set)\s*\(|"
    r"\b\w*(?:GSW|Gsw|MARK)\w*\b\s*(?:=|\s)\s*\(?)\s*(\d{4})\b")
# a constant named as a bound (..._MAX, _COUNT, _BITS, _LIMIT) holds the range's size or top, not a word in it
BOUND = re.compile(r"\w*(?:GSW|Gsw|MARK)\w*_(?:MAX|COUNT|BITS|LIMIT)\b")


def literal_uses(lo):
    """(path, line, kind, number) for each literal save-word index past the game's words in port code"""
    out = []
    for top in ("port", "src", "rels"):
        for dp, dirs, files in os.walk(os.path.join(ROOT, top)):
            dirs[:] = [d for d in dirs if d not in ("patches", "assets")]
            for f in files:
                if not f.endswith((".c", ".h", ".cpp", ".hpp", ".inc")):
                    continue
                p = os.path.join(dp, f)
                if os.path.samefile(p, HEADER):
                    continue
                try:
                    src = strip_comments(open(p, encoding="utf-8", errors="replace").read())
                except OSError:
                    continue
                pc_only = top != "port"
                depth = []
                for ln, line in enumerate(src.split("\n"), 1):
                    s = line.strip()
                    if pc_only:
                        if s.startswith("#if"):
                            depth.append("TARGET_PC" in s and "!defined(TARGET_PC)" not in s.replace(" ", ""))
                            continue
                        if s.startswith("#el") and depth:
                            depth[-1] = False
                            continue
                        if s.startswith("#endif") and depth:
                            depth.pop()
                            continue
                        if not any(depth):
                            continue
                    for m in ACCESS.finditer(line):
                        if BOUND.match(m.group(0)):
                            continue  # a bound on the words (PORT_MT_GSWF_MAX 8191), not a word
                        n = int(m.group(1))
                        kind = "GSWF" if re.search(r"gswf|GSWF|Gswf|\b_?sw(?:Set|Get|Clear)\b", m.group(0)) else "GSW"
                        if n > lo[kind]:
                            out.append((os.path.relpath(p, ROOT), ln, kind, n))
    return out


def main():
    table = rows()
    lo = {"GSW": header_value("PORT_GAME_GSW_LAST"), "GSWF": header_value("PORT_GAME_GSWF_LAST")}
    if not table or None in lo.values():
        print("save_words: can't read", HEADER)
        return 1
    if "--check" not in sys.argv:
        for k, n, a, b, w in table:
            print(f"{k:5} {a:5}..{b:<5} {n:12} {w}")
        return 0
    bad = []
    for i, (k, n, a, b, _) in enumerate(table):
        if a > b or a < 0 or b >= COUNT[k]:
            bad.append(f"{k} {n} {a}..{b}: outside 0..{COUNT[k] - 1}")
        if k in lo and a <= lo[k] and not n.startswith("AP_"):
            bad.append(f"{k} {n} {a}..{b}: inside the game's own {k}s (up to {lo[k]})")
        for k2, n2, a2, b2, _ in table[i + 1:]:
            if k == k2 and a <= b2 and a2 <= b:
                bad.append(f"{k} {n} {a}..{b} overlaps {n2} {a2}..{b2}")
    for p, ln, k, n in literal_uses(lo):
        owner = [r[1] for r in table if r[0] == k and r[2] <= n <= r[3]]
        bad.append(f"{p}:{ln}: {k} {n} as a literal ({'in ' + owner[0] if owner else 'in no range'}): "
                   "use port/include/port/save_words.h")
    for b in bad:
        print("save_words:", b)
    print(f"save_words: {len(table)} ranges, {'FAIL' if bad else 'OK'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
