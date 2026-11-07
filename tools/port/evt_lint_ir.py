#!/usr/bin/env python3
"""The compiled side of evt_lint.py: every global of a translation unit as
clang's front end emits it (LLVM IR text, -emit-llvm, no optimisation), with
its initializer decoded into Python values.

A script (EvtWord name[] = {...}) comes out as [N x i64] of integers and
pointer references; a table (BeroEntry, NpcSetupInfo, ...) as structs of
fields. Values:
  int                         a number
  Ref(name, offset)           a pointer to global/function `name` (+ byte offset)
  Str(bytes)                  a string literal's bytes (the global's own value)
  list                        an array or struct's fields (struct padding kept)
  None                        null / zeroinitializer element / undef
  float                       a float constant
"""

import re
import struct


class Ref:
    __slots__ = ("name", "off")

    def __init__(self, name, off=0):
        self.name, self.off = name, off

    def __repr__(self):
        return "Ref(%s%s)" % (self.name, "+%d" % self.off if self.off else "")


class Str(bytes):
    pass


class Zero:
    """zeroinitializer of a type: expands to zeros on demand."""
    __slots__ = ("ty",)

    def __init__(self, ty):
        self.ty = ty


TOKEN_RE = re.compile(r'''
    (?P<ws>\s+)
  | (?P<cstr>c"(?:[^"\\]|\\[0-9A-Fa-f]{2}|\\\\)*")
  | (?P<gname>@"[^"]*"|@[-\w.$]+)
  | (?P<lname>%"[^"]*"|%[-\w.$]+)
  | (?P<hex>0x[0-9A-Fa-f]+|0xK[0-9A-Fa-f]+)
  | (?P<num>-?\d+(?:\.\d+(?:e[-+]?\d+)?)?)
  | (?P<word>[A-Za-z_][\w.]*)
  | (?P<punct>\.\.\.|[\[\]{}()<>,=*!:#|])
''', re.X)


def tokenize(s):
    out = []
    for m in TOKEN_RE.finditer(s):
        k = m.lastgroup
        if k == "ws":
            continue
        out.append((k, m.group(0)))
    return out


def unq(name):
    name = name[1:]
    if name.startswith('"'):
        name = name[1:-1]
    return name


def cstr_bytes(tok):
    s = tok[2:-1]
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\":
            if s[i + 1] == "\\":
                out.append(0x5C)
                i += 2
            else:
                out.append(int(s[i + 1:i + 3], 16))
                i += 3
        else:
            out += c.encode("latin-1")
            i += 1
    return bytes(out)


