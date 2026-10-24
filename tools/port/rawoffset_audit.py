#!/usr/bin/env python3
"""Audit raw GameCube byte-offset accesses into runtime structs (PC port).

Decompiled code often reaches struct fields by hard-coded GameCube offsets,
e.g. `*(f32*)((u8*)pose + 0x98)`, `PF32(party, 0x104)` or `((s32*)p)[9]`.
On 64-bit, any struct holding pointers (or SDK types Aurora sizes differently,
GXTexObj/OSThread/...) has another layout, so such an offset hits the wrong
field. This tool finds the accesses PC code compiles, resolves the base
variable's struct type, and compares the field at that offset in the
GameCube-shaped layout (clang -m32 -malign-double, Dolphin SDK headers: same
offsets as MWCC) with the 64-bit Aurora layout (clang -m64). An access is

  ok        the covering field has the same offset on both and is scalar
  BROKEN    the offset lands elsewhere on 64-bit (or reads/writes a pointer)
  unresolved  base type not found (reported, not counted as broken)

Code PC never compiles (`#else` of TARGET_PC / NONMATCHING, asm) is skipped.

usage: tools/port/rawoffset_audit.py [-v] [--all] [--filter SUBSTR] [--max-broken N]
  -v            list every site (default: broken sites only)
  --all         also list ok/unresolved sites
  --max-broken  exit 1 if more than N broken sites remain (for CI)
"""
import argparse, collections, concurrent.futures as cf, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portversion

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLANG = os.environ.get("CLANG", "clang")
VERSION_FLAGS = portversion.defines(portversion.default())
DEFINES = ["-funsigned-char", *VERSION_FLAGS, "-DSDK_REVISION=1",
           "-DNDEBUG=1", "-DNONMATCHING", "-std=gnu99", "-w",
           "-ferror-limit=0", "-Wno-error=implicit-function-declaration",
           "-Wno-error=incompatible-function-pointer-types", "-Wno-error=int-conversion",
           "-Wno-error=incompatible-pointer-types", "-include", "port/include/port_prefix.h"]
REF = ["-m32", "-malign-double"]  # GameCube offsets (8-aligned s64/f64 as on PowerPC)
PC = ["-m64", "-fPIC", "-DTTYD_BACKEND_AURORA=1"]

def inc_flags(path, sdk):
    f = ["-Iport/include", "-Iinclude", f"-I{sdk}", "-Ilibs/musyx/include"]
    parts = path.split("/")
    if parts[0] == "rels":
        f.append(f"-Irels/{parts[1]}/include")
    return f

# ------------------------------------------------------------------ layouts
LINE = re.compile(r"^\s*(\d+)(?::\d+-\d+)? \|( +)(.*)$")
UNNAMED = re.compile(r"\((?:unnamed|anonymous) (?:struct |union )?at ([^:()]+):(\d+):(\d+)\)")

def typedef_for_unnamed(m, cache={}):
    """'struct (unnamed at f:l:c)' -> the typedef name after its closing brace."""
    path, ln, col = m.group(1), int(m.group(2)), int(m.group(3))
    key = (path, ln, col)
    if key in cache:
        return cache[key]
    name = None
    try:
        full = path if os.path.isabs(path) else os.path.join(ROOT, path)
        text = open(full, errors="replace").read()
        lines = text.split("\n")
        pos = sum(len(l) + 1 for l in lines[:ln - 1]) + col - 1
        i = text.find("{", pos)
        depth = 0
        while i < len(text):
            if text[i] == "{": depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0: break
            i += 1
        mm = re.match(r"\s*(\w+)\s*[;,\[]", text[i + 1:i + 200])
        if mm: name = mm.group(1)
    except OSError:
        pass
    cache[key] = name
    return name

def rec_name(head):
    head = head.strip()
    m = UNNAMED.search(head)
    if m:
        return typedef_for_unnamed(m)
    m = re.match(r"^(?:struct|union) (\w+)$", head)
    return m.group(1) if m else None

