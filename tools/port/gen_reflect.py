#!/usr/bin/env python3
"""Generate the game-data reflection table for mods' data patches
(port/src/aurora/reflect_gen.c, read by port/mods/mods.cpp through
port/include/port/reflect.h).

Input: port/mods/reflect.spec, one directive per line ('#' comments):
  externs <header glob>    every extern variable the headers declare
                           (relative to include/, e.g. unit/party/*.h)
  symbol <name> <header>   one variable the game defines but no header
                           declares; its type is read from its definition
                           (any .c file under src/), the header gives the type

For each variable the table holds its address, its type (fields by name,
offset, size, kind, nested structs and arrays, recursively) and its element
count (the GameCube symbol's size over the element's GameCube size, from
config/<version>/symbols.txt). Every enum constant the listed headers can see
(from include/) is listed by name too, so patches can say ITEM_MUSHROOM.
Offsets and sizes are offsetof()/sizeof() in the generated C: the compiler's
native layout.

Usage: gen_reflect.py [--check]
"""
import argparse
import glob
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_bedecode as gb  # clang probes and layout parsing
import portversion

ROOT = gb.ROOT
SPEC = os.path.join(ROOT, "port", "mods", "reflect.spec")
OUT = os.path.join(ROOT, "port", "src", "aurora", "reflect_gen.c")
DOC = os.path.join(ROOT, "docs", "MOD_DATA.md")


class Error(Exception):
    pass


