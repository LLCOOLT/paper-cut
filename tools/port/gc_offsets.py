#!/usr/bin/env python3
"""Where a GameCube patch lands on the PC: a byte offset in the disc's
main.dol or a REL, as the game's data the PC build has there.

ISO patchers (an Archipelago seed's, a randomizer's) change the game by
writing bytes at file offsets of main.dol and files/rel/*.rel. The PC build
has no such files: its data is the decomp's C, laid out for the PC. This
finds what an offset is, then where that is on the PC:

  1. the section it falls in (the DOL's header, a REL's section table; the
     disc's own files) and the decomp's symbol there (config/<version>/
     symbols.txt and splits.txt, which also give its source file);
  2. a script (the decomp's EvtWord arrays): the same word on the PC, whose
     words are 8 bytes;
     data: the same field, by the GameCube and PC layouts of the symbol's
     type (clang's, of its source file); an array's element by its index;
     code: reported as code (a patch there needs a hook, by hand);
     the DOL's .init section (0x80003100...), where patchers keep their own
     settings: low memory (the port's portGcLowMem).

  gc_offsets.py --version G8ME01 [--disc orig/G8ME01] rel/gor.rel:0x2C624 main.dol:0x400 0x803DB890 ...

(a bare address is a GameCube address of the DOL's: data, .bss, low memory)

prints one line per offset:
  rel/gor.rel:0x2C624 script gor_00::mobj_box00 +0x60 (word 12)
The names are as a mod's script or the port's lookup takes them
(port/mods/mods.cpp, gameName): `file::name` for a file's static.
"""
import argparse
import functools
import glob
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mod_fields
import gen_bedecode as gb
import portversion

ROOT = mod_fields.ROOT
REL_SECTIONS = {1: ".text", 2: ".ctors", 3: ".dtors", 4: ".rodata", 5: ".data", 6: ".bss"}
SCALARS = {"s8": 1, "u8": 1, "char": 1, "BOOL": 4, "s16": 2, "u16": 2, "s32": 4, "u32": 4, "f32": 4,
           "s64": 8, "u64": 8, "f64": 8, "int": 4, "float": 4, "double": 8, "EvtWord": 4}


class Error(Exception):
    pass


@functools.lru_cache(None)
def scripts():
    names = set()
    for src in glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True) + \
            glob.glob(os.path.join(ROOT, "rels", "**", "*.c"), recursive=True):
        text = open(src, encoding="utf-8", errors="replace").read()
        names.update(re.findall(r"\bEVT_(?:BEGIN|DEF)\(\s*(\w+)\s*\)", text))
        names.update(re.findall(r"\bEvtWord\s+\(?(\w+)\)?\s*\[", text))
    return names


@functools.lru_cache(None)
def symbols(version, rel):
    """[(section, address, size, name, type, source path)] of the DOL (rel
    None) or a REL."""
    base = os.path.join(ROOT, "config", version, *([rel] if rel else []))
    ranges = {}
    cur = None
    for ln in open(os.path.join(base, "splits.txt"), encoding="utf-8"):
        m = re.match(r"^(\S+\.c):\s*$", ln)
        if m:
            cur = (f"rels/{rel}/src/" if rel else "src/") + m.group(1)
            continue
        m = re.match(r"^\s+(\.\w+)\s+start:0x([0-9A-Fa-f]+) end:0x([0-9A-Fa-f]+)", ln)
        if m and cur:
            ranges.setdefault(m.group(1), []).append((int(m.group(2), 16), int(m.group(3), 16), cur))
    out = []
    for ln in open(os.path.join(base, "symbols.txt"), encoding="utf-8"):
        m = re.match(r"^(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // type:(\w+)(?: size:0x([0-9A-Fa-f]+))?", ln)
        if not m:
            continue
        sec, addr, size = m.group(2), int(m.group(3), 16), int(m.group(5) or "0", 16)
        src = next((s for lo, hi, s in ranges.get(sec, []) if lo <= addr < hi), None)
        out.append((sec, addr, size, m.group(1), m.group(4), src))
    return out


