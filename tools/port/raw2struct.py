#!/usr/bin/env python3
"""Convert raw `s32 name[] = { words... }` data tables into typed struct arrays for the PC port.

The decomp's generated data tables are flat lists of big-endian 32-bit words, with pointers
written as `(s32)symbol`. On 64-bit hosts these neither compile nor have the layout the
consuming code expects. For every table whose name matches an entry in
tools/port/raw2struct_types.txt this tool emits

    #ifdef TARGET_PC
    <qualifiers> StructType name[] = { { .field = value, ... }, ... };
    #else
    <original definition, unchanged>
    #endif

and rewrites the table's forward declarations / externs the same way. The GameCube struct
layout is taken from clang's PowerPC target (-fdump-record-layouts-canonical); words are mapped
onto fields by byte offset (sub-word fields are split from the word's big-endian bytes, float
fields get exact float literals, padding must be zero).

usage:
  raw2struct.py list [--all]              raw tables, grouped into families (N = digits)
  raw2struct.py convert [--filter S] [-n] convert matching tables in place (-n: dry run)
  raw2struct.py strip [files]             undo: restore the original definitions
  raw2struct.py verify [--filter S] [files]
        compile each changed file twice for powerpc-eabi (original without TARGET_PC, converted
        with TARGET_PC, so EvtWord/pointers are 32-bit) and compare every converted symbol's
        bytes and relocations; also check that `clang -E -P` without TARGET_PC is unchanged.
"""
import argparse, collections, concurrent.futures as cf, json, os, re, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portversion

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TYPES_FILE = os.path.join(ROOT, "tools/port/raw2struct_types.txt")
WORK = os.environ.get("R2S_WORK", "/tmp/raw2struct")
RESDIR = subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True).stdout.strip()

PPC = ["-target", "powerpc-unknown-eabi", "-w", "-std=gnu99", "-funsigned-char", "-fdeclspec",
       *portversion.defines(portversion.default())[:2], "-DSDK_REVISION=1", "-DNDEBUG=1",
       "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
       "-Wno-error=incompatible-pointer-types", "-Wno-error=incompatible-function-pointer-types"]
# TARGET_PC on the 32-bit PowerPC target: PC header variants, 32-bit big-endian layout.
PC32 = ["-DTARGET_PC", "-DMATH_NO_SQRT_HACK", "-ffreestanding", "-include", os.path.join(RESDIR, "include/stdint.h")]
INC = ["-Iinclude", "-Ilibs/dolsdk2004/include", "-Ilibs/dolsdk2004/include/libc",
       "-Ilibs/musyx/include"]

def inc_for(path):
    p = path.split("/")
    return ["-I" + os.path.dirname(path)] + INC + ([f"-Irels/{p[1]}/include"] if p[0] == "rels" else [])

# ---------------------------------------------------------------------------------------------
# C source scanning

def skip_ws_comments(s, i):
    n = len(s)
    while i < n:
        if s[i].isspace(): i += 1
        elif s.startswith("/*", i): i = s.index("*/", i) + 2
        elif s.startswith("//", i): i = s.find("\n", i); i = n if i < 0 else i
        else: break
    return i

def split_top(body):
    """Split an initializer body at top-level commas (strings/comments/parens aware)."""
    out, depth, i, start, n = [], 0, 0, 0, len(body)
    while i < n:
        c = body[i]
        if c == '"' or c == "'":
            j = i + 1
            while body[j] != c:
                j += 2 if body[j] == "\\" else 1
            i = j + 1; continue
        if body.startswith("/*", i): i = body.index("*/", i) + 2; continue
        if body.startswith("//", i): i = body.index("\n", i); continue
        if c in "([{": depth += 1
        elif c in ")]}": depth -= 1
        elif c == "," and depth == 0:
            out.append(body[start:i].strip()); start = i + 1
        i += 1
    last = body[start:].strip()
    if last: out.append(last)
    return out

def find_close(s, i):
    """s[i] == '{'; return index of the matching '}'."""
    depth, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '"' or c == "'":
            j = i + 1
            while s[j] != c:
                j += 2 if s[j] == "\\" else 1
            i = j + 1; continue
        if s.startswith("/*", i): i = s.index("*/", i) + 2; continue
        if s.startswith("//", i): i = s.index("\n", i); continue
        if c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: return i
        i += 1
    raise ValueError("unbalanced")

DEF_RE = re.compile(r"^(?P<q>(?:static\s+|extern\s+|const\s+|__declspec\(section\s*\"[^\"]*\"\)\s*)*)"
                    r"s32\s+(?P<n>\w+)\s*\[\s*(?P<d>[^\]]*)\]\s*=\s*\{", re.M)
DECL_RE = re.compile(r"^(?P<q>(?:static\s+|extern\s+|const\s+|__declspec\(section\s*\"[^\"]*\"\)\s*)*)"
                     r"s32\s+(?P<n>\w+)\s*\[\s*(?P<d>[^\]]*)\]\s*;[ \t]*$", re.M)

