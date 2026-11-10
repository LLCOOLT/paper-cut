#!/usr/bin/env python3
"""The Linux package's other two forms, made from its zip (release.py, on
Linux: in the container, tools/port/linux-release.Dockerfile):

  linux_packages.py ZIP OUTDIR

  Paper_Cut-<version>-x86_64.AppImage   one file to run (appimagetool), named
                                        as the AppImage catalog has it
                                        (data/Paper_Cut), with its .zsync
                                        beside it for AppImageUpdate
  papercut-<version>-linux-x64.flatpak  a Flatpak bundle (flatpak build-export
                                        and build-bundle; installing it takes
                                        its runtime from Flathub)

Both hold what the zip does: the three builds (ttyd-jp, ttyd-us, ttyd-eu, the
US build's launcher starting the disc's version), the papercut script, the
documents and the example mods, with the desktop entry and AppStream data of
port/linux. A tool that isn't installed skips its package with a note.

First the builds are checked (the release fails otherwise): no program or
library in them may ask for a newer C library (glibc) or C++ library
(libstdc++) than the oldest Ubuntu LTS still supported has (MAX_VERSIONS),
the system the AppImage catalog tests on, and each build must start
(--version) here.

The AppImage carries its update information (UPDATE_INFORMATION):
AppImageUpdate, or a software centre that knows AppImages, fetches the newest
release's AppImage through the .zsync, which the release uploads beside it.

The Flatpak's build folder is written here, not by flatpak build-init and
build-finish, so no Flatpak runtime is needed to make it: the metadata names
the runtime (org.freedesktop.Platform) and the sandbox's permissions. Those
are the whole file system (disc images and a sync folder can be anywhere,
an SD card on a Steam Deck included), every device (controllers, the GPU),
the display, sound, and the network (the update check)."""
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP_ID = "io.github.llcoolt.PaperCut"
RUNTIME_BRANCH = "24.08"
FLATHUB = "https://dl.flathub.org/repo/flathub.flatpakrepo"
REPOSITORY = ("LLCOOLT", "paper-cut")
# the AppImage's name: the catalog's entry (data/Paper_Cut) and its style, App-1.0-x86_64.AppImage
APPIMAGE_NAME = "Paper_Cut"
ARCHES = {"x64": "x86_64", "arm64": "aarch64"}  # (the zip's platform: the AppImage's)
# the newest symbol versions a build may ask of the system: Ubuntu 22.04's
# (glibc 2.35, GCC 12's libstdc++), the oldest LTS in standard support. The
# release links the C++ runtime in (TTYD_STATIC_CXX_RUNTIME), so GLIBCXX and
# CXXABI should not be asked for at all.
MAX_VERSIONS = {"GLIBC": (2, 35), "GLIBCXX": (3, 4, 30), "CXXABI": (1, 3, 13)}

FLATPAK_METADATA = f"""[Application]
name={APP_ID}
runtime=org.freedesktop.Platform/@ARCH@/{RUNTIME_BRANCH}
sdk=org.freedesktop.Sdk/@ARCH@/{RUNTIME_BRANCH}
command=papercut

[Context]
shared=network;ipc;
sockets=x11;wayland;pulseaudio;
devices=all;
filesystems=host;
"""


def appimage_file(version, arch):
    """The AppImage's file name: Paper_Cut-0.5.0-x86_64.AppImage."""
    return f"{APPIMAGE_NAME}-{version}-{arch}.AppImage"


def update_information(arch):
    """What AppImageUpdate reads (appimagetool -u): the newest GitHub release's
    AppImage .zsync. (Releases before 0.5.0 carried none, and were named
    papercut-<version>-linux-x64.AppImage: they are updated by hand once.)"""
    return f"gh-releases-zsync|{REPOSITORY[0]}|{REPOSITORY[1]}|latest|{APPIMAGE_NAME}-*-{arch}.AppImage.zsync"


def unpack(zpath, dest):
    """The zip's one folder, unpacked into dest (the executables' modes kept)."""
    with zipfile.ZipFile(zpath) as z:
        top = z.namelist()[0].split("/")[0]
        for info in z.infolist():
            rel = info.filename[len(top) + 1:]
            if not rel or info.is_dir():
                continue
            out = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(info) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
            mode = (info.external_attr >> 16) & 0o777
            os.chmod(out, mode or 0o644)
    return top


