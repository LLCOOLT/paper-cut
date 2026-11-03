#!/usr/bin/env python3
"""Rename symbols in a WebAssembly object file, as `llvm-objcopy
--redefine-syms` does for other formats (llvm-objcopy cannot for wasm).

rel_rename.py uses it for web builds (TTYD_RELS_STATIC). A wasm object names
its symbols in two places:
- the "linking" section's symbol table: every defined symbol, and undefined
  ones with an explicit name (WASM_SYM_EXPLICIT_NAME);
- the import section: an undefined function, global, table or tag without an
  explicit name is named by its import's field.
References go through symbol indices (relocations), so renaming the entries
renames the definition and every reference in the object.

--data/--bss also rename the object's data segments, standing in for
`#pragma clang section data=/bss=` (which clang ignores for wasm): each
variable has a segment of its own (.data.<name>, .bss.<name>), and wasm-ld
merges same-named segments into one output segment bounded by
__start_<name>/__stop_<name>. Read-only data (.rodata.*) stays where it is,
as with the pragma.

usage: wasm_redefine_syms.py [--data SEG] [--bss SEG] MAPFILE IN OUT
       (MAPFILE: "old new" per line)
"""
import argparse

WASM_MAGIC = b"\0asm\x01\0\0\0"
SEC_CUSTOM, SEC_IMPORT = 0, 2
LINKING_SEGMENT_INFO, LINKING_SYMBOL_TABLE = 5, 8
SYM_FUNCTION, SYM_DATA, SYM_GLOBAL, SYM_SECTION, SYM_TAG, SYM_TABLE = 0, 1, 2, 3, 4, 5
FLAG_UNDEFINED, FLAG_EXPLICIT_NAME = 0x10, 0x40
# Binding: flags & 3. A common symbol (-fcommon) is a defined data symbol
# stored with size and alignment only (no segment and offset).
BINDING_MASK, BINDING_LOCAL, BINDING_COMMON = 0x03, 0x02, 0x03


def data_fields(flags):
    """How many numbers follow a defined data symbol's name."""
    return 2 if flags & BINDING_MASK == BINDING_COMMON else 3


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def byte(self):
        b = self.data[self.pos]
        self.pos += 1
        return b

    def leb(self):
        result = shift = 0
        while True:
            b = self.byte()
            result |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                return result

    def name(self):
        n = self.leb()
        s = self.data[self.pos:self.pos + n]
        self.pos += n
        return s.decode("utf-8", "surrogateescape")

    def bytes_from(self, start):
        return self.data[start:self.pos]


def leb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def name(s):
    b = s.encode("utf-8", "surrogateescape")
    return leb(len(b)) + b


def skip_limits(r):
    flags = r.byte()
    r.leb()
    if flags & 0x01:
        r.leb()
    if flags & 0x08:  # custom page size
        r.leb()


def rewrite_imports(payload, renames):
    r = Reader(payload)
    out = bytearray()
    count = r.leb()
    out += leb(count)
    for _ in range(count):
        module = r.name()
        field = r.name()
        start = r.pos
        kind = r.byte()
        if kind == 0x00:  # function: type index
            r.leb()
        elif kind == 0x01:  # table: reftype, limits
            r.byte()
            skip_limits(r)
        elif kind == 0x02:  # memory: limits
            skip_limits(r)
        elif kind == 0x03:  # global: valtype, mutability
            r.byte()
            r.byte()
        elif kind == 0x04:  # tag: attribute, type index
            r.byte()
            r.leb()
        else:
            raise SystemExit(f"unknown import kind {kind:#x}")
        if kind != 0x02:
            field = renames.get(field, field)
        out += name(module) + name(field) + r.bytes_from(start)
    if r.pos != len(payload):
        raise SystemExit("import section: trailing bytes")
    return bytes(out)


def rewrite_symbol_table(payload, renames):
    r = Reader(payload)
    out = bytearray()
    count = r.leb()
    out += leb(count)
    for _ in range(count):
        kind = r.byte()
        flags = r.leb()
        out += bytes([kind]) + leb(flags)
        defined = not flags & FLAG_UNDEFINED
        if kind in (SYM_FUNCTION, SYM_GLOBAL, SYM_TAG, SYM_TABLE):
            out += leb(r.leb())
            if defined or flags & FLAG_EXPLICIT_NAME:
                s = r.name()
                out += name(renames.get(s, s))
        elif kind == SYM_DATA:
            s = r.name()
            out += name(renames.get(s, s))
            if defined:
                start = r.pos
                for _ in range(data_fields(flags)):  # segment, offset, size
                    r.leb()
                out += r.bytes_from(start)
        elif kind == SYM_SECTION:
            out += leb(r.leb())
        else:
            raise SystemExit(f"unknown symbol kind {kind}")
    if r.pos != len(payload):
        raise SystemExit("symbol table: trailing bytes")
    return bytes(out)


