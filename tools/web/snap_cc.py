#!/usr/bin/env python3
"""The browser build's compiler launcher for save states (CMakeLists.txt, "Save states").

On other platforms port_prefix.h puts the game's data and bss in sections of
their own (#pragma clang section ttyd_snapd/ttyd_snapb), which the save states
copy as a whole. clang ignores that pragma for WebAssembly, so this runs the
compiler and then moves the object's .data.*/.bss.* segments there itself
(tools/port/wasm_redefine_syms.py, as rel_rename.py does for the RELs), for
exactly the files the pragma is for: TTYD_SAVE_STATES and TTYD_SNAPSHOT_SECTION
defined, TTYD_NO_SNAPSHOT not. Variables marked PORT_HOST_DATA (a segment of
their own, ttyd_hostd, on the web) stay out.

usage (CMake's C_COMPILER_LAUNCHER): snap_cc.py <compiler> <args...>
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "port"))
import wasm_redefine_syms  # noqa: E402


def defined(args, name):
    return any(a == f"-D{name}" or a.startswith(f"-D{name}=") for a in args) or any(
        args[i] == "-D" and (args[i + 1] == name or args[i + 1].startswith(name + "="))
        for i in range(len(args) - 1))


def main():
    cmd = sys.argv[1:]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        return result.returncode
    args = cmd[1:]
    if not (defined(args, "TTYD_SAVE_STATES") and defined(args, "TTYD_SNAPSHOT_SECTION")) or \
            defined(args, "TTYD_NO_SNAPSHOT") or "-c" not in args or "-o" not in args:
        return 0
    obj = args[args.index("-o") + 1]
    with open(obj, "rb") as f:
        data = f.read()
    if not data.startswith(wasm_redefine_syms.WASM_MAGIC):
        return 0
    out = wasm_redefine_syms.redefine(data, {}, {".data": "ttyd_snapd", ".bss": "ttyd_snapb"})
    tmp = obj + ".snap"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, obj)
    return 0


if __name__ == "__main__":
    sys.exit(main())