def needed_versions(path):
    """The symbol versions an ELF file asks of its libraries (.gnu.version_r),
    as (library, version name) pairs; None for a file that isn't a 64-bit
    little-endian ELF."""
    with open(path, "rb") as f:
        elf = f.read()
    if elf[:4] != b"\x7fELF" or elf[4] != 2 or elf[5] != 1:
        return None
    shoff = struct.unpack_from("<Q", elf, 0x28)[0]
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x3A)
    sections = [struct.unpack_from("<IIQQQQIIQQ", elf, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for _name, kind, _flags, _addr, offset, _size, link, info, _align, _entsize in sections:
        if kind != 0x6FFFFFFE:  # SHT_GNU_verneed
            continue
        strings = sections[link][4]

        def string(at):
            return elf[strings + at:elf.index(b"\0", strings + at)].decode()
        need = offset
        for _ in range(info):
            _version, count, file_at, aux, next_need = struct.unpack_from("<HHIII", elf, need)
            entry = need + aux
            for _ in range(count):
                _hash, _vflags, _other, name_at, next_aux = struct.unpack_from("<IHHII", elf, entry)
                out.append((string(file_at), string(name_at)))
                entry += next_aux
            need += next_need
    return out


def check_versions(folder):
    """Every ELF file under folder asks for no newer glibc or libstdc++ than
    MAX_VERSIONS; the release stops otherwise, naming each one."""
    bad, checked = [], 0
    for base, _dirs, names in os.walk(folder):
        for name in names:
            path = os.path.join(base, name)
            if os.path.islink(path) or not os.path.isfile(path):
                continue
            versions = needed_versions(path)
            if versions is None:
                continue
            checked += 1
            for library, version in versions:
                m = re.fullmatch(r"(GLIBC|GLIBCXX|CXXABI)_([0-9.]+)", version)
                if m and tuple(int(x) for x in m.group(2).split(".")) > MAX_VERSIONS[m.group(1)]:
                    bad.append(f"{os.path.relpath(path, folder)}: {version} ({library})")
    if bad:
        newest = ", ".join(f"{k} {'.'.join(map(str, v))}" for k, v in MAX_VERSIONS.items())
        sys.exit("linux_packages: these ask for more than Ubuntu 22.04 has (" + newest + "):\n  " +
                 "\n  ".join(sorted(set(bad))) +
                 "\nBuild in tools/port/linux-release.Dockerfile's container, with -DTTYD_STATIC_CXX_RUNTIME=ON.")
    print(f"linux_packages: {checked} programs and libraries ask for nothing newer than Ubuntu 22.04's")


def check_starts(folder):
    """Each build starts here (--version: no window, no network), so the
    system's loader takes it."""
    for name in ("ttyd-jp", "ttyd-us", "ttyd-eu"):
        exe = os.path.join(folder, name)
        if not os.path.exists(exe):  # (a zip without one of the three is no release: never skipped unchecked)
            sys.exit(f"linux_packages: the zip has no {name}")
        r = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=120)
        if r.returncode != 0:
            sys.exit(f"linux_packages: {name} --version failed ({r.returncode}):\n{r.stdout}{r.stderr}")
        print(f"linux_packages: {name}: {r.stdout.strip()}")


def share(prefix):
    """The desktop entry, icon and AppStream data under prefix/share."""
    linux = os.path.join(ROOT, "port", "linux")
    for sub, src, name in (("applications", f"{APP_ID}.desktop", f"{APP_ID}.desktop"),
                           ("metainfo", f"{APP_ID}.metainfo.xml", f"{APP_ID}.metainfo.xml"),
                           ("icons/hicolor/256x256/apps", None, f"{APP_ID}.png")):
        os.makedirs(os.path.join(prefix, "share", sub), exist_ok=True)
        dst = os.path.join(prefix, "share", sub, name)
        if src is None:
            shutil.copy(os.path.join(ROOT, "port", "assets", "icon", "papercut-256.png"), dst)
            continue
        # LF only: a Windows checkout has these with CRLF, and appimagetool refuses such a desktop entry
        with open(os.path.join(linux, src), "rb") as f, open(dst, "wb") as out:
            out.write(f.read().replace(b"\r\n", b"\n"))


def appimage(zpath, outdir, version, arch, work):
    tool = os.environ.get("APPIMAGETOOL") or shutil.which("appimagetool")
    if tool is None:
        print("linux_packages: no appimagetool: no AppImage")
        return []
    appdir = os.path.join(work, "AppDir")
    unpack(zpath, os.path.join(appdir, "usr", "bin"))
    share(os.path.join(appdir, "usr"))
    shutil.copy(os.path.join(appdir, "usr", "share", "applications", f"{APP_ID}.desktop"), appdir)
    shutil.copy(os.path.join(appdir, "usr", "share", "icons", "hicolor", "256x256", "apps", f"{APP_ID}.png"), appdir)
    run = os.path.join(appdir, "AppRun")
    with open(run, "w", newline="\n") as f:
        f.write('#!/bin/sh\n# Paper Cut: the launcher (the US build), which starts the disc\'s version\n'
                'HERE="$(dirname "$(readlink -f "$0")")"\nexec "$HERE/usr/bin/ttyd-us" "$@"\n')
    os.chmod(run, 0o755)
    out = os.path.join(outdir, appimage_file(version, arch))
    for old in (out, out + ".zsync"):
        if os.path.exists(old):
            os.remove(old)
    env = dict(os.environ, ARCH=arch, APPIMAGE_EXTRACT_AND_RUN="1")  # (no FUSE in a container)
    # (run in outdir: the .zsync is written beside the AppImage)
    if subprocess.run([tool, "--no-appstream", "-u", update_information(arch), appdir, out], env=env,
                      cwd=outdir).returncode != 0:
        sys.exit("linux_packages: appimagetool failed")
    print("wrote", out)
    made = [out]
    if os.path.exists(out + ".zsync"):
        print("wrote", out + ".zsync")
        made.append(out + ".zsync")
    else:
        print("linux_packages: appimagetool wrote no .zsync: AppImageUpdate won't find this release")
    return made


def flatpak(zpath, outdir, top, work, arch="x86_64"):
    """the Flatpak bundle, for the zip's architecture (x86_64, aarch64: the runtime, the export and the entry)"""
    tool = shutil.which("flatpak")
    if tool is None:
        print("linux_packages: no flatpak: no Flatpak bundle")
        return []
    build = os.path.join(work, "flatpak")
    unpack(zpath, os.path.join(build, "files", "bin"))
    share(os.path.join(build, "files"))
    # what the host's menus and software centres see (as build-finish exports):
    # the desktop entry starting the app through flatpak, the disc's path or
    # invite link forwarded
    share(os.path.join(build, "export"))
    entry = os.path.join(build, "export", "share", "applications", f"{APP_ID}.desktop")
    text, n = re.subn(
        r"^Exec=papercut %([fu])(?=\r?$)",
        lambda m: f"Exec=/usr/bin/flatpak run --branch=stable --arch={arch} --command=papercut "
                  f"--file-forwarding {APP_ID} @@{'u' if m.group(1) == 'u' else ''} %{m.group(1)} @@"
                  f"\nX-Flatpak={APP_ID}",
        open(entry, encoding="utf-8").read(), flags=re.M)
    if n != 1:
        sys.exit(f"linux_packages: {APP_ID}.desktop has no 'Exec=papercut %f' or '%u' line to point at flatpak")
    open(entry, "w", encoding="utf-8", newline="\n").write(text)
    with open(os.path.join(build, "metadata"), "w", newline="\n") as f:
        f.write(FLATPAK_METADATA.replace("@ARCH@", arch))
    repo = os.path.join(work, "repo")
    out = os.path.join(outdir, f"{top}.flatpak")
    # (--disable-sandbox: the icon's check runs without bwrap, which a
    # container can't start)
    for cmd in ([tool, "build-export", "--disable-sandbox", f"--arch={arch}", repo, build, "stable"],
                [tool, "build-bundle", f"--arch={arch}", f"--runtime-repo={FLATHUB}", repo, out, APP_ID, "stable"]):
        if subprocess.run(cmd).returncode != 0:
            sys.exit(f"linux_packages: failed: {' '.join(cmd)}")
    print("wrote", out)
    return [out]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    zpath, outdir = sys.argv[1], os.path.abspath(sys.argv[2])
    top = os.path.basename(zpath)[:-len(".zip")]
    m = re.fullmatch(r"papercut-(.+)-linux-(x64|arm64)", top)
    if m is None:
        sys.exit(f"linux_packages: {top}.zip isn't papercut-<version>-linux-<x64|arm64>.zip")
    version, arch = m.group(1), ARCHES[m.group(2)]
    work = os.path.join(outdir, f"_{top}_linux")
    shutil.rmtree(work, ignore_errors=True)
    builds = os.path.join(work, "check")
    unpack(zpath, builds)
    check_versions(builds)
    check_starts(builds)
    shutil.rmtree(builds)
    made = appimage(zpath, outdir, version, arch, work) + flatpak(zpath, outdir, top, work, arch)
    shutil.rmtree(work, ignore_errors=True)
    if not made:
        print("linux_packages: neither package made")
    return 0


if __name__ == "__main__":
    sys.exit(main())
