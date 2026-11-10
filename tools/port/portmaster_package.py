#!/usr/bin/env python3
"""The PortMaster package: Paper Cut for Linux ARM64 handhelds (docs/HANDHELDS.md).

  portmaster_package.py VERSION --build us=DIR [--build jp=DIR] [--build eu=DIR] [--out DIST]

DIR is an ARM64 build folder (cmake/toolchains/linux-arm64.cmake); the US build is needed (its
launcher starts the other versions' builds beside it), the others are optional. Writes
DIST/papercut-<version>-portmaster.zip, laid out as PortMaster installs ports:

  Paper Cut.sh              the launch script (PortMaster's control.txt conventions)
  papercut/
    ttyd-us [ttyd-jp ttyd-eu]   the builds (stripped)
    libs/libz.so.1              the zlib the builds link (Aurora's zlib-ng)
    port.json, gameinfo.xml     PortMaster's and EmulationStation's descriptions
    cover.png                   the brand's own art (docs/images/social-preview.png)
    README.md                   where the player's disc image goes, and what to expect
    games/                      the player's disc image goes here (none is ever shipped)
    LICENSE.txt, THIRD_PARTY.txt, OFL.txt

Nothing from the game is in it: the disc image is the player's own, and every executable is checked
for the disc's bytes as the desktop packages are (release.py check_no_disc_data).
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
FOLDER = "papercut"
SCRIPT = "Paper Cut.sh"
VERSIONS = {"jp": "G8MJ01", "us": "G8ME01", "eu": "G8MP01"}
DISC_EXTENSIONS = ("iso", "gcm", "ciso", "gcz", "rvz", "wia", "nfs", "tgc")

LAUNCH = r"""#!/bin/bash
# Paper Cut, a native port of Paper Mario: The Thousand-Year Door (PortMaster).
# Your own disc image goes in ports/papercut/games/ (README.md there).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/papercut"
CONFDIR="$GAMEDIR/conf"
mkdir -p "$CONFDIR" "$GAMEDIR/cache" "$GAMEDIR/games"
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Paper Cut reads the controller itself (SDL3): PortMaster's mapping for this device, no gptokeyb.
# Select + Start quits cleanly (where you are is kept for Continue); Select opens the settings.
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# the saves, settings and caches in the port's folder, the handheld settings on
export TTYD_USER_DIR="$CONFDIR"
export TTYD_CACHE_DIR="$GAMEDIR/cache"
export TTYD_HANDHELD=1
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
# no X11 or Wayland on most firmwares: SDL draws to the screen itself (KMSDRM); a desktop session's own otherwise
if [ -z "$WAYLAND_DISPLAY" ] && [ -z "$DISPLAY" ]; then
  export SDL_VIDEO_DRIVER=kmsdrm
fi

