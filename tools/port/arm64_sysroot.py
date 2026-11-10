#!/usr/bin/env python3
"""The sysroot the Linux ARM64 build (cmake/toolchains/linux-arm64.cmake) is
cross-compiled against: Debian packages unpacked into one folder, on any
host (Windows, macOS, Linux; no dpkg or root needed).

  arm64_sysroot.py [OUT]     (default: build-arm64-sysroot beside the checkout)

What is in it, and why:
  - Debian 11 (bullseye)'s C library, glibc 2.31, and its kernel headers: the
    executable links against it, so it runs on every handheld firmware from
    glibc 2.31 up (Knulli, muOS, ROCKNIX, the makers' own; PortMaster's
    devices) rather than only on new desktops.
  - Debian 13 (trixie)'s GCC 14 C++ library and runtime, linked in statically
    (-static-libstdc++ -static-libgcc): Aurora is C++20 and the prebuilt Dawn
    package is built with GCC 13 or later. The few newer C-library symbols
    those archives name are provided by port/src/pc/glibc_compat.c.
  - the development headers SDL3 compiles its video, audio and input drivers
    against (KMSDRM with GBM and EGL, Wayland, X11, ALSA, PulseAudio, udev,
    D-Bus); SDL loads those libraries at run time, so only their headers and
    pkg-config files are used.

The packages' symbolic links are copied as files where the host cannot make
them (Windows), with absolute targets resolved inside the sysroot; the
pkg-config files keep their /usr prefix (tools/port/pkg_config.py rewrites it
to the sysroot). A stamp records what was unpacked: run again, only a changed
list is fetched again.
"""
import io
import lzma
import os
import posixpath
import shutil
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MIRROR = "https://deb.debian.org/debian"
ARCH = "arm64"

PACKAGES = {
    "bullseye": [
        # the C library and kernel headers
        "libc6", "libc6-dev", "linux-libc-dev", "libcrypt1", "libcrypt-dev",
        # KMSDRM: DRM, GBM, EGL and GLES headers
        "libdrm-dev", "libdrm2", "libgbm-dev", "libgbm1", "libegl-dev", "libegl1", "libgles-dev", "libgles2",
        "libgl-dev", "libglx-dev", "libglvnd-dev", "libopengl-dev", "mesa-common-dev",
        # Wayland and X11 headers
        "libwayland-dev", "libwayland-client0", "libwayland-egl1", "libwayland-cursor0", "wayland-protocols",
        "libxkbcommon-dev", "libx11-dev", "x11proto-dev", "libxext-dev", "libxrandr-dev", "libxrender-dev",
        "libxcursor-dev", "libxfixes-dev", "libxi-dev", "libxss-dev", "libxtst-dev", "libxcb1-dev", "libxau-dev",
        "libxdmcp-dev", "xtrans-dev",
        # sound, input, desktop bus
        "libasound2-dev", "libpulse-dev", "libudev-dev", "libdbus-1-dev",
        # the libraries those development links name (CMake's find_library wants the file)
        "libasound2", "libpulse0", "libudev1", "libdbus-1-3", "libx11-6", "libxext6", "libxrandr2", "libxrender1",
        "libxcursor1", "libxfixes3", "libxi6", "libxss1", "libxtst6", "libxcb1", "libxau6", "libxdmcp6",
        "libxkbcommon0",
    ],
    "trixie": [
        # GCC 14's C++ library (headers and static archive) and runtime
        "libstdc++-14-dev", "libgcc-14-dev",
    ],
}


def fetch(url):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=120) as r:
                return r.read()
        except OSError as e:
            if attempt == 3:
                sys.exit(f"arm64_sysroot: {url}: {e}")


