#!/usr/bin/env python3
"""Port a GameCube mod's game-struct field accesses onto the decomp's names
(docs/MODDING.md, "Porting a GameCube mod").

GameCube mods (ttyd-tools, Practice Codes, Infinite Pit...) carry their own
copies of the game's structs: GameCube layout, their own field names. The
decomp's headers describe the same memory under other names. Both are laid
out by clang for the GameCube (powerpc-unknown-eabi), and fields are matched
by offset and size: the mod's Player.playerPosition at 0x8C is the decomp's
MarioWork.position. Then every member access in the mod's sources (clang's
AST, so `unk_8` in one struct is told from `unk_8` in another) is listed,
and with --apply rewritten to the decomp's name.

  mod_fields.py --mod DIR [--include include] [--define TTYD_US]
                [--sources 'source/**/*.cpp'] [--version G8ME01]
                [--alias ModStruct=DecompStruct ...] [--report FILE] [--apply]

The mod's struct is found in the decomp under the same name, or an --alias
(as the PC build's headers name them). A field the decomp types otherwise
(its s16 for the mod's ItemId) is wrapped so the mod sees its own type,
ttydAs<ItemId[121]>(pouch->items); a part of a wider field the mod split out
(the low word of an s64) is ttydPart<T, offset>(...) on the little-endian PC
(ttyd_mod/loose.h). Accesses that don't map (a gap the decomp hasn't named, a
field the decomp splits differently) are listed in the report for porting by
hand.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_bedecode as gb
import portversion

ROOT = gb.ROOT
CLANG = os.environ.get("CLANG", "clang")


def host_includes():
    """-idirafter flags for the host's C++ library and C runtime headers
    (clang targeting Windows finds no C++ library by itself): the newest
    MSVC and Windows SDK installed. Elsewhere clang's own defaults do."""
    if os.name != "nt":
        return []
    pf = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vc = sorted(glob.glob(os.path.join(pf, "Microsoft Visual Studio", "*", "*", "VC", "Tools", "MSVC", "*", "include")))
    ucrt = sorted(glob.glob(os.path.join(pf, "Windows Kits", "10", "Include", "*", "ucrt")))
    return [f for d in (vc[-1:] + ucrt[-1:]) for f in ("-idirafter", d)]

# Minimal C++ standard headers for the GameCube layout probe (no C++ library
# for powerpc here): enough for struct definitions.
STD_SHIMS = {
    "cstdint": "#include <stdint.h>\n",
    "cstddef": "#include <stddef.h>\n",
    "cstdarg": "#include <stdarg.h>\n",
    "cinttypes": "#include <stdint.h>\n",
    "cstring": "extern \"C\" { void* memcpy(void*, const void*, unsigned long); void* memset(void*, int, unsigned long);"
               " int strcmp(const char*, const char*); unsigned long strlen(const char*); }\n",
    "cstdio": "extern \"C\" int sprintf(char*, const char*, ...);\n",
    "cmath": "",
    "bit": "",
    "new": "",
    "type_traits": "namespace std { template <class T> struct remove_cv { typedef T type; }; }\n",
}


# ------------------------------------------------------------ layouts
LAYOUT_LINE = re.compile(r"^\s*(\d+)(:\d+-\d+)? \|( +)(.*)$")


BITFIELD = "<bit-field>"
UNNAMED = re.compile(r"^\(unnamed at (.+):(\d+):\d+\)$")


