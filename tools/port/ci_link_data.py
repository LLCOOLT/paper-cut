#!/usr/bin/env python3
"""The self-hosted runner's checkout gets this machine's disc images and
extracted data (git-ignored, never committed) as links to the ones in
$TTYD_LOCAL_DATA (a checkout that has them, set in the runner's .env;
docs/RELEASING.md): data/ as a whole, and inside orig/<version>/ (which the
repo keeps with a .gitkeep) each disc image and extracted folder. Without
them local_ci.py skips what needs a disc.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def link(target, at):
    """at -> target: a directory junction or a hard link (no administrator
    rights needed for either, unlike symbolic links on Windows)."""
    if os.path.lexists(at):
        # a symbolic link to somewhere else ($TTYD_LOCAL_DATA moved) or to
        # nothing is made again; anything else is left as it is
        if not os.path.islink(at) or (os.path.exists(at) and os.path.realpath(at) == os.path.realpath(target)):
            return "there"
        os.remove(at)
    if os.name == "nt":
        kind = "/J" if os.path.isdir(target) else "/H"
        subprocess.run(["cmd", "/c", "mklink", kind, at, target], check=True, stdout=subprocess.DEVNULL)
    else:
        os.symlink(target, at)
    return "linked"


def main():
    src = os.environ.get("TTYD_LOCAL_DATA")
    if not src:
        print("ci_link_data: $TTYD_LOCAL_DATA is not set: no disc images (stages that need them skip)")
        return 0
    pairs = [(os.path.join(src, "data"), os.path.join(ROOT, "data"))]
    for version in ("G8MJ01", "G8ME01"):
        have = os.path.join(src, "orig", version)
        if os.path.isdir(have):
            os.makedirs(os.path.join(ROOT, "orig", version), exist_ok=True)
            for name in os.listdir(have):
                if name != ".gitkeep":
                    pairs.append((os.path.join(have, name), os.path.join(ROOT, "orig", version, name)))
    for target, at in pairs:
        if not os.path.exists(target):
            print(f"ci_link_data: {target} missing")
            continue
        # (where it really is, when $TTYD_LOCAL_DATA links on: a container
        # or a service may not reach that place)
        real = os.path.realpath(target)
        where = f" (really {real})" if os.path.normcase(real) != os.path.normcase(os.path.abspath(target)) else ""
        print(f"ci_link_data: {os.path.relpath(at, ROOT)} {link(target, at)}{where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
