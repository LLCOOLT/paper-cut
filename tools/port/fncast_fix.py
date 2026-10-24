#!/usr/bin/env python3
"""Rewrite calls through recast function pointers so they are correct on PC.

Each `((CAST)fn)(args)` flagged by -Wcast-function-type-mismatch in an audit
build (see fncast_audit.py) becomes

    PCCALL(((CAST)fn)(args), fn(pc args))

PCCALL (include/pc_call.h) expands to the original call on GameCube, so the
matching build is unchanged, and to a direct call with the function's real
prototype on PC. The PC arguments follow PowerPC's assignment: the k-th
integer/pointer/struct argument of the cast feeds the k-th such parameter of
the real function, the k-th float argument the k-th float parameter.
Conversions: a struct the real function takes by value but the cast passes
as a pointer is dereferenced (and the reverse takes the address); pointers
and integers are converted through intptr_t. Extra cast arguments are
dropped; real parameters the cast does not supply get 0 and are reported.

usage: fncast_fix.py AUDIT_LOG [--apply]   (dry run lists the rewrites)
"""
import collections
import re
import sys

ROOT_RE = re.compile(r"^.*?ttyd-port[/\\]")
WARN = re.compile(r"^(.*?):(\d+):(\d+): warning: cast from '([^']*)'(?: \(aka '([^']*)'\))? to '([^']*)'(?: \(aka '([^']*)'\))?"
                  r".*\[-Wcast-function-type-mismatch\]")
NOTE = re.compile(r"^(.*?):(\d+):(\d+): note: expanded from macro '(\w+)'")
FLOATS = {"f32", "f64", "float", "double"}


def rel(p):
    return ROOT_RE.sub("", p).replace("\\", "/")


