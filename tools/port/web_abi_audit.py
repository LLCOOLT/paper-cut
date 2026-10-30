#!/usr/bin/env python3
"""List game code that traps or fails to link in WebAssembly (web builds).

WebAssembly checks every call against the callee's exact signature (its
parameter and result types as i32/i64/f32/f64), and a function's address is
an index into the function table, not memory. Native PCs are looser, so the
decomp gets away with three things that wasm does not allow:

1. Functions declared as data (`extern s32 envSetYami[];`, to put the
   function's address in a script or table): wasm-ld refuses to link
   ("symbol type mismatch"). Found by scanning the sources for array or
   variable declarations of names the build defines as functions.
2. Direct calls through a local prototype that differs from the definition
   (return type, or number/kind of arguments): wasm-ld links the call to a
   stub that traps ("function signature mismatch"). From the link log.
3. Calls through a function pointer cast to a different signature
   (`((f32 (*)(f32, f32, s32))intplGetValue)(...)`): `call_indirect` traps
   when the signature differs. From an audit build's
   -Wcast-function-type-* warnings; only casts whose wasm signatures differ
   are listed, and each says whether the cast result is called right there.

usage (after building, see docs/PC_PORT.md "Web"):
    ninja -C build-web-us ttyd > link-us.txt         # with -Wl,--error-limit=0
    ninja -C build-web-audit-G8ME01 -k 0 ttyd > audit-us.txt   # TTYD_PTRWIDTH_WARNINGS=ON
    web_abi_audit.py US=build-web-us,link-us.txt,audit-us.txt JP=... > report.md
"""
import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wasm_redefine_syms as wasm  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


# ---------------------------------------------------------------- sources

def obj_source(build, obj):
    """The source file (repo-relative) an object path from a link log came from."""
    obj = obj.replace("\\", "/")
    m = re.match(r"rel/rel_(\w+)\.a\(\d+_(.+)\.o\)$", obj)
    if m:
        rel, base = m.groups()
        hits = glob.glob(os.path.join(build, "CMakeFiles", f"rel_{rel}.dir", "**", base + ".o"), recursive=True)
        if not hits:
            return f"rels/{rel}/…/{base}"
        obj = hits[0].replace("\\", "/")
    m = re.search(r"CMakeFiles/[^/]+\.dir/(?:sjis/)?(.+)\.o$", obj)
    path = m.group(1) if m else obj
    m = re.search(r"(src|rels|libs|port|include)/.*", path)
    return m.group(0) if m else path


_texts = {}


def text(path):
    if path not in _texts:
        try:
            with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
                _texts[path] = f.read()
        except OSError:
            _texts[path] = ""
    return _texts[path]


def line_of(t, pos):
    return t.count("\n", 0, pos) + 1


def find_decl(path, name):
    """Line of the prototype of name in path (not a call, not the definition)."""
    t = text(path)
    n = re.escape(name)
    for pat in (rf"USER_FUNC\(\s*{n}\s*\)\s*;",
                rf"(?m)^[ \t]*(?:extern\s+|static\s+)?[A-Za-z_][\w \t\*]*?\b{n}\s*\([^;{{}}]*\)\s*;"):
        m = re.search(pat, t)
        if m:
            return line_of(t, m.start())
    m = re.search(rf"\b{n}\b", t)
    return line_of(t, m.start()) if m else None


def find_def(path, name):
    t = text(path)
    n = re.escape(name)
    for pat in (rf"USER_FUNC\(\s*{n}\s*\)\s*\{{",
                rf"(?m)^[ \t]*(?:static\s+)?[A-Za-z_][\w \t\*]*?\b{n}\s*\([^;]*?\)\s*\{{",
                rf"(?m)^\s*\w+\(\s*{n}\s*[,)]"):  # defined through a macro
        m = re.search(pat, t)
        if m:
            return line_of(t, m.start())
    m = re.search(rf"\b{n}\b", t)
    return line_of(t, m.start()) if m else None


def loc(path, line):
    return f"{path}:{line}" if line else path


# ---------------------------------------------------------------- objects