def index(suite):
    """The suite's arm64 packages: name -> Filename."""
    text = lzma.decompress(fetch(f"{MIRROR}/dists/{suite}/main/binary-{ARCH}/Packages.xz")).decode("utf-8")
    out = {}
    for stanza in text.split("\n\n"):
        fields = {}
        for line in stanza.splitlines():
            if line and not line[0].isspace() and ":" in line:
                k, v = line.split(":", 1)
                fields[k] = v.strip()
        if "Package" in fields and "Filename" in fields:
            out[fields["Package"]] = fields["Filename"]
    return out


def ar_members(data):
    """The members of a .deb (an ar archive): name -> bytes."""
    if data[:8] != b"!<arch>\n":
        raise ValueError("not an ar archive")
    pos, out = 8, {}
    while pos + 60 <= len(data):
        header = data[pos:pos + 60]
        name = header[:16].decode().strip().rstrip("/")
        size = int(header[48:58].decode().strip())
        out[name] = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
    return out


def data_tar(deb):
    members = ar_members(deb)
    for name, blob in members.items():
        if name.startswith("data.tar"):
            if name.endswith(".xz"):
                return tarfile.open(fileobj=io.BytesIO(lzma.decompress(blob)))
            if name.endswith(".zst"):
                from compression import zstd  # Python 3.14
                return tarfile.open(fileobj=io.BytesIO(zstd.decompress(blob)))
            return tarfile.open(fileobj=io.BytesIO(blob))
    raise ValueError("no data.tar in the package")


def unpack(tar, out, links):
    for m in tar.getmembers():
        rel = posixpath.normpath(m.name.lstrip("./"))
        if rel in (".", "") or rel.startswith(("..", "usr/share/doc", "usr/share/man", "usr/share/locale")):
            continue
        dest = os.path.join(out, *rel.split("/"))
        if m.isdir():
            os.makedirs(dest, exist_ok=True)
        elif m.issym() or m.islnk():
            links.append((rel, m.linkname, m.islnk()))
        elif m.isfile():
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with tar.extractfile(m) as src, open(dest, "wb") as dst:
                shutil.copyfileobj(src, dst)


def resolve_links(out, links):
    """Each link as a copy of what it names (a chain of links followed; one
    whose target is not in the sysroot dropped)."""
    pending = list(links)
    for _ in range(8):
        left = []
        for rel, target, hard in pending:
            if hard:
                src_rel = posixpath.normpath(target.lstrip("./"))
            elif target.startswith("/"):
                src_rel = posixpath.normpath(target.lstrip("/"))
            else:
                src_rel = posixpath.normpath(posixpath.join(posixpath.dirname(rel), target))
            src = os.path.join(out, *src_rel.split("/"))
            dest = os.path.join(out, *rel.split("/"))
            if os.path.isfile(src):
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copyfile(src, dest)
            elif os.path.isdir(src):
                shutil.copytree(src, dest, dirs_exist_ok=True)
            else:
                left.append((rel, target, hard))
        if len(left) == len(pending):
            break
        pending = left
    return pending


def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build-arm64-sysroot"))
    want = "\n".join(f"{s} {p}" for s, ps in PACKAGES.items() for p in ps) + "\n"
    stamp = os.path.join(out, ".packages")
    if os.path.isfile(stamp) and open(stamp).read() == want:
        print(f"arm64_sysroot: {out} is up to date")
        return 0
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    links = []
    for suite, names in PACKAGES.items():
        files = index(suite)
        for name in names:
            if name not in files:
                sys.exit(f"arm64_sysroot: no package {name} in {suite}")
            print(f"  {suite} {name}", flush=True)
            unpack(data_tar(fetch(f"{MIRROR}/{files[name]}")), out, links)
    missing = resolve_links(out, links)
    for rel, target, _ in missing:
        print(f"  (link {rel} -> {target}: not in the sysroot, left out)")
    # Debian 11 keeps the C library in /lib: usr/lib's copies are links into it, already resolved
    with open(stamp, "w") as f:
        f.write(want)
    print(f"arm64_sysroot: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
