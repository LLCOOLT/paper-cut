#!/usr/bin/env python3
"""Compare the port's frames with the original game's in Dolphin.

For each map: Dolphin runs the user's disc with a Gecko code that, at the
title (or the opening demo), does what the port's $TTYD_MAP warp does
(port/src/common/trace_pc.c): a new game at story $STORY (stg0_00_init,
swInit, swByteSet(0, story)) and a map change to MAP (seqSetSeq). Dolphin
dumps its frames (every 10th kept); the port's Aurora build captures the
map's frame after the same warp (map_sweep --shots, no partner: the Gecko
code adds none). The Dolphin frame that matches the port's best is found,
and the difference reported: a large one is a frame to look at (a model
the wrong size, things in the wrong place, something missing), written
side by side into the output folder.

  dolphin_compare.py --version G8MJ01 --disc ISO --port EXE [--only MAPS]
                     [--story N] [--out DIR] [-j N]

Dolphin: /Applications/Dolphin.app (or $DOLPHIN), run with its own user
folder (the user's settings untouched), batch mode, uncapped speed.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_sweep  # noqa: E402

DOLPHIN = os.environ.get("DOLPHIN", "/Applications/Dolphin.app/Contents/MacOS/Dolphin")
AS = os.path.join(ROOT, "build", "binutils", "powerpc-eabi-as")
OBJCOPY = os.path.join(ROOT, "build", "binutils", "powerpc-eabi-objcopy")


def symbols(version):
    want = {"now_seq", "next_seq", "seqSetSeq", "swByteSet", "swInit", "stg0_00_init", "gp", "seqMain"}
    out = {}
    for line in open(os.path.join(ROOT, "config", version, "symbols.txt")):
        name = line.split(" ", 1)[0]
        if name in want:
            out[name] = int(line.split(":0x", 1)[1].split(";", 1)[0], 16)
    return out


def gecko(version, map_name, bero, story, calls=True):
    """The warp as a Gecko C2 code: inserted at seqMain's start, so it runs on
    the game's main thread once a frame (a C0 code runs from the VI interrupt,
    where calling into the game, or changing the sequence, hangs it)."""
    s = symbols(version)

    def hi(v):
        return (v >> 16) + (1 if v & 0x8000 else 0)

    def lo(v):
        return v & 0xFFFF if v & 0xFFFF < 0x8000 else (v & 0xFFFF) - 0x10000

    def call(name):
        return (f"lis 12, {s[name] >> 16}\n ori 12, 12, {s[name] & 0xFFFF}\n mtctr 12\n bctrl\n")

    setup = (call('stg0_00_init') + call('swInit') + f" li 3, 0\n li 4, {story}\n" + call('swByteSet')) if calls else ""

    def string16(text):
        b = text.encode()[:15]
        return b + b"\0" * (16 - len(b))

    asm = f"""
 .section .text
 # once: at the title, or in the opening demo's map (dmo_*), not after asking
 lis 12, {hi(s['now_seq'])}
 lwz 0, {lo(s['now_seq'])}(12)
 cmpwi 0, 1
 beq go
 cmpwi 0, 2
 bne done
 lis 12, {hi(s['gp'])}
 lwz 11, {lo(s['gp'])}(12)
 lwz 0, 0x12C(11)
 lis 12, 0x646D
 ori 12, 12, 0x6F5F
 cmpw 0, 12
 bne done
go:
 lis 12, {hi(s['next_seq'])}
 lwz 0, {lo(s['next_seq'])}(12)
 cmpwi 0, 3
 beq done
 mflr 0
 stwu 1, -0x20(1)
 stw 0, 0x24(1)
{setup}
 bl strings
 .byte {','.join(str(c) for c in string16(map_name))}
 .byte {','.join(str(c) for c in string16(bero or ''))}
strings:
 mflr 4
 addi 5, 4, 16
 li 3, 3
 {'' if bero else 'li 5, 0'}
{call('seqSetSeq')}
 lwz 0, 0x24(1)
 addi 1, 1, 0x20
 mtlr 0
done:
 stwu 1, -0x10(1)  # seqMain's first instruction, replaced by the hook