def section_of(disc, file, offset):
    """(section name, offset in it, its address for the DOL) of a file offset."""
    data = open(os.path.join(disc, *(["sys", "main.dol"] if file == "main.dol" else ["files", file])), "rb").read()
    if file == "main.dol":
        offs = struct.unpack(">18I", data[0:72])
        addrs = struct.unpack(">18I", data[72:144])
        sizes = struct.unpack(">18I", data[144:216])
        for i in range(18):
            if sizes[i] and offs[i] <= offset < offs[i] + sizes[i]:
                return i, offset - offs[i], addrs[i] + offset - offs[i]
        raise Error("not in a section of main.dol")
    count, info = struct.unpack(">II", data[0xC:0x14])
    for i in range(count):
        off, size = struct.unpack(">II", data[info + i * 8:info + i * 8 + 8])
        off &= ~1
        if off and off <= offset < off + size:
            return REL_SECTIONS.get(i, f"section {i}"), offset - off, None
    raise Error(f"not in a section of {file}")


@functools.lru_cache(None)
def dol_section_names(version, disc):
    """{DOL section index: its name}: the DOL's sections that have contents,
    in order, are the decomp's (splits.txt's header) that aren't .bss."""
    names = []
    for ln in open(os.path.join(ROOT, "config", version, "splits.txt"), encoding="utf-8"):
        m = re.match(r"^\s+(\.\w+)\s+type:(\w+)", ln)
        if m and m.group(2) != "bss":
            names.append(m.group(1))
        elif ln.strip() and not ln.startswith(("Sections", "	", " ")):
            break
    data = open(os.path.join(disc, "sys", "main.dol"), "rb").read(0x100)
    sizes = struct.unpack(">18I", data[144:216])
    return dict(zip([i for i in range(18) if sizes[i]], names))


def find_symbol(version, rel, section, address):
    for sec, addr, size, name, kind, src in symbols(version, rel):
        if sec == section and addr <= address < addr + max(size, 1):
            return name, address - addr, kind, src
    raise Error(f"no symbol at {section}:0x{address:X}")


@functools.lru_cache(None)
def variable_type(src, name, version, rel):
    """The C type the decomp gives a variable in its source file."""
    gb.VERSION_ARGS[:] = portversion.version_args(version)
    extra = [f"-Irels/{rel}/include"] if rel and os.path.isdir(os.path.join(ROOT, "rels", rel, "include")) else []
    r = mod_fields.run([mod_fields.CLANG, "-fsyntax-only", "-w", *gb.DISC_ARGS, *gb.VERSION_ARGS, *extra,
                        "-ferror-limit=0", "-Xclang", "-ast-dump", "-fno-color-diagnostics", src], ROOT)
    for ln in r.stdout.splitlines():
        m = re.search(rf"VarDecl 0x[0-9a-f]+ .*? {re.escape(name)} '([^']*)'", ln)
        if m:
            return m.group(1)
    raise Error(f"no type for {name} in {src}")


@functools.lru_cache(None)
def layouts(src, version, rel, pc):
    """Record layouts of a source file's types, the GameCube's or the PC's."""
    gb.VERSION_ARGS[:] = portversion.version_args(version)
    extra = [f"-Irels/{rel}/include"] if rel and os.path.isdir(os.path.join(ROOT, "rels", rel, "include")) else []
    args = gb.NATIVE_ARGS if pc else gb.DISC_ARGS
    r = mod_fields.run([mod_fields.CLANG, "-fsyntax-only", "-w", *args, *gb.VERSION_ARGS, *extra,
                        "-ferror-limit=0", "-Xclang", "-fdump-record-layouts-complete", src], ROOT)
    return mod_fields.parse_layouts(r.stdout, ROOT)


def type_size(typ, lay):
    t = re.sub(r"^(const |volatile |struct |union )+", "", typ.strip())
    if t.endswith("*"):
        return None  # a pointer: its size is the target's
    m = re.match(r"^(.*?)\s*\[(\d+)\]$", t)
    if m:
        n = type_size(m.group(1), lay)
        return n * int(m.group(2)) if n is not None else None
    if t in SCALARS:
        return SCALARS[t]
    return lay[t]["size"] if t in lay else None