def parse_layouts(text):
    """{name: {"size": n, "fields": [(path, off, type)]}} (all depths, absolute offsets)."""
    out = {}
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        lines = block.strip("\n").splitlines()
        m = LINE.match(lines[0])
        if not m:
            continue
        name = rec_name(m.group(3))
        head = m.group(3).strip()
        ctype = name if UNNAMED.search(head) else head
        if not name or name in out:
            continue
        fields, stack, size = [], [], None
        for ln in lines[1:]:
            ms = re.search(r"\[sizeof=(\d+)", ln)
            if ms:
                size = int(ms.group(1)); break
            mf = LINE.match(ln)
            if not mf:
                continue
            depth = (len(mf.group(2)) - 1) // 2
            body = mf.group(3).strip()
            if " " not in body:
                continue
            if body.endswith(")"):  # anonymous struct/union member: no name in C paths
                typ, fname = body, ""
            else:
                typ, fname = body.rsplit(" ", 1)
            del stack[depth - 1:]
            stack.append(fname)
            fields.append((".".join(x for x in stack if x), int(mf.group(1)), typ.strip(), depth))
        out[name] = {"size": size, "fields": fields, "ctype": ctype}
    return out

def dump(path, arch, sdk):
    cmd = [CLANG, "-fsyntax-only", *arch, *DEFINES, *inc_flags(path, sdk),
           "-Xclang", "-fdump-record-layouts-complete", path]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return parse_layouts(r.stdout)

# ------------------------------------------------------------------ source scan
def strip_comments(s):
    def rep(m):
        t = m.group(0)
        return t if t[0] in "\"'" else re.sub(r"[^\n]", " ", t)
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', rep, s, flags=re.S)

KNOWN = {"TARGET_PC": True, "NONMATCHING": True, "PORT_BEDECODE_DISC": False,
         "MUSY_TARGET_PC": True, "__MWERKS__": False, "TTYD_BACKEND_AURORA": None}

def eval_cond(expr):
    e = re.sub(r"defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)",
               lambda m: f"D_{m.group(1) or m.group(2)}", expr)
    names = set(re.findall(r"\bD_(\w+)", e))
    env = {}
    for n in names:
        v = KNOWN.get(n)
        if v is None:
            return None
        env["D_" + n] = v
    e = e.replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)
    if re.search(r"[A-Za-z_]\w*", re.sub(r"\bD_\w+|\band\b|\bor\b|\bnot\b", "", e)):
        return None
    try:
        return bool(eval(e, {}, env))
    except Exception:
        return None

def pc_live_lines(text):
    """Set of 1-based line numbers the PC build may compile."""
    live, stack = set(), []  # stack of (parent_live, this_branch_value, any_taken)
    cur = True
    in_asm = 0
    for no, line in enumerate(text.split("\n"), 1):
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", s)
        if m:
            kw, rest = m.group(1), m.group(2).strip()
            if kw in ("ifdef", "ifndef", "if"):
                if kw == "ifdef": v = eval_cond(f"defined({rest.split()[0]})")
                elif kw == "ifndef":
                    v = eval_cond(f"defined({rest.split()[0]})"); v = None if v is None else not v
                else: v = eval_cond(rest)
                stack.append((cur, v, v is True))
                cur = cur and v is not False
            elif kw == "elif" and stack:
                parent, _, taken = stack[-1]
                v = False if taken else eval_cond(rest)
                stack[-1] = (parent, v, taken or v is True)
                cur = parent and v is not False
            elif kw == "else" and stack:
                parent, v, taken = stack[-1]
                nv = False if taken else (None if v is None else not v)
                stack[-1] = (parent, nv, True)
                cur = parent and nv is not False
            elif kw == "endif" and stack:
                cur = stack.pop()[0]
            continue
        if cur:
            live.add(no)
    return live

BASE = r"(\(?\s*[&*]?\s*[A-Za-z_]\w*(?:\([^()]*\))?(?:(?:->|\.)\w+|\[[^\]]*\])*\s*\)?)"
OFF = r"(0x[0-9A-Fa-f]+|\d+)"
BYTEPTR = r"\(\s*(?:const\s+)?(?:u8|s8|char|unsigned\s+char|signed\s+char)\s*\*\s*\)\s*"
SITE_RES = [re.compile(BYTEPTR + BASE + r"\s*\+\s*" + OFF + r"\b"),
            re.compile(r"\b" + OFF + r"\s*\+\s*\(\s*" + BYTEPTR + BASE)]
ALIAS_RE = re.compile(r"\*\s*\(\s*((?:const\s+|struct\s+)*\w+(?:\s*\*)*)\s*\*\s*\)\s*\(\s*(\w+)\s*\+\s*" + OFF + r"\s*\)")
WORD_RE = re.compile(r"\(\s*\(\s*(s32|u32|int|f32|BOOL|s16|u16|void\s*\*)\s*\*\s*\)\s*" + BASE +
                     r"\s*\)\s*\[\s*" + OFF + r"\s*\]")
