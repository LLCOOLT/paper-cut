#!/usr/bin/env python3
"""The Linux package's other two forms, made from its zip (release.py, on
Linux: in the container, tools/port/linux-release.Dockerfile):

  linux_packages.py ZIP OUTDIR

  papercut-<version>-linux-x64.AppImage   one file to run (appimagetool)
  papercut-<version>-linux-x64.flatpak    a Flatpak bundle (flatpak build-export
                                          and build-bundle; installing it takes
                                          its runtime from Flathub)

Both hold what the zip does: the three builds (ttyd-jp, ttyd-us, ttyd-eu, the
US build's launcher starting the disc's version), the papercut script, the
documents and the example mods, with the desktop entry and AppStream data of
port/linux. A tool that isn't installed skips its package with a note.

The Flatpak's build folder is written here, not by flatpak build-init and
build-finish, so no Flatpak runtime is needed to make it: the metadata names
the runtime (org.freedesktop.Platform) and the sandbox's permissions. Those
are the whole file system (disc images and a sync folder can be anywhere,
an SD card on a Steam Deck included), every device (controllers, the GPU),
the display, sound, and the network (the update check)."""
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP_ID = "io.github.llcoolt.PaperCut"
RUNTIME_BRANCH = "24.08"
FLATHUB = "https://dl.flathub.org/repo/flathub.flatpakrepo"

FLATPAK_METADATA = f"""[Application]
name={APP_ID}
runtime=org.freedesktop.Platform/x86_64/{RUNTIME_BRANCH}
sdk=org.freedesktop.Sdk/x86_64/{RUNTIME_BRANCH}
command=papercut

[Context]
shared=network;ipc;
sockets=x11;wayland;pulseaudio;
devices=all;
filesystems=host;
"""


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


def share(prefix):
    """The desktop entry, icon and AppStream data under prefix/share."""
    linux = os.path.join(ROOT, "port", "linux")
    for sub, src, name in (("applications", f"{APP_ID}.desktop", f"{APP_ID}.desktop"),
                           ("metainfo", f"{APP_ID}.metainfo.xml", f"{APP_ID}.metainfo.xml"),
                           ("icons/hicolor/256x256/apps", None, f"{APP_ID}.png")):
        os.makedirs(os.path.join(prefix, "share", sub), exist_ok=True)
        source = os.path.join(linux, src) if src else os.path.join(ROOT, "port", "assets", "icon", "papercut-256.png")
        shutil.copy(source, os.path.join(prefix, "share", sub, name))


def appimage(zpath, outdir, top, work):
    tool = os.environ.get("APPIMAGETOOL") or shutil.which("appimagetool")
    if tool is None:
        print("linux_packages: no appimagetool: no AppImage")
        return None
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
    out = os.path.join(outdir, f"{top}.AppImage")
    env = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")  # (no FUSE in a container)
    if subprocess.run([tool, "--no-appstream", appdir, out], env=env).returncode != 0:
        sys.exit("linux_packages: appimagetool failed")
    print("wrote", out)
    return out


def flatpak(zpath, outdir, top, work):
    tool = shutil.which("flatpak")
    if tool is None:
        print("linux_packages: no flatpak: no Flatpak bundle")
        return None
    build = os.path.join(work, "flatpak")
    unpack(zpath, os.path.join(build, "files", "bin"))
    share(os.path.join(build, "files"))
    # what the host's menus and software centres see (as build-finish exports):
    # the desktop entry starting the app through flatpak, the disc's path forwarded
    share(os.path.join(build, "export"))
    entry = os.path.join(build, "export", "share", "applications", f"{APP_ID}.desktop")
    text = open(entry, encoding="utf-8").read().replace(
        "Exec=papercut %f",
        f"Exec=/usr/bin/flatpak run --branch=stable --arch=x86_64 --command=papercut --file-forwarding {APP_ID} "
        f"@@ %f @@\nX-Flatpak={APP_ID}")
    open(entry, "w", encoding="utf-8", newline="\n").write(text)
    with open(os.path.join(build, "metadata"), "w", newline="\n") as f:
        f.write(FLATPAK_METADATA)
    repo = os.path.join(work, "repo")
    out = os.path.join(outdir, f"{top}.flatpak")
    for cmd in ([tool, "build-export", "--arch=x86_64", repo, build, "stable"],
                [tool, "build-bundle", "--arch=x86_64", f"--runtime-repo={FLATHUB}", repo, out, APP_ID, "stable"]):
        if subprocess.run(cmd).returncode != 0:
            sys.exit(f"linux_packages: failed: {' '.join(cmd)}")
    print("wrote", out)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    zpath, outdir = sys.argv[1], sys.argv[2]
    top = os.path.basename(zpath)[:-len(".zip")]
    work = os.path.join(outdir, f"_{top}_linux")
    shutil.rmtree(work, ignore_errors=True)
    made = [p for p in (appimage(zpath, outdir, top, work), flatpak(zpath, outdir, top, work)) if p]
    shutil.rmtree(work, ignore_errors=True)
    if not made:
        print("linux_packages: neither package made")
    return 0


if __name__ == "__main__":
    sys.exit(main())