def map_data(typ, offset, gc, pc):
    """(PC offset, GameCube scalar's width, PC scalar's width) of the
    scalar at a GameCube offset within a value of type typ."""
    t = re.sub(r"^(const |volatile |struct |union )+", "", typ.strip())
    m = re.match(r"^(.*?)\s*\[(\d*)\]$", t)
    if m:  # an array: its element, by index
        elem = m.group(1)
        gsz = type_size(elem, gc) or 4
        psz = type_size(elem, pc) or (8 if elem.strip().endswith("*") else gsz)
        sub, gw, pw = map_data(elem, offset % gsz, gc, pc)
        return (offset // gsz) * psz + sub, gw, pw
    if t.endswith("*"):
        if offset:
            raise Error("inside a pointer")
        return 0, 4, 8
    if t in SCALARS:
        if offset:
            raise Error(f"inside a {t}")
        return 0, SCALARS[t], SCALARS[t] if t != "EvtWord" else 8
    if t not in gc or t not in pc:
        raise Error(f"no layout of {t}")
    gf, pf = gc[t]["fields"], pc[t]["fields"]
    # the top-level field holding the offset, then within it
    tops = [(i, f) for i, f in enumerate(gf) if f[0] == 1]
    for k, (i, (depth, off, ftyp, name)) in enumerate(tops):
        end = tops[k + 1][1][1] if k + 1 < len(tops) else gc[t]["size"]
        if off <= offset < end:
            pmatch = [f for f in pf if f[0] == 1 and f[3] == name]
            if not pmatch:
                raise Error(f"no field {name} of {t} on the PC")
            sub, gw, pw = map_data(ftyp, offset - off, gc, pc)
            return pmatch[0][1] + sub, gw, pw
    raise Error(f"past the end of {t}")


def translate(version, disc, file, offset):
    """{kind, name, pc_offset, gc_width, pc_width, gc_symbol_offset}."""
    rel = None if file == "main.dol" else os.path.splitext(os.path.basename(file))[0]
    sec, soff, address = section_of(disc, file, offset)
    if rel is None:
        if sec == 0:  # .init: past its code, the patchers' own space
            return {"kind": "lowmem", "name": "portGcLowMem", "pc_offset": address - 0x80000000,
                    "gc_width": 0, "pc_width": 0}
        name, within, kind, src = find_symbol(version, None, dol_section_names(version, disc)[sec], address)
    else:
        name, within, kind, src = find_symbol(version, rel, sec, soff)
    t = _symbol(version, rel, name, within, kind, src)
    if t["kind"] == "script" and src:
        word = pc_script_word(version, disc, file, offset - within, name, src, within // 4)
        if word is not None and word != t["word"]:
            t["word"], t["pc_offset"] = word, word * 8
    return t


def _file_bytes(disc, file):
    return _read(os.path.join(disc, *(["sys", "main.dol"] if file == "main.dol" else ["files", file])))


@functools.lru_cache(None)
def _read(path):
    return open(path, "rb").read()


def _symbol_file_offset(disc, file, rel, sec, addr):
    """The file offset of a symbol's start in a disc's main.dol or REL."""
    if rel is None:
        return dol_offset(disc, addr)
    data = _file_bytes(disc, file)
    index = next((i for i, n in REL_SECTIONS.items() if n == sec), None)
    count, info = struct.unpack(">II", data[0xC:0x14])
    if index is None or index >= count:
        return None
    off, size = struct.unpack(">II", data[info + index * 8:info + index * 8 + 8])
    off &= ~1
    return off + addr if off else None


def other_version_offset(us_disc, version, disc, file, offset, width=4):
    """The file offset in `version`'s disc of what the US disc has at `file`:`offset` (an
    apworld patch offset, made for the US disc): the same symbol (by name, in the same
    source) and, in a script, the same argument of the same command (the two discs'
    scripts lined up by their command headers); in data, the same offset when the symbol
    has the same size. Checked: the value there must be the US disc's (the game's own
    item or price). Raises Error when it can't be placed."""
    if version == "G8ME01":
        return offset
    rel = None if file == "main.dol" else os.path.splitext(os.path.basename(file))[0]
    if rel is not None and not os.path.exists(os.path.join(disc, "files", file)):
        raise Error(f"{version} has no {file}")
    sec, soff, address = section_of(us_disc, file, offset)
    if rel is None:
        if sec == 0:
            return offset  # .init: the patchers' own space, the same address everywhere
        name, within, kind, src = find_symbol("G8ME01", None, dol_section_names("G8ME01", us_disc)[sec], address)
    else:
        name, within, kind, src = find_symbol("G8ME01", rel, sec, soff)
    us_size = next((e[2] for e in symbols("G8ME01", rel) if e[3] == name and e[5] == src), 0)
    cands = [e for e in symbols(version, rel) if e[3] == name and e[5] == src] or \
            [e for e in symbols(version, rel) if e[3] == name]
    if len(cands) != 1:
        raise Error(f"{name}: {len(cands)} symbols of that name on {version}")
    vsec, vaddr, vsize, _, _, _ = cands[0]
    start = _symbol_file_offset(disc, file, rel, vsec, vaddr)
    if start is None:
        raise Error(f"{name}: no file offset on {version}")
    us_data = _file_bytes(us_disc, file)
    v_data = _file_bytes(disc, file)
    if name in scripts():
        import difflib
        us_ops = _gc_ops(us_data, offset - within)
        v_ops = _gc_ops(v_data, start)
        word = within // 4
        k = max(j for j, (w, h) in enumerate(us_ops) if w <= word)
        slot = word - us_ops[k][0]
        sm = difflib.SequenceMatcher(None, [h for _, h in us_ops], [h for _, h in v_ops], autojunk=False)
        vword = None
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal" and i1 <= k < i2:
                vword = v_ops[j1 + (k - i1)][0] + slot
        if vword is None:
            raise Error(f"word {word} of {name} is in a command {version} has otherwise")
        at = start + vword * 4
    else:
        if vsize != us_size:
            raise Error(f"{name} is 0x{vsize:X} bytes on {version}, 0x{us_size:X} on the US disc")
        at = start + within
    fmt = ">I" if width == 4 else ">H"
    want, got = struct.unpack_from(fmt, us_data, offset)[0], struct.unpack_from(fmt, v_data, at)[0]
    if want != got:
        raise Error(f"{name}: {version}'s word holds 0x{got:X}, the US disc's 0x{want:X}")
    return at


def _gc_ops(data, start):
    """[(word index, header)] of the GameCube script at file offset start, to its END_SCRIPT."""
    out, i = [], 0
    while start + i * 4 + 4 <= len(data) and len(out) < 100000:
        h = struct.unpack_from(">I", data, start + i * 4)[0]
        out.append((i, h))
        if h & 0xFFFF == 1 or h & 0xFFFF > 118:  # END_SCRIPT (or not a script after all)
            break
        i += 1 + (h >> 16)
    return out


def pc_script_word(version, disc, file, start, name, src, gc_word):
    """The PC word of a script's GameCube word: the same unless Paper Cut's own lines (#ifdef TARGET_PC)
    add commands to the script before it. Then the two scripts' commands are lined up (difflib, by their
    headers) and the word is the same argument of the same command. The script's PC words come from
    evt_lint.py's IR of its unit, built with the version's own Aurora build's flags (build-pc-<jp|us|eu>-aurora:
    a script with region #ifs has other words on JP than on US). None when it has no PC-only lines."""
    text = open(os.path.join(ROOT, src), encoding="utf-8", errors="replace").read()
    m = re.search(r"\bEVT_BEGIN\(\s*" + re.escape(name) + r"\s*\)(.*?)\bEVT_END", text, re.S)
    if not m or "TARGET_PC" not in m.group(1):
        return None
    import difflib
    import pickle
    import portversion
    build_name = f"build-pc-{portversion.SHORT[version]}-aurora"
    if re.search(r"#\s*if.*\bVERSION\b", m.group(1)) and not os.path.exists(
            os.path.join(ROOT, build_name, "compile_commands.json")):
        raise Error(f"{name} has PC-only commands and region #ifs: configure {build_name} to line them up")
    if not os.path.exists(os.path.join(ROOT, build_name, "compile_commands.json")):
        build_name = "build-pc-us-aurora"  # (no region #if in the script: the US build's words are the same)
    cache = os.path.join(ROOT, build_name, "evt_lint", src.replace("/", "__") + ".pkl")
    build = os.path.join(ROOT, build_name)
    if os.path.exists(os.path.join(build, "compile_commands.json")):
        # that unit's IR, as evt_lint.py makes it (kept in its cache while the source is unchanged)
        import evt_lint
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        for rel, orig, e in evt_lint.tu_list(build):
            if rel == src:
                evt_lint.build_one((rel, orig, e, os.path.dirname(cache)))
    if not os.path.exists(cache):
        raise Error(f"{name} has PC-only commands: configure {build_name} to line them up")
    g = pickle.load(open(cache, "rb"))["globals"].get(name)
    if not g or g[0] != "words":
        raise Error(f"{name} is not in evt_lint's cache")
    pcw = g[1]
    pc_ops, i = [], 0
    while i < len(pcw) and isinstance(pcw[i], int):
        pc_ops.append((i, pcw[i] & 0xFFFFFFFF))
        if pcw[i] & 0xFFFF == 1:
            break
        i += 1 + ((pcw[i] >> 16) & 0xFFFF)
    data = open(os.path.join(disc, *(["sys", "main.dol"] if file == "main.dol" else ["files", file])), "rb").read()
    gc_ops = _gc_ops(data, start)
    k = max(j for j, (w, h) in enumerate(gc_ops) if w <= gc_word)
    slot = gc_word - gc_ops[k][0]
    sm = difflib.SequenceMatcher(None, [h for _, h in gc_ops], [h for _, h in pc_ops], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal" and i1 <= k < i2:
            return pc_ops[j1 + (k - i1)][0] + slot
    raise Error(f"word {gc_word} of {name} is in a command Paper Cut changed")


def _symbol(version, rel, name, within, kind, src):
    if kind == "function":
        return {"kind": "code", "name": name, "pc_offset": within, "gc_width": 0, "pc_width": 0}
    stem = os.path.splitext(os.path.basename(src))[0] if src else None
    qualified = f"{stem}::{name}" if stem else name
    if name in scripts():
        if within % 4:
            raise Error(f"inside a word of the script {name}")
        return {"kind": "script", "name": qualified, "pc_offset": within // 4 * 8, "gc_width": 4, "pc_width": 8,
                "word": within // 4, "export": export_name(version, name, src)}
    if src is None:
        raise Error(f"no source file for {name}")
    typ = variable_type(src, name, version, rel)
    pc_off, gw, pw = map_data(typ, within, layouts(src, version, rel, False), layouts(src, version, rel, True))
    return {"kind": "data", "name": qualified, "pc_offset": pc_off, "gc_width": gw, "pc_width": pw, "type": typ,
            "export": export_name(version, name, src)}


@functools.lru_cache(None)
def statics(version):
    """{source path: set of its statics} (port/statics_<version>.txt)."""
    out = {}
    path = os.path.join(ROOT, "port", f"statics_{version}.txt")
    if os.path.exists(path):
        for ln in open(path, encoding="utf-8"):
            if ln.strip() and not ln.startswith("#"):
                parts = ln.split()
                out[parts[0]] = set(parts[1:])
    return out


def export_name(version, name, src):
    """The name the PC build exports a symbol by: a file's static through
    its pointer (port_static__...), a REL's with its prefix."""
    rel = src.split("/")[1] if src and src.startswith("rels/") else None
    if src and name in statics(version).get(src, ()):
        sym = "port_static__" + re.sub(r"[^A-Za-z0-9_]", "_", src[:-2]) + "__" + name
    else:
        sym = name
    return (f"rel_{rel}__" if rel else "") + sym, bool(src and name in statics(version).get(src, ()))


def dol_offset(disc, address):
    """main.dol's file offset of a GameCube address in its data or code."""
    data = open(os.path.join(disc, "sys", "main.dol"), "rb").read(0x100)
    offs = struct.unpack(">18I", data[0:72])
    addrs = struct.unpack(">18I", data[72:144])
    sizes = struct.unpack(">18I", data[144:216])
    for i in range(18):
        if sizes[i] and addrs[i] <= address < addrs[i] + sizes[i]:
            return offs[i] + address - addrs[i]
    return None  # .bss and above: no file offset


def translate_address(version, disc, address):
    """translate() for a GameCube address (a DOL symbol's, .bss too)."""
    if address < 0x80005000:
        return {"kind": "lowmem", "name": "portGcLowMem", "pc_offset": address - 0x80000000,
                "gc_width": 0, "pc_width": 0}
    off = dol_offset(disc, address)
    if off is not None:
        return translate(version, disc, "main.dol", off)
    for sec in (".bss", ".sbss", ".sbss2"):
        try:
            name, within, kind, src = find_symbol(version, None, sec, address)
        except Error:
            continue
        return _symbol(version, None, name, within, kind, src)
    raise Error(f"no symbol at 0x{address:08X}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="G8ME01")
    ap.add_argument("--disc", default=None, help="the extracted disc (sys/, files/); default orig/<version>")
    ap.add_argument("offsets", nargs="+", help="main.dol:0x400, rel/gor.rel:0x2C624, or an address: 0x803DB890")
    a = ap.parse_args()
    disc = a.disc or os.path.join(ROOT, "orig", a.version)
    bad = 0
    for item in a.offsets:
        file, _, off = item.rpartition(":")
        try:
            t = translate(a.version, disc, file, int(off, 0)) if file else translate_address(a.version, disc, int(off, 0))
            extra = f" (word {t['word']})" if "word" in t else f" ({t.get('type', '')})" if t["kind"] == "data" else ""
            print(f"{item} {t['kind']} {t['name']} +0x{t['pc_offset']:X}{extra}")
        except Error as e:
            bad += 1
            print(f"{item} ? {e}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