CAST_BEFORE = re.compile(r"\(\s*((?:const\s+|volatile\s+|struct\s+)*\w+(?:\s*\*)*)\s*\*\s*\)\s*\(*\s*$")
MACRO_DEF = re.compile(r"^[ \t]*#\s*define\s+(\w+)\s*\(([^)]*)\)\s*(.*(?:\\\n.*)*)", re.M)
WIDTH = {"s32": 4, "u32": 4, "int": 4, "f32": 4, "BOOL": 4, "s16": 2, "u16": 2}

def find_sites(text):
    """[(pos, base, off, castType)] for direct sites and the macros that wrap them."""
    sites = []
    for rx in SITE_RES:
        for m in rx.finditer(text):
            if rx is SITE_RES[0]: base, off, og = m.group(1), m.group(2), 2
            else: off, base, og = m.group(1), m.group(2), 1
            lo = max(0, m.start() - 80)
            cm = CAST_BEFORE.search(text[lo:m.start()])
            sites.append((m.start(), base, int(off, 0), cm.group(1) if cm else None,
                          {"kind": "byte", "span": m.span(og),
                           "castspan": (lo + cm.start(1), lo + cm.end(1)) if cm else None}))
    for m in ALIAS_RE.finditer(text):
        var = m.group(2)
        before = text[:m.start()]
        src = None
        for am in re.finditer(r"\b" + re.escape(var) + r"\s*=\s*\(\s*(?:u8|char|s8)\s*\*\s*\)\s*([^;]+);", before):
            src = am.group(1)
        if src is None:
            continue
        sites.append((m.start(), src.strip(), int(m.group(3), 0), m.group(1).strip(),
                      {"kind": "byte", "span": m.span(3), "castspan": m.span(1), "alias": var}))
    for m in WORD_RE.finditer(text):
        t = m.group(1).replace(" ", "")
        w = 8 if t == "void*" else WIDTH.get(t, 4)
        # ((void**)p)[N] on PC indexes by 8; the GC index is N*4
        sites.append((m.start(), m.group(2), int(m.group(3), 0) * 4,
                      "void*" if t == "void*" else t, {"kind": "word", "span": m.span(), "elem": m.group(1)}))
    return sites

MBODY = re.compile(BYTEPTR + r"\(?\s*(\w+)\s*\)?\s*\+\s*\(?\s*(0x[0-9A-Fa-f]+|\d+|\w+)\s*\)?")

def split_args(text, i):
    """Arguments of a call whose '(' ends at i: (args, spans, end)."""
    args, spans, depth, cur, st = [], [], 1, "", i
    while i < len(text) and depth:
        c = text[i]
        if c in "([{": depth += 1
        elif c in ")]}": depth -= 1
        if depth == 1 and c == ",": args.append(cur); spans.append((st, i)); cur = ""; st = i + 1
        elif depth: cur += c
        i += 1
    args.append(cur); spans.append((st, i - 1))
    return args, spans, i

def const_int(expr):
    """Value of a constant offset like 0x1C8 + 4, else None."""
    e = expr.strip()
    if not re.fullmatch(r"[0-9a-fA-FxX+\-*() ]+", e) or not re.search(r"\d", e):
        return None
    try:
        v = eval(re.sub(r"\b0+(\d)", r"\1", e), {"__builtins__": {}}, {})
    except Exception:
        return None
    return v if isinstance(v, int) else None

def macro_sites(text, live_of):
    """Expand uses of function-like macros whose body is a raw-offset access."""
    out = []
    for md in MACRO_DEF.finditer(text):
        name, params = md.group(1), [p.strip() for p in md.group(2).split(",")]
        body = md.group(3)
        lineno = text.count("\n", 0, md.start()) + 1
        if lineno not in live_of:
            continue
        for bm in MBODY.finditer(body):
            bname, offtok = bm.group(1), bm.group(2)
            if bname not in params:
                continue
            bi = params.index(bname)
            oi = params.index(offtok) if offtok in params else None
            if oi is None and not re.match(r"0x[0-9A-Fa-f]+$|\d+$", offtok):
                continue
            fixed = 0 if oi is not None else int(offtok, 0)
            cm = CAST_BEFORE.search(body[max(0, bm.start() - 80):bm.start()])
            cast = cm.group(1) if cm else None
            ci = params.index(cast.strip()) if cast and cast.strip() in params else None
            for u in re.finditer(r"\b" + name + r"\s*\(", text):
                if u.start() == md.start() + md.group(0).find(name):
                    continue
                args, spans, end = split_args(text, u.end())
                if len(args) != len(params):
                    continue
                o = fixed
                if oi is not None:
                    o = const_int(args[oi])
                    if o is None: continue
                c = args[ci].strip() if ci is not None else cast
                if ci is not None and c.endswith("*"):
                    c = c  # pointer-typed access
                elif ci is not None:
                    c = c  # scalar
                info = {"kind": "macro", "span": spans[oi] if oi is not None else None,
                        "full": (u.start(), end), "fixed": fixed, "typearg": ci is not None}
                out.append((u.start(), args[bi].strip(), o, c, name, info))
    return out