# the disc images in games/, in the launcher's list (it picks the build of the disc's version)
for disc in "$GAMEDIR"/games/*; do
  case "${disc,,}" in
    *.iso|*.gcm|*.ciso|*.gcz|*.rvz|*.wia|*.nfs|*.tgc)
      grep -qxF "$disc" "$CONFDIR/library.txt" 2>/dev/null || echo "$disc" >> "$CONFDIR/library.txt" ;;
  esac
done

$ESUDO chmod +x "$GAMEDIR"/ttyd-*
pm_platform_helper "$GAMEDIR/ttyd-us"
./ttyd-us

pm_finish
"""

README = """# Paper Cut on a handheld

Paper Cut is a native port of Paper Mario: The Thousand-Year Door. It plays from **your own
disc image**: nothing from the game comes with it.

## Your disc image

Copy your disc image (made from your own GameCube disc: `.iso`, `.gcm`, `.ciso`, `.gcz`, `.rvz`,
`.wia`, `.nfs` or `.tgc`) into

    ports/papercut/games/

Start **Paper Cut** from the Ports list. The launcher lists the discs in `games/`; pick one and
Play. The US build is always included; a Japanese or European disc needs its own build
(`ttyd-jp`, `ttyd-eu`) beside it, as the package you have may or may not carry.

## Controls

The device's own buttons are a GameCube controller (A, B, X, Y, L, R, Z on the shoulder or
the second shoulder, the sticks). **Select** opens Paper Cut's settings (Controls there changes
any button), and **Select + Start** together quits (where you are is kept: Continue in the
launcher comes back to it).

## What is set for a handheld

At the first start: OpenGL ES, the screen's own resolution and shape (4:3, or 3:2 and wider at
their own aspect), 30 frames a second (Settings > Display > Battery saver: Off for 60), and
the costly effects off (anti-aliasing, bloom, paper effects, rewind, supersampling). The saves,
settings and logs are in `ports/papercut/conf` and `log.txt`.

More: https://github.com/LLCOOLT/paper-cut/blob/main/docs/HANDHELDS.md
"""

GAMES_README = """Your own disc image of Paper Mario: The Thousand-Year Door goes here
(.iso, .gcm, .ciso, .gcz, .rvz, .wia, .nfs or .tgc). Paper Cut lists it at its next start.
"""


def port_json(version):
    return {
        "version": 2,
        "name": "papercut.zip",
        "items": [SCRIPT, FOLDER],
        "items_opt": [],
        "attr": {
            "title": "Paper Cut",
            "porter": ["LLCOOLT"],
            "desc": "A native port of Paper Mario: The Thousand-Year Door, built on its decompilation: "
                    "the whole game, with quick resume, settings for every taste and play as Luigi.",
            "inst": "Copy your own disc image of Paper Mario: The Thousand-Year Door (.iso, .rvz, ...) "
                    "into ports/papercut/games/.",
            "genres": ["rpg", "adventure"],
            "image": {"cover": "cover.png"},
            "rtr": False,
            "exp": True,
            "runtime": None,
            "reqs": [],
            "arch": ["aarch64"],
            "version": version,
            "store": [],
            "availability": "full",
        },
    }


def gameinfo(version):
    return f"""<?xml version="1.0"?>
<gameList>
  <game>
    <path>./{SCRIPT}</path>
    <name>Paper Cut</name>
    <desc>A native port of Paper Mario: The Thousand-Year Door ({version}). Needs your own disc image in ports/papercut/games/.</desc>
    <developer>LLCOOLT</developer>
    <genre>RPG</genre>
    <image>./{FOLDER}/cover.png</image>
  </game>
</gameList>
"""


def check_elf_arm64(path):
    """An ARM64 ELF executable, or a clear stop."""
    head = open(path, "rb").read(20)
    if head[:4] != b"\x7fELF" or int.from_bytes(head[18:20], "little") != 183:  # EM_AARCH64
        sys.exit(f"portmaster_package: {path} is not an ARM64 Linux executable")


def build_strip(build):
    """The strip the build's own toolchain found (CMAKE_STRIP in its CMakeCache.txt): the cross
    toolchain's llvm-strip, which LLVM's Windows installer doesn't put on PATH."""
    try:
        with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("CMAKE_STRIP:"):
                    found = line.split("=", 1)[1].strip()
                    return found if found and os.path.isfile(found) else None
    except OSError:
        pass
    return None


def stripped(exe, tmp):
    """The executable without its symbols (the build's llvm-strip, else llvm-strip or the cross
    strip on PATH); unstripped, said so, when there is none."""
    tool = build_strip(os.path.dirname(exe)) or shutil.which("llvm-strip") or shutil.which("aarch64-linux-gnu-strip")
    if tool is None:
        print(f"portmaster_package: no llvm-strip: {exe} goes in unstripped")
        return exe
    out = os.path.join(tmp, os.path.basename(exe))
    if subprocess.run([tool, "-o", out, exe]).returncode != 0:
        print(f"portmaster_package: {tool} failed: {exe} goes in unstripped")
        return exe
    return out


def zlib_of(build):
    """The shared zlib a build links (Aurora's zlib-ng), the real file."""
    found = sorted(glob.glob(os.path.join(build, "_deps", "zlib-build", "libz.so.1*")))
    return os.path.realpath(found[0]) if found else None


def write(path, version, builds):
    """The zip, from the builds (short -> executable)."""
    import third_party
    from release import check_no_disc_data
    for short, exe in builds.items():
        check_elf_arm64(exe)
        check_no_disc_data([exe], VERSIONS[short])
    notices = third_party.text(os.path.dirname(builds["us"]))
    os.makedirs(os.path.dirname(path), exist_ok=True)

    def text(z, arc, body, mode=0o644):
        info = zipfile.ZipInfo(arc)
        info.compress_type, info.external_attr = zipfile.ZIP_DEFLATED, mode << 16
        z.writestr(info, body.replace("\r\n", "\n"))

    zlib = zlib_of(os.path.dirname(builds["us"]))
    tmp = tempfile.mkdtemp()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        text(z, SCRIPT, LAUNCH, 0o755)
        for short, exe in builds.items():
            info = zipfile.ZipInfo(f"{FOLDER}/ttyd-{short}")
            info.compress_type, info.external_attr = zipfile.ZIP_DEFLATED, 0o755 << 16
            with open(stripped(exe, tmp), "rb") as f:
                z.writestr(info, f.read())
        if zlib:
            z.write(zlib, f"{FOLDER}/libs/libz.so.1")
        text(z, f"{FOLDER}/port.json", json.dumps(port_json(version), indent=2) + "\n")
        text(z, f"{FOLDER}/gameinfo.xml", gameinfo(version))
        text(z, f"{FOLDER}/README.md", README)
        text(z, f"{FOLDER}/games/README.txt", GAMES_README)
        text(z, f"{FOLDER}/THIRD_PARTY.txt", notices)
        z.write(os.path.join(ROOT, "docs", "images", "social-preview.png"), f"{FOLDER}/cover.png")
        z.write(os.path.join(ROOT, "LICENSE"), f"{FOLDER}/LICENSE.txt")
        z.write(os.path.join(ROOT, "port", "assets", "fonts", "OFL.txt"), f"{FOLDER}/OFL.txt")
    shutil.rmtree(tmp, ignore_errors=True)
    print("wrote", path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version", help="e.g. 0.5.0")
    ap.add_argument("--build", action="append", default=[], metavar="SHORT=DIR",
                    help="an ARM64 build folder: us=DIR (needed), jp=DIR, eu=DIR")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist"))
    a = ap.parse_args()
    builds = {}
    for b in a.build:
        short, _, folder = b.partition("=")
        if short not in VERSIONS or not folder:
            ap.error(f"--build {b}: SHORT=DIR, SHORT one of {', '.join(VERSIONS)}")
        builds[short] = os.path.join(folder, f"ttyd-{short}")
        if not os.path.isfile(builds[short]):
            ap.error(f"no {builds[short]}")
    if "us" not in builds:
        ap.error("the US build is needed (--build us=DIR)")
    write(os.path.join(a.out, f"papercut-{a.version}-portmaster.zip"), a.version, builds)
    return 0


if __name__ == "__main__":
    sys.exit(main())