def objects(build):
    return glob.glob(os.path.join(build, "CMakeFiles", "**", "*.o"), recursive=True)


def scope_of(obj):
    m = re.search(r"rel_(\w+?)\.dir", obj.replace("\\", "/"))
    return m.group(1) if m else "dol"


def defined_functions(build):
    """name -> set of scopes ('dol' or a REL) that define it as a function."""
    out = collections.defaultdict(set)
    for o in objects(build):
        with open(o, "rb") as f:
            syms = wasm.symbols(f.read())
        for kind, flags, n in syms:
            if (n and kind == wasm.SYM_FUNCTION and not flags & wasm.FLAG_UNDEFINED
                    and flags & wasm.BINDING_MASK != wasm.BINDING_LOCAL):
                out[n].add(scope_of(o))
    return out


def defines_function(build, obj, name):
    if "(" in obj:  # archive member: renamed REL symbol
        return None
    p = os.path.join(build, obj)
    if not os.path.exists(p):
        return None
    with open(p, "rb") as f:
        for kind, flags, n in wasm.symbols(f.read()):
            if n == name and kind == wasm.SYM_FUNCTION and not flags & wasm.FLAG_UNDEFINED:
                return True
    return False


# ---------------------------------------------------------------- 1. data

DATA_DECL = re.compile(r"(?m)^[ \t]*extern\s+(?:const\s+|volatile\s+)*[A-Za-z_]\w*[\s\*]+(\w+)\s*(?:\[[^\]]*\]\s*)*;")


def functions_as_data(build, game_files):
    funcs = defined_functions(build)
    out = []
    for f in game_files:
        t = text(f)
        m_rel = re.match(r"rels/(\w+)/", f)
        scope = m_rel.group(1) if m_rel else "dol"
        for m in DATA_DECL.finditer(t):
            n = m.group(1)
            # a REL resolves its own definitions first, then the DOL's
            if n in funcs and (scope in funcs[n] or "dol" in funcs[n]):
                out.append((f, line_of(t, m.start()), n, m.group(0).strip()))
    return out


# ---------------------------------------------------------------- 2. link

MISMATCH = re.compile(r"function signature mismatch: (\S+)\n>>> defined as (.+?) in (\S+)\n"
                      r">>> defined as (.+?) in (\S+)")


def signature_mismatches(build, log):
    with open(log, encoding="utf-8", errors="replace") as f:
        t = f.read().replace("\r\n", "\n")
    out = []
    for name, sig_a, obj_a, sig_b, obj_b in MISMATCH.findall(t):
        da, db = defines_function(build, obj_a, name), defines_function(build, obj_b, name)
        if db and not da:
            sig_a, obj_a, sig_b, obj_b = sig_b, obj_b, sig_a, obj_a
        elif not da and not db:
            # both REL members: the defining one is where the function has a body
            pa, pb = obj_source(build, obj_a), obj_source(build, obj_b)
            plain = re.sub(r"^rel_\w+?__", "", name)
            if find_def(pb, plain) and re.search(rf"\b{re.escape(plain)}\s*\([^;]*\)\s*\{{", text(pb)):
                sig_a, obj_a, sig_b, obj_b = sig_b, obj_b, sig_a, obj_a
        plain = re.sub(r"^rel_\w+?__", "", name)
        dsrc, csrc = obj_source(build, obj_a), obj_source(build, obj_b)
        out.append({"name": plain, "def_sig": sig_a, "def": loc(dsrc, find_def(dsrc, plain)),
                    "call_sig": sig_b, "decl": loc(csrc, find_decl(csrc, plain))})
    return out


def sig_parts(s):
    m = re.match(r"\((.*)\) -> (\S+)", s)
    return (m.group(1), m.group(2)) if m else (s, "")


# ---------------------------------------------------------------- 3. casts

CAST = re.compile(r"^(.*?):(\d+):(\d+): warning: cast from '([^']*)'(?: \(aka '([^']*)'\))? to '([^']*)'"
                  r"(?: \(aka '([^']*)'\))?.*\[-Wcast-function-type(?:-mismatch|-strict)?\]")