DECL_CACHE = {}
def tag_of_typedef_body(t, pp):
    """typedef struct TAG { ... } t;  ->  TAG"""
    m = re.search(r"\}\s*" + re.escape(t) + r"\s*;", pp)
    if not m:
        return None
    depth, i = 0, m.start()
    while i >= 0:
        c = pp[i]
        if c == "}": depth += 1
        elif c == "{":
            depth -= 1
            if depth == 0: break
        i -= 1
    mt = re.search(r"typedef\s+(?:struct|union)\s+(\w+)\s*$", pp[max(0, i - 200):i])
    return mt.group(1) if mt else None

def strip_base(base):
    b = base.strip()
    while b.startswith("(") or b.startswith("&"):
        if b.startswith("&"): b = b[1:].strip(); continue
        # drop a leading '(' only if unbalanced
        if b.count("(") > b.count(")"): b = b[1:].strip()
        else: break
    while b.endswith(")") and b.count(")") > b.count("("):
        b = b[:-1].strip()
    return b

def alias(t, layouts, pp):
    seen = set()
    while t and t not in layouts and pp and t not in seen and t not in UNTYPED:
        seen.add(t)
        tg = tag_of_typedef_body(t, pp)
        if tg and tg in layouts:
            return tg
        m = re.search(r"typedef\s+(?:struct|union)\s+(\w+)\s+" + re.escape(t) + r"\s*;", pp) or \
            re.search(r"typedef\s+(\w+)\s+" + re.escape(t) + r"\s*;", pp)
        if not m:
            break
        t = m.group(1)
    return t

def resolve_type(text, pos, base, layouts, live=None, pp=""):
    """Struct name and whether base is an array of it."""
    b = strip_base(base)
    mc = re.match(r"^\(\s*(?:struct\s+)?(\w+)\s*\*\s*\)\s*(.*)$", b)
    if mc and mc.group(1) not in UNTYPED:  # explicit cast of the base
        return alias(mc.group(1), layouts, pp), False
    mf = re.match(r"^(\w+)\s*\(.*\)$", b)
    if mf:  # function call: its declared return type
        for src in (text, pp):
            m = re.search(r"\b(?:struct\s+)?(\w+)\s*\*\s*" + re.escape(mf.group(1)) + r"\s*\(", src)
            if m:
                return alias(m.group(1), layouts, pp), False
        return None, False
    chain = re.split(r"->|\.", b)
    var = re.sub(r"\[.*", "", chain[0])
    before = text[:pos]
    t, arr = None, False
    pats = [re.compile(r"\b(?:struct\s+)?(\w+)\s*\*+\s*(?:const\s+)?" + re.escape(var) + r"\s*[;,=)\[]"),
            re.compile(r"\b(?:struct\s+)?(\w+)\s+" + re.escape(var) + r"\s*(\[)"),
            re.compile(r"\b(?:struct\s+)?(\w+)\s+" + re.escape(var) + r"\s*[;=,)]")]
    best = -1
    for i, p in enumerate(pats):
        for m in p.finditer(before):
            if m.group(1) in ("return", "sizeof", "else", "case", "goto", "extern"):
                continue
            if live is not None and (text.count("\n", 0, m.start()) + 1) not in live:
                continue
            if m.start() > best:
                best, t, arr = m.start(), m.group(1), i == 1
    if t is None and pp:
        for i, p in enumerate(pats):
            m = None
            for m in p.finditer(pp):
                pass
            if m and m.group(1) not in ("return", "sizeof", "else", "case", "goto"):
                t, arr = m.group(1), i == 1
                break
    if t is None:
        return None, False
    t = alias(t, layouts, pp)
    # local index into an array var: v[i] -> element type
    if "[" in chain[0] and not arr:
        pass
    for f in chain[1:]:
        f = re.sub(r"\[.*", "", f)
        rec = layouts.get(t)
        if not rec: return None, False
        ft = next((ty for path, _, ty, d in rec["fields"] if d == 1 and path == f), None)
        if ft is None: return None, False
        mm = re.match(r"(?:struct\s+)?(\w+)\s*(\*?)", ft)
        t = alias(mm.group(1), layouts, pp) if mm else None
        arr = "[" in ft
    return t, arr