class RawDef:
    pass

def preproc_depth_ok(s, pos):
    """True if pos is not inside an #if 0 / #ifdef NONMATCHING etc.? (we accept any level)."""
    return True

def scan_defs(text):
    defs = []
    for m in DEF_RE.finditer(text):
        ob = m.end() - 1
        try: cb = find_close(text, ob)
        except (ValueError, IndexError): continue
        if "#" in text[ob:cb]: continue   # split by preprocessor lines; leave alone
        j = skip_ws_comments(text, cb + 1)
        if j >= len(text) or text[j] != ";": continue
        end = j + 1
        if end < len(text) and text[end] == "\n": end += 1
        d = RawDef()
        d.start, d.end, d.qual, d.name, d.dim = m.start(), end, m.group("q"), m.group("n"), m.group("d").strip()
        d.text = text[d.start:d.end]
        d.words = split_top(text[ob + 1:cb])
        defs.append(d)
    return defs

def scan_decls(text, names):
    out = []
    for m in DECL_RE.finditer(text):
        if m.group("n") in names:
            end = m.end()
            if end < len(text) and text[end] == "\n": end += 1
            out.append((m.start(), end, m.group("q"), m.group("n"), m.group("d").strip()))
    return out

def game_files():
    out = []
    for d in ("src", "rels"):
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for fn in fns:
                if fn.endswith(".c"):
                    out.append(os.path.relpath(os.path.join(dp, fn), ROOT))
    return sorted(out)

def read(path):
    with open(os.path.join(ROOT, path), "rb") as f:
        return f.read().decode("latin-1")   # byte-exact round trip

def write(path, text):
    with open(os.path.join(ROOT, path), "wb") as f:
        f.write(text.encode("latin-1"))

def family(name):
    return re.sub(r"\d+", "N", name)

def already_guarded(text, pos):
    """True if the definition at pos is the #else branch of one of our blocks."""
    before = text[max(0, pos - 12):pos]
    return before.endswith("#else\n")

# ---------------------------------------------------------------------------------------------
# type mapping

class Rule:
    pass

def load_rules():
    rules = []
    for ln in open(TYPES_FILE, encoding="utf-8"):
        ln = ln.split("#", 1)[0].strip()
        if not ln: continue
        parts = ln.split()
        r = Rule()
        r.name_re = re.compile(parts[0])
        r.path_re = re.compile(parts[1]) if parts[1] != "*" else None
        r.type = " ".join(parts[2:-1])
        r.header = parts[-1]
        r.mode = "array"
        rules.append(r)
    return rules

def rule_for(rules, path, name):
    for r in rules:
        if r.name_re.fullmatch(name) and (r.path_re is None or r.path_re.search(path)):
            return r
    return None

# ---------------------------------------------------------------------------------------------
# layouts via clang's PowerPC target

PRIM = {"char": (1, "u"), "signed char": (1, "s"), "unsigned char": (1, "u"), "_Bool": (1, "u"),
        "short": (2, "s"), "unsigned short": (2, "u"), "int": (4, "s"), "unsigned int": (4, "u"),
        "long": (4, "s"), "unsigned long": (4, "u"), "long long": (8, "s"),
        "unsigned long long": (8, "u"), "float": (4, "f"), "double": (8, "f")}

WORD_TYPES = ("EvtWord", "intptr_t", "uintptr_t")

def strip_cv(t):
    return re.sub(r"\b(const|volatile)\s+", "", t).strip()

class Field:
    def __init__(self, name, ctype, off, depth, bits=None):
        self.name, self.ctype, self.off, self.depth, self.bits = name, ctype, off, depth, bits
        self.children = []
        self.kind = None

def parse_layouts(dump):
    recs = {}
    for block in dump.split("*** Dumping AST Record Layout")[1:]:
        lines = [l for l in block.splitlines() if "|" in l]
        if not lines: continue
        stack, root = [], None
        for l in lines:
            left, right = l.split("|", 1)
            left = left.strip()
            if right.lstrip().startswith("[sizeof="):
                m = re.search(r"sizeof=(\d+)", right); root.size = int(m.group(1))
                m = re.search(r"align=(\d+)", right); root.align = int(m.group(1))
                break
            depth = (len(right) - len(right.lstrip(" "))) // 2
            decl = right.strip()
            bits = None
            if ":" in left:
                o, b = left.split(":"); lo, hi = b.split("-")
                off, bits = int(o), (int(lo), int(hi))
            else:
                off = int(left)
            if depth == 0:
                root = Field("", decl, off, 0); root.kind = "union" if decl.startswith("union ") else "struct"
                stack = [root]; continue
            # "type name" -- the name is the last identifier unless anonymous
            m = re.match(r"(.*?)\s*(\b\w+)?$", decl)
            ctype, name = decl, ""
            m2 = re.match(r"^(.*[\s\*\]\)])(\w+)$", decl)
            if m2 and not decl.endswith("*"):
                ctype, name = m2.group(1).strip(), m2.group(2)
                if name in ("int", "char", "short", "long", "float", "double") or \
                        ctype in ("struct", "union", "enum", "const"):
                    ctype, name = decl, ""   # unnamed member
            f = Field(name, ctype, off, depth, bits)
            while len(stack) > depth: stack.pop()
            stack[-1].children.append(f)
            stack.append(f)
        if root is not None and hasattr(root, "size"):
            recs[root.ctype] = root
            nm = re.sub(r"^(struct|union)\s+", "", root.ctype)
            recs.setdefault(nm, root)
    return recs

