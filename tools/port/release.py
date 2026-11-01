#!/usr/bin/env python3
"""Build and package a release of the port (docs/RELEASING.md).

  release.py VERSION [--out DIR] [--targets LIST] [--no-android] [--publish TAG]

Targets (--targets, comma-separated; by default this machine's):
  desktop  this machine's own build: Windows, macOS or Linux
  android  both APKs (debug-signed), with an Android SDK
  ios      an unsigned .ipa per version, for sideloading (on a Mac)
  linux    the Linux x86-64 build, inside a Docker container (on a Mac or
           another machine with Docker: tools/port/linux-release.Dockerfile)
Windows makes desktop and android; a Mac desktop, ios and linux; Linux desktop.

For each game version (JP, US) and target: a build of its own
(build-release-*, -DTTYD_NO_DISC_ASSETS=ON: the stand-ins for everything
extracted from the disc), checked to hold none of the disc's data where the
extracted data is there to check against, packaged as
ttyd-port-VERSION-<jp|us>-<platform>.zip (the game, its libraries, the C
runtime on Windows, README, LICENSE and the example mods) or .apk/.ipa.
SHA256SUMS.txt and notes.md beside them.

--publish TAG: the files on the GitHub release TAG (gh; in the release
workflow, GITHUB_TOKEN): a new draft with the notes, or, when the release is
there already (another runner made it), added to it with their checksums.
Nothing from the disc is ever packaged.
"""
import argparse
import glob
import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import local_ci

ROOT = local_ci.ROOT
VERSIONS = {"jp": "G8MJ01", "us": "G8ME01", "eu": "G8MP01"}
REDIST = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
TOOLCHAINS = os.path.join(ROOT, "cmake", "toolchains")


def host():
    return "windows" if os.name == "nt" else "macos" if sys.platform == "darwin" else "linux"


def arch():
    return "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"


def default_targets():
    return {"windows": "desktop,android", "macos": "desktop,ios,linux", "linux": "desktop"}[host()]


def platform_name():
    """The example mod's lib/<platform>/ folder (TTYD_MOD_PLATFORM)."""
    return local_ci.mod_platform()


def package_platform():
    """The package's platform in its name: windows-x64, macos-arm64, linux-x64."""
    return f"{host()}-{arch()}"


def run(cmd, retry=True, env=None):
    """cmd, which must succeed. Each command here may download (CMake's
    FetchContent, Cargo, Gradle, GitHub) and is safe to repeat, so a failure
    is retried (local_ci.RETRY_WAITS) unless retry=False."""
    env = env or local_ci.ENV
    # (Windows looks a bare command up on this process's PATH, not env's)
    cmd = [shutil.which(cmd[0], path=env.get("PATH")) or cmd[0]] + list(cmd[1:])

    def attempt():
        print("$", " ".join(cmd), flush=True)
        return subprocess.run(cmd, env=env).returncode == 0
    if not (local_ci.retrying(attempt, os.path.basename(cmd[0])) if retry else attempt()):
        sys.exit(f"release: failed: {' '.join(cmd)}")


def build(short, version_string, kind="desktop"):
    # (Windows keeps its folders' old names; the Mac's checkout is shared with
    # the Linux container, so each build there has a folder of its own)
    suffix = "" if (kind, host()) == ("desktop", "windows") else f"-{kind if kind != 'desktop' else host()}"
    out = os.path.join(ROOT, f"build-release-{short}{suffix}")
    args = ["cmake", "-S", ROOT, "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DTTYD_VERSION={VERSIONS[short]}", "-DTTYD_BACKEND=aurora", "-DTTYD_NO_DISC_ASSETS=ON",
            f"-DTTYD_PORT_VERSION={version_string}"]
    if kind == "ios":
        # unsigned: AltStore, SideStore and Sideloadly sign it on install
        args += [f"--toolchain={os.path.join(TOOLCHAINS, 'ios.cmake')}", "-DTTYD_IOS_CODESIGN_IDENTITY="]
    elif host() == "windows":
        args.append(f"--toolchain={os.path.join(TOOLCHAINS, 'windows-clang.cmake')}")
    elif host() == "linux":
        args.append(f"--toolchain={os.path.join(TOOLCHAINS, 'linux-clang.cmake')}")
    run(args)
    run(["ninja", "-C", out, "ttyd", "ttyd_ipa"] if kind == "ios" else ["ninja", "-C", out, "ttyd", "hello-native"])
    return out