def leaf_at(rec, off):
    best = None
    fl = rec["fields"]
    for idx, (path, o, ty, d) in enumerate(fl):
        if o <= off:
            nxt = rec["size"]
            for p2, o2, _, d2 in fl[idx + 1:]:
                if d2 <= d: nxt = o2; break
            if off < nxt and (best is None or d > best[3]):
                best = (path, o, ty, d)
    return best

def is_ptr(ty):
    return "*" in ty or "(*)" in ty

UNTYPED = {"void", "s32", "u32", "u8", "s8", "char", "f32", "s16", "u16", "int", "EvtWord", "BOOL"}

def same_layout(r, p):
    return r["size"] == p["size"] and [f[1:3] for f in r["fields"]] == [f[1:3] for f in p["fields"]]

def map_off(ref, pc, sname, off, prefix=""):
    """GC offset in sname -> (C path, gc offset of leaf, pc offset of leaf, leaf type) or (None, why)."""
    r, p = ref.get(sname), pc.get(sname)
    if not r or not p or not r["size"]:
        return None, f"{sname}?"
    leaf = leaf_at(r, off)
    if not leaf:
        return None, f"{sname}+{off:#x} past end"
    path, o, ty, _ = leaf
    po = next((po for pp, po, _, _ in p["fields"] if pp == path), None)
    if po is None:
        return None, f"{sname}.{path} missing on PC"
    ma = re.match(r"^(?:struct\s+)?(\w+)\s*\[(\d+)\]$", ty)
    if ma and ma.group(1) in ref and ma.group(1) in pc and ref[ma.group(1)]["size"]:
        el = ma.group(1)
        idx, inner = divmod(off - o, ref[el]["size"])
        sub, why = map_off(ref, pc, el, inner)
        if sub is None:
            return None, why
        spath, so, spo, sty = sub
        return (f"{path}[{idx}]" + (("." + spath) if spath else ""), o + idx * ref[el]["size"] + so,
                po + idx * pc[el]["size"] + spo, sty), None
    mp = re.match(r"^(.*\*)\s*\[(\d+)\]$", ty)
    if mp and (off - o) % 4 == 0:  # array of pointers: 4-byte GC slots, 8-byte PC slots
        idx = (off - o) // 4
        return (f"{path}[{idx}]", off, po + idx * (p["size"] and 8), mp.group(1)), None
    return (path, o, po, ty), None

def classify(ref, pc, sname, off, cast, arr):
    if sname in UNTYPED:
        if cast and cast.replace(" ", "").endswith("*"):
            return "untyped-ptr", f"pointer stored in a {sname} buffer"
        return "untyped", f"{sname} buffer"
    r, p = ref.get(sname), pc.get(sname)
    if not r or not p or not r["size"]:
        return "unresolved", f"{sname}?"
    ptrcast = bool(cast and cast.replace(" ", "").endswith("*"))
    if same_layout(r, p) and not ptrcast:
        return "ok", f"{sname} same layout"
    if arr or off >= r["size"]:
        idx, off = divmod(off, r["size"])
        if idx and r["size"] != p["size"]:
            return "BROKEN", f"{sname}[{idx}] stride {r['size']:#x}->{p['size']:#x}"
    m, why = map_off(ref, pc, sname, off)
    if m is None:
        return "BROKEN", why
    path, o, po, ty = m
    where = f"{sname}.{path}"
    if po != o:
        return "BROKEN", f"{where} {o:#x}->{po:#x}"
    if (is_ptr(ty) != ptrcast) and (cast or "").strip() not in ("", "void"):
        return "BROKEN", f"{where} pointer/int width mismatch"
    return "ok", where

def site_mapping(ref, pc, sname, off, cast, arr):
    """(idx, path, delta, leaftype) so that GC offset off == idx*size + offsetof(path) + delta."""
    idx = 0
    if arr or off >= ref[sname]["size"]:
        idx, off = divmod(off, ref[sname]["size"])
    m, why = map_off(ref, pc, sname, off)
    if m is None:
        return None
    path, o, po, ty = m
    return idx, path, off - o, ty