def get_layouts(types, headers):
    """types: list of type names; returns {type: root Field} (32-bit, TARGET_PC headers)."""
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "layout_probe.c")
    with open(src, "w") as f:
        for h in sorted(set(headers)): f.write(f'#include "{h}"\n')
        for i, t in enumerate(types): f.write(f"{t} __r2s_probe_{i};\n")
    dumps = []
    for opt in ("-fdump-record-layouts-canonical", "-fdump-record-layouts"):
        r = subprocess.run(["clang", *PPC, *PC32, *INC, "-S", "-emit-llvm", "-o", "/dev/null",
                            "-Xclang", opt, src], cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            sys.exit("layout probe failed:\n" + r.stderr[-3000:])
        dumps.append(r.stdout)
    recs = parse_layouts(dumps[0])
    sugared = parse_layouts(dumps[1])
    # attach the sugared (typedef) spelling of each field: EvtWord/intptr_t fields take pointers
    def attach(a, b):
        a.sugar = b.ctype
        for x, y in zip(a.children, b.children): attach(x, y)
    for k, rec in recs.items():
        sk = k
        if sk not in sugared:
            continue
        attach(rec, sugared[sk])
    # sizeof each probed type (typedef names map to their record)
    out = {}
    for t in types:
        # resolve typedef -> record name via a second tiny probe
        out[t] = resolve_record(t, recs, headers)
    return out, recs

_typedef_cache = {}
def resolve_record(t, recs, headers):
    if t in recs: return recs[t]
    if t in _typedef_cache: return recs[_typedef_cache[t]]
    # ask clang for the canonical type of t
    src = os.path.join(WORK, "td_probe.c")
    with open(src, "w") as f:
        for h in sorted(set(headers)): f.write(f'#include "{h}"\n')
        f.write(f"{t} __r2s_x;\n")
    r = subprocess.run(["clang", *PPC, *PC32, *INC, "-fsyntax-only", "-Xclang", "-ast-dump",
                        "-Xclang", "-ast-dump-filter=__r2s_x", src], cwd=ROOT, capture_output=True, text=True)
    m = re.search(r"__r2s_x '[^']*'(?::'([^']*)')?", r.stdout)
    canon = m.group(1) if m and m.group(1) else t
    _typedef_cache[t] = canon
    return recs[canon]

def flatten(root, recs):
    """Leaves of a record: (off, size, kind, path, ctype, bits). Unions -> ('union', members)."""
    out = []
    def elem_info(ctype):
        t = strip_cv(ctype)
        if "*" in t or "(" in t: return 4, "p", None
        if t.startswith("enum "): return 4, "s", None
        if t in PRIM: return PRIM[t][0], PRIM[t][1], None
        rec = recs.get(t) or recs.get(re.sub(r"^(struct|union)\s+", "", t))
        if rec is None: raise KeyError(f"no layout for {ctype}")
        return rec.size, "r", rec
    def walk(fields, base, prefix, out):
        for f in fields:
            path = prefix + ("." + f.name if f.name else "")
            t = strip_cv(f.ctype)
            am = re.match(r"^(.*?)((?:\[\d+\])+)$", t)
            if am and "(" not in t:
                et = am.group(1).strip(); dims = [int(x) for x in re.findall(r"\[(\d+)\]", am.group(2))]
                es, ek, erec = elem_info(et)
                esug = re.sub(r"(?:\[\d+\])+$", "", strip_cv(getattr(f, "sugar", ""))).strip()
                idxs = [[]]
                for d in dims: idxs = [i + [k] for i in idxs for k in range(d)]
                for n, ix in enumerate(idxs):
                    p = path + "".join(f"[{k}]" for k in ix)
                    o = base + f.off + n * es
                    if ek == "r": walk_rec(erec, o, p, out)
                    elif ek == "s" and es == 4 and esug in WORD_TYPES:
                        out.append((o, es, "w", p, esug, None))   # EvtWord name[N]
                    else: out.append((o, es, ek, p, et, None))
                continue
            if f.children:
                if f.ctype.startswith("union"):
                    members = []
                    for c in f.children:
                        sub = []; walk([c], base, path, sub); members.append(sub)
                    out.append(("union", members)); continue
                walk(f.children, base, path, out); continue
            es, ek, erec = elem_info(t)
            if ek == "r": walk_rec(erec, base + f.off, path, out); continue
            sug = strip_cv(getattr(f, "sugar", ""))
            if ek == "s" and es == 4 and sug in WORD_TYPES:
                out.append((base + f.off, es, "w", path, sug, f.bits)); continue
            out.append((base + f.off, es, ek, path, t, f.bits))
    def walk_rec(rec, base, prefix, out):
        if rec.kind == "union":
            members = []
            for c in rec.children:
                sub = []; walk([c], base, prefix, sub); members.append(sub)
            out.append(("union", members))
        else:
            walk(rec.children, base, prefix, out)
    walk_rec(root, 0, "", out)
    return out

# ---------------------------------------------------------------------------------------------
# value conversion

CAST_RE = re.compile(r"^\(\s*s32\s*\)\s*")
COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)