def segment_name(s, segments):
    for prefix, new in segments.items():
        if new and (s == prefix or s.startswith(prefix + ".")):
            return new
    return s


def rewrite_segment_info(payload, segments):
    r = Reader(payload)
    out = bytearray()
    count = r.leb()
    out += leb(count)
    for _ in range(count):
        s = r.name()
        start = r.pos
        r.leb()  # alignment (log2)
        r.leb()  # flags
        out += name(segment_name(s, segments)) + r.bytes_from(start)
    if r.pos != len(payload):
        raise SystemExit("segment info: trailing bytes")
    return bytes(out)


def rewrite_linking(payload, renames, segments):
    r = Reader(payload)
    out = bytearray(leb(r.leb()))  # version
    while r.pos < len(payload):
        kind = r.byte()
        size = r.leb()
        sub = payload[r.pos:r.pos + size]
        r.pos += size
        if kind == LINKING_SYMBOL_TABLE:
            sub = rewrite_symbol_table(sub, renames)
        elif kind == LINKING_SEGMENT_INFO:
            sub = rewrite_segment_info(sub, segments)
        out += bytes([kind]) + leb(len(sub)) + sub
    return bytes(out)


def redefine(data, renames, segments=None):
    segments = segments or {}
    if data[:8] != WASM_MAGIC:
        raise SystemExit("not a WebAssembly object")
    r = Reader(data, 8)
    out = bytearray(WASM_MAGIC)
    while r.pos < len(data):
        sid = r.byte()
        size = r.leb()
        payload = data[r.pos:r.pos + size]
        r.pos += size
        if sid == SEC_IMPORT:
            payload = rewrite_imports(payload, renames)
        elif sid == SEC_CUSTOM:
            pr = Reader(payload)
            if pr.name() == "linking":
                payload = payload[:pr.pos] + rewrite_linking(payload[pr.pos:], renames, segments)
        out += bytes([sid]) + leb(len(payload)) + payload
    return bytes(out)


def symbols(data):
    """(kind, flags, name) of every named symbol in a wasm object: what
    rel_rename.py would ask llvm-nm for (llvm-nm crashes on some wasm objects).
    Undefined symbols named by their import report None as the name."""
    if data[:8] != WASM_MAGIC:
        raise SystemExit("not a WebAssembly object")
    r = Reader(data, 8)
    while r.pos < len(data):
        sid = r.byte()
        size = r.leb()
        end = r.pos + size
        if sid == SEC_CUSTOM and Reader(data, r.pos).name() == "linking":
            pr = Reader(data, r.pos)
            pr.name()
            pr.leb()  # version
            while pr.pos < end:
                kind = pr.byte()
                sub_end = pr.leb() + pr.pos
                if kind != LINKING_SYMBOL_TABLE:
                    pr.pos = sub_end
                    continue
                out = []
                for _ in range(pr.leb()):
                    skind = pr.byte()
                    flags = pr.leb()
                    defined = not flags & FLAG_UNDEFINED
                    sym_name = None
                    if skind in (SYM_FUNCTION, SYM_GLOBAL, SYM_TAG, SYM_TABLE):
                        pr.leb()
                        if defined or flags & FLAG_EXPLICIT_NAME:
                            sym_name = pr.name()
                    elif skind == SYM_DATA:
                        sym_name = pr.name()
                        if defined:
                            for _ in range(data_fields(flags)):
                                pr.leb()
                    else:
                        pr.leb()
                    out.append((skind, flags, sym_name))
                return out
        r.pos = end
    return []


def defined_globals(path):
    """Names of the global (non-local) symbols a wasm object defines."""
    with open(path, "rb") as f:
        syms = symbols(f.read())
    return {n for kind, flags, n in syms
            if n and kind != SYM_SECTION and not flags & FLAG_UNDEFINED
            and flags & BINDING_MASK != BINDING_LOCAL}


def read_map(path):
    renames = {}
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                renames[parts[0]] = parts[1]
    return renames


def main():
    ap = argparse.ArgumentParser(description="Rename symbols (and data segments) in a wasm object.")
    ap.add_argument("--data", help="segment for .data.* (initialised data)")
    ap.add_argument("--bss", help="segment for .bss.* (zeroed data)")
    ap.add_argument("mapfile")
    ap.add_argument("src")
    ap.add_argument("dst")
    a = ap.parse_args()
    with open(a.src, "rb") as f:
        data = f.read()
    with open(a.dst, "wb") as f:
        f.write(redefine(data, read_map(a.mapfile), {".data": a.data, ".bss": a.bss}))


if __name__ == "__main__":
    main()