def split_params(params):
    out, depth, cur = [], 0, ""
    for ch in params:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def wasm_type(c):
    """A canonical C type as clang's wasm32 ABI passes it."""
    c = re.sub(r"\b(const|volatile|restrict|__restrict)\b", "", c).strip()
    if c.endswith("*") or "(*)" in c or c.endswith("]"):
        return "i32"
    if c in ("float",):
        return "f32"
    if c in ("double",):
        return "f64"
    if c in ("long long", "unsigned long long", "long long int", "unsigned long long int"):
        return "i64"
    if c == "void":
        return None
    if c.startswith(("struct ", "union ")):
        return "agg"
    return "i32"  # int, char, short, long, _Bool, enum


def wasm_sig(fn):
    """'float (*)(int, struct Vec)' -> ('(i32, i32) -> f32', unprototyped)."""
    m = re.match(r"^(.*?)\s*\(\*\)\((.*)\)$", fn.strip())
    if not m:
        return None, False
    ret, params = m.group(1), m.group(2).strip()
    unproto = params == ""
    ps = [] if params in ("", "void") else split_params(params)
    out = []
    for p in ps:
        if p == "...":
            out.append("i32")  # varargs buffer
            continue
        out.append("i32" if wasm_type(p) == "agg" else wasm_type(p))
    r = wasm_type(ret)
    if r == "agg":  # returned through a hidden pointer
        out.insert(0, "i32")
        r = None
    return f"({', '.join(out)}) -> {r or 'void'}", unproto


def call_args(path, line, col):
    """If the cast at path:line:col is the callee of a call, as in
    ((T)f)(a, b), the number of arguments passed; else None."""
    lines = text(path).split("\n")
    if not line or line > len(lines):
        return None
    rest = "\n".join(lines[line - 1:line + 12])
    i = col - 1
    if i <= 0 or rest[i - 1] != "(":
        return None
    depth = 0
    for j in range(i - 1, len(rest)):
        if rest[j] == "(":
            depth += 1
        elif rest[j] == ")":
            depth -= 1
            if depth == 0:
                after = rest[j + 1:].lstrip()
                if not after.startswith("("):
                    return None
                depth2, inner = 0, ""
                for ch in after:
                    if ch == "(":
                        depth2 += 1
                        if depth2 == 1:
                            continue
                    elif ch == ")":
                        depth2 -= 1
                        if depth2 == 0:
                            break
                    inner += ch
                inner = inner.strip()
                return len(split_params(inner)) if inner else 0
    return None


def promoted_safe(fn, nargs):
    """A call through an unprototyped pointer passes its arguments with the
    default promotions (float becomes double, narrow integers int): it matches
    a function with that many integer and pointer parameters."""
    m = re.match(r"^(.*?)\s*\(\*\)\((.*)\)$", fn.strip())
    if not m or nargs is None:
        return False
    params = m.group(2).strip()
    ps = [] if params in ("", "void") else split_params(params)
    return len(ps) == nargs and all(wasm_type(p) == "i32" for p in ps)


def cast_mismatches(log):
    out = []
    with open(log, encoding="utf-8", errors="replace") as f:
        for raw in f:
            m = CAST.match(raw.strip())
            if not m:
                continue
            path = re.sub(r"^.*?ttyd-port[/\\]", "", m.group(1)).replace("\\", "/")
            path = re.sub(r"^build[^/]*/(?:sjis/)?", "", path)
            src = m.group(5) or m.group(4)
            dst = m.group(7) or m.group(6)
            a, ua = wasm_sig(src)
            b, ub = wasm_sig(dst)
            if a is None or b is None or (a == b and not (ua or ub)):
                continue
            ln, col = int(m.group(2)), int(m.group(3))
            nargs = call_args(path, ln, col)
            if ub and not ua and promoted_safe(src, nargs):
                continue  # ((void (*)())f)(a, b): the call passes f's own arguments
            out.append({"site": f"{path}:{ln}", "from": src, "to": dst, "from_sig": a, "to_sig": b,
                        "called": nargs is not None, "unproto": ua or ub})
    return out


# ---------------------------------------------------------------- report

