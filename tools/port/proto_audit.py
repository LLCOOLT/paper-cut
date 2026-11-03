#!/usr/bin/env python3
"""Find prototypes that disagree with their function's definition (PC hazards).

The decomp often declares a function locally with whatever types made MWCC
match (`BOOL psndENVOn(s32 a, s32 b);` for `psndENVOn(u32, const char*)`).
Across translation units no compiler sees the clash. On PowerPC it is
harmless; on 64-bit PC a pointer passed through an s32 parameter is cut to
32 bits, and int/float positions or return kinds that differ pick the wrong
registers. This lists every prototype in src/, rels/ and include/ whose
parameter or return kinds differ from the definition's:

    ptr   pointers, arrays, function pointers, EvtWord/AddrWord (64-bit on PC)
    int   integers, enums, BOOL (32-bit or narrower)
    float f32/f64
    struct by value, void

usage: proto_audit.py [-v]     (-v: also integer-width-only differences)
"""
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORDS = {"EvtWord", "AddrWord", "SfxNameWord", "intptr_t", "uintptr_t", "size_t", "OSTime", "s64", "u64"}
FLOATS = {"f32", "f64", "float", "double", "f128"}
STRUCTS = {"Vec", "GXColor", "Mtx", "Quaternion", "Vec2", "Vec2d", "S16Vec"}
DECL = re.compile(r"^[ \t]*((?:extern|static|inline|const|unsigned|signed|struct|enum|volatile|\w+)[\w\s\*]*?)"
                  r"\b(\w+)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*(;|\{|$)", re.M)
SKIP_NAMES = {"if", "while", "for", "switch", "return", "sizeof", "USER_FUNC", "EVT_BEGIN", "defined"}


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


# include/version.h: the ordered version values the game code tests, read from the header
with open(os.path.join(ROOT, "include", "version.h"), encoding="utf-8") as _f:
    VERSION_VALUES = {m.group(1): int(m.group(2))
                      for m in re.finditer(r"^#define (VERSION_[A-Z0-9_]+) (\d+)\b", _f.read(), re.M)}
# the versions the PC port builds (name, VERSION value)
PORT_VERSIONS = (("JP", VERSION_VALUES["VERSION_JP"]), ("US", VERSION_VALUES["VERSION_US"]),
                 ("EU", VERSION_VALUES["VERSION_EU"]))


def version_cond(cond, version):
    """True/False for a condition on VERSION (include/version.h), None otherwise."""
    if not re.search(r"\bVERSION\b", cond):
        return None
    e = re.sub(r"\bVERSION_[A-Z0-9_]+\b", lambda m: str(VERSION_VALUES.get(m.group(0), "X")), cond)
    e = re.sub(r"\bVERSION\b", str(version), e).replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)
    if re.search(r"[A-Za-z_]", re.sub(r"\b(and|or|not)\b", "", e)):
        return None  # mixed with other macros: count as active
    try:
        return bool(eval(e, {"__builtins__": {}}))
    except Exception:
        return None


def pc_only(t, version=None):
    """Blank the lines the PC build does not compile: #ifndef TARGET_PC bodies,
    the #else of #ifdef TARGET_PC and, for a version, the branches of
    #if VERSION ... it does not take (other conditions count as active)."""
    out, stack = [], []  # [kind ("pc"/"ver"/"other"), active, taken]
    for line in t.split("\n"):
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", s)
        if m:
            d, cond = m.group(1), m.group(2).strip()
            if d in ("ifdef", "ifndef", "if"):
                pc = re.fullmatch(r"(defined\s*\(?\s*)?TARGET_PC\s*\)?", cond) is not None or \
                    re.fullmatch(r"!\s*defined\s*\(?\s*TARGET_PC\s*\)?", cond) is not None
                vc = version_cond(cond, version) if version is not None and d == "if" else None
                if pc:
                    act = not (d == "ifndef" or cond.startswith("!"))
                    stack.append(["pc", act, act])
                elif vc is not None:
                    stack.append(["ver", vc, vc])
                else:
                    stack.append(["other", True, True])
            elif d == "elif" and stack:
                e = stack[-1]
                if e[0] == "ver":
                    vc = version_cond(cond, version)
                    act = not e[2] and (vc if vc is not None else True)
                    e[1], e[2] = act, e[2] or act
                elif e[0] == "pc":
                    e[1], e[2] = not e[2], True
                else:
                    e[1] = True
            elif d == "else" and stack:
                e = stack[-1]
                e[1] = not e[2] if e[0] in ("pc", "ver") else True
            elif d == "endif" and stack:
                stack.pop()
            out.append("")
            continue
        out.append(line if all(e[1] for e in stack) else "")
    return "\n".join(out)