def asset_bytes(version):
    """Byte strings from the disc's extracted assets (the build/<version>
    includes), to look for in a release build. None without them."""
    found = []
    for inc in glob.glob(os.path.join(ROOT, "build", version, "include", "assets", "*.inc")):
        nums = re.findall(r"0x([0-9A-Fa-f]{2})\b", open(inc, encoding="utf-8", errors="replace").read())
        data = bytes(int(n, 16) for n in nums)
        # a run of varied bytes from the middle (blank areas would match anything)
        for at in range(len(data) // 2, len(data) - 32, 16):
            chunk = data[at:at + 32]
            if len(set(chunk)) > 12:
                found.append((os.path.basename(inc), chunk))
                break
    return found


def check_no_disc_data(paths, version):
    probes = asset_bytes(version)
    if not probes:
        print(f"(no extracted {version} assets here to check against: the build used stand-ins anyway)")
        return
    for path in paths:
        blob = open(path, "rb").read()
        for name, chunk in probes:
            if chunk in blob:
                sys.exit(f"release: {os.path.basename(path)} contains bytes of the disc's {name}: not releasable")
    print(f"checked {len(paths)} file(s): no bytes of the disc's {', '.join(n for n, _ in probes)}")


def package_desktop(short, out, version_string, dist):
    plat = platform_name()
    name = f"ttyd-port-{version_string}-{short}-{package_platform()}"
    files = []  # (source, path in the package); a source folder is copied whole
    if host() == "windows":
        files.append((os.path.join(out, f"ttyd-{short}.exe"), f"ttyd-{short}.exe"))
        files += [(p, os.path.basename(p)) for p in sorted(glob.glob(os.path.join(out, "*.dll")))]
        crt = sorted(glob.glob(os.path.join(REDIST, "14.*", "x64", "Microsoft.VC143.CRT")))
        if crt:  # app-local C runtime: no separate VC++ redistributable install needed
            files += [(p, os.path.basename(p)) for p in sorted(glob.glob(os.path.join(crt[-1], "*.dll")))]
        check_no_disc_data([p for p, _ in files], VERSIONS[short])
    elif host() == "macos":
        # the bundle as cmake --install leaves it: its non-system libraries
        # copied in, signed ad hoc (not notarized: Open Anyway once)
        stage = os.path.join(out, "stage")
        shutil.rmtree(stage, ignore_errors=True)
        run(["cmake", "--install", out, "--prefix", stage])
        app = os.path.join(stage, f"ttyd-{short}.app")
        check_no_disc_data([os.path.join(app, "Contents", "MacOS", f"ttyd-{short}")], VERSIONS[short])
        files.append((app, f"ttyd-{short}.app"))
    else:
        files.append((os.path.join(out, f"ttyd-{short}"), f"ttyd-{short}"))
        files += [(p, os.path.basename(p)) for p in sorted(glob.glob(os.path.join(out, "*.so*")))]
        check_no_disc_data([p for p, _ in files], VERSIONS[short])
    lib = next((p for p in sorted(glob.glob(os.path.join(out, "mods", "hello-native.*")))
                if os.path.splitext(p)[1] in (".dll", ".so", ".dylib")), None)
    files += [(os.path.join(ROOT, "README.md"), "README.md"), (os.path.join(ROOT, "LICENSE"), "LICENSE.txt"),
              (os.path.join(ROOT, "docs", "MODDING.md"), "docs/MODDING.md")]
    for path in glob.glob(os.path.join(ROOT, "examples", "mods", "**", "*"), recursive=True):
        if os.path.isfile(path):
            files.append((path, os.path.relpath(path, ROOT).replace(os.sep, "/")))
    if lib:
        files.append((lib, f"examples/mods/hello-native/lib/{plat}/{os.path.basename(lib)}"))
    zpath = os.path.join(dist, name + ".zip")
    if host() == "macos":
        # ditto keeps the bundle's symlinks, permissions and signature (its
        # files, not extended attributes: without them, no __MACOSX/ beside)
        tree = os.path.join(dist, "_" + name, name)
        shutil.rmtree(os.path.dirname(tree), ignore_errors=True)
        for src, arc in files:
            dst = os.path.join(tree, arc)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.isdir(src):
                shutil.copytree(src, dst, symlinks=True)
            else:
                shutil.copy2(src, dst)
        open(os.path.join(tree, "HOW TO PLAY.txt"), "w", encoding="utf-8").write(how_to_play(short))
        run(["ditto", "-c", "-k", "--norsrc", "--noextattr", "--noqtn", "--keepParent", tree, zpath])
        shutil.rmtree(os.path.dirname(tree))
    else:
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            for src, arc in files:
                z.write(src, f"{name}/{arc}")  # (keeps the file's mode: the Linux executable stays one)
            info = zipfile.ZipInfo(f"{name}/HOW TO PLAY.txt")
            info.compress_type, info.external_attr = zipfile.ZIP_DEFLATED, 0o644 << 16  # (rw-r--r--)
            z.writestr(info, how_to_play(short))
    print("wrote", zpath)
    return zpath


def how_to_play(short):
    region = {"jp": "Japanese (G8MJ01)", "us": "US (G8ME01)", "eu": "European (G8MP01)"}[short]
    start = {"windows": f"ttyd-{short}.exe",
             "macos": f"ttyd-{short}.app (the first time macOS blocks it: System Settings >\n"
                      "Privacy & Security > Open Anyway; before macOS 15, right-click > Open)",
             "linux": f"ttyd-{short}"}[host()]
    return f"""Paper Mario: The Thousand-Year Door, native port ({region})

This package holds no game data. You need your own copy of the game: a disc
image of the {region} version (.iso, .rvz, .gcm, .ciso, .gcz, .wia, ...).

Start {start}: the first time, it asks for the disc image and remembers it.
F1 (or a controller's Back/Select) opens the settings page.

Mods go in the mods folder (Settings > Mods > Open the mods folder); the
examples/ folder here has four to try (see docs/MODDING.md).
"""


def package_android(dist, version_string):
    sdk_home = os.path.join(os.path.expanduser("~"), "Android")
    gradle = shutil.which("gradle") or next(iter(sorted(glob.glob(os.path.join(
        sdk_home, "gradle-*", "bin", "gradle.bat" if os.name == "nt" else "gradle")))), None)
    if gradle is None:
        print("no gradle: the APKs are skipped")
        return []
    env = dict(local_ci.ENV)
    env.setdefault("ANDROID_HOME", os.path.join(sdk_home, "sdk"))
    if not env.get("JAVA_HOME"):
        jdk = sorted(glob.glob(os.path.join(sdk_home, "jdk-17*")))
        if jdk:
            env["JAVA_HOME"] = jdk[-1]
    run([gradle, "--no-daemon", "-p", os.path.join(ROOT, "port", "android"), "-PnoDiscAssets",
         "assembleJpDebug", "assembleUsDebug"], env=env)
    out = []
    for short in ("jp", "us"):
        apk = sorted(glob.glob(os.path.join(ROOT, "port", "android", "app", "build", "outputs", "apk", short, "debug",
                                            "*.apk")))
        if not apk:
            sys.exit(f"release: no {short} APK")
        dst = os.path.join(dist, f"ttyd-port-{version_string}-{short}-android-arm64.apk")
        shutil.copy(apk[0], dst)
        with zipfile.ZipFile(dst) as z:  # the native library, checked like the desktop builds
            libs = [n for n in z.namelist() if n.endswith("libmain.so")]
            tmp = os.path.join(dist, f"_{short}_libmain.so")
            open(tmp, "wb").write(z.read(libs[0]))
        check_no_disc_data([tmp], VERSIONS[short])
        os.remove(tmp)
        out.append(dst)
        print("wrote", dst)
    return out


def package_ios(short, out, version_string, dist):
    """The unsigned .ipa (Payload/ttyd-<short>.app) the ttyd_ipa target made."""
    check_no_disc_data([os.path.join(out, f"ttyd-{short}.app", f"ttyd-{short}")], VERSIONS[short])
    dst = os.path.join(dist, f"ttyd-port-{version_string}-{short}-ios-unsigned.ipa")
    shutil.copy(os.path.join(out, f"ttyd-{short}.ipa"), dst)
    print("wrote", dst)
    return dst


def package_linux_container(version_string, dist):
    """Both Linux x86-64 packages, built by this script inside an Ubuntu
    container over this checkout (Docker, OrbStack...; on Apple silicon the
    x86-64 container runs emulated)."""
    import linux_container
    if linux_container.run(["python3", "tools/port/release.py", version_string, "--targets", "desktop",
                            "--out", "/src/dist-linux"]) != 0:
        sys.exit("release: the Linux packages' build failed (in the container)")
    out = []
    for z in sorted(glob.glob(os.path.join(ROOT, "dist-linux", "*.zip"))):
        out.append(shutil.copy(z, dist))
        print("wrote", out[-1])
    return out


def checksums(files):
    return [f"{hashlib.sha256(open(f, 'rb').read()).hexdigest()}  {os.path.basename(f)}" for f in files]


def notes(version_string, sums):
    template = os.path.join(ROOT, "docs", "release_notes", f"{version_string}.md")
    body = open(template, encoding="utf-8").read() if os.path.exists(template) else f"Release {version_string}.\n"
    body += "\n**No game data is included.** You need your own disc image of the matching version.\n"
    body += "\nSHA-256:\n```\n" + "\n".join(sums) + "\n```\n"
    return body


def gh_out(gh, args):
    """gh's output, or None when it fails (retried: GitHub's API fails now
    and then too)."""
    out = []

    def attempt():
        r = subprocess.run([gh, *args], capture_output=True, text=True, env=local_ci.ENV)
        out[:] = [r]
        return r.returncode == 0
    local_ci.retrying(attempt, "gh " + args[0])
    return out[0].stdout if out[0].returncode == 0 else None


def publish(tag, version_string, files, sums, dist):
    gh = shutil.which("gh", path=local_ci.ENV.get("PATH")) or "gh"
    # (whether the release is there: `gh release view` fails for "not
    # found" as for an error, so the list of releases is asked instead)
    tags = gh_out(gh, ["release", "list", "--limit", "100", "--json", "tagName", "-q", ".[].tagName"])
    if tags is None:
        sys.exit("release: can't list the repository's releases")
    if tag not in tags.split():
        # a new draft (created once: it isn't retried, the uploads are),
        # then the files
        run([gh, "release", "create", tag, "--draft", "--title", f"TTYD port {version_string}",
             "--notes-file", os.path.join(dist, "notes.md")], retry=False)
        run([gh, "release", "upload", tag, "--clobber", *files, os.path.join(dist, "SHA256SUMS.txt")])
        return
    # another runner's release: these files beside its own, their checksums
    # in a file of their own and in the notes (in place of this runner's from
    # an earlier run)
    extra = os.path.join(dist, f"SHA256SUMS-{host()}.txt")
    shutil.copy(os.path.join(dist, "SHA256SUMS.txt"), extra)
    run([gh, "release", "upload", tag, "--clobber", *files, extra])
    body = gh_out(gh, ["release", "view", tag, "--json", "body", "-q", ".body"])
    if body is None:
        sys.exit("release: can't read the release's notes")
    head = "\n\nSHA-256 (" + os.path.basename(extra) + "):\n"
    body = body.rstrip("\n")
    if head in body:
        start = body.index(head)
        end = body.find("```\n", body.index("```\n", start) + 4)
        body = body[:start] + (body[end + 4:] if end >= 0 else "")
    body = body.rstrip("\n") + head + "```\n" + "\n".join(sums) + "\n```\n"
    path = os.path.join(dist, "notes-added.md")
    open(path, "w", encoding="utf-8", newline="\n").write(body)
    run([gh, "release", "edit", tag, "--notes-file", path])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version", help="e.g. 0.1.0")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist"))
    ap.add_argument("--targets", default=default_targets(), help="desktop,android,ios,linux")
    ap.add_argument("--no-android", action="store_true")
    ap.add_argument("--publish", metavar="TAG", help="the files on the GitHub release TAG (a draft if new)")
    a = ap.parse_args()
    targets = [t for t in a.targets.split(",") if t and not (a.no_android and t == "android")]
    unknown = set(targets) - {"desktop", "android", "ios", "linux"}
    if unknown:
        sys.exit(f"release: unknown target(s) {', '.join(sorted(unknown))}")
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(a.out)
    files = []
    for short in ("jp", "us"):
        if "desktop" in targets:
            files.append(package_desktop(short, build(short, a.version), a.version, a.out))
        if "ios" in targets:
            files.append(package_ios(short, build(short, a.version, "ios"), a.version, a.out))
    if "android" in targets:
        files += package_android(a.out, a.version)
    if "linux" in targets:
        files += package_linux_container(a.version, a.out)
    sums = checksums(files)
    # (LF line endings, written on Windows too: `sha256sum -c` takes a CR as
    # part of the file's name)
    open(os.path.join(a.out, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(sums) + "\n")
    open(os.path.join(a.out, "notes.md"), "w", encoding="utf-8", newline="\n").write(notes(a.version, sums))
    if a.publish:
        publish(a.publish, a.version, files, sums, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
