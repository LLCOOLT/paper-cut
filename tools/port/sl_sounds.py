#!/usr/bin/env python3
"""Every sound and music name Super Luigi's code uses must be the game's.

The game looks a sound up by name (src/pmario_sound.c's tables, the BGM and
ENV lists); a name it doesn't know plays nothing, silently. This lists every
"SFX_..."/"BGM_..."/"ENV_..." string in port/src/aurora/superluigi_*.c (and
any other files given) that the game's tables don't have.

    python tools/port/sl_sounds.py [files...]     exit 1 if any is missing
"""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


def known_names():
    names = set()
    for path in [os.path.join(ROOT, "src", "pmario_sound.c")] + glob.glob(os.path.join(ROOT, "src", "*sound*.c")):
        with open(path, encoding="utf-8", errors="replace") as f:
            # the SFX and BGM tables ({ "NAME", ... }) and the ENV name lists
            names.update(re.findall(r'"((?:SFX|BGM|ENV)_[A-Z0-9_]+)"', f.read()))
    return names


def main(argv):
    files = argv or sorted(glob.glob(os.path.join(ROOT, "port", "src", "aurora", "superluigi_*.c")))
    known = known_names()
    missing = 0
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                for name in re.findall(r'"((?:SFX|BGM|ENV)_[A-Z0-9_]+)"', line):
                    if name not in known:
                        print(f"{os.path.relpath(path, ROOT)}:{n}: {name} isn't one of the game's")
                        missing += 1
    print(f"{missing} unknown sound name(s) in {len(files)} file(s), {len(known)} known")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