def parse_spec():
    externs, symbols = [], []
    for lineno, raw in enumerate(open(SPEC, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].split()
        if not line:
            continue
        if line[0] == "externs" and len(line) == 2:
            found = sorted(glob.glob(os.path.join(ROOT, "include", line[1])))
            if not found:
                raise Error(f"reflect.spec:{lineno}: no header matches {line[1]}")
            externs += [os.path.relpath(p, os.path.join(ROOT, "include")).replace(os.sep, "/") for p in found]
        elif line[0] == "symbol" and len(line) == 3:
            symbols.append((line[1], line[2]))
        else:
            raise Error(f"reflect.spec:{lineno}: cannot parse: {raw.strip()}")
    return externs, symbols


LOC_FILE = re.compile(r"<([A-Za-z0-9_./\\-]+\.[ch]):\d+")
VAR = re.compile(r"^[|`]-VarDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:used |referenced )?(\w+) '([^']*)'(?::'([^']*)')?(.*)$")
ENUM_CONST = re.compile(r"EnumConstantDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:referenced )?(\w+) ")
RECORD = re.compile(r"^[|`]-RecordDecl .* (struct|union) (\w+) definition")
FIELD = re.compile(r"^[| ] [|`]-FieldDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:referenced )?(\w+)? ?'([^']*)'(?::'([^']*)')?")


def scan_ast(text):
    """Variables (name -> (canonical type, header, extern)), enum constants by
    file, records (tag -> [(field, canonical type)] or None if not describable)."""
    variables, enums, records, typedefs = {}, [], {}, {}
    cur_file = None
    cur_rec = None
    for ln in text.splitlines():
        m = LOC_FILE.search(ln)
        if m:
            cur_file = m.group(1).replace("\\", "/")
        m = RECORD.match(ln)
        if m:
            cur_rec = None
            if m.group(2) not in records:
                cur_rec = m.group(2)
                records[cur_rec] = [] if m.group(1) == "struct" else None
            continue
        if re.match(r"^[|`]-", ln):
            cur_rec = None
        mt = re.match(r"^[|`]-TypedefDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:referenced )?(\w+) '([^']*)'(?::'([^']*)')?", ln)
        if mt:
            typedefs[mt.group(1)] = mt.group(3) or mt.group(2)
        mv = VAR.match(ln)
        if mv:
            variables.setdefault(mv.group(1), (mv.group(3) or mv.group(2), cur_file, "extern" in mv.group(4)))
        me = ENUM_CONST.search(ln)
        if me and cur_file and cur_file.startswith("include/"):
            enums.append(me.group(1))
        if cur_rec is not None and records.get(cur_rec) is not None:
            mf = FIELD.match(ln)
            if mf:
                if not mf.group(1):
                    records[cur_rec] = None  # anonymous member: not describable
                else:
                    records[cur_rec].append((mf.group(1), mf.group(3) or mf.group(2)))
            elif re.match(r"^[| ] [|`]-(RecordDecl|IndirectFieldDecl)", ln):
                records[cur_rec] = None  # nested anonymous struct/union
    return variables, enums, records, typedefs


def strip_qual(t):
    t = re.sub(r"\b(const|volatile)\b", "", t)
    return re.sub(r"\s+", " ", t).strip()


def classify(t, typedefs):
    """-> (kind, count, struct tag or None) for one field/variable type."""
    t = strip_qual(t)
    count = 1
    m = re.match(r"^(.*?)\s*((?:\[\d+\])+)$", t)
    if m:
        t = m.group(1).strip()
        for n in re.findall(r"\[(\d+)\]", m.group(2)):
            count *= int(n)
    seen = 0
    while t in typedefs and seen < 16:
        t = strip_qual(typedefs[t])
        seen += 1
    if t.endswith("*"):
        return ("STR" if strip_qual(t[:-1]) == "char" else "PTR"), count, None
    m = re.match(r"^struct (\w+)$", t)
    if m:
        return "STRUCT", count, m.group(1)
    if t.startswith("union ") or "(" in t:
        return "OPAQUE", count, None
    if t.startswith("enum "):
        return "S", count, None
    if t in ("float", "double"):
        return "F", count, None
    if t.startswith("unsigned") or t == "_Bool":
        return "U", count, None
    if t in ("char", "signed char", "short", "int", "long", "long long", "signed"):
        return "S", count, None
    return "OPAQUE", count, None


def gc_sizes(version):
    """GameCube symbol sizes: name -> size (DOL symbols, first definition)."""
    out = {}
    for ln in open(os.path.join(ROOT, "config", version, "symbols.txt"), encoding="utf-8"):
        m = re.match(r"^(\w+) = \.\w+:0x[0-9A-Fa-f]+; // type:object size:0x([0-9A-Fa-f]+)", ln)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def generate(version):
    gb.VERSION_ARGS[:] = portversion.version_args(version)
    externs, symbols = parse_spec()
    headers = list(dict.fromkeys(externs + [h for _, h in symbols]))
    with tempfile.TemporaryDirectory() as td:
        probe = os.path.join(td, "probe.c")
        with open(probe, "w") as f:
            for h in headers:
                f.write(f'#include "{h}"\n')
        native = gb.run_clang(gb.NATIVE_ARGS, probe, ["-Xclang", "-ast-dump"])
        variables, enums, records, typedefs = scan_ast(native)
        disc_text = gb.run_clang(gb.DISC_ARGS, probe, ["-Xclang", "-fdump-record-layouts-complete"])
    sizes = gc_sizes(version)

    # The variables: extern ones from the listed headers, and the named ones,
    # whose type comes from their definition.
    wanted = []
    for name, (ctype, path, ext) in variables.items():
        if ext and path and path[len("include/"):] in externs:
            wanted.append((name, ctype, False))
    for name, header in symbols:
        defn = None
        for src in glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True):
            text = open(src, encoding="utf-8", errors="replace").read()
            m = re.search(r"^(?:const\s+)?(\w+)\s+" + name + r"\s*(\[[^\]]*\])?\s*=", text, re.M)
            if m:
                defn = m.group(1) + (" []" if m.group(2) else "")
                break
        if defn is None:
            raise Error(f"symbol {name}: no definition found under src/")
        ctype = typedefs.get(defn.split()[0], defn.split()[0]) + (" []" if defn.endswith("[]") else "")
        wanted.append((name, ctype, True))

    # Element types and counts.
    structs = {}  # tag -> fields or None
    def need(tag):
        if tag in structs:
            return
        fields = records.get(tag)
        structs[tag] = None
        if fields is None:
            return
        out = []
        for fname, ftype in fields:
            kind, count, sub = classify(ftype, typedefs)
            if kind == "STRUCT":
                need(sub)
                if structs.get(sub) is None:
                    kind, sub = "OPAQUE", None
            out.append((fname, kind, count, sub))
        structs[tag] = out
    syms = []
    for name, ctype, declare in sorted(wanted):
        array = ctype.endswith("[]") or re.search(r"\[\d+\]$", ctype) is not None
        base = re.sub(r"\s*\[\d*\]$", "", ctype)
        kind, _, tag = classify(base, typedefs)
        if kind == "STRUCT":
            need(tag)
            if structs.get(tag) is None:
                continue
        elif kind not in ("U", "S", "F"):
            continue
        count = 1
        if array:
            m = re.search(r"\[(\d+)\]$", ctype)
            if m:
                count = int(m.group(1))
            else:
                if kind != "STRUCT":
                    continue
                elem = gc_struct_size(disc_text, tag)
                if name not in sizes or not elem:
                    continue
                count = sizes[name] // elem
        syms.append((name, base, kind, tag, count, array, declare))

    out = []
    w = out.append
    w("/* GENERATED by tools/port/gen_reflect.py from port/mods/reflect.spec. Do not edit. */")
    w('#include "port/reflect.h"')
    for h in headers:
        w(f'#include "{h}"')
    w("#include <stddef.h>")
    w("")
    w("#define FSIZE(T, f) sizeof(((T*)0)->f)")
    w("")
    for name, base, kind, tag, count, array, declare in syms:
        if declare:
            w(f"extern {'struct ' + tag if tag else base} {name}{'[]' if array else ''};")
    w("")
    order = [t for t in structs if structs[t] is not None]
    for tag in order:
        w(f"extern const PortReflStruct portRefl_{tag};")
    w("")
    kinds = {"U": "PORT_REFL_UNSIGNED", "S": "PORT_REFL_SIGNED", "F": "PORT_REFL_FLOAT", "STR": "PORT_REFL_STRING",
             "PTR": "PORT_REFL_POINTER", "STRUCT": "PORT_REFL_STRUCT", "OPAQUE": "PORT_REFL_OPAQUE"}
    for tag in order:
        T = f"struct {tag}"
        w(f"static const PortReflField portReflFields_{tag}[] = {{")
        for fname, kind, count, sub in structs[tag]:
            w(f'    {{"{fname}", {kinds[kind]}, offsetof({T}, {fname}), FSIZE({T}, {fname}) / {count}, {count}, '
              f'{"&portRefl_" + sub if sub else "NULL"}}},')
        w("};")
        w(f'const PortReflStruct portRefl_{tag} = {{"{tag}", sizeof({T}), '
          f"sizeof(portReflFields_{tag}) / sizeof(portReflFields_{tag}[0]), portReflFields_{tag}}};")
        w("")
    w("const PortReflSymbol portReflSymbols[] = {")
    for name, base, kind, tag, count, array, declare in syms:
        elem = f"sizeof(struct {tag})" if tag else f"sizeof({name}{'[0]' if array else ''})"
        w(f'    {{"{name}", (void*)&{name}, {kinds[kind]}, {elem}, {count}, {"&portRefl_" + tag if tag else "NULL"}}},')
    w("};")
    w("const int portReflSymbolCount = sizeof(portReflSymbols) / sizeof(portReflSymbols[0]);")
    w("")
    w("const PortReflEnum portReflEnums[] = {")
    for e in sorted(set(enums)):
        w(f'    {{"{e}", (long long){e}}},')
    w("};")
    w("const int portReflEnumCount = sizeof(portReflEnums) / sizeof(portReflEnums[0]);")
    return "\n".join(out) + "\n", reference(syms, structs, order, enums, version)