class Parser:
    def __init__(self, toks, types):
        self.t = toks
        self.i = 0
        self.types = types

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else (None, None)

    def next(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def expect(self, v):
        tok = self.next()
        if tok[1] != v:
            raise ValueError("expected %r got %r at %d" % (v, tok, self.i))

    # types: returned as nested tuples: ('i', bits) ('ptr',) ('f', bits) ('arr', n, T) ('st', [T], packed) ('named', name)
    def parse_type(self):
        k, v = self.next()
        if k == "word":
            if v.startswith("i") and v[1:].isdigit():
                return ("i", int(v[1:]))
            if v == "ptr":
                return ("ptr",)
            if v in ("float",):
                return ("f", 32)
            if v in ("double",):
                return ("f", 64)
            if v == "void":
                return ("void",)
            if v in ("x86_fp80",):
                return ("f", 80)
            raise ValueError("type " + v)
        if v == "[":
            n = int(self.next()[1])
            self.expect("x")
            el = self.parse_type()
            self.expect("]")
            return ("arr", n, el)
        if v == "{":
            fields = []
            if self.peek()[1] != "}":
                while True:
                    fields.append(self.parse_type())
                    if self.peek()[1] == ",":
                        self.next()
                        continue
                    break
            self.expect("}")
            return ("st", tuple(fields), False)
        if v == "<":
            self.expect("{")
            fields = []
            if self.peek()[1] != "}":
                while True:
                    fields.append(self.parse_type())
                    if self.peek()[1] == ",":
                        self.next()
                        continue
                    break
            self.expect("}")
            self.expect(">")
            return ("st", tuple(fields), True)
        if k == "lname":
            return ("named", unq(v))
        raise ValueError("type token %r" % (v,))

    def parse_value(self, ty):
        k, v = self.next()
        if k == "num":
            if "." in v or "e" in v:
                return float(v)
            return int(v)
        if k == "hex":
            if ty and ty[0] == "f":
                h = v[2:]
                if h.startswith("K"):
                    return 0.0
                return struct.unpack(">d", bytes.fromhex(h.rjust(16, "0")))[0]
            return int(v, 16)
        if k == "cstr":
            return Str(cstr_bytes(v))
        if k == "gname":
            return Ref(unq(v))
        if k == "word":
            if v in ("null", "undef", "poison"):
                return None
            if v == "zeroinitializer":
                return Zero(ty)
            if v == "true":
                return 1
            if v == "false":
                return 0
            if v in ("ptrtoint", "inttoptr", "bitcast", "trunc", "zext", "sext", "addrspacecast"):
                self.expect("(")
                t = self.parse_type()
                val = self.parse_value(t)
                self.expect("to")
                self.parse_type()
                self.expect(")")
                if v == "trunc" and isinstance(val, int):
                    return val  # (caller masks)
                return val
            if v == "getelementptr":
                while self.peek()[1] in ("inbounds", "nuw", "nusw", "inrange"):
                    self.next()
                    if self.peek()[1] == "(" and self.t[self.i - 1][1] == "inrange":
                        self.skip_parens()
                self.expect("(")
                base_ty = self.parse_type()
                self.expect(",")
                pt = self.parse_type()
                base = self.parse_value(pt)
                idxs = []
                while self.peek()[1] == ",":
                    self.next()
                    if self.peek()[1] == "inrange":
                        self.next()
                    it = self.parse_type()
                    idxs.append(self.parse_value(it))
                self.expect(")")
                off = self.gep_offset(base_ty, idxs)
                if isinstance(base, Ref):
                    return Ref(base.name, base.off + off)
                return base
            if v in ("add", "sub", "mul", "and", "or", "xor", "shl", "lshr", "ashr"):
                while self.peek()[1] in ("nuw", "nsw", "exact", "disjoint"):
                    self.next()
                self.expect("(")
                t = self.parse_type()
                a = self.parse_value(t)
                self.expect(",")
                t2 = self.parse_type()
                b = self.parse_value(t2)
                self.expect(")")
                if isinstance(a, int) and isinstance(b, int):
                    return {"add": a + b, "sub": a - b, "mul": a * b, "and": a & b, "or": a | b, "xor": a ^ b,
                            "shl": a << b, "lshr": a >> b, "ashr": a >> b}[v]
                if v in ("add", "sub") and isinstance(a, Ref) and isinstance(b, int):
                    return Ref(a.name, a.off + (b if v == "add" else -b))
                return ("expr", v, a, b)
            if v == "blockaddress" or v == "dso_local_equivalent" or v == "no_cfi":
                if self.peek()[1] == "(":
                    self.skip_parens()
                else:
                    self.next()
                return None
            raise ValueError("value word %r" % v)
        if v == "[":
            el = ty[2] if ty and ty[0] == "arr" else None
            vals = []
            if self.peek()[1] != "]":
                while True:
                    t = self.parse_type()
                    vals.append(self.parse_value(t))
                    if self.peek()[1] == ",":
                        self.next()
                        continue
                    break
            self.expect("]")
            return vals
        if v == "{" or v == "<":
            if v == "<":
                self.expect("{")
            vals = []
            if self.peek()[1] != "}":
                while True:
                    t = self.parse_type()
                    vals.append(self.parse_value(t))
                    if self.peek()[1] == ",":
                        self.next()
                        continue
                    break
            self.expect("}")
            if v == "<":
                self.expect(">")
            return vals
        raise ValueError("value token %r" % (v,))

    def skip_parens(self):
        depth = 0
        while True:
            k, v = self.next()
            if v == "(":
                depth += 1
            elif v == ")":
                depth -= 1
                if depth == 0:
                    return

    def resolve(self, ty):
        while ty[0] == "named":
            ty = self.types.get(ty[1], ("st", (), False))
        return ty

    def size_align(self, ty):
        ty = self.resolve(ty)
        k = ty[0]
        if k == "i":
            b = max(1, (ty[1] + 7) // 8)
            return b, b
        if k == "ptr":
            return 8, 8
        if k == "f":
            return ty[1] // 8, ty[1] // 8
        if k == "arr":
            s, a = self.size_align(ty[2])
            return s * ty[1], a
        if k == "st":
            off, al = 0, 1
            for f in ty[1]:
                s, a = self.size_align(f)
                if ty[2]:
                    a = 1
                off = (off + a - 1) // a * a
                off += s
                al = max(al, a)
            off = (off + al - 1) // al * al
            return off, al
        return 0, 1

    def field_offset(self, ty, idx):
        ty = self.resolve(ty)
        off = 0
        for i, f in enumerate(ty[1]):
            s, a = self.size_align(f)
            if ty[2]:
                a = 1
            off = (off + a - 1) // a * a
            if i == idx:
                return off
            off += s
        return off

    def gep_offset(self, base_ty, idxs):
        if not idxs or not all(isinstance(x, int) for x in idxs):
            return 0
        off = idxs[0] * self.size_align(base_ty)[0]
        ty = base_ty
        for x in idxs[1:]:
            ty = self.resolve(ty)
            if ty[0] == "arr":
                ty = ty[2]
                off += x * self.size_align(ty)[0]
            elif ty[0] == "st":
                off += self.field_offset(ty, x)
                ty = ty[1][x]
            else:
                break
        return off


GLOBAL_RE = re.compile(r'^(@"[^"]*"|@[-\w.$]+) = ((?:\w+ )*?)(global|constant) (.*)$')
TYPE_RE = re.compile(r'^(%"[^"]*"|%[-\w.$]+) = type (.*)$')
DEFINE_RE = re.compile(r'^define [^@]*(@"[^"]*"|@[-\w.$]+)\(')
REF_RE = re.compile(r'@"[^"]*"|@[-\w.$]+')


class Module:
    def __init__(self):
        self.types = {}
        self.globals = {}  # name -> (type, value, linkage words)
        self.func_refs = {}  # defined function -> set of referenced globals
        self.declared = set()

    def size_of(self, ty):
        return Parser([], self.types).size_align(ty)[0]


def parse_module(text):
    mod = Module()
    lines = text.split("\n")
    pending_types = []
    cur_fn = None
    for ln in lines:
        if cur_fn is not None:
            if ln.startswith("}"):
                cur_fn = None
                continue
            if "@" in ln:
                for m in REF_RE.finditer(ln):
                    mod.func_refs[cur_fn].add(unq(m.group(0)))
            continue
        if ln.startswith("%"):
            m = TYPE_RE.match(ln)
            if m:
                pending_types.append((unq(m.group(1)), m.group(2)))
            continue
        if ln.startswith("define "):
            m = DEFINE_RE.match(ln)
            if m:
                cur_fn = unq(m.group(1))
                mod.func_refs[cur_fn] = set()
            continue
        if ln.startswith("declare "):
            m = re.search(r'(@"[^"]*"|@[-\w.$]+)\(', ln)
            if m:
                mod.declared.add(unq(m.group(1)))
            continue
    for name, body in pending_types:
        if body.strip() == "opaque":
            mod.types[name] = ("st", (), False)
            continue
        try:
            mod.types[name] = Parser(tokenize(body), mod.types).parse_type()
        except (ValueError, IndexError):
            mod.types[name] = ("st", (), False)
    for ln in lines:
        if not ln.startswith("@"):
            continue
        m = GLOBAL_RE.match(ln)
        if not m:
            continue
        name = unq(m.group(1))
        linkage = m.group(2).split()
        rest = m.group(4)
        # cut the trailing ", align N", ", section ...", ", comdat"
        toks = tokenize(rest)
        p = Parser(toks, mod.types)
        try:
            ty = p.parse_type()
            if p.peek()[0] is None or p.peek()[1] == ",":
                val = None  # external declaration
                if "external" in linkage or "extern_weak" in linkage:
                    continue
            else:
                val = p.parse_value(ty)
        except (ValueError, IndexError):
            continue
        mod.globals[name] = (ty, val, linkage)
    return mod


def flat_words(mod, ty, val):
    """An [N x i64] (or i32) initializer as a list of ints/Refs (zeros expanded)."""
    p = Parser([], mod.types)
    ty = p.resolve(ty)
    if ty[0] != "arr":
        return None
    n = ty[1]
    if isinstance(val, Zero) or val is None:
        return [0] * n
    if isinstance(val, Str):
        return list(val)
    out = []
    for v in val:
        if isinstance(v, Zero) or v is None:
            out.append(0)
        else:
            out.append(v)
    return out


def struct_fields(mod, ty, val):
    """A struct value's fields as a list, padding byte arrays dropped."""
    p = Parser([], mod.types)
    ty = p.resolve(ty)
    if ty[0] != "st":
        return None
    if isinstance(val, Zero) or val is None:
        val = [None] * len(ty[1])
    out = []
    for fty, v in zip(ty[1], val):
        r = p.resolve(fty)
        if r[0] == "arr" and r[2] == ("i", 8) and (isinstance(v, Zero) or v is None):
            continue
        if isinstance(v, Zero):
            v = 0 if r[0] in ("i", "f") else None
        out.append(v)
    return out


def array_of_structs(mod, ty, val):
    p = Parser([], mod.types)
    ty = p.resolve(ty)
    if ty[0] != "arr":
        return None
    el = ty[2]
    if isinstance(val, Zero) or val is None:
        val = [None] * ty[1]
    return [struct_fields(mod, el, v) for v in val]


def all_refs(val, out):
    if isinstance(val, Ref):
        out.add(val.name)
    elif isinstance(val, list):
        for v in val:
            all_refs(v, out)
    elif isinstance(val, tuple) and val and val[0] == "expr":
        all_refs(val[2], out)
        all_refs(val[3], out)
    return out