"""
    tmp = tempfile.mkdtemp(prefix="gecko-")
    try:
        with open(os.path.join(tmp, "w.s"), "w") as f:
            f.write(asm)
        subprocess.run([AS, "-mregnames", "-o", os.path.join(tmp, "w.o"), os.path.join(tmp, "w.s")], check=True)
        subprocess.run([OBJCOPY, "-O", "binary", "-j", ".text", os.path.join(tmp, "w.o"), os.path.join(tmp, "w.bin")],
                       check=True)
        code = open(os.path.join(tmp, "w.bin"), "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    # the codehandler puts its branch back in the last word
    code += b"\x00\x00\x00\x00" if len(code) % 8 else b"\x60\x00\x00\x00\x00\x00\x00\x00"
    lines = [f"C2{s['seqMain'] & 0x1FFFFFF:06X} {len(code) // 8:08X}"]
    for i in range(0, len(code), 8):
        lines.append(f"{code[i:i + 4].hex().upper()} {code[i + 4:i + 8].hex().upper()}")
    return lines


def png_load(path):
    """(w, h, rows of RGB bytes); 8-bit RGB or RGBA, non-interlaced."""
    d = open(path, "rb").read()
    i, idat, w, h, ctype = 8, b"", 0, 0, 2
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t = d[i + 4:i + 8]
        c = d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", c[:8])
            ctype = c[9]
        elif t == b"IDAT":
            idat += c
        i += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line) if bpp == 3 else bytes(v for k, v in enumerate(line) if k % 4 != 3))
        prev = line
    return w, h, rows


def thumb(path, size=(80, 60)):
    """A small grayscale version: comparing those finds the matching frame."""
    w, h, rows = png_load(path)
    tw, th = size
    out = []
    for ty in range(th):
        y = ty * h // th
        r = rows[y]
        for tx in range(tw):
            x = tx * w // tw
            out.append((r[3 * x] * 3 + r[3 * x + 1] * 6 + r[3 * x + 2]) // 10)
    return out


def distance(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def run_dolphin(version, disc, item, work, seconds):
    name, map_name, bero, story = item
    user = os.path.join(work, "user")
    os.makedirs(os.path.join(user, "GameSettings"), exist_ok=True)
    with open(os.path.join(user, "GameSettings", version + ".ini"), "w") as f:
        f.write("[Gecko]\n$warp\n" + "\n".join(gecko(version, map_name, bero, story)) + "\n[Gecko_Enabled]\n$warp\n")
    frames = os.path.join(user, "Dump", "Frames")
    os.makedirs(frames, exist_ok=True)
    cmd = [DOLPHIN, "-u", user, "-b", "-e", disc,
           "-C", "Dolphin.Core.EnableCheats=True",
           "-C", "Dolphin.Core.EmulationSpeed=0",
           "-C", "Dolphin.Movie.DumpFrames=True",
           "-C", "Dolphin.Movie.DumpFramesSilent=True",
           "-C", "GFX.Settings.DumpFramesAsImages=True",
           "-C", "GFX.Settings.PNGCompressionLevel=0",
           "-C", "GFX.Settings.InternalResolution=1",
           "-C", "Dolphin.Display.RenderToMain=False",
           "-C", "Dolphin.Interface.ConfirmStop=False",
           "-C", "Dolphin.Analytics.PermissionAsked=True"]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    kept = []
    end = time.time() + seconds
    while time.time() < end and proc.poll() is None:
        time.sleep(0.5)
        # keep every 10th frame dumped (a frame dump is half a megabyte)
        for fn in sorted(os.listdir(frames)):
            if not fn.endswith(".png"):
                continue
            path = os.path.join(frames, fn)
            try:
                idx = int("".join(ch for ch in fn if ch.isdigit()) or 0)
            except ValueError:
                continue
            if path in kept:
                continue
            if idx % 10 == 0:
                kept.append(path)
            else:
                try:
                    os.remove(path)
                except OSError:
                    pass
    proc.kill()
    proc.wait()
    return kept


def compare(item, port_shot, kept, out):
    if not os.path.exists(port_shot):
        return (item[0], None, "no port capture")
    if not kept:
        return (item[0], None, "no Dolphin frames")
    target = thumb(port_shot)
    best, best_path = None, None
    for path in kept[-120:]:  # the last 120 kept: after the warp
        try:
            d = distance(target, thumb(path))
        except Exception:
            continue
        if best is None or d < best:
            best, best_path = d, path
    if best_path is not None:
        shutil.copyfile(best_path, os.path.join(out, item[0] + "__dolphin.png"))
        shutil.copyfile(port_shot, os.path.join(out, item[0] + "__port.png"))
    return (item[0], best, os.path.basename(best_path or ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="G8MJ01")
    ap.add_argument("--disc", required=True)
    ap.add_argument("--port", required=True, help="the Aurora build's executable")
    ap.add_argument("--only", default="")
    ap.add_argument("--story", type=int, default=0)
    ap.add_argument("--out", default=os.path.join(tempfile.gettempdir(), "dolphin_compare"))
    ap.add_argument("--seconds", type=int, default=90, help="how long Dolphin runs per map")
    ap.add_argument("-j", type=int, default=2)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    data = os.path.join(ROOT, "data", a.version)
    names = map_sweep.maps_of(os.path.join(data, "files"))
    if a.only:
        names = [n for n in names if n.startswith(tuple(a.only.split(",")))]
    items = [(n, n, map_sweep.first_bero(n), a.story) for n in names]
    # the port: every map's frame, the same warp without a partner
    shots = os.path.join(a.out, "_port")
    env = dict(os.environ, TTYD_PARTY="0", TTYD_STORY=str(a.story))
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "port", "map_sweep.py"), a.port, data, "--disc", a.disc,
                    "--shots", shots, "-j", str(a.j + 2)] + (["--only", a.only] if a.only else []), env=env, check=False)
    results = []
    lock = threading.Lock()

    def one(item):
        work = tempfile.mkdtemp(prefix="dolphin-" + item[0] + "-")
        try:
            kept = run_dolphin(a.version, a.disc, item, work, a.seconds)
            r = compare(item, os.path.join(shots, item[0] + ".png"), kept, a.out)
        finally:
            shutil.rmtree(work, ignore_errors=True)
        with lock:
            results.append(r)
            print(f"{r[0]}: {'-' if r[1] is None else f'{r[1]:.1f}'} {r[2]}", flush=True)

    with ThreadPoolExecutor(a.j) as ex:
        list(ex.map(one, items))
    results.sort(key=lambda r: -(r[1] or 999))
    with open(os.path.join(a.out, "report.txt"), "w") as f:
        for r in results:
            f.write(f"{r[0]}\t{'' if r[1] is None else f'{r[1]:.1f}'}\t{r[2]}\n")
    print(f"dolphin_compare: {len(results)} maps; largest differences first in {a.out}/report.txt")


if __name__ == "__main__":
    sys.exit(main())