def int_literal(expr):
    e = COMMENT_RE.sub("", expr).strip()
    try:
        if re.fullmatch(r"[-+]?\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*", e):
            v = int(re.sub(r"[uUlL]+$", "", e.replace(" ", "")), 0)
            return v & 0xFFFFFFFF
    except ValueError:
        pass
    return None

def float_literal(bits):
    b = struct.pack(">I", bits)
    v = struct.unpack(">f", b)[0]
    if v != v or v in (float("inf"), float("-inf")): return None
    for p in range(1, 18):
        s = "%.*g" % (p, v)
        if struct.pack(">f", float(s)) == b:
            if "e" not in s and "." not in s: s += ".0"
            return s + "f"
    return None

class ConvError(Exception):
    pass

def is_ptr_expr(expr):
    return int_literal(expr) is None

def scalar_record(typ):
    root = Field("", "struct __scalar", 0, 0); root.kind = "struct"; root.size = 4
    root.children = [Field("", typ, 0, 1)]
    return root

def conv_table(d, rec, recs, rule):
    size = rec.size
    nbytes = len(d.words) * 4
    if nbytes % size: raise ConvError(f"{nbytes} bytes not a multiple of sizeof({rule.type})={size}")
    count = nbytes // size
    if rule.mode == "one" and count != 1: raise ConvError(f"mode one but {count} elements")
    leaves = flatten(rec, recs)
    words = d.words
    lits = [int_literal(w) for w in words]
    def byte_at(o):
        w = o // 4
        if lits[w] is None: raise ConvError(f"word {w} ({words[w]!r}) is symbolic but split into bytes")
        return (lits[w] >> (8 * (3 - o % 4))) & 0xFF
    elems = []
    for i in range(count):
        base = i * size
        covered = set()
        inits = []
        def do_leaf(leaf, inits, covered, dry=False):
            off, sz, kind, path, ctype, bits = leaf
            o = base + off
            if bits is not None:
                lo, hi = bits
                # storage unit of the bitfield = the declared type
                ssz = PRIM.get(ctype, (4,))[0]
                so = o  # clang reports the byte of the storage unit
                v = 0
                for k in range(ssz): v = (v << 8) | byte_at(so + k)
                width = hi - lo + 1
                val = (v >> (ssz * 8 - 1 - hi)) & ((1 << width) - 1)
                for k in range(lo, hi + 1): covered.add((so * 8 + k))
                if val: inits.append((path, str(val)))
                return
            for k in range(sz): covered.update(range((o + k) * 8, (o + k) * 8 + 8))
            if sz == 4 and o % 4 == 0:
                expr = words[o // 4]
                lit = lits[o // 4]
                if kind == "p":
                    if lit == 0: return
                    if lit is not None:
                        inits.append((path, f"({ctype})0x{lit:X}")); return
                    inits.append((path, CAST_RE.sub("", expr, count=1)))
                    return
                if kind == "f":
                    if lit is None:
                        inits.append((path, expr)); return
                    if lit == 0: return
                    fl = float_literal(lit)
                    if fl is None: raise ConvError(f"bad float bits {lit:#x} for {path}")
                    inits.append((path, fl)); return
                if kind == "w":
                    if lit is None:
                        inits.append((path, f"({ctype})" + CAST_RE.sub("", expr, count=1))); return
                    if lit == 0: return
                    inits.append((path, expr.strip())); return
                # integer field
                if lit is None:
                    raise ConvError(f"pointer value {expr!r} in integer field {path} ({ctype})")
                if lit == 0: return
                inits.append((path, expr.strip())); return
            if sz == 8 and o % 4 == 0 and kind in ("s", "u", "f"):
                hi_, lo_ = lits[o // 4], lits[o // 4 + 1]
                if hi_ is None or lo_ is None: raise ConvError(f"symbolic 8-byte field {path}")
                v = (hi_ << 32) | lo_
                if v == 0: return
                if kind == "f":
                    inits.append((path, repr(struct.unpack(">d", struct.pack(">Q", v))[0])))
                else:
                    if kind == "s" and v >= 1 << 63: v -= 1 << 64
                    inits.append((path, f"{v}LL" if kind == "s" else f"{v}ULL"))
                return
            if kind == "p": raise ConvError(f"misaligned pointer field {path}")
            v = 0
            for k in range(sz): v = (v << 8) | byte_at(o + k)
            if v == 0: return
            if kind == "f": raise ConvError(f"odd float field {path}")
            if kind == "s" and v >= 1 << (8 * sz - 1): v -= 1 << (8 * sz)
            inits.append((path, str(v)))
        for leaf in leaves:
            if leaf[0] == "union":
                members = leaf[1]
                # pick the first member whose conversion succeeds and that covers
                # all nonzero bytes; prefer pointer members for symbolic words
                chosen = None
                for mem in sorted(members, key=lambda m: 0 if any(l[2] == "p" for l in m if l[0] != "union") else 1):
                    try:
                        ii, cc = [], set()
                        for l in mem: do_leaf(l, ii, cc)
                        chosen = (ii, cc)
                        # must not lose symbolic words
                        break
                    except ConvError:
                        continue
                if chosen is None: raise ConvError("no union member fits")
                ii, cc = chosen
                inits.extend(ii); covered |= cc
                continue
            do_leaf(leaf, inits, covered)
        # padding check: every bit not covered must be zero
        for o in range(base, base + size):
            for b in range(8):
                if (o * 8 + b) in covered: continue
                w = o // 4
                if lits[w] is None: raise ConvError(f"symbolic word {w} lands in padding")
                if (byte_at(o) >> (7 - b)) & 1: raise ConvError(f"nonzero padding at byte {o - base} of element {i}")
        elems.append(inits)
    return count, elems

def render(qual, typ, name, count, elems, mode):
    scalar = "*" in typ
    def one(inits):
        if len(inits) == 1 and inits[0][0] == "": return inits[0][1]
        if not inits: return "0" if scalar else "{ 0 }"
        return "{ " + ", ".join(f"{p} = {v}" for p, v in inits) + " }"
    if mode == "one":
        return f"{qual}{typ} {name} = {one(elems[0])};\n"
    body = "".join(f"    {one(e)},\n" for e in elems)
    return f"{qual}{typ} {name}[] = {{\n{body}}};\n"

def render_decl(qual, typ, name, dim, count, mode):
    if mode == "one": return f"{qual}{typ} {name};\n"
    return f"{qual}{typ} {name}[{count if dim else ''}];\n"

def guard(new, old):
    if not old.endswith("\n"): old += "\n"
    return f"#ifdef TARGET_PC\n{new}#else\n{old}#endif\n"

# ---------------------------------------------------------------------------------------------

SKIPPED = []

def collect(rules, filt=None):
    """{path: [(def, rule)]} for raw definitions not yet converted."""
    out = collections.OrderedDict()
    for p in game_files():
        if filt and filt not in p: continue
        t = read(p)
        if "s32" not in t: continue
        defs = scan_defs(t)
        # a name with a definition we could not parse (e.g. split by #if lines) is left alone:
        # converting only some of its definitions/declarations would make them disagree
        parsed = collections.Counter(d.name for d in defs)
        allm = collections.Counter(m.group("n") for m in DEF_RE.finditer(t))
        for d in defs:
            if already_guarded(t, d.start): continue
            if allm[d.name] != parsed[d.name]:
                SKIPPED.append((p, d.name, "another definition is split by preprocessor lines")); continue
            out.setdefault(p, []).append((d, rule_for(rules, p, d.name)))
    return out

def cmd_list(a):
    rules = load_rules()
    fams = collections.defaultdict(list)
    for p, lst in collect(rules, a.filter).items():
        for d, r in lst:
            hasptr = any(int_literal(w) is None for w in d.words)
            if not a.all and not hasptr: continue
            fams[family(d.name)].append((p, d.name, len(d.words), r.type if r else None))
    rows = sorted(fams.items(), key=lambda kv: -len(kv[1]))
    for fam, lst in rows:
        ty = {x[3] for x in lst}
        sizes = sorted({x[2] for x in lst})
        print(f"{len(lst):5d} {fam:40s} {','.join(str(t) for t in ty):20s} words={sizes[:6]} e.g. {lst[0][0]}")
    print(sum(len(v) for v in fams.values()), "tables")

def cmd_convert(a):
    rules = load_rules()
    todo = collect(rules, a.filter)
    types = sorted({r.type for lst in todo.values() for _, r in lst if r and "*" not in r.type})
    headers = sorted({r.header for lst in todo.values() for _, r in lst if r and r.header != "-"})
    layouts, recs = get_layouts(types, headers) if types else ({}, {})
    report = {"converted": [], "failed": [], "unmapped": []}
    renames = {}   # name -> (type, count, mode, header, static, path)
    for p, lst in todo.items():
        text = read(p)
        edits, hdrs = [], set()
        for d, r in lst:
            if r is None:
                if any(int_literal(w) is None for w in d.words):
                    report["unmapped"].append((p, d.name, len(d.words)))
                continue
            try:
                lay = scalar_record(r.type) if "*" in r.type else layouts[r.type]
                count, elems = conv_table(d, lay, recs, r)
            except (ConvError, ValueError, KeyError) as e:
                report["failed"].append((p, d.name, r.type, str(e))); continue
            new = render(d.qual, r.type, d.name, count, elems, r.mode)
            edits.append((d.start, d.end, guard(new, d.text)))
            hdrs.add(r.header)
            report["converted"].append((p, d.name, r.type, count))
            renames[(p, d.name)] = (r.type, count, r.mode, r.header, "static" in d.qual)
        if not edits: continue
        # forward declarations in the same file
        names = {n for (pp, n) in renames if pp == p}
        for s, e, q, n, dim in scan_decls(text, names):
            if already_guarded(text, s): continue
            ty, cnt, mode, hdr, _ = renames[(p, n)]
            edits.append((s, e, guard(render_decl(q, ty, n, dim, cnt, mode), text[s:e])))
        edits.sort(reverse=True)
        for s, e, rep in edits:
            text = text[:s] + rep + text[e:]
        text = add_includes(text, hdrs)
        if not a.n: write(p, text)
    # externs in other files for non-static tables
    glob = {n: v for (pp, n), v in renames.items() if not v[4]}
    if glob:
        for p in game_files():
            t = read(p)
            hits = [x for x in scan_decls(t, set(glob)) if "extern" in x[2] and not already_guarded(t, x[0])]
            if not hits: continue
            hdrs = set()
            for s, e, q, n, dim in sorted(hits, reverse=True):
                ty, cnt, mode, hdr, _ = glob[n]
                t = t[:s] + guard(render_decl(q, ty, n, dim, cnt, mode), t[s:e]) + t[e:]
                hdrs.add(hdr)
            t = add_includes(t, hdrs)
            if not a.n: write(p, t)
    report["skipped"] = SKIPPED
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(WORK, "report.json"), "w") as f: json.dump(report, f, indent=1)
    print(f"converted {len(report['converted'])}, failed {len(report['failed'])}, "
          f"unmapped(with pointers) {len(report['unmapped'])}, skipped {len(SKIPPED)}")
    fc = collections.Counter(re.sub(r"\d+", "N", re.sub(r"0x[0-9a-f]+|\d+", "N", f[3]))[:90] for f in report["failed"])
    for k, n in fc.most_common(30): print(f"  {n:5d} {k}")

INC_LINE_RE = re.compile(r'^#include [<"][^>"]+[>"][^\n]*\n', re.M)

def add_includes(text, hdrs):
    need = [h for h in sorted(hdrs) if h != "-" and f'#include "{h}"' not in text]
    if not need: return text
    ms = list(INC_LINE_RE.finditer(text))
    pos = ms[-1].end() if ms else 0
    blk = "#ifdef TARGET_PC\n" + "".join(f'#include "{h}"\n' for h in need) + "#endif\n"
    return text[:pos] + blk + text[pos:]

# ---------------------------------------------------------------------------------------------
# verification

def elf_symbols(path):
    data = open(path, "rb").read()
    assert data[:4] == b"\x7fELF" and data[4] == 1 and data[5] == 2
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    secs = []
    for i in range(shnum):
        nm, ty, fl, addr, off, sz, link, info, al, es = struct.unpack_from(">10I", data, shoff + i * shentsize)
        secs.append(dict(nm=nm, ty=ty, off=off, sz=sz, link=link, info=info, es=es))
    shs = secs[shstrndx]
    def sname(o, tab):
        s = tab["off"] + o; e = data.index(b"\0", s); return data[s:e].decode()
    for s in secs: s["name"] = sname(s["nm"], shs)
    symtab = next(s for s in secs if s["ty"] == 2)
    strtab = secs[symtab["link"]]
    syms = []
    for i in range(symtab["sz"] // 16):
        nm, val, sz, info, oth, shn = struct.unpack_from(">IIIBBH", data, symtab["off"] + i * 16)
        syms.append(dict(name=sname(nm, strtab), val=val, sz=sz, type=info & 15, shn=shn))
    relocs = collections.defaultdict(list)   # target section index -> [(off, type, sym, addend)]
    for s in secs:
        if s["ty"] == 4:
            for i in range(s["sz"] // 12):
                o, info, add = struct.unpack_from(">IIi", data, s["off"] + i * 12)
                relocs[s["info"]].append((o, info & 0xFF, info >> 8, add))
    def content(shn): s = secs[shn]; return data[s["off"]:s["off"] + s["sz"]] if s["ty"] != 8 else b"\0" * s["sz"]
    return secs, syms, relocs, content

def describe_target(secs, syms, content, symi, add):
    s = syms[symi]
    if s["type"] == 3 or (s["name"] == "" and s["shn"] not in (0,)) or s["name"].startswith(".L"):
        # section symbol / local label: describe by the bytes it points at (string literals)
        shn = s["shn"]; c = content(shn); o = s["val"] + add
        if secs[shn]["name"].startswith(".rodata.str") or ".str" in secs[shn]["name"]:
            e = c.index(b"\0", o); return ("str", c[o:e])
        return ("sec", secs[shn]["name"], o)
    return ("sym", s["name"], add)

def obj_tables(path, names):
    secs, syms, relocs, content = elf_symbols(path)
    out = {}
    for s in syms:
        if s["name"] in names and s["type"] == 1 and 0 < s["shn"] < 0xFF00:
            c = content(s["shn"])[s["val"]:s["val"] + s["sz"]]
            rl = sorted((o - s["val"], t, describe_target(secs, syms, content, si, a))
                        for (o, t, si, a) in relocs.get(s["shn"], []) if s["val"] <= o < s["val"] + s["sz"])
            out.setdefault(s["name"], []).append((s["sz"], c, rl))
    return out

def compile_obj(src_path, out, pc, cwd=ROOT, srcname=None, extra=()):
    flags = [*PPC, *(PC32 if pc else []), *extra, *inc_for(srcname or src_path), "-fdata-sections", "-fno-common",
             "-c", "-o", out, src_path]
    r = subprocess.run(["clang", *flags], cwd=cwd, capture_output=True, text=True)
    return r.returncode == 0, r.stderr

def preprocess(path_or_dash, srcname, stdin=None):
    r = subprocess.run(["clang", "-E", "-P", *PPC, *inc_for(srcname), "-x", "c", path_or_dash],
                       cwd=ROOT, capture_output=True, input=stdin)
    return r.returncode, r.stdout

def base_text(a, p):
    if a.base_dir:
        f = os.path.join(a.base_dir, p)
        return open(f, "rb").read() if os.path.exists(f) else None
    r = subprocess.run(["git", "show", f"{a.base}:{p}"], cwd=ROOT, capture_output=True)
    return r.stdout if r.returncode == 0 else None

GUARD_NAME_RE = re.compile(r"#ifdef TARGET_PC\n(?:static\s+|extern\s+|const\s+|__declspec\(section\s*\"[^\"]*\"\)\s*)*"
                           r"((?:const\s+)?\w+\s*\**)\s*(\w+)\s*(?:\[[^\]]*\])?\s*=\s*\{", re.M)

def _rule_headers():
    try: return sorted({r.header for r in load_rules() if r.header != "-"} | OLD_HEADERS)
    except OSError: return sorted(OLD_HEADERS)
# headers earlier versions of the type map used (so `strip` still recognises their blocks)
OLD_HEADERS = {"evt/evt_bero.h", "evt/evt_npc.h", "evt/evt_door.h", "dolphin/types.h"}
BLOCK_INC_RE = re.compile(r'#ifdef TARGET_PC\n(?:#include "(?:' + "|".join(re.escape(h) for h in _rule_headers())
                          + r')"\n)+#endif\n')
BLOCK_RE = re.compile(r"#ifdef TARGET_PC\n([^#]*?)#else\n([^#]*?\bs32\s+\w+\s*\[[^#]*?)#endif\n")

def strip_blocks(text):
    """The file as it was before conversion: drop our include blocks, keep the #else branches."""
    return BLOCK_RE.sub(lambda m: m.group(2), BLOCK_INC_RE.sub("", text))

def verify_one(a, p):
    res = {"path": p, "pp": None, "tables": {}, "note": ""}
    cur = read(p).encode("latin-1")
    if "#ifdef TARGET_PC" not in read(p): return None
    base = strip_blocks(read(p)).encode("latin-1")
    if base == cur:
        return None
    head = base_text(a, p)
    res["head_same"] = head == base   # untouched by anything but this tool
    # 1. MWCC sees the same text
    tmpd = os.path.join(WORK, "v", p.replace("/", "__"))
    os.makedirs(tmpd, exist_ok=True)
    bsrc = os.path.join(tmpd, "base.c")
    open(bsrc, "wb").write(base)
    # compile base from a copy placed next to the original so relative includes resolve
    shadow = bsrc
    try:
        r1, o1 = preprocess(shadow, p)
        r2, o2 = preprocess(p, p)
        # -P still emits blank lines for line gaps, so inserted (untaken) #ifdef lines can
        # shift blank lines; compare with blank lines dropped (same tokens, same order).
        nb = lambda o: b"\n".join(l for l in o.split(b"\n") if l.strip())
        res["pp"] = (r1 == 0 and r2 == 0 and nb(o1) == nb(o2))
        res["pp_exact"] = o1 == o2
        if r1 or r2: res["note"] += " preprocess-error"
        names = set(m.group(2) for m in GUARD_NAME_RE.finditer(cur.decode("latin-1")))
        if not names: return res
        ob, oc = os.path.join(tmpd, "base.o"), os.path.join(tmpd, "cur.o")
        # keep otherwise-unreferenced static tables alive
        keep = "\nvoid* __r2s_keep[] = {" + ", ".join(f"(void*){n}" for n in sorted(names)) + "};\n"
        csrc = os.path.join(tmpd, "cur.c")
        open(shadow, "wb").write(base + keep.encode())
        open(csrc, "wb").write(cur + keep.encode())
        for extra in ([], ["-Dstatic="], ["-DNONMATCHING"], ["-DNONMATCHING", "-Dstatic="]):   # clang rejects some linkage mixes MWCC accepts
            ok1, e1 = compile_obj(shadow, ob, False, srcname=p, extra=extra)
            ok2, e2 = compile_obj(csrc, oc, True, srcname=p, extra=extra)
            if ok1 and ok2: break
    finally:
        pass
    if not (ok1 and ok2):
        res["note"] += " compile-failed: " + ("base " if not ok1 else "") + ("pc " if not ok2 else "")
        res["err"] = ((e1 if not ok1 else "") + (e2 if not ok2 else ""))[-1500:]
        for n in names: res["tables"][n] = "uncompiled"
        return res
    tb, tc = obj_tables(ob, names), obj_tables(oc, names)
    for n in names:
        if n not in tb or n not in tc:
            res["tables"][n] = "missing symbol"; continue
        if len(tb[n]) != len(tc[n]):
            res["tables"][n] = "symbol count differs"; continue
        bad = []
        for (sb, cb, rb), (sc, cc, rc) in zip(tb[n], tc[n]):
            if sb != sc: bad.append(f"size {sb} vs {sc}")
            elif cb != cc:
                k = next(i for i in range(sb) if cb[i] != cc[i]); bad.append(f"byte {k}: {cb[k]:#x} vs {cc[k]:#x}")
            elif rb != rc:
                diff = [x for x in zip(rb, rc) if x[0] != x[1]][:2]
                bad.append(f"relocs differ ({len(rb)} vs {len(rc)}) {diff}")
        res["tables"][n] = "ok" if not bad else "; ".join(bad)
    return res

def cmd_strip(a):
    """Undo a conversion (restores the original definitions, drops our include blocks)."""
    n = 0
    for f in (a.files or game_files()):
        if a.filter and a.filter not in f: continue
        t = read(f); u = strip_blocks(t)
        if u != t:
            write(f, u); n += 1
    print(f"restored {n} files")

def cmd_verify(a):
    if a.files:
        files = a.files
    elif True:
        files = [f for f in game_files() if BLOCK_RE.search(read(f))]
    else:
        r = subprocess.run(["git", "diff", "--name-only", a.base if not a.base_dir else "HEAD", "--", "src", "rels"],
                           cwd=ROOT, capture_output=True, text=True)
        files = [f for f in r.stdout.split() if f.endswith(".c")]
        if a.base_dir:
            files = [f for f in game_files() if os.path.exists(os.path.join(a.base_dir, f))
                     and open(os.path.join(a.base_dir, f), "rb").read() != read(f).encode("latin-1")]
    if a.filter: files = [f for f in files if a.filter in f]
    stats = collections.Counter()
    bad = []
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        for res in ex.map(lambda p: verify_one(a, p), files):
            if res is None: continue
            stats["files"] += 1
            stats["pp_same" if res["pp"] else "pp_DIFF"] += 1
            stats["pp_exact" if res.get("pp_exact") else "pp_blanklines_only"] += res["pp"] is True
            stats["files_base_eq_HEAD"] += bool(res.get("head_same"))
            if not res["pp"]: bad.append(f"{res['path']}: preprocessed text differs {res['note']}")
            for n, st in res["tables"].items():
                stats["tbl_" + ("ok" if st == "ok" else st.split(":")[0].split(" (")[0])] += 1
                if st != "ok": bad.append(f"{res['path']}: {n}: {st}")
            if res.get("err") and a.v: bad.append(res["err"])
    for b in bad: print(b)
    for k, v in sorted(stats.items()): print(f"{k}: {v}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["list", "convert", "verify", "strip"])
    ap.add_argument("--filter")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("-n", action="store_true")
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--base", default="HEAD")
    ap.add_argument("--base-dir", help="directory holding the original tree (instead of git)")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    {"list": cmd_list, "convert": cmd_convert, "verify": cmd_verify, "strip": cmd_strip}[a.mode](a)

if __name__ == "__main__":
    main()
