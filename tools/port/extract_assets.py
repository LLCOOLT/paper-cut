#!/usr/bin/env python3
"""Generate the asset headers the game includes (config.yml "extract:"
entries, e.g. assets/_mariost_icon_tex.inc) for the PC build.

With the user's own main.dol (orig/<version>/sys/main.dol, from their disc) the
real bytes are emitted. Without it, zero-filled placeholders are written so
the code still builds; only the memory-card icon/banner are affected.
Nothing from the disc is ever committed.

usage: extract_assets.py OUT_DIR [--version G8MJ01|G8ME01|G8MP01]
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def dol_reader(path):
    data = open(path, "rb").read()
    offs = struct.unpack(">18I", data[0x00:0x48])
    addrs = struct.unpack(">18I", data[0x48:0x90])
    sizes = struct.unpack(">18I", data[0x90:0xD8])
    def read(addr, size):
        for o, a, s in zip(offs, addrs, sizes):
            if s and a <= addr and addr + size <= a + s:
                return data[o + addr - a : o + addr - a + size]
        raise KeyError(f"0x{addr:08x} not in a DOL section")
    return read

def main():
    out = sys.argv[1]
    version = sys.argv[sys.argv.index("--version") + 1] if "--version" in sys.argv else "G8MJ01"
    cfg = open(os.path.join(ROOT, "config", version, "config.yml"), encoding="utf-8").read()
    extract = cfg.split("\nextract:", 1)[1].split("\nmodules:", 1)[0]
    entries = re.findall(r"- symbol: (\w+)\n\s+header: (\S+)(?:\n\s+header_type: (\w+))?", extract)
    symtab = open(os.path.join(ROOT, "config", version, "symbols.txt"), encoding="utf-8").read()
    dol = os.path.join(ROOT, "orig", version, "sys", "main.dol")
    read = dol_reader(dol) if os.path.exists(dol) else None
    if read is None:
        print(f"extract_assets: {dol} not found; writing zero-filled placeholders", file=sys.stderr)
    for sym, header, htype in entries:
        m = re.search(rf"^{sym} = \.\w+:0x([0-9A-Fa-f]+); // .*size:0x([0-9A-Fa-f]+)", symtab, re.M)
        addr, size = int(m.group(1), 16), int(m.group(2), 16)
        blob = read(addr, size) if read else bytes(size)
        body = ",\n".join(", ".join(f"0x{b:02X}" for b in blob[i:i + 16]) for i in range(0, size, 16))
        path = os.path.join(out, header)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            if htype == "raw":
                f.write(body + "\n")  # included inside an initializer
            else:
                f.write(f"const u8 {sym}[0x{size:X}] ATTRIBUTE_ALIGN(32) = {{\n{body}\n}};\n")

if __name__ == "__main__":
    main()