TYPEDEFS = {}  # name -> kind, from every header and source


def collect_typedefs(texts):
    for text in texts:
        for m in re.finditer(r"typedef\s+(struct|union)\s*\w*\s*\{", text):
            depth, i = 0, m.end() - 1
            while i < len(text):
                depth += text[i] == "{"
                depth -= text[i] == "}"
                if depth == 0:
                    break
                i += 1
            n = re.match(r"\s*(\w+)\s*(?:,\s*\*\s*(\w+)\s*)?;", text[i + 1:])  # } Name[, *NamePtr];
            if n:
                TYPEDEFS[n.group(1)] = "struct"
                if n.group(2):
                    TYPEDEFS[n.group(2)] = "ptr"
        for m in re.finditer(r"typedef\s+enum\s*\w*\s*(?::\s*\w+\s*)?\{[^}]*\}\s*(\w+)\s*;", text):
            TYPEDEFS[m.group(1)] = "int"
        for m in re.finditer(r"typedef\s+[^;{}()]*?\(\s*\*\s*(\w+)\s*\)\s*\(", text):
            TYPEDEFS[m.group(1)] = "ptr"
        for m in re.finditer(r"typedef\s+([^;{}()]+?)\s*\b(\w+)\s*(\[[^\]]*\])?\s*;", text):
            body, name = m.group(1).strip(), m.group(2)
            if m.group(3) or "*" in body:
                TYPEDEFS[name] = "ptr"
            elif re.match(r"^(struct|union)\s+\w+$", body):
                TYPEDEFS.setdefault(name, "struct")
            else:
                TYPEDEFS.setdefault(name, ("alias", body))


def kind(t):
    t = re.sub(r"\b(const|volatile|register|extern|static|inline)\b", "", t).strip()
    t = re.sub(r"\s+\w+$", "", t) if re.search(r"[\w\*\]]\s+\w+$", t) and not t.endswith("*") else t
    if "*" in t or "[" in t or "(" in t:
        return "ptr"
    base = t.split()[-1] if t.split() else t
    if base in WORDS or t in WORDS:
        return "ptr"
    seen = 0
    while base in TYPEDEFS and seen < 8 and base not in FLOATS and base not in STRUCTS:
        k = TYPEDEFS[base]
        if isinstance(k, str):
            return k
        if "*" in k[1]:
            return "ptr"
        base = k[1].split()[-1]
        seen += 1
    if base in WORDS:
        return "ptr"
    if base in FLOATS:
        return "float"
    if t.startswith(("struct ", "union ")) or base in STRUCTS:
        return "struct"
    if base == "void" or t == "":
        return "void"
    if base == "...":
        return "..."
    return "int"


