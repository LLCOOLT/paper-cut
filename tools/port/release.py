#!/usr/bin/env python3
"""Build and package a release of the port (docs/RELEASING.md).

  release.py VERSION [--out DIR] [--targets LIST] [--no-android] [--publish TAG]

Targets (--targets, comma-separated; by default this machine's):
  desktop  this machine's own build: Windows, macOS or Linux (Linux: the zip,
           Paper_Cut-VERSION-x86_64.AppImage and a Flatpak,
           tools/port/linux_packages.py)
  android  the three APKs (debug-signed), with an Android SDK
  ios      an unsigned .ipa per version, for sideloading (on a Mac)
  linux    the Linux x86-64 build, inside a Docker container (on a Mac or
           another machine with Docker: tools/port/linux-release.Dockerfile)
  portmaster  optional, on any machine with clang and lld: the Linux ARM64
           builds, cross-compiled against a Debian sysroot
           (cmake/toolchains/linux-arm64.cmake, tools/port/arm64_sysroot.py),
           as papercut-VERSION-portmaster.zip for handhelds
           (tools/port/portmaster_package.py, docs/HANDHELDS.md)
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
    if kind == "arm64":
        # Linux ARM64 handhelds, cross-compiled (the pkg-config wrapper runs this Python)
        args += [f"--toolchain={os.path.join(TOOLCHAINS, 'linux-arm64.cmake')}",
                 f"-DPython3_EXECUTABLE={sys.executable}"]
    elif kind == "ios":
        # unsigned: AltStore, SideStore and Sideloadly sign it on install
        args += [f"--toolchain={os.path.join(TOOLCHAINS, 'ios.cmake')}", "-DTTYD_IOS_CODESIGN_IDENTITY="]
    elif host() == "windows":
        args.append(f"--toolchain={os.path.join(TOOLCHAINS, 'windows-clang.cmake')}")
    elif host() == "linux":
        # (the C++ runtime linked in: the packages ask only for 22.04's glibc,
        # which tools/port/linux_packages.py checks)
        args += [f"--toolchain={os.path.join(TOOLCHAINS, 'linux-clang.cmake')}", "-DTTYD_STATIC_CXX_RUNTIME=ON"]
    # --fresh: a new cache each time, so an option set by hand in an earlier configure (a dev switch) never
    # reaches a release; the folder's top-level libraries and programs go too, so the build makes again
    # exactly the ones this configuration has (the package takes every *.dll / *.so* there: one left from an
    # earlier configuration, a dependency since linked statically, was zipped)
    run(args[:1] + ["--fresh"] + args[1:])
    for pattern in ("*.dll", "*.exe", "*.so", "*.so.*", "ttyd-jp", "ttyd-us", "ttyd-eu"):
        for stale in glob.glob(os.path.join(out, pattern)):
            if os.path.isfile(stale):
                os.remove(stale)
    targets = ["ttyd", "ttyd_ipa"] if kind == "ios" else ["ttyd"] if kind == "arm64" else ["ttyd", "hello-native"]
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


# decoded secrets written to disk, and this run's keychain: removed by cleanup_secrets whatever happens
# (main's finally: a failed signtool, notarization or build exits in between, and they stayed in dist/)
SECRETS = []
KEYCHAINS = []


def secret_file(dist, name, b64_var, path_var):
    """A secret file: $path_var's path, or $b64_var's base64 written to dist
    (removed by cleanup_secrets at the end). None without either."""
    if env(path_var):
        return env(path_var)
    if env(b64_var):
        import base64
        path = os.path.join(dist, f"_{name}")
        SECRETS.append(path)
        open(path, "wb").write(base64.b64decode(env(b64_var)))
        return path
    return None


def cleanup_secrets():
    for path in SECRETS:
        try:
            os.remove(path)
        except OSError:
            pass
    for keychain in KEYCHAINS:  # (deleting it also takes it off the search list)
        subprocess.run(["security", "delete-keychain", keychain], capture_output=True)
    SECRETS.clear()
    KEYCHAINS.clear()


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
        SECRETS.append(p12)
        password = secrets.token_hex(16)
        subprocess.run(["security", "delete-keychain", keychain], capture_output=True)
        KEYCHAINS.append(keychain)  # (deleted at the end, whatever happens: main's cleanup)
        try:
            open(p12, "wb").write(base64.b64decode(env("PAPERCUT_MACOS_CERT_P12_BASE64")))
            run(["security", "create-keychain", "-p", password, keychain], retry=False)
            run(["security", "set-keychain-settings", "-lut", "3600", keychain], retry=False)
            run(["security", "unlock-keychain", "-p", password, keychain], retry=False)
            r = subprocess.run(["security", "import", p12, "-k", keychain, "-P",
                                env("PAPERCUT_MACOS_CERT_PASSWORD") or "", "-T", "/usr/bin/codesign"])
        finally:
            if os.path.exists(p12):
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


# --publish (set by main): a package that can't be checked for the disc's bytes, or a Linux package not made, stops it
PUBLISHING = [False]


def asset_bytes(version):
    """Byte strings from the disc's assets, to look for in a release build: from the disc's own main.dol
    (orig/<version>/sys/main.dol, read as tools/port/extract_assets.py does: any runner with the disc
    linked can check, the decomp build or not), else the decomp build's includes (build/<version>). Several
    runs of varied bytes from each (start, middle, end: blank areas would match anything). [] without
    either."""
    import extract_assets
    blobs = extract_assets.disc_assets(version) or {}
    if not blobs:
        for inc in glob.glob(os.path.join(ROOT, "build", version, "include", "assets", "*.inc")):
            nums = re.findall(r"0x([0-9A-Fa-f]{2})\b", open(inc, encoding="utf-8", errors="replace").read())
            blobs[os.path.basename(inc)] = bytes(int(n, 16) for n in nums)
    found = []
    for name, data in sorted(blobs.items()):
        for start in (len(data) // 8, len(data) // 2, len(data) * 3 // 4):
            for at in range(start, len(data) - 32, 16):
                chunk = data[at:at + 32]
                if len(set(chunk)) > 12:
                    found.append((os.path.basename(name), chunk))
                    break
    return found


def check_no_disc_data(paths, version):
    """Every file given (a package's programs and libraries) holds none of the disc's bytes"""
    probes = asset_bytes(version)
    if not probes:
        if PUBLISHING[0]:
            sys.exit(f"release: nothing of the {version} disc here (orig/{version}/sys/main.dol) to check the "
                     f"package against: link the data (tools/port/ci_link_data.py) before publishing")
        print(f"(no {version} disc data here to check against: the build used stand-ins anyway)")
        return
    for path in paths:
        blob = open(path, "rb").read()
        for name, chunk in probes:
            if chunk in blob:
                sys.exit(f"release: {os.path.basename(path)} contains bytes of the disc's {name}: not releasable")
    names = sorted({n for n, _ in probes})
    print(f"checked {len(paths)} file(s): no bytes of the disc's {', '.join(names)} ({len(probes)} probes)")


def check_package(path):
    """Every file inside a package (.zip, .apk, .ipa) holds none of any version's disc bytes: the package as
    uploaded, whichever runner or container made it (the Linux zip from the Mac's container, the Mac's app)"""
    probes = [(v, n, c) for v in VERSIONS.values() for n, c in asset_bytes(v)]
    have = {v for v, _, _ in probes}
    lacking = [v for v in VERSIONS.values() if v not in have]
    if lacking and PUBLISHING[0]:
        sys.exit(f"release: no disc data for {', '.join(lacking)} here (orig/<id>/sys/main.dol) to check "
                 f"{os.path.basename(path)} against: link it (tools/port/ci_link_data.py) before publishing")
    if not probes:
        print(f"(no disc data here to check {os.path.basename(path)} against)")
        return
    count = 0
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            blob = z.read(info)
            count += 1
            for v, name, chunk in probes:
                if chunk in blob:
                    sys.exit(f"release: {os.path.basename(path)}: {info.filename} contains bytes of the {v} disc's "
                             f"{name}: not releasable")
    print(f"checked {os.path.basename(path)}: {count} files, no bytes of the {', '.join(sorted(have))} discs")


def binaries(folder):
    """The programs and libraries in a folder, to check (an app bundle's, a stage's)"""
    out = []
    for p in glob.glob(os.path.join(folder, "**", "*"), recursive=True):
        if os.path.isfile(p) and not os.path.islink(p) and (
                p.endswith((".dll", ".exe", ".so", ".dylib")) or ".so." in os.path.basename(p) or
                os.path.basename(os.path.dirname(p)) == "MacOS" or os.access(p, os.X_OK) and os.name != "nt"):
            out.append(p)
    return out


def tracked_files(folder):
    """The files git tracks under folder (relative to ROOT, with /), as they are in the checkout"""
    r = subprocess.run(["git", "-C", ROOT, "ls-files", "-z", "--", folder], capture_output=True)
    if r.returncode != 0:
        sys.exit(f"release: git ls-files {folder} failed: {r.stderr.decode(errors='replace').strip()}")
    return sorted(p for p in r.stdout.decode("utf-8").split("\0") if p and os.path.isfile(os.path.join(ROOT, p)))


def c_runtime():
    """The newest Visual C++ runtime's DLLs to ship beside the builds (Microsoft.VC14x.CRT): from any Visual
    Studio or Build Tools edition and version, in either Program Files (as the toolchain finds MSVC,
    cmake/toolchains/windows-clang.cmake), or vswhere's; $PAPERCUT_VC_REDIST, a ...\\VC\\Redist\\MSVC folder,
    over all of them. Stops the release when none is found: a zip without it fails to start."""
    machine = "arm64" if arch() == "arm64" else "x64"
    roots = []
    if env("PAPERCUT_VC_REDIST"):
        roots.append(env("PAPERCUT_VC_REDIST"))
    for pf in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
        if pf:
            roots += glob.glob(os.path.join(pf, "Microsoft Visual Studio", "*", "*", "VC", "Redist", "MSVC"))
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)") or r"C:\Program Files (x86)", "Microsoft Visual Studio",
                           "Installer", "vswhere.exe")
    if os.path.exists(vswhere):
        r = subprocess.run([vswhere, "-all", "-products", "*", "-property", "installationPath"],
                           capture_output=True, text=True)
        roots += [os.path.join(p.strip(), "VC", "Redist", "MSVC") for p in r.stdout.splitlines() if p.strip()]

    def version(path):  # (14.44.35112 and so on: by number, not by text)
        return tuple(int(n) for n in re.findall(r"\d+", os.path.basename(os.path.dirname(os.path.dirname(path)))))
    found = [d for root in dict.fromkeys(roots)
             for d in glob.glob(os.path.join(root, "14.*", machine, "Microsoft.VC14*.CRT"))]
    found = [d for d in found if glob.glob(os.path.join(d, "vcruntime140*.dll"))]
    if not found:
        sys.exit(f"release: no Visual C++ runtime ({machine}, Microsoft.VC14x.CRT) in any Visual Studio's "
                 f"VC\\Redist\\MSVC (or $PAPERCUT_VC_REDIST): the package would not start without the "
                 f"VC++ redistributable")
    crt = max(found, key=version)
    print("the C runtime from", crt)
    return sorted(glob.glob(os.path.join(crt, "*.dll")))


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
        # the app-local C runtime: no separate VC++ redistributable install needed (the builds use the DLL
        # runtime, so without it the zip doesn't start on a PC that lacks the redistributable)
        files += [(p, os.path.basename(p)) for p in c_runtime()]
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
    # (the checkout's own files only: a local build or notes left untracked in examples/mods is not shipped)
    for rel in tracked_files("examples/mods"):
        files.append((os.path.join(ROOT, rel), rel))
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
    check_no_disc_data(binaries(os.path.join(out, f"ttyd-{short}.app")), VERSIONS[short])
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
    for z in linux_package_files(os.path.join(ROOT, "dist-linux"), version_string):
        out.append(shutil.copy(z, dist))
        print("wrote", out[-1])
    return out


def linux_package_files(folder, version_string):
    """This version's Linux packages in folder: the zip, the AppImage
    (Paper_Cut-<version>-x86_64.AppImage, tools/port/linux_packages.py) and
    its .zsync, the Flatpak; not an earlier release's left there."""
    out = []
    patterns = (f"papercut-{version_string}-linux-*.zip", f"Paper_Cut-{version_string}-*.AppImage",
                f"Paper_Cut-{version_string}-*.AppImage.zsync", f"papercut-{version_string}-linux-*.flatpak")
    for pattern in patterns:
        found = sorted(glob.glob(os.path.join(folder, pattern)))
        if not found and PUBLISHING[0]:
            # (a published release without its AppImage, .zsync or Flatpak leaves those players on the last
            # one: the AppImage's update information points at the newest release)
            sys.exit(f"release: no {pattern} in {folder} (linux_packages.py's tool missing?): not published")
        out += found
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
        # a new draft (created once: it isn't retried, the uploads are)
        run([gh, "release", "create", tag, "--draft", "--title", f"Paper Cut {version_string}",
             "--notes-file", os.path.join(dist, "notes.md")], retry=False)
    # the files (in place of the same names from an earlier run, or beside another runner's), then the
    # checksums of what the release really holds now: SHA256SUMS.txt and the notes' SHA-256 block made again
    # from its files as uploaded, every runner's and every run's (a re-run's rebuilt zip and APKs hash
    # differently: the first run's sums stayed, and `sha256sum -c` failed for players)
    run([gh, "release", "upload", tag, "--clobber", *files])
    sums = release_checksums(gh, tag, dist)
    open(os.path.join(dist, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(sums) + "\n")
    run([gh, "release", "upload", tag, "--clobber", os.path.join(dist, "SHA256SUMS.txt")])
    for old in release_assets(gh, tag):  # (per-runner sums files from before: all in SHA256SUMS.txt now)
        if re.fullmatch(r"SHA256SUMS-\w+\.txt", old):
            run([gh, "release", "delete-asset", tag, old, "-y"])
    body = gh_out(gh, ["release", "view", tag, "--json", "body", "-q", ".body"])
    if body is None:
        sys.exit("release: can't read the release's notes")
    path = os.path.join(dist, "notes-added.md")
    open(path, "w", encoding="utf-8", newline="\n").write(with_sums(body, sums))
    run([gh, "release", "edit", tag, "--notes-file", path])


SUMS_BLOCK = re.compile(r"\n*SHA-256( \([^)\n]*\))?:\n```\n.*?```\n?", re.S)


def with_sums(body, sums):
    """the notes with one SHA-256 block, these sums, in place of every one there was (the first run's, the
    per-runner blocks of earlier versions of this script)"""
    body = SUMS_BLOCK.sub("", body).rstrip("\n")
    return body + "\n\nSHA-256:\n```\n" + "\n".join(sums) + "\n```\n"


def release_assets(gh, tag):
    names = gh_out(gh, ["release", "view", tag, "--json", "assets", "-q", ".assets[].name"])
    if names is None:
        sys.exit(f"release: can't list release {tag}'s files")
    return [n for n in names.splitlines() if n.strip()]


def release_checksums(gh, tag, dist):
    """SHA-256 of every file on the release (but the sums and the AltStore source), downloaded back"""
    names = [n for n in release_assets(gh, tag) if not n.startswith("SHA256SUMS") and n != ALTSTORE_SOURCE]
    got = os.path.join(dist, "_release_files")
    shutil.rmtree(got, ignore_errors=True)
    os.makedirs(got)
    try:
        args = [gh, "release", "download", tag, "--dir", got, "--clobber"]
        for n in names:
            args += ["--pattern", n]
        run(args)
        missing = [n for n in names if not os.path.isfile(os.path.join(got, n))]
        if missing:
            sys.exit(f"release: {', '.join(missing)} didn't download from release {tag}")
        return checksums([os.path.join(got, n) for n in sorted(names)])
    finally:
        shutil.rmtree(got, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version", help="e.g. 0.1.0")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist"))
    ap.add_argument("--targets", default=default_targets(), help="desktop,android,ios,linux,portmaster")
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
    unknown = set(targets) - {"desktop", "android", "ios", "linux", "portmaster"}
    if unknown:
        sys.exit(f"release: unknown target(s) {', '.join(sorted(unknown))}")
    if a.publish:
        PUBLISHING[0] = True  # (an upload is never unchecked for the disc's bytes)
        # the checkout as the tag has it: the runners keep their folders between runs (clean: false, for the
        # links to the disc data and the builds), so a file changed by hand there would be built in
        # (a submodule's own working tree is left out: the build applies port/patches/aurora to Aurora's)
        changed = subprocess.run(["git", "-C", ROOT, "status", "--porcelain", "--untracked-files=no",
                                  "--ignore-submodules=dirty"],
                                 capture_output=True, text=True).stdout.strip()
        if changed:
            sys.exit("release: the checkout has changes the tag hasn't; not published:\n" + changed)
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(a.out)
    try:
        return make(a, targets)
    finally:
        cleanup_secrets()  # (a decoded .pfx/.p8/.p12 and the release keychain never outlive the run)


def make(a, targets):
    files = []
    if "desktop" in targets:
        # one package: every version's build, behind the launcher
        files.append(package_desktop({short: build(short, a.version) for short in VERSIONS}, a.version, a.out))
        if host() == "linux":
            # the AppImage (with its .zsync) and the Flatpak, from the zip
            # (tools/port/linux_packages.py), which first checks the builds run on Ubuntu 22.04
            run([sys.executable, os.path.join(ROOT, "tools", "port", "linux_packages.py"), files[-1], a.out],
                retry=False)
            files += [p for p in linux_package_files(a.out, a.version) if not p.endswith(".zip")]
    if "ios" in targets:
        # an app per version (an iOS app can't start another program)
        for short in VERSIONS:
            files.append(package_ios(short, build(short, a.version, "ios"), a.version, a.out))
    if "android" in targets:
        files += package_android(a.out, a.version)
    if "linux" in targets:
        files += package_linux_container(a.version, a.out)
    if "portmaster" in targets:
        # Linux ARM64 handhelds (not in the release workflow yet): the sysroot, the three builds, the port
        run([sys.executable, os.path.join(ROOT, "tools", "port", "arm64_sysroot.py")])
        import portmaster_package
        builds = {short: os.path.join(build(short, a.version, "arm64"), f"ttyd-{short}") for short in VERSIONS}
        files.append(portmaster_package.write(os.path.join(a.out, f"papercut-{a.version}-portmaster.zip"),
                                              a.version, builds))
    cleanup_secrets()  # (before anything is uploaded from the folder they're in)
    for f in files:  # (each package as it will be uploaded, every file in it)
        if f.endswith((".zip", ".apk", ".ipa")):
            check_package(f)
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
