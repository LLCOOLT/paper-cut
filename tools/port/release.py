#!/usr/bin/env python3
"""Build and package a release of the port (docs/RELEASING.md).

  release.py VERSION [--out DIR] [--targets LIST] [--no-android] [--publish TAG]

Targets (--targets, comma-separated; by default this machine's):
  desktop  this machine's own build: Windows, macOS or Linux (Linux: the zip,
           an AppImage and a Flatpak, tools/port/linux_packages.py)
  android  the three APKs (debug-signed), with an Android SDK
  ios      an unsigned .ipa per version, for sideloading (on a Mac)
  linux    the Linux x86-64 build, inside a Docker container (on a Mac or
           another machine with Docker: tools/port/linux-release.Dockerfile)
Windows makes desktop and android; a Mac desktop, ios and linux; Linux desktop.

For each game version (JP, US, EU) and target: a build of its own
(build-release-*, -DTTYD_NO_DISC_ASSETS=ON: the stand-ins for everything
extracted from the disc), checked to hold none of the disc's data where the
extracted data is there to check against. Desktop: one package with all
three builds behind the launcher, papercut-VERSION-<platform>.zip (Paper Cut
to start, the builds, their libraries, the C runtime on Windows, README,
LICENSE, THIRD_PARTY.txt, OFL.txt and the example mods); phones: papercut-VERSION-<jp|us|eu>-*.apk/.ipa.
SHA256SUMS.txt and notes.md beside them. With the IPAs, altstore-source.json:
the AltStore / SideStore source for the three iOS apps (docs/UPDATES.md),
uploaded with them; --altstore-source writes it alone from --out's IPAs.

--publish TAG: the files on the GitHub release TAG (gh; in the release
workflow, GITHUB_TOKEN): a new draft with the notes, or, when the release is
there already (another runner made it), added to it with their checksums.
Nothing from the disc is ever packaged.

Code signing (docs/RELEASING.md, "Code signing"), only when the environment
has its secrets, and skipped with a line saying so when it doesn't: Windows
Authenticode on the executables and the unsigned libraries
(PAPERCUT_SIGN_PFX[_BASE64] + PAPERCUT_SIGN_PFX_PASSWORD, or
PAPERCUT_SIGN_THUMBPRINT); on a Mac a Developer ID with the hardened runtime
(PAPERCUT_MACOS_SIGN_IDENTITY, PAPERCUT_MACOS_CERT_P12_BASE64 +
PAPERCUT_MACOS_CERT_PASSWORD), then notarization and stapling
(PAPERCUT_NOTARY_KEY[_BASE64] + _KEY_ID + _ISSUER, or _APPLE_ID + _PASSWORD
+ _TEAM_ID).
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
import third_party

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
    targets = ["ttyd", "ttyd_ipa"] if kind == "ios" else ["ttyd", "hello-native"]
    if kind == "desktop" and host() == "windows" and short == "us":
        targets.append("papercut")  # Paper Cut.exe, what the player starts
    run(["ninja", "-C", out, *targets])
    return out


# --- code signing (docs/RELEASING.md, "Code signing"): only with its secrets
# in the environment; without them each step says so and the build goes on
# as before (unsigned on Windows, signed ad hoc on the Mac)

def env(name):
    value = os.environ.get(name, "").strip()
    return value or None


def secret_file(dist, name, b64_var, path_var):
    """A secret file: $path_var's path, or $b64_var's base64 written to dist
    (removed with the dist's other _ files). None without either."""
    if env(path_var):
        return env(path_var)
    if env(b64_var):
        import base64
        path = os.path.join(dist, f"_{name}")
        open(path, "wb").write(base64.b64decode(env(b64_var)))
        return path
    return None


def windows_signing(dist):
    """signtool's arguments for Authenticode, or None: a code-signing
    certificate as a .pfx (PAPERCUT_SIGN_PFX, a path, or PAPERCUT_SIGN_PFX_BASE64)
    with PAPERCUT_SIGN_PFX_PASSWORD; or one in the certificate store by its
    SHA-1 thumbprint (PAPERCUT_SIGN_THUMBPRINT: a hardware token's, as EV and
    since 2023 all OV certificates come). PAPERCUT_SIGN_TIMESTAMP: the RFC 3161
    timestamp server (DigiCert's by default)."""
    stamp = env("PAPERCUT_SIGN_TIMESTAMP") or "http://timestamp.digicert.com"
    common = ["/fd", "SHA256", "/tr", stamp, "/td", "SHA256", "/d", "Paper Cut",
              "/du", "https://github.com/LLCOOLT/paper-cut"]
    if env("PAPERCUT_SIGN_THUMBPRINT"):
        return ["sign", "/sha1", env("PAPERCUT_SIGN_THUMBPRINT"), *common]
    pfx = secret_file(dist, "sign.pfx", "PAPERCUT_SIGN_PFX_BASE64", "PAPERCUT_SIGN_PFX")
    if pfx:
        args = ["sign", "/f", pfx, *common]
        if env("PAPERCUT_SIGN_PFX_PASSWORD"):
            args[3:3] = ["/p", env("PAPERCUT_SIGN_PFX_PASSWORD")]
        return args
    return None


def signtool():
    if env("PAPERCUT_SIGNTOOL"):
        return env("PAPERCUT_SIGNTOOL")
    found = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\signtool.exe"))
    return found[-1] if found else shutil.which("signtool")


def sign_windows(paths, dist):
    """Authenticode on Paper Cut's own executables and libraries, before
    they're zipped (and the zip's checksum taken)."""
    args = windows_signing(dist)
    if args is None:
        print("(no Windows signing certificate in the environment: the executables stay unsigned)")
        return False
    tool = signtool()
    if tool is None:
        sys.exit("release: a signing certificate is set but there's no signtool (the Windows SDK)")
    # (the password isn't echoed: run() prints the command)
    shown = ["***" if i > 0 and args[i - 1] == "/p" else a for i, a in enumerate(args)]
    print("$", tool, " ".join(shown), f"<{len(paths)} files>", flush=True)
    if subprocess.run([tool, *args, *paths], env=local_ci.ENV).returncode != 0:
        sys.exit("release: signtool failed")
    run([tool, "verify", "/pa", *paths], retry=False)
    return True


MAC_ENTITLEMENTS = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <!-- code mods: players' own libraries, not signed by this team (docs/MODDING.md) -->
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
"""


def macos_identity():
    """The Developer ID Application identity (PAPERCUT_MACOS_SIGN_IDENTITY,
    e.g. "Developer ID Application: Name (TEAMID)"), its certificate in the
    keychain already or imported from PAPERCUT_MACOS_CERT_P12_BASE64 with
    PAPERCUT_MACOS_CERT_PASSWORD into a keychain of this run's own."""
    identity = env("PAPERCUT_MACOS_SIGN_IDENTITY")
    if identity and env("PAPERCUT_MACOS_CERT_P12_BASE64"):
        import base64
        import secrets
        keychain = os.path.join(os.path.expanduser("~"), "Library", "Keychains", "papercut-release.keychain-db")
        p12 = os.path.join(ROOT, "dist", "_cert.p12")
        os.makedirs(os.path.dirname(p12), exist_ok=True)
        open(p12, "wb").write(base64.b64decode(env("PAPERCUT_MACOS_CERT_P12_BASE64")))
        password = secrets.token_hex(16)
        subprocess.run(["security", "delete-keychain", keychain], capture_output=True)
        run(["security", "create-keychain", "-p", password, keychain], retry=False)
        run(["security", "set-keychain-settings", "-lut", "3600", keychain], retry=False)
        run(["security", "unlock-keychain", "-p", password, keychain], retry=False)
        r = subprocess.run(["security", "import", p12, "-k", keychain, "-P", env("PAPERCUT_MACOS_CERT_PASSWORD") or "",
                            "-T", "/usr/bin/codesign"])
        os.remove(p12)
        if r.returncode != 0:
            sys.exit("release: the signing certificate couldn't be imported")
        run(["security", "set-key-partition-list", "-S", "apple-tool:,apple:", "-s", "-k", password, keychain],
            retry=False)
        listed = subprocess.run(["security", "list-keychains", "-d", "user"], capture_output=True, text=True).stdout
        run(["security", "list-keychains", "-d", "user", "-s", keychain,
             *[k.strip().strip('"') for k in listed.split()]], retry=False)
    return identity


def notary_args(dist):
    """notarytool's credentials, or None: an App Store Connect API key
    (PAPERCUT_NOTARY_KEY_ID, PAPERCUT_NOTARY_ISSUER, the .p8 as
    PAPERCUT_NOTARY_KEY or PAPERCUT_NOTARY_KEY_BASE64), or an Apple ID with an
    app-specific password (PAPERCUT_NOTARY_APPLE_ID, PAPERCUT_NOTARY_PASSWORD,
    PAPERCUT_NOTARY_TEAM_ID)."""
    key = secret_file(dist, "notary.p8", "PAPERCUT_NOTARY_KEY_BASE64", "PAPERCUT_NOTARY_KEY")
    if key and env("PAPERCUT_NOTARY_KEY_ID") and env("PAPERCUT_NOTARY_ISSUER"):
        return ["--key", key, "--key-id", env("PAPERCUT_NOTARY_KEY_ID"), "--issuer", env("PAPERCUT_NOTARY_ISSUER")]
    if env("PAPERCUT_NOTARY_APPLE_ID") and env("PAPERCUT_NOTARY_PASSWORD") and env("PAPERCUT_NOTARY_TEAM_ID"):
        return ["--apple-id", env("PAPERCUT_NOTARY_APPLE_ID"), "--password", env("PAPERCUT_NOTARY_PASSWORD"),
                "--team-id", env("PAPERCUT_NOTARY_TEAM_ID")]
    return None


def sign_macos(app, dist):
    """The app signed: with a Developer ID (hardened runtime, a secure
    timestamp), then notarized and stapled when notarytool's credentials are
    there too; otherwise ad hoc, as before. True when notarized."""
    identity = macos_identity()
    if identity is None:
        print("(no Developer ID in the environment: the app is signed ad hoc, not notarized)")
        run(["codesign", "--force", "--deep", "--sign", "-", app])
        return False
    ent = os.path.join(dist, "_entitlements.plist")
    open(ent, "w", encoding="utf-8").write(MAC_ENTITLEMENTS)
    # inside out: the libraries and the other builds' executables, then the bundle
    inner = [p for p in glob.glob(os.path.join(app, "Contents", "**", "*"), recursive=True)
             if os.path.isfile(p) and not os.path.islink(p) and
             (p.endswith((".dylib", ".so")) or os.path.dirname(p).endswith(os.path.join("Contents", "MacOS")))]
    for path in sorted(inner, key=len, reverse=True):
        run(["codesign", "--force", "--timestamp", "--options", "runtime", "--entitlements", ent,
             "--sign", identity, path], retry=False)
    run(["codesign", "--force", "--timestamp", "--options", "runtime", "--entitlements", ent,
         "--sign", identity, app], retry=False)
    run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", app], retry=False)
    os.remove(ent)
    creds = notary_args(dist)
    if creds is None:
        print("(no notarization credentials in the environment: signed with the Developer ID, not notarized)")
        return False
    upload = os.path.join(dist, "_notarize.zip")
    run(["ditto", "-c", "-k", "--keepParent", app, upload], retry=False)
    # (the credentials aren't echoed)
    print("$ xcrun notarytool submit", os.path.basename(upload), "--wait <credentials>", flush=True)
    result = {}

    def attempt():
        import json
        r = subprocess.run(["xcrun", "notarytool", "submit", upload, "--wait", "--output-format", "json", *creds],
                           capture_output=True, text=True, env=local_ci.ENV)
        try:
            result.update(json.loads(r.stdout))
        except ValueError:
            print(r.stdout, r.stderr)
            return False  # (no answer: a network failure, tried again)
        return True
    local_ci.retrying(attempt, "notarytool")
    if result.get("status") != "Accepted":
        sys.exit(f"release: notarization: {result.get('status', 'no answer')} "
                 f"(xcrun notarytool log {result.get('id', '<id>')} says why)")
    os.remove(upload)
    run(["xcrun", "stapler", "staple", app])
    run(["spctl", "--assess", "--type", "execute", "--verbose", app], retry=False)
    return True


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


def package_desktop(outs, version_string, dist):
    """One package with every version's build (outs: short -> build folder):
    the player starts Paper Cut, which shows the launcher of the US build;
    a disc of another version starts that version's build beside it
    (port/settings/launcher_ui.cpp). The builds keep their names (ttyd-jp,
    ttyd-us, ttyd-eu): code mods are linked against them."""
    plat = platform_name()
    name = f"papercut-{version_string}-{package_platform()}"
    us = outs["us"]
    files = []  # (source, path in the package); a source folder is copied whole
    notarized = False
    if host() == "windows":
        files.append((os.path.join(us, "Paper Cut.exe"), "Paper Cut.exe"))
        for short, out in outs.items():
            exe = os.path.join(out, f"ttyd-{short}.exe")
            check_no_disc_data([exe], VERSIONS[short])
            files.append((exe, f"ttyd-{short}.exe"))
        # the libraries: the same set in every build (the US build's)
        files += [(p, os.path.basename(p)) for p in sorted(glob.glob(os.path.join(us, "*.dll")))]
        crt = sorted(glob.glob(os.path.join(REDIST, "14.*", "x64", "Microsoft.VC143.CRT")))
        if crt:  # app-local C runtime: no separate VC++ redistributable install needed
            files += [(p, os.path.basename(p)) for p in sorted(glob.glob(os.path.join(crt[-1], "*.dll")))]
    elif host() == "macos":
        # one app: the US build's bundle as cmake --install leaves it (its
        # non-system libraries copied in), the other builds' executables
        # beside its own in Contents/MacOS, then signed again (sign_macos: with
        # a Developer ID and notarized when the environment has them, else ad
        # hoc: Open Anyway once)
        stages = {}
        for short, out in outs.items():
            stage = os.path.join(out, "stage")
            shutil.rmtree(stage, ignore_errors=True)
            run(["cmake", "--install", out, "--prefix", stage])
            stages[short] = os.path.join(stage, f"ttyd-{short}.app")
            check_no_disc_data([os.path.join(stages[short], "Contents", "MacOS", f"ttyd-{short}")], VERSIONS[short])
        app = os.path.join(dist, "_app", "Paper Cut.app")
        shutil.rmtree(os.path.dirname(app), ignore_errors=True)
        shutil.copytree(stages["us"], app, symlinks=True)
        for short in outs:
            if short != "us":
                shutil.copy2(os.path.join(stages[short], "Contents", "MacOS", f"ttyd-{short}"),
                             os.path.join(app, "Contents", "MacOS", f"ttyd-{short}"))
        notarized = sign_macos(app, dist)  # (ad hoc without a Developer ID in the environment)
        files.append((app, "Paper Cut.app"))
    else:
        for short, out in outs.items():
            exe = os.path.join(out, f"ttyd-{short}")
            check_no_disc_data([exe], VERSIONS[short])
            files.append((exe, f"ttyd-{short}"))
        files += [(p, os.path.basename(p)) for p in sorted(glob.glob(os.path.join(us, "*.so*")))]
        files.append((os.path.join(ROOT, "port", "assets", "icon", "papercut-256.png"), "papercut.png"))
    # the third-party notices: every component the builds embed and link, with
    # its licence text (tools/port/third_party.py), and the font's licence
    notices = os.path.join(dist, f"_{name}_THIRD_PARTY.txt")
    open(notices, "w", encoding="utf-8", newline="\n").write(third_party.text(us))
    files += [(os.path.join(ROOT, "README.md"), "README.md"), (os.path.join(ROOT, "LICENSE"), "LICENSE.txt"),
              (notices, "THIRD_PARTY.txt"), (os.path.join(ROOT, "port", "assets", "fonts", "OFL.txt"), "OFL.txt"),
              (os.path.join(ROOT, "docs", "MODDING.md"), "docs/MODDING.md"),
              (os.path.join(ROOT, "docs", "LUA_MODDING.md"), "docs/LUA_MODDING.md")]
    for path in glob.glob(os.path.join(ROOT, "examples", "mods", "**", "*"), recursive=True):
        if os.path.isfile(path):
            files.append((path, os.path.relpath(path, ROOT).replace(os.sep, "/")))
    # the example code mod, built for each version (lib/<platform>/<game ID>/, port/mods/mods.cpp)
    for short, out in outs.items():
        lib = next((p for p in sorted(glob.glob(os.path.join(out, "mods", "hello-native.*")))
                    if os.path.splitext(p)[1] in (".dll", ".so", ".dylib")), None)
        if lib:
            files.append((lib, f"examples/mods/hello-native/lib/{plat}/{VERSIONS[short]}/{os.path.basename(lib)}"))
    if host() == "windows":
        # Authenticode, with a certificate in the environment: the executables, and the
        # libraries no one signed yet (Microsoft's keep their own signature)
        if windows_signing(dist) is not None and signtool() is not None:
            def unsigned(path):
                return subprocess.run([signtool(), "verify", "/pa", "/q", path], capture_output=True).returncode != 0
            sign_windows([src for src, _ in files if src.endswith(".exe") or (src.endswith(".dll") and unsigned(src))],
                         dist)
        else:
            sign_windows([], dist)  # (says why not)
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
        open(os.path.join(tree, "HOW TO PLAY.txt"), "w", encoding="utf-8").write(how_to_play(notarized))
        run(["ditto", "-c", "-k", "--norsrc", "--noextattr", "--noqtn", "--keepParent", tree, zpath])
        shutil.rmtree(os.path.dirname(tree))
        shutil.rmtree(os.path.dirname(app), ignore_errors=True)
    else:
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            for src, arc in files:
                z.write(src, f"{name}/{arc}")  # (keeps the file's mode: the Linux executables stay ones)
            info = zipfile.ZipInfo(f"{name}/HOW TO PLAY.txt")
            info.compress_type, info.external_attr = zipfile.ZIP_DEFLATED, 0o644 << 16  # (rw-r--r--)
            z.writestr(info, how_to_play(notarized))
            if host() == "linux":
                # what the player starts: the US build, whose launcher starts the disc's version
                info = zipfile.ZipInfo(f"{name}/papercut")
                info.compress_type, info.external_attr = zipfile.ZIP_DEFLATED, 0o755 << 16  # (rwxr-xr-x)
                z.writestr(info, '#!/bin/sh\n# Paper Cut: the launcher (the US build), which starts the disc\'s '
                                 'version\nexec "$(dirname "$0")/ttyd-us" "$@"\n')
    os.remove(notices)
    print("wrote", zpath)
    return zpath


def how_to_play(notarized=False):
    start = {"windows": "Paper Cut.exe",
             "macos": "Paper Cut.app" if notarized else "Paper Cut.app (the first time macOS blocks it: System Settings >\n"
                      "Privacy & Security > Open Anyway; before macOS 15, right-click > Open)",
             "linux": "papercut"}[host()]
    return f"""Paper Cut: Paper Mario: The Thousand-Year Door, native

This package holds no game data. You need your own copy of the game: a disc
image of the Japanese, US or European release (.iso, .rvz, .gcm, .ciso, .gcz,
.wia, ...).

Start {start}. Add your disc image on the Play tab; Paper Cut names
its version and plays it with that version's build (ttyd-jp, ttyd-us or
ttyd-eu, all in this package). The list remembers your discs.
F1 (or a controller's Back/Select) opens the settings page in the game.

Mods go in the mods folder (Settings > Mods > Open the mods folder); the
examples/ folder here has some to try: data and code mods (docs/MODDING.md)
and Lua mods (docs/LUA_MODDING.md).
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
    # the release variant: the native code optimized and built with NDEBUG, as the desktop builds are.
    # The debug variant kept Aurora's internal checks, and one of them (GXBegin without a GXEnd, which
    # the game's own code does: sysDummyDraw) aborted the first frame. Both are signed with the same
    # (debug) key (port/android/app/build.gradle), so an update installs over an earlier APK.
    run([gradle, "--no-daemon", "-p", os.path.join(ROOT, "port", "android"), "-PnoDiscAssets",
         f"-PTTYD_PORT_VERSION={version_string}", "assembleJpRelease", "assembleUsRelease", "assembleEuRelease"],
        env=env)
    out = []
    for short in ("jp", "us", "eu"):
        apk = sorted(glob.glob(os.path.join(ROOT, "port", "android", "app", "build", "outputs", "apk", short, "release",
                                            "*.apk")))
        if not apk:
            sys.exit(f"release: no {short} APK")
        dst = os.path.join(dist, f"papercut-{version_string}-{short}-android-arm64.apk")
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
    dst = os.path.join(dist, f"papercut-{version_string}-{short}-ios-unsigned.ipa")
    shutil.copy(os.path.join(out, f"ttyd-{short}.ipa"), dst)
    print("wrote", dst)
    return dst


def package_linux_container(version_string, dist):
    """The Linux x86-64 packages (the zip, the AppImage and the Flatpak),
    built by this script inside an Ubuntu container over this checkout
    (Docker, OrbStack...; on Apple silicon the x86-64 container runs
    emulated)."""
    import linux_container
    if linux_container.run(["python3", "tools/port/release.py", version_string, "--targets", "desktop",
                            "--out", "/src/dist-linux"]) != 0:
        sys.exit("release: the Linux packages' build failed (in the container)")
    out = []
    for pattern in ("*.zip", "*.AppImage", "*.flatpak"):
        for z in sorted(glob.glob(os.path.join(ROOT, "dist-linux", pattern))):
            out.append(shutil.copy(z, dist))
            print("wrote", out[-1])
    return out


REPOSITORY = "LLCOOLT/paper-cut"
ALTSTORE_SOURCE = "altstore-source.json"
NAMES = {"jp": ("JP", "Japanese", "G8MJ01"), "us": ("US", "US", "G8ME01"), "eu": ("EU", "European", "G8MP01")}


def highlights(version_string):
    """The release notes' Highlights bullets (docs/release_notes/VERSION.md), as lines"""
    path = os.path.join(ROOT, "docs", "release_notes", f"{version_string}.md")
    if not os.path.exists(path):
        return []
    out, inside = [], False
    for line in open(path, encoding="utf-8").read().splitlines():
        if line.startswith("**"):
            inside = line.strip().strip("*").strip().lower() == "highlights"
        elif inside and line.startswith("- "):
            out.append(line[2:].strip())
    return out


def altstore_source(dist, version_string, tag=None):
    """altstore-source.json, an AltStore / SideStore source (https://faq.altstore.io/developers/make-a-source)
    for the IPAs in dist: one app per game version, this release's IPA as its version. Uploaded with the
    release, so releases/latest/download/altstore-source.json is always the newest (docs/UPDATES.md); the
    game's iOS notice adds it (port/settings/features/update_check.cpp). None when dist has no IPAs."""
    import datetime
    import json
    import plistlib
    tag = tag or f"v{version_string}"
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    notes = "\n".join("- " + h for h in highlights(version_string)) or f"Paper Cut {version_string}."
    icon = f"https://raw.githubusercontent.com/{REPOSITORY}/main/port/assets/icon/papercut-512.png"
    apps = []
    for short in VERSIONS:
        name = f"papercut-{version_string}-{short}-ios-unsigned.ipa"
        ipa = os.path.join(dist, name)
        if not os.path.exists(ipa):
            continue
        with zipfile.ZipFile(ipa) as z:
            plists = [n for n in z.namelist() if re.fullmatch(r"Payload/[^/]+\.app/Info\.plist", n)]
            if not plists:
                sys.exit(f"release: {name} has no Payload/*.app/Info.plist")
            info = plistlib.loads(z.read(plists[0]))
        label, language, game_id = NAMES[short]
        url = f"https://github.com/{REPOSITORY}/releases/download/{tag}/{name}"
        size = os.path.getsize(ipa)
        version = {"version": info["CFBundleShortVersionString"], "buildVersion": info["CFBundleVersion"],
                   "date": date, "localizedDescription": notes, "downloadURL": url, "size": size,
                   "minOSVersion": info.get("MinimumOSVersion", "14.0")}
        apps.append({
            "name": f"Paper Cut ({label})",
            "bundleIdentifier": info["CFBundleIdentifier"],
            "developerName": "Paper Cut",
            "subtitle": f"The Thousand-Year Door, {language} version ({game_id})",
            "localizedDescription": (
                f"Paper Cut plays the {language} version ({game_id}) of Paper Mario: The Thousand-Year Door, from "
                "the game's C decompilation, with no emulator. It includes no game data: add a disc image made "
                "from your own disc in the Files app. A fan project, not affiliated with or endorsed by Nintendo."),
            "iconURL": icon,
            "tintColor": "#CF7017",
            "category": "games",
            "screenshots": [],
            "versions": [version],
            # (older AltStore and SideStore read these)
            "version": version["version"], "versionDate": date, "versionDescription": notes,
            "downloadURL": url, "size": size,
            "appPermissions": {
                "entitlements": [],
                "privacy": {k: v for k, v in info.items() if k.startswith("NS") and k.endswith("UsageDescription")},
            },
        })
    if not apps:
        return None
    source = {
        "name": "Paper Cut",
        "subtitle": "Paper Mario: The Thousand-Year Door, from its decompilation",
        "description": "Paper Cut's iOS and iPadOS apps, one per game version. Each release adds its version here.",
        "identifier": "io.github.ttyd-port.source",
        "sourceURL": f"https://github.com/{REPOSITORY}/releases/latest/download/{ALTSTORE_SOURCE}",
        "iconURL": icon,
        "website": f"https://github.com/{REPOSITORY}",
        "tintColor": "#CF7017",
        "nsfw": False,
        "featuredApps": [a["bundleIdentifier"] for a in apps],
        "apps": apps,
        "news": [],
    }
    path = os.path.join(dist, ALTSTORE_SOURCE)
    open(path, "w", encoding="utf-8", newline="\n").write(json.dumps(source, indent=2, ensure_ascii=False) + "\n")
    print("wrote", path)
    return path


def checksums(files):
    return [f"{hashlib.sha256(open(f, 'rb').read()).hexdigest()}  {os.path.basename(f)}" for f in files]


def tested_on(version_string):
    """docs/tested_on.json's rows for this release as a Markdown table, or ""
    when none has it (a row that lists the release; "not yet" rows too)."""
    import json
    path = os.path.join(ROOT, "docs", "tested_on.json")
    if not os.path.exists(path):
        return ""
    data = json.load(open(path, encoding="utf-8"))
    levels = data.get("levels", [])
    rows = [d for d in data.get("devices", []) if version_string in d.get("releases", [])]
    if not rows:
        return ""
    rows.sort(key=lambda d: levels.index(d["how"]) if d.get("how") in levels else len(levels))
    out = ["**Tested on**", "", "| Device | System | Versions | How far | |", "|---|---|---|---|---|"]
    for d in rows:
        cells = [d.get("device", ""), d.get("system", ""), ", ".join(d.get("versions", [])), d.get("how", ""),
                 d.get("what", "")]
        out.append("| " + " | ".join(c.replace("|", "\\|") for c in cells) + " |")
    return "\n".join(out) + "\n"


def notes(version_string, sums):
    template = os.path.join(ROOT, "docs", "release_notes", f"{version_string}.md")
    body = open(template, encoding="utf-8").read() if os.path.exists(template) else f"Release {version_string}.\n"
    # what it was run on (docs/tested_on.json), unless the notes have their own
    table = tested_on(version_string)
    if table and "**Tested on**" not in body:
        body = body.rstrip("\n") + "\n\n" + table
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
        run([gh, "release", "create", tag, "--draft", "--title", f"Paper Cut {version_string}",
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
    ap.add_argument("--tested-on", action="store_true",
                    help="only print the release's 'Tested on' table (docs/tested_on.json), for the README")
    ap.add_argument("--altstore-source", action="store_true",
                    help="only write altstore-source.json from the IPAs already in --out (TAG from --publish, "
                         "else vVERSION); nothing is built, removed or uploaded")
    a = ap.parse_args()
    if a.tested_on:
        print(tested_on(a.version) or f"(docs/tested_on.json has no rows for {a.version})")
        return 0
    if a.altstore_source:
        return 0 if altstore_source(a.out, a.version, a.publish) else f"release: no IPAs in {a.out}"
    targets = [t for t in a.targets.split(",") if t and not (a.no_android and t == "android")]
    unknown = set(targets) - {"desktop", "android", "ios", "linux"}
    if unknown:
        sys.exit(f"release: unknown target(s) {', '.join(sorted(unknown))}")
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(a.out)
    files = []
    if "desktop" in targets:
        # one package: every version's build, behind the launcher
        files.append(package_desktop({short: build(short, a.version) for short in VERSIONS}, a.version, a.out))
        if host() == "linux":
            # the AppImage and the Flatpak, from the zip (tools/port/linux_packages.py)
            run([sys.executable, os.path.join(ROOT, "tools", "port", "linux_packages.py"), files[-1], a.out],
                retry=False)
            files += sorted(glob.glob(os.path.join(a.out, "*.AppImage")) + glob.glob(os.path.join(a.out, "*.flatpak")))
    if "ios" in targets:
        # an app per version (an iOS app can't start another program)
        for short in VERSIONS:
            files.append(package_ios(short, build(short, a.version, "ios"), a.version, a.out))
    if "android" in targets:
        files += package_android(a.out, a.version)
    if "linux" in targets:
        files += package_linux_container(a.version, a.out)
    for secret in ("_sign.pfx", "_notary.p8"):  # (secret_file's, written from the environment)
        if os.path.exists(os.path.join(a.out, secret)):
            os.remove(os.path.join(a.out, secret))
    sums = checksums(files)
    # the AltStore / SideStore source, beside the IPAs (the Mac's run): uploaded with them, not in the sums
    if "ios" in targets and altstore_source(a.out, a.version, a.publish):
        files.append(os.path.join(a.out, ALTSTORE_SOURCE))
    # (LF line endings, written on Windows too: `sha256sum -c` takes a CR as
    # part of the file's name)
    open(os.path.join(a.out, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(sums) + "\n")
    open(os.path.join(a.out, "notes.md"), "w", encoding="utf-8", newline="\n").write(notes(a.version, sums))
    if a.publish:
        publish(a.publish, a.version, files, sums, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