def audit_file(path, want_map=False):
    full = os.path.join(ROOT, path)
    raw = open(full, errors="replace").read()
    text = strip_comments(raw)
    live = pc_live_lines(text)
    found = [(p, b, o, c, None, i) for p, b, o, c, i in find_sites(text)] + macro_sites(text, live)
    res = []
    def define_params(pos):
        """Params of the #define whose body contains pos (None if not in one)."""
        ls = text.rfind("\n", 0, pos) + 1
        while True:
            line_end = text.find("\n", ls)
            m = re.match(r"\s*#\s*define\s+\w+(?:\(([^)]*)\))?", text[ls:line_end if line_end >= 0 else None])
            if m:
                return [x.strip() for x in (m.group(1) or "").split(",")]
            if ls == 0 or not text[:ls - 1].rstrip(" \t").endswith("\\"):
                return None
            ls = text.rfind("\n", 0, ls - 1) + 1
    keep = []
    for s_ in found:
        if (text.count("\n", 0, s_[0]) + 1) not in live:
            continue
        params = define_params(s_[0])
        if params is not None and s_[4] is None and strip_base(s_[1]) in params:
            continue  # a macro's own body: its uses are audited instead
        keep.append(s_)
    found = keep
    if not found:
        return path, res
    ref = dump(path, REF, "libs/dolsdk2004/include")
    pc = dump(path, PC, "extern/aurora/include")
    r = subprocess.run([CLANG, "-E", "-P", *PC, *DEFINES, *inc_flags(path, "extern/aurora/include"), path],
                       cwd=ROOT, capture_output=True, text=True)
    pp = r.stdout
    for pos, base, off, cast, macro, info in found:
        line = text.count("\n", 0, pos) + 1
        sname, arr = resolve_type(text, pos, base, pc, live, pp)
        mapping = None
        if sname is None:
            v = re.sub(r"^[(\s&]+|[)\s]+$", "", base)
            if re.search(r"(?:^|[\s*])" + re.escape(v) + r"\s*\([^;{]*\)\s*[;{]", text, re.M) \
                    or "jumptable_" in raw.split("\n")[line - 1]:
                continue  # code address (switch jump tables), not a struct
            st, why = "unresolved", f"type of {base}?"
        else:
            st, why = classify(ref, pc, sname, off, cast, arr)
            if st == "BROKEN" and want_map:
                mapping = site_mapping(ref, pc, sname, off, cast, arr)
        res.append((line, st, base, off, why, macro) if not want_map else
                   {"line": line, "st": st, "base": base, "off": off, "why": why, "macro": macro,
                    "info": info, "sname": sname, "arr": arr, "cast": cast, "map": mapping,
                    "pctype": pc.get(sname, {}).get("ctype") if sname else None})
    return path, res

def files(filt):
    return portversion.sources(VERSION, filt)

VERSION = portversion.default()

def set_version(version):
    """Layouts and sources of one game version (JP and US structs differ)."""
    global VERSION
    VERSION = version
    i = DEFINES.index(VERSION_FLAGS[0])
    DEFINES[i:i + len(VERSION_FLAGS)] = VERSION_FLAGS[:] = portversion.defines(version)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--filter")
    ap.add_argument("--max-broken", type=int)
    portversion.add_argument(ap)
    a = ap.parse_args()
    set_version(a.version)
    tot = collections.Counter()
    per_struct = collections.Counter()
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        for path, res in ex.map(audit_file, files(a.filter)):
            for line, st, base, off, why, macro in res:
                tot[st] += 1
                if st == "BROKEN":
                    per_struct[why.split(".")[0].split("[")[0].split("+")[0].split(" ")[0]] += 1
                if st == "BROKEN" or a.all or (a.v and st != "ok"):
                    m = f" via {macro}" if macro else ""
                    print(f"{path}:{line}: {st}: {base}+{off:#x}{m}  [{why}]")
    print("broken by struct:")
    for s, n in per_struct.most_common():
        print(f"{n:6d}  {s}")
    print(f"raw-offset sites: {sum(tot.values())}  ok {tot['ok']}  BROKEN {tot['BROKEN']}  "
          f"untyped {tot['untyped']}  untyped-ptr {tot['untyped-ptr']}  unresolved {tot['unresolved']}")
    if a.max_broken is not None and tot["BROKEN"] > a.max_broken:
        sys.exit(1)

if __name__ == "__main__":
    main()