def typedef_name(name, base):
    """`typedef struct { ... } Vec;` is "(unnamed at FILE:LINE:COL)" in a
    layout dump: its typedef's name, read from the file (the closing brace's
    declarator), so it can be matched and aliased like a named struct."""
    m = UNNAMED.match(name)
    if not m:
        return name
    try:
        lines = open(os.path.join(base, m.group(1)), encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return name
    depth = 0
    for ln in lines[int(m.group(2)) - 1:]:
        for i, ch in enumerate(ln):
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    t = re.match(r"\s*(?:__attribute__\(\(.*?\)\)\s*)?(\w+)\s*[;,]", ln[i + 1:])
                    return t.group(1) if t else name
    return name


def parse_layouts(text, base):
    """{record name: {"size": n, "fields": [(depth, offset, type, name)]}} from
    -fdump-record-layouts-complete, every depth (nested records expanded);
    base: the directory the dump's file names are relative to."""
    out = {}
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        lines = block.strip("\n").splitlines()
        m = LAYOUT_LINE.match(lines[0])
        if not m:
            continue
        head = m.group(4).strip()
        mh = re.match(r"^(?:struct|class|union) (.+)$", head)
        if not mh:
            continue
        name = typedef_name(mh.group(1).strip(), base)
        # a namespace's record (ttyd::hitdrv::HitCheckQuery) by its own name,
        # as the AST's member accesses name it
        if "(" not in name and "::" in name:
            name = name.rsplit("::", 1)[1]
        rec = {"size": None, "fields": []}
        for ln in lines[1:]:
            ms = re.search(r"\[sizeof=(\d+)", ln)
            if ms:
                rec["size"] = int(ms.group(1))
                break
            mf = LAYOUT_LINE.match(ln)
            if not mf:
                continue
            depth = (len(mf.group(3)) - 1) // 2
            body = mf.group(4).strip()
            if " " not in body:
                continue
            if re.search(r"\((?:anonymous|unnamed) at [^)]*\)$", body):
                typ, fname = body, ""  # an anonymous struct or union: its fields are the record's
            else:
                typ, fname = body.rsplit(" ", 1)
            # a bit-field's type: never cast (no reference to a bit-field)
            rec["fields"].append((depth, int(mf.group(1)), BITFIELD if mf.group(2) else typ.strip(), fname))
        if name not in out:
            out[name] = rec
    return out


def flatten(rec):
    """[(path, offset, size)] for every field at every depth, size from the
    next field at the same or a shallower depth (or the record's end)."""
    fields = rec["fields"]
    out = []
    stack = []
    for i, (depth, off, typ, name) in enumerate(fields):
        del stack[depth - 1:]
        stack.append(name)
        if not name:
            continue  # an anonymous member itself: only its fields have a path
        end = rec["size"]
        for d2, o2, _, _ in fields[i + 1:]:
            if d2 <= depth:
                end = o2
                break
        out.append((".".join(n for n in stack if n), off, max(0, end - off), typ))
    return out


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace")


def mod_layouts(mod, includes, defines, tmp):
    shim = os.path.join(tmp, "stdshim")
    os.makedirs(shim, exist_ok=True)
    for name, text in STD_SHIMS.items():
        open(os.path.join(shim, name), "w").write(text)
    headers = []
    for inc in includes:
        for sub in ("ttyd", "gc"):
            headers += sorted(glob.glob(os.path.join(mod, inc, sub, "**", "*.h"), recursive=True))
    probe = os.path.join(tmp, "mod_probe.cpp")
    # Each struct laid out on demand (a sizeof): -fdump-record-layouts-complete
    # lays a struct out before a trailing __attribute__((packed)) applies, and
    # the mods' structs are written that way. A name that isn't a global type
    # is an error; the rest still dump.
    names = set()
    for h in headers:
        text = open(h, encoding="utf-8", errors="replace").read()
        # a struct in a namespace by its qualified name (ttyd::windowdrv::Window)
        spaces = []
        for m in re.finditer(r"\bnamespace\s+([\w:]+)\s*\{", text):
            depth, i = 1, m.end()
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            spaces.append((m.start(), i, m.group(1)))
        for m in re.finditer(r"^\s*(?:typedef\s+)?(?:struct|union|class)\s+(\w+)\s*(?:final\s*)?(?::[^;{]*)?\{",
                             text, re.M):
            names.add("::".join([n for a, b, n in spaces if a < m.start() < b] + [m.group(1)]))
    with open(probe, "w") as f:
        for h in headers:
            f.write(f'#include "{h}"\n')
        for n in sorted(names):
            f.write(f"static_assert(sizeof({n}) > 0);\n")
    cmd = [CLANG, "-x", "c++", "-std=gnu++20", "-fsyntax-only", "-w", "-target", "powerpc-unknown-eabi",
           "-nostdlibinc", "-isystem", shim, *[f"-I{os.path.join(mod, i)}" for i in includes],
           *[f"-D{d}" for d in defines], "-ferror-limit=0", "-Xclang", "-fdump-record-layouts", probe]
    r = run(cmd, mod)
    return parse_layouts(r.stdout, mod), r.stderr


def pc_records(version, tmp):
    """The struct names the PC build's headers define (a record's tag, or a
    typedef naming a record): a mod builds against these, so only these
    replace its own copies (the GameCube SDK has some the PC build doesn't)."""
    gb.VERSION_ARGS[:] = portversion.defines(version)[:2]
    probe = os.path.join(tmp, "pc_records.c")
    with open(probe, "w") as f:
        for p in sorted(glob.glob(os.path.join(ROOT, "include", "**", "*.h"), recursive=True)):
            f.write(f'#include "{os.path.relpath(p, os.path.join(ROOT, "include")).replace(os.sep, "/")}"\n')
        f.write('#include "dolphin/gx.h"\n')
    r = run([CLANG, "-fsyntax-only", "-w", *gb.NATIVE_ARGS, *gb.VERSION_ARGS, "-ferror-limit=0",
             "-Xclang", "-ast-dump", "-fno-color-diagnostics", probe], ROOT)
    names = set()
    for ln in r.stdout.splitlines():
        m = re.search(r"RecordDecl 0x[0-9a-f]+ .*? (?:struct|union) (\w+) definition\b", ln)
        if m:
            names.add(m.group(1))
        m = re.search(r"TypedefDecl 0x[0-9a-f]+ .*? (\w+) '(?:struct|union) [^']*'", ln)
        if m:
            names.add(m.group(1))
    return names


def decomp_layouts(version, tmp):
    gb.VERSION_ARGS[:] = portversion.defines(version)[:2]
    # the game's headers and the SDK's (both on the include path)
    roots = [os.path.join(ROOT, "include"), os.path.join(ROOT, "libs", "dolsdk2004", "include")]
    headers = []
    for top in roots:
        headers += sorted(os.path.relpath(p, top).replace(os.sep, "/")
                          for p in glob.glob(os.path.join(top, "**", "*.h"), recursive=True)
                          if not os.path.relpath(p, top).startswith("libc"))
    # Some headers don't stand alone: drop the ones clang chokes on, and retry.
    bad = set()
    for _ in range(12):
        probe = os.path.join(tmp, "decomp_probe.c")
        with open(probe, "w") as f:
            for h in headers:
                if h not in bad:
                    f.write(f'#include "{h}"\n')
        cmd = [CLANG, "-fsyntax-only", "-w", *gb.DISC_ARGS, *gb.VERSION_ARGS, "-ferror-limit=0",
               "-Xclang", "-fdump-record-layouts-complete", probe]
        r = run(cmd, ROOT)
        if r.returncode == 0:
            break
        newly = set()
        for m in re.finditer(r"([\w/\\.-]+\.h):\d+:\d+: error", r.stderr):
            for top in roots:
                rel = os.path.relpath(os.path.join(ROOT, m.group(1)), top).replace(os.sep, "/")
                if rel in headers:
                    newly.add(rel)
        if not newly:
            break
        bad |= newly
    return parse_layouts(r.stdout, ROOT), sorted(bad)


# --------------------------------------------------------------- AST
def ast_accesses(mod, includes, defines, src):
    """Member accesses in src (and only there): (offset, length, name,
    record, field)."""
    cmd = [CLANG, "-x", "c++", "-std=gnu++23", "-fsyntax-only", "-w", "-fno-exceptions", "-fno-rtti",
           *[f"-I{os.path.join(mod, i)}" for i in includes], *[f"-D{d}" for d in defines],
           *host_includes(), "-Xclang", "-ast-dump=json", src]
    r = run(cmd, mod)
    try:
        ast = json.loads(r.stdout)
    except json.JSONDecodeError:
        return [], f"{src}: no AST"
    fields = {}   # FieldDecl id -> (record, field)
    out = []
    cur = {"file": None}
    mod_dir = os.path.normcase(os.path.abspath(mod)) + os.sep

    # clang's JSON names a location's file only when it differs from the last
    # one printed, in printing order: a node's loc, its range's begin and end
    # (each a spelling and an expansion location within a macro), then its
    # children. Follow the same order; return (file, offset, length) of the
    # spelled token.
    def track(loc):
        if not isinstance(loc, dict):
            return None
        spelled = None
        for part in ([loc["spellingLoc"], loc["expansionLoc"]] if "spellingLoc" in loc else [loc]):
            if "file" in part:
                cur["file"] = part["file"]
            if spelled is None and "offset" in part:
                spelled = (cur["file"], part["offset"], part.get("tokLen", 0))
        return spelled

    def walk(node, record):
        if not isinstance(node, dict):
            return
        track(node.get("loc"))
        rng = node.get("range")
        begin = end = None
        if isinstance(rng, dict):
            begin = track(rng.get("begin"))
            end = track(rng.get("end"))
        kind = node.get("kind")
        if kind in ("CXXRecordDecl", "RecordDecl") and node.get("name"):
            record = node["name"]
        if kind == "FieldDecl" and record:
            fields[node["id"]] = (record, node.get("name"))
        ref = node.get("referencedDecl") if kind == "DeclRefExpr" else None
        if isinstance(ref, dict) and ref.get("kind") == "FieldDecl" and end is not None:
            # Struct::field outside a member access (sizeof(PouchData::items)):
            # renamed, never cast (it isn't an object)
            here, offset, length = end
            path = os.path.normcase(os.path.abspath(os.path.join(mod, here))) if here else None
            if path and path.startswith(mod_dir):
                out.append((path, offset, length or len(ref.get("name", "")), ref.get("name"), ref.get("id"), None))
        if kind == "MemberExpr" and end is not None:
            # the member's name is the range's end token; any file of the mod
            # (its headers' inline functions too)
            here, offset, length = end
            path = os.path.normcase(os.path.abspath(os.path.join(mod, here))) if here else None
            # the whole expression's start, when it's spelled in the same file
            start = begin[1] if begin and begin[0] == here and begin[1] <= offset else None
            if path and path.startswith(mod_dir):
                out.append((path, offset, length or len(node.get("name", "")), node.get("name"),
                            node.get("referencedMemberDecl"), start))
        for child in node.get("inner", []) or []:
            walk(child, record)

    sys.setrecursionlimit(100000)
    walk(ast, None)
    return [(p, o, n, name, *fields.get(fid, (None, None)), st) for p, o, n, name, fid, st in out], None


# --------------------------------------------------------------- map
def map_field(mod_rec, dec_rec, field):
    """(the decomp's path for the mod's field (same offset and size), the
    mod's type, the decomp's type), or None."""
    mf = {p: (o, s, t) for p, o, s, t in flatten(mod_rec) if "." not in p}
    if field not in mf:
        return None
    off, size, mtyp = mf[field]
    dec = flatten(dec_rec)
    # the shallowest field of the same offset and size; of a union's
    # members, the one typed like the mod's (lwData, not its float view)
    same = [(path.count("."), canon(t, {}) != canon(mtyp, {}), i, path, t)
            for i, (path, o, s, t) in enumerate(dec) if o == off and s == size]
    if same:
        _, _, _, path, t = min(same)
        return (path, mtyp, t)
    # the same type at the same offset (a size from the next field's offset
    # counts the padding after it, which the two may place differently)
    for path, o, s, t in dec:
        if o == off and canon(t, {}) == canon(mtyp, {}) and t != BITFIELD:
            return (path, mtyp, t)
    # an element of an array, or bytes inside one (a pad the decomp hasn't
    # named): that element, seen as the mod's type
    for path, o, s, t in dec:
        am = re.match(r"^(.*)\[(\d+)\]$", t)
        if am and o <= off and off + size <= o + s and int(am.group(2)):
            esize = s // int(am.group(2))
            if esize and (off - o) % esize == 0:
                elem = f"{path}[{(off - o) // esize}]"
                return (elem, mtyp, am.group(1) if esize == size else f"<{size} bytes>")
    # part of a larger integer (the low word of an s64, as the mod split it):
    # the same bytes on a little-endian PC are at the other end
    for path, o, s, t in dec:
        width = SCALAR_SIZE.get(canon(t, {}))
        if width and o <= off and off + size <= o + width and size < width:
            return (path, mtyp, f"<part {width - (off - o) - size}>")
    return None


SCALAR_SIZE = {"uint8_t": 1, "int8_t": 1, "uint16_t": 2, "int16_t": 2, "uint32_t": 4, "int32_t": 4,
               "uint64_t": 8, "int64_t": 8, "float": 4, "double": 8, "OSTime": 8, "OSTick": 4}


SAME_INT = {"u8": "uint8_t", "unsigned char": "uint8_t", "s8": "int8_t", "signed char": "int8_t",
            "u16": "uint16_t", "unsigned short": "uint16_t", "s16": "int16_t", "short": "int16_t",
            "u32": "uint32_t", "unsigned int": "uint32_t", "s32": "int32_t", "int": "int32_t",
            "u64": "uint64_t", "unsigned long long": "uint64_t", "s64": "int64_t", "long long": "int64_t",
            "f32": "float", "f64": "double",
            "EvtWord": "int32_t"}  # a script word (a pointer's width on the PC; 4 bytes here)


def canon(typ, aliases):
    """A field's type, spelled so the mod's and the decomp's compare equal
    when they're the same type: aliases applied, integer typedefs folded."""
    t = re.sub(r"\b(?:struct|union|enum|class)\s+", "", typ)
    for name, target in aliases.items():
        t = re.sub(rf"\b{re.escape(name)}\b", target, t)
    for name, target in sorted(SAME_INT.items(), key=lambda kv: -len(kv[0])):
        t = re.sub(rf"(?<![\w ]){re.escape(name)}(?!\w)", target, t)
    return re.sub(r"\s+", "", t)


def cast_type(mtyp, dtyp, aliases):
    """The mod's type to see the decomp's field as, or None when they're the
    same (or it can't be cast: a bit-field, an unnamed type)."""
    if BITFIELD in (mtyp, dtyp) or "unnamed" in mtyp or "anonymous" in mtyp:
        return None
    if canon(mtyp, aliases) == canon(dtyp, aliases):
        return None
    # without the elaborated keyword: the mod's struct may be an alias of the
    # decomp's now, which `struct X` can't name
    return re.sub(r"\b(?:struct|union|enum|class)\s+", "", mtyp)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mod", required=True)
    ap.add_argument("--include", action="append", default=None)
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--sources", default="source/**/*.cpp")
    ap.add_argument("--version", default="G8ME01")
    ap.add_argument("--alias", action="append", default=[], help="ModStruct=DecompStruct")
    ap.add_argument("--report", default=None)
    ap.add_argument("--apply", action="store_true")
    a = ap.parse_args()
    a.mod = os.path.abspath(a.mod)
    includes = a.include or ["include"]
    aliases = dict(x.split("=", 1) for x in a.alias)
    with tempfile.TemporaryDirectory() as tmp:
        mod_lay, mod_err = mod_layouts(a.mod, includes, a.define, tmp)
        dec_lay, bad = decomp_layouts(a.version, tmp)
        pc = pc_records(a.version, tmp)
        dec_lay = {k: v for k, v in dec_lay.items() if k in pc}
    print(f"mod structs: {len(mod_lay)}; decomp structs: {len(dec_lay)} ({len(bad)} headers skipped)", flush=True)

    sources = sorted(glob.glob(os.path.join(a.mod, a.sources), recursive=True))
    per_file = {}
    stats = {}   # record -> [mapped, renamed, unmapped]
    unmapped = {}  # (record, field) -> [locations]
    missing_rec = {}
    seen = set()  # (file, offset): a header's access is seen by every source including it
    texts = {}
    for src in sources:
        acc, err = ast_accesses(a.mod, includes, a.define, src)
        if err:
            print(err)
            continue
        for file, off, n, name, record, field, start in acc:
            if record is None or record not in mod_lay or (file, off) in seen:
                continue  # not a game struct (the mod's own classes), or done
            seen.add((file, off))
            if file not in texts:
                texts[file] = open(file, "rb").read()  # clang gives byte offsets
            if texts[file][off:off + n] != field.encode():
                continue  # an implicit member's (a defaulted copy's), at the struct's name
            dec_name = aliases.get(record, record)
            st = stats.setdefault(record, [0, 0, 0, 0])
            if dec_name not in dec_lay:
                missing_rec.setdefault(record, 0)
                missing_rec[record] += 1
                continue
            hit = map_field(mod_lay[record], dec_lay[dec_name], field)
            line = texts[file].count(b"\n", 0, off) + 1
            if hit is None:
                st[2] += 1
                unmapped.setdefault((record, field), []).append(f"{os.path.relpath(file, a.mod)}:{line}")
                continue
            path, mtyp, dtyp = hit
            st[0] += 1
            edits = per_file.setdefault(file, [])
            if path != field:
                st[1] += 1
                edits.append((off, -1, n, path))
            # the decomp's field typed otherwise (s16 for the mod's ItemId, Vec
            # for its Vec3): seen as the mod's type, so the mod's code is as
            # it was written (ttyd_mod/loose.h's ttydAs, ttydPart)
            part = re.match(r"<part (\d+)>$", dtyp)
            want = mtyp if part else cast_type(mtyp, dtyp, aliases)
            # an expression, not a declaration the AST ranged over (a struct's
            # own definition, an initializer list)
            if start is not None and any(c in texts[file][start:off] for c in b"{};"):
                start = None
            if want is not None and start is not None:
                st[3] += 1
                end = off + n
                edits.append((end, -1, 0, ")"))
                edits.append((start, end - start, 0,
                              f"ttydPart<{want}, {part.group(1)}>(" if part else f"ttydAs<{want}>("))
    # Struct::field and offsetof(Struct, field) in the text: clang folds some
    # (an array bound's sizeof, offsetof) before its AST shows the reference
    qual = re.compile(rb"\b(\w+)::(\w+)\b|\boffsetof\s*\(\s*(?:struct\s+)?(\w+)\s*,\s*(\w+)")
    files_all = set(sources) | {os.path.normcase(os.path.abspath(p)) for inc in includes
                                for p in glob.glob(os.path.join(a.mod, inc, "**", "*.h"), recursive=True)}
    for file in sorted(files_all):
        file = os.path.normcase(os.path.abspath(file))
        if file not in texts:
            texts[file] = open(file, "rb").read()
        for m in qual.finditer(texts[file]):
            record = (m.group(1) or m.group(3)).decode()
            field = (m.group(2) or m.group(4)).decode()
            at = m.start(2) if m.group(2) else m.start(4)
            if record not in mod_lay or (file, at) in seen:
                continue
            dec_name = aliases.get(record, record)
            hit = map_field(mod_lay[record], dec_lay[dec_name], field) if dec_name in dec_lay else None
            if hit is None:
                continue
            seen.add((file, at))
            if hit[0] != field:
                per_file.setdefault(file, []).append((at, -1, len(field), hit[0]))
    if a.apply:
        for file, edits in per_file.items():
            text = texts[file]
            # from the end; at one position, an inner expression's edits
            # before an outer's (each insertion lands left of the last)
            for pos, _, n, new in sorted(set(edits), key=lambda e: (-e[0], e[1])):
                text = text[:pos] + new.encode() + text[pos + n:]
            with open(file, "wb") as f:
                f.write(text)

    lines = ["# Field accesses: the mod's names onto the decomp's", ""]
    lines.append("| Mod struct | Decomp struct | Mapped | Renamed | Cast | Unmapped |")
    lines.append("|---|---|---|---|---|---|")
    for rec, (m, r, u, c) in sorted(stats.items(), key=lambda kv: -sum(kv[1])):
        lines.append(f"| {rec} | {aliases.get(rec, rec) if rec not in missing_rec else '(none)'} | {m} | {r} | {c} | {u} |")
    if missing_rec:
        lines += ["", "## Mod structs with no decomp struct of that name (give --alias)", ""]
        for rec, n in sorted(missing_rec.items(), key=lambda kv: -kv[1]):
            size = mod_lay.get(rec, {}).get("size")
            cands = [d for d, r in dec_lay.items() if r["size"] == size and size]
            lines.append(f"- `{rec}` (0x{size or 0:X} bytes, {n} accesses): same size: {', '.join(cands[:8]) or '-'}")
    if unmapped:
        lines += ["", "## Accesses with no decomp field of the same offset and size", ""]
        for (rec, field), locs in sorted(unmapped.items(), key=lambda kv: -len(kv[1])):
            lines.append(f"- `{rec}.{field}` ({len(locs)}): {', '.join(locs[:4])}{' ...' if len(locs) > 4 else ''}")
    report = "\n".join(lines) + "\n"
    if a.report:
        open(a.report, "w", encoding="utf-8").write(report)
    total = [sum(v[i] for v in stats.values()) for i in range(4)]
    print(f"accesses: {total[0]} mapped ({total[1]} renamed, {total[3]} cast), {total[2]} unmapped, "
          f"{sum(missing_rec.values())} on structs with no decomp match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