def reference(syms, structs, order, enums, version):
    """docs/MOD_DATA.md: what patches can name, for mod authors."""
    kind_text = {"U": "whole number (≥ 0)", "S": "whole number", "F": "number", "STR": "\"text\"",
                 "PTR": "(pointer: fixed)", "OPAQUE": "(fixed)"}
    out = [f"<!-- GENERATED by tools/port/gen_reflect.py from port/mods/reflect.spec. Do not edit. -->",
           "Game data for patches",
           "=====================",
           "",
           "What a mod's `patches/` files can change ([MODDING.md](MODDING.md#patches-game-data)), "
           f"generated from the decomp's headers ({version}; the JP build has the same unless noted).",
           "",
           "## Tables", "", "| Name | Of | Elements |", "|---|---|---|"]
    for name, base, kind, tag, count, array, declare in syms:
        out.append(f"| `{name}` | {('[' + tag + '](#' + tag.lower() + ')') if tag else kind_text[kind]} | {count} |")
    for tag in order:
        out += ["", f"## {tag}", "", "| Field | Value | Count |", "|---|---|---|"]
        for fname, kind, count, sub in structs[tag]:
            what = f"[{sub}](#{sub.lower()})" if kind == "STRUCT" else kind_text[kind]
            out.append(f"| `{fname}` | {what} | {count if count > 1 else ''} |")
    prefixes = {}
    for e in sorted(set(enums)):
        p = e.split("_")[0] if "_" in e else e
        prefixes.setdefault(p, []).append(e)
    out += ["", "## Constants", "",
            "Indexes and values can be the decomp's enum constants. Items, for example "
            "(`include/mario/mario_pouch.h`):", ""]
    items = [e for e in sorted(set(enums)) if e.startswith("ITEM_")]
    out.append(", ".join(f"`{e}`" for e in items))
    out += ["", "Other groups: " + ", ".join(f"`{p}_*` ({len(v)})" for p, v in sorted(prefixes.items())
                                            if len(v) > 3 and p != "ITEM") + "."]
    return "\n".join(out) + "\n"


def gc_struct_size(disc_text, tag):
    lay = gb.parse_layouts(disc_text, {tag}) if tag else {}
    return lay.get(tag, {}).get("size")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if the generated file is stale")
    a = ap.parse_args()
    try:
        gens = {v: generate(v) for v in portversion.VERSIONS}
    except (Error, gb.SpecError) as e:
        print(f"gen_reflect: {e}", file=sys.stderr)
        return 2
    texts = {v: g[0] for v, g in gens.items()}
    if len(set(texts.values())) == 1:
        text = texts["G8MJ01"]
    else:
        head = texts["G8MJ01"].split("\n", 1)[0]
        text = (f"{head}\n/* The versions differ. */\n"
                + portversion.version_switch({v: t.split("\n", 1)[1] for v, t in texts.items()}))
    doc = gens["G8ME01"][1]
    if gens["G8MJ01"][1] != doc.replace("G8ME01", "G8MJ01"):
        doc = doc.replace("the JP build has the same unless noted", "the JP build's differs in places")
    stale = 0
    for path, content in ((OUT, text), (DOC, doc)):
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        rel = os.path.relpath(path, ROOT)
        if a.check:
            print(f"{'ok   ' if old == content else 'STALE'} {rel}")
            stale += old != content
        elif old != content:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)
            print(f"wrote {rel}")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
