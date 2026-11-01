#!/usr/bin/env python3
"""Generate big-endian decoder descriptors (port/src/common/bedecode_gen_<fmt>.c).

Inputs:
  port/bedecode/<fmt>.spec   which structs a disc format contains and what each
                             pointer field leads to (syntax below)
  clang record layouts       the disc layout (powerpc-unknown-eabi, GameCube
                             headers as MWCC sees them: no TARGET_PC,
                             -enum int, -char unsigned) and the native layout
                             (x86_64, TARGET_PC, Aurora headers, port_prefix.h)

Fields are matched by name. Scalar kinds come from the disc field's canonical C
type; every pointer field must be described in the spec. The generated file
uses offsetof()/sizeof() for native values and _Static_asserts them against the
x86_64 layout, so a host whose layout differs (or a header edit that was not
regenerated) fails the build.

Spec syntax (one directive per line, '#' comments):
  format <name>                 symbol prefix: portBE_<name>_<Struct>
  header <path>                 header to include (relative to include/), repeatable
  struct <Name>                 a struct (named struct tag) the format contains
    <field> -> <Type>           pointer to one <Type>
    <field> -> <Type>[<N>]      pointer to N contiguous <Type>
    <field> -> <Type>[<count>]  count read from sibling field <count> (BE, 1/2/4 bytes)
    <field> -> <Type>[until <f> == <v>]  elements up to and including the one whose <f> == v
    <field> -> <Type>[call <fn>] count from uint32_t fn(PortDecodeCtx*, uint32_t structOff)
    <field> payload if <count>  as payload, but NULL (not checked) when sibling <count> is 0
                                (an array of payloads with a same-length <count> array:
                                element i is gated by <count>[i])
    <field> payload             pointer into the blob (GX data etc.), not decoded
                                (payload/string/null/raw also apply to inline
                                arrays of pointers, e.g. u16* idx[8])
    <field> string              pointer into the blob, NUL-terminated
    <field> null                runtime-only pointer: NULL
    <field> raw                 keep the u32 offset as an integer in the pointer field
    <field> = <value>           native field set to a constant (e.g. unpacked = 1)
    <field> as <kind>           override the scalar kind (u8 s8 u16 s16 u32 s32 f32 ...)
    <field> as <kind>[<N>]      a struct field, or an array of such structs (e.g. the anonymous Vec / GXColor
                                typedefs) decoded as N scalars of <kind>
    <field>[<count>] ...        flexible array member 'T field[]' (or a trailing 'T field[1]'), <count> elements
                                (sibling field, BE 1/2/4 bytes); add a directive
                                for pointer elements ('-> T' = one T each,
                                payload, string, ...); embeds/scalars need none.
                                The native node is allocated with the elements.
    Inline arrays of struct pointers ('T* f[8]' with 'f -> T') point to one T each.
    <field> skip                leave zero
  Embedded structs (by value) need their type declared as a struct too.

Structs can differ between game versions (e.g. GlobalWork in the save file),
so each spec is generated for every version (-DVERSION_<v>). When the outputs
agree the file is written once; otherwise both are written, the US one under
#ifdef VERSION_G8ME01, so one file serves both per-version builds.

Usage: gen_bedecode.py [--check] [fmt ...]   (default: every spec)
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portversion

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SPEC_DIR = os.path.join(ROOT, "port", "bedecode")
OUT_DIR = os.path.join(ROOT, "port", "src", "common")
CLANG = os.environ.get("CLANG", "clang")

# PORT_BEDECODE_DISC lets a header give a typed definition (shared with
# TARGET_PC) of a struct MWCC sees padded; MWCC never defines it.
DISC_ARGS = ["-target", "powerpc-unknown-eabi", "-fdeclspec", "-funsigned-char", "-DPORT_BEDECODE_DISC=1",
             "-DPORT_ENUM_BASE(type)=: type",  # MWCC enumsalwaysint off
             "-Iinclude", "-Ilibs/dolsdk2004/include", "-Ilibs/dolsdk2004/include/libc"]
NATIVE_COMMON = ["-funsigned-char", "-DTARGET_PC=1", "-DTTYD_BACKEND_AURORA=1",
                 "-include", "port/include/port_prefix.h",
                 "-Iport/include", "-Iinclude", "-Iextern/aurora/include"]
# Layouts do not depend on the C library, so on hosts without glibc headers
# (Windows) the probes parse against declaration-only stand-ins instead
# (tools/port/probe_include) plus clang's own freestanding headers.
HOST_LIBC = os.path.exists("/usr/include/math.h")
PROBE_LIBC = [] if HOST_LIBC else ["-nostdlibinc", "-isystem", "tools/port/probe_include"]
NATIVE_COMMON += PROBE_LIBC
NATIVE_ARGS = ["-target", "x86_64-unknown-linux-gnu", *NATIVE_COMMON]
# Other 64-bit hosts must produce the same layout as x86_64 (checked here, so
# the generated _Static_asserts hold everywhere). Windows (LLP64) is probed
# with the host libc headers; only struct layout matters.
# glibc's <stdint.h> types int64_t/intptr_t as 'long' (4 bytes on LLP64), so the
# Windows probe gets MSVC's fixed-width types from a shim and keeps glibc's out.
WIN_STDINT_SHIM = """#define _STDINT_H 1
#define _BITS_STDINT_INTN_H 1
#define _BITS_STDINT_UINTN_H 1
typedef signed char int8_t; typedef short int16_t; typedef int int32_t; typedef long long int64_t;
typedef unsigned char uint8_t; typedef unsigned short uint16_t; typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef int8_t int_least8_t; typedef int16_t int_least16_t; typedef int32_t int_least32_t;
typedef int64_t int_least64_t; typedef uint8_t uint_least8_t; typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t; typedef uint64_t uint_least64_t;
typedef int8_t int_fast8_t; typedef int32_t int_fast16_t; typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t; typedef uint8_t uint_fast8_t; typedef uint32_t uint_fast16_t;
typedef uint32_t uint_fast32_t; typedef uint64_t uint_fast64_t;
typedef long long intptr_t; typedef unsigned long long uintptr_t;
typedef long long intmax_t; typedef unsigned long long uintmax_t;
#define INT8_MAX 127
#define INT16_MAX 32767
#define INT32_MAX 2147483647
#define INT64_MAX 9223372036854775807LL
#define INT8_MIN (-128)
#define INT16_MIN (-32768)
#define INT32_MIN (-2147483647 - 1)
#define INT64_MIN (-INT64_MAX - 1)
#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#define SIZE_MAX UINT64_MAX
"""
WIN_SHIM_ARG = "@WIN_STDINT_SHIM@"
CROSS_64 = {
    "aarch64-linux": ["-target", "aarch64-unknown-linux-gnu", *NATIVE_COMMON],
    # Without glibc, clang's own stdint.h already has the LLP64 fixed-width types.
    "x86_64-windows": ["-target", "x86_64-pc-windows-msvc", "-fms-extensions",
                       *(["-include", WIN_SHIM_ARG] if HOST_LIBC else []), *NATIVE_COMMON,
                       *(["-isystem", "/usr/include", "-isystem", "/usr/include/x86_64-linux-gnu"]
                         if HOST_LIBC else [])],
    # Apple arm64 (macOS, iOS) is its own ABI; always against the stand-ins.
    "arm64-apple": ["-target", "arm64-apple-macos11", *NATIVE_COMMON,
                    *([] if PROBE_LIBC else ["-nostdlibinc", "-isystem", "tools/port/probe_include"])],
}
# 32-bit headless bring-up build (TTYD_PC_32BIT): asserted in an #else branch.
NATIVE_32 = ["-target", "i686-unknown-linux-gnu", *NATIVE_COMMON]

SCALARS = {  # canonical disc C type -> (kind, width)
    "unsigned char": ("U8", 1), "char": ("U8", 1), "signed char": ("S8", 1), "_Bool": ("U8", 1),
    "unsigned short": ("U16", 2), "short": ("S16", 2),
    "unsigned int": ("U32", 4), "int": ("S32", 4),
    "unsigned long": ("U32", 4), "long": ("S32", 4),
    "unsigned long long": ("U64", 8), "long long": ("S64", 8),
    "float": ("F32", 4), "double": ("F64", 8),
}
# Dolphin SDK scalar typedefs as the disc (powerpc) headers define them.
SDK_TYPEDEFS = {"u8": "unsigned char", "s8": "signed char", "u16": "unsigned short", "s16": "short",
                "u32": "unsigned long", "s32": "long", "u64": "unsigned long long",
                "s64": "long long", "f32": "float", "f64": "double", "BOOL": "int"}
KIND_WIDTH = {"U8": 1, "S8": 1, "U16": 2, "S16": 2, "U32": 4, "S32": 4, "U64": 8, "S64": 8,
              "F32": 4, "F64": 8}


class SpecError(Exception):
    pass


# ------------------------------------------------------------------ spec
def parse_spec(path):
    spec = {"format": None, "headers": [], "structs": {}, "order": []}
    cur = None
    for lineno, raw in enumerate(open(path), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        where = f"{os.path.relpath(path, ROOT)}:{lineno}"
        toks = line.split()
        indented = raw[0] in " \t"
        if not indented:
            if toks[0] == "format" and len(toks) == 2:
                spec["format"] = toks[1]
            elif toks[0] == "header" and len(toks) == 2:
                spec["headers"].append(toks[1])
            elif toks[0] == "struct" and len(toks) == 2:
                cur = toks[1]
                if cur in spec["structs"]:
                    raise SpecError(f"{where}: struct {cur} declared twice")
                spec["structs"][cur] = {}
                spec["order"].append(cur)
            else:
                raise SpecError(f"{where}: unknown directive: {line.strip()}")
            continue
        if cur is None:
            raise SpecError(f"{where}: field directive outside a struct")
        s = line.strip()
        # Flexible array member 'T f[]': 'f[countField]' plus an optional
        # directive for the elements ('f[n] -> T', 'f[n] string', ...).
        flex = None
        mx = re.match(r"^(\w+)\[(\w+)\](.*)$", s)
        if mx:
            flex = mx.group(2)
            s = mx.group(1) + mx.group(3)
            if not mx.group(3).strip():
                spec["structs"][cur][mx.group(1)] = {"kind": "FLEX_ONLY", "flex": flex}
                continue
        m = re.match(r"^(\w+)\s*->\s*(\w+)\s*(?:\[(.*)\])?$", s)
        if m:
            name, tgt, cnt = m.groups()
            d = {"kind": "PTR_STRUCT", "target": tgt, "count": ("ONE",)}
            if cnt is not None:
                cnt = cnt.strip()
                mu = re.match(r"^until\s+(\w+)\s*==\s*(-?\w+)$", cnt)
                mc = re.match(r"^call\s+(\w+)$", cnt)
                if re.match(r"^(0x[0-9a-fA-F]+|\d+)$", cnt):
                    d["count"] = ("FIXED", int(cnt, 0))
                elif mu:
                    d["count"] = ("UNTIL", mu.group(1), int(mu.group(2), 0))
                elif mc:
                    d["count"] = ("CALLBACK", mc.group(1))
                elif re.match(r"^\w+$", cnt):
                    d["count"] = ("FIELD", cnt)
                else:
                    raise SpecError(f"{where}: bad count [{cnt}]")
            if flex:
                if cnt is not None:
                    raise SpecError(f"{where}: elements of a flexible array point to one {tgt} each")
                d["flex"] = flex
            spec["structs"][cur][name] = d
            continue
        m = re.match(r"^(\w+)\s+payload\s+if\s+(\w+)$", s)
        if m:
            spec["structs"][cur][m.group(1)] = {"kind": "PTR_PAYLOAD", "count": ("FIELD", m.group(2))}
            continue
        m = re.match(r"^(\w+)\s+(payload|string|null|raw|skip)$", s)
        if m:
            spec["structs"][cur][m.group(1)] = {"kind": {"payload": "PTR_PAYLOAD", "string": "PTR_STRING",
                                                         "null": "PTR_NULL", "raw": "PTR_RAW",
                                                         "skip": "SKIP"}[m.group(2)]}
            if flex:
                spec["structs"][cur][m.group(1)]["flex"] = flex
            continue
        if flex:
            raise SpecError(f"{where}: cannot parse flexible array directive: {s}")
        # 'f as f32[3]': a struct field (e.g. an anonymous Vec/GXColor typedef)
        # decoded as N scalars of one kind.
        m = re.match(r"^(\w+)\s+as\s+(\w+)\[(\d+)\]$", s)
        if m and m.group(2).upper() in KIND_WIDTH:
            spec["structs"][cur][m.group(1)] = {"kind": "AS", "as": m.group(2).upper(), "asCount": int(m.group(3))}
            continue
        m = re.match(r"^(\w+)\s*=\s*(-?\w+)$", s)
        if m:
            spec["structs"][cur][m.group(1)] = {"kind": "CONST", "value": int(m.group(2), 0)}
            continue
        m = re.match(r"^(\w+)\s+as\s+(\w+)$", s)
        if m and m.group(2).upper() in KIND_WIDTH:
            spec["structs"][cur][m.group(1)] = {"kind": "AS", "as": m.group(2).upper()}
            continue
        raise SpecError(f"{where}: cannot parse field directive: {s}")
    if not spec["format"]:
        raise SpecError(f"{path}: missing 'format'")
    return spec


# ----------------------------------------------------------------- clang
VERSION_ARGS = []  # -DVERSION_<v> flags of the version being generated


def run_clang(args, probe, extra):
    cmd = [CLANG, "-fsyntax-only", "-w", *args, *VERSION_ARGS, *extra, probe]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        raise SpecError("clang failed: " + " ".join(cmd))
    return r.stdout


LAYOUT_LINE = re.compile(r"^\s*(\d+)(:\d+-\d+)? \|( +)(.*)$")


def parse_layouts(text, wanted):
    """{struct: {"size": n, "align": a, "fields": {name: (offset, typetext)}}} for top-level fields."""
    out = {}
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        lines = block.strip("\n").splitlines()
        m = LAYOUT_LINE.match(lines[0])
        if not m:
            continue
        head = m.group(4).strip()
        mh = re.match(r"^(struct|union) (\w+)$", head)
        if not mh or mh.group(2) not in wanted or mh.group(2) in out:
            continue
        rec = {"fields": {}, "size": None}
        for ln in lines[1:]:
            ms = re.search(r"\[sizeof=(\d+), (?:dsize=\d+, )?align=(\d+)", ln)
            if ms:
                rec["size"], rec["align"] = int(ms.group(1)), int(ms.group(2))
                break
            mf = LAYOUT_LINE.match(ln)
            if not mf:
                continue
            depth = (len(mf.group(3)) - 1) // 2
            if depth != 1:
                continue
            if mf.group(2):
                raise SpecError(f"{mh.group(2)}: bitfield '{mf.group(4)}' (describe the word with 'as u32')")
            typ, name = mf.group(4).rsplit(" ", 1)
            rec["fields"][name] = (int(mf.group(1)), typ.strip())
        out[mh.group(2)] = rec
    return out


def parse_ast_fields(text, wanted):
    """{struct: [(name, canonical type)]} in declaration order, from -ast-dump."""
    out = {}
    cur = None
    for ln in text.splitlines():
        # clang 23+ appends attributes such as "external-linkage" after "definition".
        m = re.match(r"^[|`]-RecordDecl .* struct (\w+) definition(?: [\w-]+)*$", ln)
        if m:
            cur = m.group(1) if m.group(1) in wanted and m.group(1) not in out else None
            if cur:
                out[cur] = []
            continue
        if re.match(r"^[|`]-", ln):
            cur = None
            continue
        if cur:
            mf = re.match(r"^[| ] [|`]-FieldDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:referenced )?(\w+)? ?'([^']*)'(?::'([^']*)')?", ln)
            if mf:
                if not mf.group(1):
                    raise SpecError(f"{cur}: anonymous member not supported")
                out[cur].append((mf.group(1), mf.group(3) or mf.group(2)))
    return out


# ------------------------------------------------------------- generate
TYPEDEFS = {}  # typedef name -> underlying type, from the disc AST


def parse_typedefs(text):
    """Arrays of typedefs ('u8[16]') carry no desugared type in -ast-dump."""
    out = {}
    for ln in text.splitlines():
        m = re.match(r"^[|`]-TypedefDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:referenced )?(\w+) '([^']*)'(?::'([^']*)')?", ln)
        if m:
            out[m.group(1)] = m.group(3) or m.group(2)
    return out


def resolve_typedef(ctype):
    seen = 0
    while ctype in TYPEDEFS and seen < 16:
        ctype = TYPEDEFS[ctype]
        seen += 1
    return ctype


def classify(spec, sname, fname, ctype):
    """-> dict describing one disc field from its canonical type and the spec."""
    c = classify_inner(spec, sname, fname, ctype)
    d = spec["structs"][sname].get(fname) or {}
    if d.get("kind") == "SKIP":
        return c  # a skipped flexible array member is handled by hand-written glue
    if c.get("flex") != d.get("flex") and d.get("kind") != "SKIP":
        if d.get("flex"):
            raise SpecError(f"{sname}.{fname}: '[{d['flex']}]' needs a flexible array member (T {fname}[])")
        raise SpecError(f"{sname}.{fname}: flexible array member needs '{fname}[countField]' in the spec")
    return c


def classify_inner(spec, sname, fname, ctype):
    d = spec["structs"][sname].get(fname)
    arr = None
    flex = None
    m = re.match(r"^(.*?)\s*\[(\d*)\]$", ctype)
    if m:
        ctype, arr = m.group(1).strip(), int(m.group(2)) if m.group(2) else 0
        if not m.group(2):
            flex = (d or {}).get("flex") or "?"
        elif arr == 1 and (d or {}).get("flex"):
            flex = d["flex"]  # old-style trailing 'T f[1]' used as a flexible array member
        # clang does not desugar the element type of an array of typedefs
        ctype = SDK_TYPEDEFS.get(ctype, ctype)
        ctype = resolve_typedef(ctype)
    if d and d["kind"] == "FLEX_ONLY":
        d = None
    where = f"{sname}.{fname}"
    c = classify_elem(spec, d, where, ctype, arr)
    if flex:
        c["flex"] = flex
    return c


def classify_elem(spec, d, where, ctype, arr):
    if ctype.endswith("*"):
        if d is None:
            raise SpecError(f"{where}: pointer field ({ctype}) needs a spec directive")
        if arr is not None and d["kind"] == "PTR_STRUCT" and d["count"][0] != "ONE":
            raise SpecError(f"{where}: elements of a pointer array point to one struct each")
        if d["kind"] in ("PTR_STRUCT", "PTR_PAYLOAD", "PTR_STRING", "PTR_NULL", "PTR_RAW", "SKIP"):
            return dict(d, width=4, arr=arr)
        raise SpecError(f"{where}: '{d['kind']}' does not apply to a pointer")
    if d and d["kind"] in ("CONST", "SKIP"):
        return dict(d, width=None, arr=arr)
    if d and d["kind"] == "AS" and d.get("asCount"):
        # A struct field, or an array of structs made only of that scalar (e.g.
        # MapTexXform[8], whose anonymous union the AST parser cannot name),
        # decoded as asCount scalars; the size is asserted against the field.
        if arr is not None and d["asCount"] % max(arr, 1):
            raise SpecError(f"{where}: 'as {d['as'].lower()}[{d['asCount']}]' does not split into {arr} elements")
        return {"kind": d["as"], "width": KIND_WIDTH[d["as"]], "arr": d["asCount"], "asArray": True}
    if d and d["kind"] == "AS":
        return {"kind": d["as"], "width": KIND_WIDTH[d["as"]], "arr": arr}
    if (d and d["kind"] in ("PTR_STRING", "PTR_PAYLOAD", "PTR_NULL", "PTR_RAW") and arr == 4
            and ctype in ("unsigned char", "char", "signed char")):
        # a pointer the decomp declares as its 4 bytes (u8 nameRef[4]): on disc
        # the word is an offset like any pointer's; the native field is a pointer
        return dict(d, width=4, arr=None)
    if d:
        raise SpecError(f"{where}: '{d['kind']}' needs a pointer field, field is {ctype}")
    ms = re.match(r"^struct (\w+)$", ctype)
    if ms:
        if ms.group(1) not in spec["structs"]:
            raise SpecError(f"{where}: embedded struct {ms.group(1)} must be declared in the spec")
        return {"kind": "EMBED", "target": ms.group(1), "width": None, "arr": arr}
    if ctype.startswith("enum "):
        return {"kind": "U32", "width": 4, "arr": arr}  # MWCC -enum int
    if ctype in SCALARS:
        k, w = SCALARS[ctype]
        return {"kind": k, "width": w, "arr": arr}
    raise SpecError(f"{where}: unsupported type '{ctype}'")


def generate(spec_path):
    spec = parse_spec(spec_path)
    fmt = spec["format"]
    names = spec["order"]
    with tempfile.TemporaryDirectory() as td:
        probe = os.path.join(td, "probe.c")
        with open(probe, "w") as f:
            for h in spec["headers"]:
                f.write(f'#include "{h}"\n')
            for i, n in enumerate(names):
                f.write(f"struct {n}* __bedecode_probe_{i};\n")
        disc_lay = parse_layouts(run_clang(DISC_ARGS, probe, ["-Xclang", "-fdump-record-layouts-complete"]), names)
        ast_text = run_clang(DISC_ARGS, probe, ["-Xclang", "-ast-dump"])
        disc_ast = parse_ast_fields(ast_text, names)
        TYPEDEFS.clear()
        TYPEDEFS.update(parse_typedefs(ast_text))
        dump = ["-Xclang", "-fdump-record-layouts-complete"]
        nat_lay = parse_layouts(run_clang(NATIVE_ARGS, probe, dump), names)
        nat32 = parse_layouts(run_clang(NATIVE_32, probe, dump), names)
        shim = os.path.join(td, "win_stdint.h")
        with open(shim, "w") as f:
            f.write(WIN_STDINT_SHIM)
        for host, args in CROSS_64.items():
            args = [shim if a == WIN_SHIM_ARG else a for a in args]
            other = parse_layouts(run_clang(args, probe, dump), names)
            for n in names:
                a, b = nat_lay.get(n), other.get(n)
                if not b or a["size"] != b["size"] or {k: v[0] for k, v in a["fields"].items()} != \
                        {k: v[0] for k, v in b["fields"].items()}:
                    raise SpecError(f"struct {n}: native layout on {host} differs from x86_64; "
                                    "make the header host-independent (fixed-width types)")
    for n in names:
        for what, tab in (("disc layout", disc_lay), ("disc AST", disc_ast), ("native layout", nat_lay)):
            if n not in tab:
                raise SpecError(f"struct {n}: not found in {what} (is it a named struct tag in the headers?)")
        for f in spec["structs"][n]:
            if f not in disc_lay[n]["fields"]:
                raise SpecError(f"{n}.{f}: spec names a field the struct does not have")

    sym = lambda n: f"portBE_{fmt}_{n}"
    out = []
    w = out.append
    rel = os.path.relpath(spec_path, ROOT).replace(os.sep, "/")
    w(f"/* GENERATED by tools/port/gen_bedecode.py from {rel}. Do not edit. */")
    w('#include "port/bedecode.h"')
    for h in spec["headers"]:
        w(f'#include "{h}"')
    w("#include <stddef.h>")
    w("#include <stdint.h>")
    w("")
    w("#define PORT_FSIZE(T, f) sizeof(((T*)0)->f)")
    w("")
    for n in names:
        w(f"extern const PortType {sym(n)};")
    w("")
    callbacks = set()
    for n in names:
        for d in spec["structs"][n].values():
            if d.get("count", ("ONE",))[0] == "CALLBACK":
                callbacks.add(d["count"][1])
    for cb in sorted(callbacks):
        w(f"uint32_t {cb}(PortDecodeCtx* ctx, uint32_t structOff);")
    if callbacks:
        w("")

    for n in names:
        dl, nl = disc_lay[n], nat_lay[n]
        T = f"struct {n}"
        w(f"/* {n}: disc 0x{dl['size']:X} bytes, native (x86_64) 0x{nl['size']:X} */")
        n32 = nat32[n]
        w("#if UINTPTR_MAX > 0xFFFFFFFFu")
        w(f"_Static_assert(sizeof({T}) == {nl['size']}, \"{n}: native size differs from x86_64; regenerate or fix the header\");")
        for fname, _ in disc_ast[n]:
            w(f"_Static_assert(offsetof({T}, {fname}) == {nl['fields'][fname][0]}, \"{n}.{fname}: native offset\");")
        w("#else /* 32-bit headless bring-up build */")
        w(f"_Static_assert(sizeof({T}) == {n32['size']}, \"{n}: native size differs from i686\");")
        for fname, _ in disc_ast[n]:
            w(f"_Static_assert(offsetof({T}, {fname}) == {n32['fields'][fname][0]}, \"{n}.{fname}: native offset\");")
        w("#endif")
        rows = []
        for fname, ctype in disc_ast[n]:
            if fname not in nl["fields"]:
                raise SpecError(f"{n}.{fname}: in the disc layout but not the native one")
            goff = dl["fields"][fname][0]
            noff = nl["fields"][fname][0]
            c = classify(spec, n, fname, ctype)
            if c["kind"] == "SKIP":
                continue
            arr = c["arr"]
            flex = c.get("flex")
            elem = f"{fname}[0]" if arr is not None else fname
            nsize = f"PORT_FSIZE({T}, {elem})"
            if c.get("asArray"):
                w(f"_Static_assert(PORT_FSIZE({T}, {fname}) == {arr * c['width']}, \"{n}.{fname}: not {arr} x {c['width']} bytes\");")
                nsize = str(c["width"])
            elif c["kind"] in KIND_WIDTH:
                w(f"_Static_assert({nsize} == {c['width']}, \"{n}.{fname}: native width differs from disc\");")
            gstride = nstride = "0"
            if arr is not None:
                if c["kind"] == "EMBED":
                    gstride = str(disc_lay[c["target"]]["size"])
                elif c["kind"] in KIND_WIDTH or c["kind"].startswith("PTR_"):
                    gstride = str(c["width"])
                else:
                    gstride = "0"
                nstride = nsize
            cnt_kind, count, coff, cwidth, value, target, fn = "ONE", 0, 0, 0, c.get("value", 0), "NULL", "NULL"
            if c["kind"] in ("PTR_STRUCT", "EMBED"):
                if c["target"] not in spec["structs"]:
                    raise SpecError(f"{n}.{fname}: target {c['target']} is not declared in the spec")
                target = f"&{sym(c['target'])}"
            if c["kind"] == "PTR_STRUCT" or (c["kind"] == "PTR_PAYLOAD" and c.get("count")):
                cnt = c["count"]
                cnt_kind = cnt[0]
                if cnt_kind == "FIXED":
                    count = cnt[1]
                elif cnt_kind == "FIELD":
                    if cnt[1] not in dl["fields"]:
                        raise SpecError(f"{n}.{fname}: count field {cnt[1]} not in {n}")
                    coff = dl["fields"][cnt[1]][0]
                    ct = dict(disc_ast[n])[cnt[1]]
                    mper = re.match(r"^(.*?)\s*\[(\d+)\]$", ct)
                    if mper and c["kind"] == "PTR_PAYLOAD" and arr == int(mper.group(2)):
                        ct, value = resolve_typedef(mper.group(1).strip()), 1  # element i gated by count[i]
                    if ct not in SCALARS or SCALARS[ct][0][0] != "U" and SCALARS[ct][0][0] != "S" or SCALARS[ct][1] > 4:
                        raise SpecError(f"{n}.{fname}: count field {cnt[1]} is not a 1/2/4-byte integer")
                    cwidth = SCALARS[ct][1]
                elif cnt_kind == "UNTIL":
                    tgt = c["target"]
                    if cnt[1] not in disc_lay[tgt]["fields"]:
                        raise SpecError(f"{n}.{fname}: terminator field {cnt[1]} not in {tgt}")
                    coff = disc_lay[tgt]["fields"][cnt[1]][0]
                    ct = dict(disc_ast[tgt])[cnt[1]]
                    cwidth = SCALARS.get(ct, ("", 4))[1] if not ct.endswith("*") else 4
                    value = cnt[2]
                elif cnt_kind == "CALLBACK":
                    fn = cnt[1]
            if arr is not None:
                count = arr
            inline = 1 if arr is not None else 0
            if flex:  # flexible array member: count from a sibling field
                if flex not in dl["fields"]:
                    raise SpecError(f"{n}.{fname}: count field {flex} not in {n}")
                ct = dict(disc_ast[n])[flex]
                if ct not in SCALARS or SCALARS[ct][0][0] not in "US" or SCALARS[ct][1] > 4:
                    raise SpecError(f"{n}.{fname}: count field {flex} is not a 1/2/4-byte integer")
                inline, cnt_kind, count, coff, cwidth = 2, "FIELD", 0, dl["fields"][flex][0], SCALARS[ct][1]
            kind = c["kind"]
            rows.append(f"    {{ \"{fname}\", 0x{goff:X}, offsetof({T}, {fname}), PORT_FK_{kind}, PORT_CNT_{cnt_kind}, "
                        f"{cwidth}, {inline}, {count}, 0x{coff:X}, {gstride}, {nstride}, "
                        f"{nsize}, {value}, {target}, {fn} }},")
        w(f"static const PortField {sym(n)}_fields[] = {{")
        out.extend(rows)
        w("};")
        w(f"const PortType {sym(n)} = {{ \"{n}\", 0x{dl['size']:X}, sizeof({T}), _Alignof({T}), "
          f"{len(rows)}, {sym(n)}_fields }};")
        w("")
    return fmt, "\n".join(out) + "\n"


def generate_all_versions(spec_path):
    """One file for every version: shared when the layouts agree, else per version."""
    texts = {}
    for v in portversion.VERSIONS:
        VERSION_ARGS[:] = portversion.defines(v)[:2]
        try:
            fmt, texts[v] = generate(spec_path)
        except SpecError as e:
            raise SpecError(f"{v}: {e}")
    jp, us = texts["G8MJ01"], texts["G8ME01"]
    if jp == us:
        return fmt, jp
    head, jp_body = jp.split("\n", 1)
    us_body = us.split("\n", 1)[1]
    return fmt, (f"{head}\n/* Layouts differ between versions: US (G8ME01) first, then JP (G8MJ01). */\n"
                 f"#ifdef VERSION_G8ME01\n{us_body}#else\n{jp_body}#endif\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if a generated file is stale")
    ap.add_argument("formats", nargs="*")
    a = ap.parse_args()
    specs = sorted(f for f in os.listdir(SPEC_DIR) if f.endswith(".spec"))
    if a.formats:
        specs = [f"{x}.spec" for x in a.formats]
    stale = 0
    for s in specs:
        try:
            fmt, text = generate_all_versions(os.path.join(SPEC_DIR, s))
        except SpecError as e:
            print(f"gen_bedecode: {e}", file=sys.stderr)
            return 2
        path = os.path.join(OUT_DIR, f"bedecode_gen_{fmt}.c")
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if a.check:
            if old != text:
                print(f"STALE {os.path.relpath(path, ROOT)} (run tools/port/gen_bedecode.py)")
                stale += 1
            else:
                print(f"ok    {os.path.relpath(path, ROOT)}")
        elif old != text:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print(f"wrote {os.path.relpath(path, ROOT)}")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