def main():
    specs = []
    for arg in sys.argv[1:]:
        label, _, rest = arg.partition("=")
        parts = rest.split(",")
        specs.append((label, parts[0], parts[1] if len(parts) > 1 else None, parts[2] if len(parts) > 2 else None))
    if not specs:
        raise SystemExit(__doc__)

    import subprocess
    game_files = [f for f in subprocess.run(["git", "ls-files", "--", "src", "rels", "include", "libs"],
                                            cwd=ROOT, capture_output=True, text=True).stdout.split()
                  if f.endswith((".c", ".h"))]

    data = collections.OrderedDict()
    sigs = collections.OrderedDict()
    casts = collections.OrderedDict()
    for label, build, link, audit in specs:
        for f, ln, n, decl in functions_as_data(build, game_files):
            data.setdefault((f, ln, n, decl), []).append(label)
        if link:
            for r in signature_mismatches(build, link):
                key = (r["name"], r["def_sig"], r["def"], r["call_sig"], r["decl"])
                sigs.setdefault(key, []).append(label)
        if audit:
            for r in cast_mismatches(audit):
                key = (r["site"], r["from"], r["to"], r["from_sig"], r["to_sig"], r["called"], r["unproto"])
                casts.setdefault(key, []).append(label)

    labels = [s[0] for s in specs]

    def vers(v):
        return "all" if sorted(set(v)) == sorted(labels) else "/".join(sorted(set(v)))

    print("<!-- Generated by tools/port/web_abi_audit.py; types are wasm's: i32 is any 32-bit"
          " integer or pointer, a struct passed by value is an i32 (its address). -->\n")
    print(f"## 1. Functions declared as data ({len(data)})\n")
    print("Fix: declare the function as what it is (`USER_FUNC(name);` for a script function, "
          "its prototype otherwise).\n")
    print("| Declared at | Declaration | Versions |\n|---|---|---|")
    for (f, ln, n, decl), v in sorted(data.items()):
        print(f"| {f}:{ln} | `{decl}` | {vers(v)} |")

    args = [(k, v) for k, v in sigs.items() if sig_parts(k[1])[0] != sig_parts(k[3])[0]]
    rets = [(k, v) for k, v in sigs.items() if sig_parts(k[1])[0] == sig_parts(k[3])[0]]
    print(f"\n## 2. Local prototypes that differ from the definition ({len(sigs)})\n")
    print("Fix: make the declaration the definition's (better: include the header that declares it), "
          "and drop or add arguments at the calls.\n")
    for title, rows in ((f"Argument lists differ ({len(args)})", args), (f"Return type differs ({len(rets)})", rets)):
        print(f"### {title}\n")
        print("| Function | Definition | Defined at | Declared as | Declared at | Versions |\n|---|---|---|---|---|---|")
        for (n, ds, d, cs, c), v in sorted(rows, key=lambda kv: (kv[0][0], kv[0][4])):
            print(f"| `{n}` | `{ds}` | {d} | `{cs}` | {c} | {vers(v)} |")
        print()

    called = [(k, v) for k, v in casts.items() if k[5]]
    other = [(k, v) for k, v in casts.items() if not k[5]]
    print(f"## 3. Function pointers cast to a different signature ({len(casts)})\n")
    print("A call through the cast traps when the signatures differ. \"Called here\": the cast is the "
          "callee of a call at that line (`((T)f)(...)`). The others store or pass the pointer (a "
          "display or camera callback, say), which is then called as the cast type, or call it through "
          "a macro (`kemuriEntry`): they trap all the same.\n")
    for title, rows in ((f"Called here ({len(called)})", called), (f"Stored or passed ({len(other)})", other)):
        print(f"### {title}\n")
        print("| Site | Function type | Cast to | wasm | Versions |\n|---|---|---|---|---|")
        for (site, a, b, sa, sb, _c, unp), v in sorted(rows):
            note = " (unprototyped)" if unp else ""
            print(f"| {site} | `{a}` | `{b}` | `{sa}` → `{sb}`{note} | {vers(v)} |")
        print()


if __name__ == "__main__":
    main()