def params(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        depth += ch in "([" and 1 or 0
        depth -= ch in ")]" and 1 or 0
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    if out == ["void"]:
        return []
    return out


def ptype(p):
    """'const char* name' -> 'const char*'; 'f32 x' -> 'f32'; 'void (*cb)(s32)' -> ptr."""
    if "(" in p:
        return "fnptr*"
    p = re.sub(r"\[[^\]]*\]", "*", p)
    m = re.match(r"^(.*?[\s\*])(\w+)$", p)
    if m and m.group(1).strip() and not re.fullmatch(r"(const\s+|volatile\s+|unsigned\s+|signed\s+|struct\s+|enum\s+)*",
                                                      m.group(1).strip() + " "):
        return m.group(1).strip()
    return p.strip()


FILES = None


def scan(version=None):
    global FILES
    defs, protos = {}, collections.defaultdict(list)
    if FILES is None:
        files = []
        for pat in ("src/**/*.c", "rels/**/*.c", "include/**/*.h", "rels/*/include/**/*.h",
                    "libs/dolsdk2004/include/**/*.h"):
            files += glob.glob(os.path.join(ROOT, pat), recursive=True)
        collect_typedefs(strip_comments(open(f, encoding="utf-8", errors="replace").read()) for f in files)
        FILES = [(f, strip_comments(open(f, encoding="utf-8", errors="replace").read()))
                 for f in files if "libs" not in f.replace("\\", "/").split("/")]
    for f, stripped in FILES:
        rel = os.path.relpath(f, ROOT).replace("\\", "/")
        text = pc_only(stripped, version)
        for m in DECL.finditer(text):
            ret, name, ps, end = m.group(1), m.group(2), m.group(3), m.group(4)
            if name in SKIP_NAMES or ret.strip().split()[-1] in ("return", "else", "case", "goto") or "=" in ret:
                continue
            if re.search(r"\bstatic\b", ret):
                continue  # a static function is its own definition in this file
            line = text.count("\n", 0, m.start()) + 1
            sig = (kind(ret), [kind(ptype(p)) for p in params(ps)], ret.strip(), ps.strip())
            if end == "{" or (end == "" and text[m.end():m.end() + 40].lstrip().startswith("{")):
                defs.setdefault(name, (rel, line, sig))
            elif end == ";":
                protos[name].append((rel, line, sig))
    return defs, protos


def main():
    verbose = "-v" in sys.argv
    hits = collections.defaultdict(list)
    defs_of = {}
    # each version as it compiles (#if VERSION ... resolved), merged
    for vname, vval in PORT_VERSIONS:
        defs, protos = scan(vval)
        audit(defs, protos, hits, defs_of, vname)
    total = 0
    for name in sorted(hits):
        drel, dline, (_, _, dretT, dps) = defs_of[name]
        print(f"{name}: defined {drel}:{dline} as {dretT} {name}({dps})")
        seen = collections.OrderedDict()
        for rel, line, why, decl, v in hits[name]:
            seen.setdefault((rel, line, why, decl), []).append(v)
        for (rel, line, why, decl), vs in seen.items():
            total += 1
            print(f"    {rel}:{line}: {why}   [{decl}]   ({'/'.join(vs)})")
    print(f"proto_audit: {total} mismatched prototypes of {len(hits)} functions")
    return 1 if total else 0


def audit(defs, protos, hits, defs_of, vname):
    for name, ps in protos.items():
        if name not in defs:
            continue
        drel, dline, (dret, dk, dretT, dps) = defs[name]
        for rel, line, (ret, k, retT, pst) in ps:
            why = []
            if ("..." in dk) != ("..." in k) and (pst or "..." in dk):
                # Apple arm64 passes variadic arguments on the stack, not in
                # registers: a call through a non-variadic prototype (or an
                # unprototyped 'T f();') of a variadic definition, or the
                # reverse, reads garbage there (other 64-bit ABIs tolerate it).
                why.append("variadic mismatch" if pst else "unprototyped () declaration of a variadic function")
                hits[name].append((rel, line, ", ".join(why), f"{retT} {name}({pst})", vname))
                defs_of.setdefault(name, defs[name])
                continue
            if "..." in dk or "..." in k:
                continue
            n = min(len(k), len(dk))
            for i in range(n):
                if k[i] != dk[i] and "void" not in (k[i], dk[i]):
                    why.append(f"arg{i} {k[i]} vs {dk[i]}")
            if len(k) < len(dk) and k:
                why.append(f"{len(dk) - len(k)} arg(s) missing")
            if not pst and dk:
                # old-style 'T f();': calls pass f32 as double, which a prototyped
                # f32 parameter does not read
                if "float" in dk:
                    why.append("unprototyped () declaration of a function taking floats")
            if ret != dret and "void" not in (ret, dret):
                why.append(f"return {ret} vs {dret}")
            if why:
                hits[name].append((rel, line, ", ".join(why), f"{retT} {name}({pst})", vname))
                defs_of.setdefault(name, defs[name])


if __name__ == "__main__":
    sys.exit(main())