def split_params(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip() and cur.strip() != "void":
        out.append(cur.strip())
    return out


def fn_type(t):
    m = re.match(r"^(.*?)\s*\(\*\)\((.*)\)$", t.strip())
    return (m.group(1).strip(), split_params(m.group(2))) if m else None


def cls(t):
    """PowerPC register class plus a finer kind for conversions."""
    t0 = t.strip()
    if t0.endswith("*") or "(*)" in t0 or t0.endswith("]"):
        return "gpr", "ptr"
    base = re.sub(r"\b(const|volatile)\b", "", t0).strip()
    if base in FLOATS:
        return "fpr", "float"
    if base.startswith("struct ") or base.startswith("union ") or base in ("Vec", "Mtx", "GXColor", "Quaternion"):
        return "gpr", "struct"
    return "gpr", "int"


def collect(log):
    sites = {}
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    for i, line in enumerate(lines):
        m = WARN.match(line.strip())
        if not m:
            continue
        loc = (rel(m.group(1)), int(m.group(2)), int(m.group(3)))
        j = i + 1
        while j < len(lines):
            n = NOTE.match(lines[j].strip())
            if n:
                loc = (rel(n.group(1)), int(n.group(2)), int(n.group(3)))
            elif not lines[j].startswith(" ") and ": note:" not in lines[j] and "|" not in lines[j][:8]:
                break
            j += 1
        real = m.group(5) or m.group(4)
        cast = m.group(7) or m.group(6)
        sites[loc] = (real, cast)
    return sites


def balanced(text, i):
    """text[i] == '(' -> index after its matching ')'."""
    depth = 0
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        elif text[i] == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        i += 1
    raise ValueError("unbalanced")


def find_call(text, off):
    """From the cast's '(' at off: (outer_start, outer_end, fn, args)."""
    if text[off] != "(":
        off = text.rindex("(", 0, off + 1)
    type_end = balanced(text, off)
    m = re.match(r"\s*(\w+)\s*\)", text[type_end:])
    if not m:
        raise ValueError("not a ((T)fn)(...) call")
    fn = m.group(1)
    outer = text.rindex("(", 0, off)
    if text[outer + 1:off].strip():
        raise ValueError("unexpected text before cast")
    close = type_end + m.end()
    k = close
    while text[k] in " \t\r\n\\":
        k += 1
    if text[k] != "(":
        raise ValueError("cast not called")
    end = balanced(text, k)
    return outer, end, fn, split_params(text[k + 1:end - 1])


def pc_args(real, cast, args, notes):
    rr, rp = fn_type(real)
    cr, cp = fn_type(cast)
    pools = collections.defaultdict(list)
    for t, a in zip(cp, args):
        pools[cls(t)[0]].append((t, a))
    out = []
    for t in rp:
        c, k = cls(t)
        if not pools[c]:
            notes.append(f"real parameter '{t}' not supplied by the cast: passing 0")
            out.append("0")
            continue
        ct, a = pools[c].pop(0)
        ck = cls(ct)[1]
        if k == ck or (k == "int" and ck == "int"):
            out.append(a)
        elif k == "struct" and ck == "ptr":
            out.append(f"*({a})")
        elif k == "ptr" and ck == "struct":
            out.append(f"&({a})")
        elif k == "ptr" and ck == "int":
            out.append(f"({t})(intptr_t)({a})")
        elif k == "int" and ck == "ptr":
            out.append(f"({t})(intptr_t)({a})")
        else:
            out.append(a)
    return out


OBJ_MACRO = re.compile(r"^(#define\s+(\w+)\s+)\(\s*(\(.*\))\s*(\w+)\s*\)\s*$")


def macro_rewrite(line, real, cast, notes):
    """'#define NAME ((CAST)fn)' -> a function-like macro making the same call."""
    m = OBJ_MACRO.match(line)
    if not m:
        return None
    n = len(fn_type(cast)[1])
    params = [f"a{i}" for i in range(n)]
    fn = m.group(4)
    gc = f"({m.group(3)}{fn})({', '.join(params)})"
    pc = f"{fn}({', '.join(pc_args(real, cast, params, notes))})"
    return f"#define {m.group(2)}({', '.join(params)}) PCCALL({gc}, {pc})"


def main():
    log = sys.argv[1]
    apply = "--apply" in sys.argv
    sites = collect(log)
    by_file = collections.defaultdict(list)
    for (f, line, col), types in sites.items():
        by_file[f].append((line, col, types))
    failures = 0
    for f, items in sorted(by_file.items()):
        text = open(f, encoding="utf-8").read()
        starts = [0]
        for ln in text.split("\n"):
            starts.append(starts[-1] + len(ln) + 1)
        edits = {}
        for line, col, (real, cast) in items:
            off = starts[line - 1] + col - 1
            notes = []
            try:
                s, e, fn, args = find_call(text, off)
                if text[max(0, s - 7):s] == "PCCALL(":
                    continue
                pc = f"{fn}({', '.join(pc_args(real, cast, args, notes))})"
                edits[(s, e)] = (text[s:e], pc)
                tag = "; ".join(notes)
                print(f"{f}:{line}: {fn}{' [' + tag + ']' if tag else ''}")
            except Exception as ex:  # noqa: BLE001
                src_line = text[starts[line - 1]:starts[line] - 1]
                new = macro_rewrite(src_line, real, cast, notes)
                if new:
                    edits[(starts[line - 1], starts[line] - 1)] = (None, new)
                    print(f"{f}:{line}: macro {new.split()[1].split('(')[0]}")
                    continue
                failures += 1
                print(f"{f}:{line}: SKIPPED ({ex})")
        if apply and edits:
            for (s, e), (orig, pc) in sorted(edits.items(), reverse=True):
                text = text[:s] + (pc if orig is None else f"PCCALL({orig}, {pc})") + text[e:]
            if '#include "pc_call.h"' not in text:
                m = re.search(r"^#include .*\n", text, re.M)
                pos = m.end() if m else 0
                text = text[:pos] + '#include "pc_call.h"\n' + text[pos:]
            open(f, "w", encoding="utf-8", newline="\n").write(text)
    print(f"{len(sites)} sites, {failures} skipped")


if __name__ == "__main__":
    main()
