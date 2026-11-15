#!/usr/bin/env python3
"""The second screen's Start menu on Android (docs/SECOND_SCREEN_MENU.md), checked
in the Android emulator with a second display: a dual-screen handheld such as the
AYN Thor without the device. Headless (the emulator with -no-window), PASS/FAIL
lines, exit 0 only when every check passes.

  android_dualscreen_test.py [--apk APK] [--disc ISO] [--keep] [--no-create]
                             [--boot-timeout S] [--timeout S] [--out DIR]

What it does:
  1. The SDK: $ANDROID_HOME, else ~/Android/sdk (tools/port/local_ci.py's). The
     emulator and the system image (Android 14, Google APIs, x86_64: arm64 apps
     run there through the image's ARM translation) are installed with the SDK's
     sdkmanager when missing.
  2. The AVD "papercut_thor": landscape 1920x1080 at 420 dpi, as the Thor's main
     screen; recreated when its config is not this script's. The second display
     (1240x1080 at 320 dpi, as the Thor's lower one) is added and removed at run
     time through the emulator's console (`adb emu multidisplay`).
  3. The US APK (--apk; default port/android's assembleUsRelease or
     assembleUsDebug output) installed, the disc (--disc; default orig/G8ME01's
     ISO) copied once into the app's external files folder on the emulator only,
     and env.txt there: TTYD_DISC, a TTYD_MAP start (a new game on gor_01),
     TTYD_FAST=1, a fresh TTYD_USER_DIR, TTYD_COMPANION_DUMP=1 and
     TTYD_COMPANION_ANY_DISPLAY=1 (the emulator's display is not a built-in one,
     which the app otherwise requires).
  4. Checks, each a PASS/FAIL line:
     - presentation   the second screen shows on the second display (log)
     - menu           its picture is the Start menu (menu mode: the A button drawn)
     - tap            a tap the game acts on: the menu's opening asks for the
                      Mailbox SP, and the game adds it ([companion] lines)
     - touch          a tap on its R and L buttons changes the menu's tab
                      ([companion] menu input ... tab N, TTYD_COMPANION_DUMP)
     - game           the main display's picture keeps changing (the game runs)
     - rotation       the main display turned upside down and back: the app
                      alive, the second screen still the menu
     - display2-gone  the second display removed: the app alive, no crash
     - display2-back  added back: the second screen there again, the menu
     - background     Home and back: the app alive, the second screen again
  Screenshots of both displays (screencap -d) go to --out.

The emulator's GPU is the host's (gfxstream): speed and driver behaviour say
nothing about a device; logic and layout do. The emulator is shut down at the
end unless --keep. The AVD (about 7 GB once booted: userdata, the disc copy)
stays in ~/.android/avd (papercut_thor.avd and papercut_thor.ini); delete both
when done (the script writes them again).

Needs: a JDK 17 only when sdkmanager has to install something ($JAVA_HOME, else
~/Android/jdk-17), and CPU virtualization for the emulator (Windows: the Windows
Hypervisor Platform feature or the Android Emulator hypervisor driver; without either the emulator
falls back to software emulation, far too slow for this).
"""
import argparse
import glob
import math
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AVD = "papercut_thor"
# x86_64 hosts: the x86_64 image (the arm64 game through its ARM translation);
# arm64 hosts (Apple silicon): the arm64 image, the game natively
HOST_ARM = platform.machine().lower() in ("arm64", "aarch64")
IMAGE = "system-images;android-34;google_apis;" + ("arm64-v8a" if HOST_ARM else "x86_64")
PACKAGE = "dev.ttyd.port.us"
MAIN = (1920, 1080, 420)
SECOND = (1240, 1080, 320)
SECOND_ID = 1  # the emulator console's id for the added display
# TtydCompanion.java's colours: the night background, the A button
NIGHT = (0x18, 0x1C, 0x4A)
A_GREEN = (0x28, 0xAA, 0x78)

AVD_CONFIG = {
    "hw.lcd.width": str(MAIN[0]),
    "hw.lcd.height": str(MAIN[1]),
    "hw.lcd.density": str(MAIN[2]),
    "hw.initialOrientation": "landscape",
    "hw.ramSize": "4096M",
    "disk.dataPartition.size": "8G",
    "hw.gpu.enabled": "yes",
    "hw.gpu.mode": "host",
    "hw.keyboard": "yes",
    "showDeviceFrame": "no",
    "sdcard.size": "64 MB",
    "hw.cpu.ncore": "4",
    "papercut.dualscreen": "1",  # this script's mark
}

results = []


def log(msg):
    print(msg, flush=True)


def check(name, ok, detail=""):
    results.append((name, ok))
    log(f"{'PASS' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}")
    return ok


# --- the SDK ---------------------------------------------------------------

class Sdk:
    def __init__(self):
        home = os.path.expanduser("~/Android")
        self.root = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or os.path.join(home, "sdk")
        jdk = os.environ.get("JAVA_HOME") or os.path.join(home, "jdk-17")
        self.env = dict(os.environ, ANDROID_HOME=self.root, ANDROID_SDK_ROOT=self.root, JAVA_HOME=jdk)
        exe = ".exe" if os.name == "nt" else ""
        bat = ".bat" if os.name == "nt" else ""
        self.adb_exe = os.path.join(self.root, "platform-tools", "adb" + exe)
        self.emulator_exe = os.path.join(self.root, "emulator", "emulator" + exe)
        tools = os.path.join(self.root, "cmdline-tools", "latest", "bin")
        self.sdkmanager = os.path.join(tools, "sdkmanager" + bat)
        self.serial = None

    def ensure(self):
        if not os.path.exists(self.adb_exe):
            sys.exit(f"no adb in {self.root} (set ANDROID_HOME)")
        want = []
        if not os.path.exists(self.emulator_exe):
            want.append("emulator")
        if not os.path.isdir(os.path.join(self.root, *IMAGE.split(";"))):
            want.append(IMAGE)
        if want:
            log(f"installing {', '.join(want)} with sdkmanager")
            subprocess.run([self.sdkmanager] + want, input="y\n" * 10, text=True, env=self.env, check=True,
                           stdout=subprocess.DEVNULL)

    def adb(self, *args, timeout=120, check=False, binary=False):
        cmd = [self.adb_exe] + (["-s", self.serial] if self.serial else []) + list(args)
        r = subprocess.run(cmd, capture_output=True, timeout=timeout, env=self.env)
        if check and r.returncode != 0:
            raise RuntimeError(f"{' '.join(args)}: {r.stderr.decode(errors='replace').strip()}")
        return r.stdout if binary else r.stdout.decode(errors="replace").replace("\r\n", "\n")

    def shell(self, cmd, timeout=120):
        return self.adb("shell", cmd, timeout=timeout)


def avd_dir():
    return os.path.join(os.path.expanduser("~"), ".android", "avd", AVD + ".avd")


def ensure_avd(sdk, recreate):
    cfg = os.path.join(avd_dir(), "config.ini")
    if os.path.exists(cfg) and not recreate:
        # (the emulator rewrites the file as "key = value")
        have = dict(map(str.strip, l.split("=", 1)) for l in open(cfg).read().splitlines() if "=" in l)
        if have.get("papercut.dualscreen") == "1":
            return
    # written directly rather than with avdmanager: no JDK needed, and every line is ours anyway
    log(f"creating the AVD {AVD}")
    shutil.rmtree(avd_dir(), ignore_errors=True)
    os.makedirs(avd_dir())
    _, api, tag, abi = IMAGE.split(";")
    config = {
        "avd.ini.encoding": "UTF-8",
        "AvdId": AVD,
        "avd.ini.displayname": AVD,
        "PlayStore.enabled": "false",
        "abi.type": abi,
        "hw.cpu.arch": "arm64" if HOST_ARM else "x86_64",
        "image.sysdir.1": "/".join(IMAGE.split(";")) + "/",
        "tag.id": tag,
        "target": api,
        **AVD_CONFIG,
    }
    with open(cfg, "w", newline="\n") as f:
        f.writelines(f"{k}={v}\n" for k, v in config.items())
    with open(avd_dir()[:-len(".avd")] + ".ini", "w", newline="\n") as f:
        f.write(f"avd.ini.encoding=UTF-8\npath={avd_dir()}\npath.rel=avd/{AVD}.avd\ntarget={api}\n")


def boot(sdk, timeout):
    already = [l.split()[0] for l in sdk.adb("devices").splitlines()[1:] if l.strip().startswith("emulator-")]
    if already:
        sys.exit(f"an emulator is already running ({already[0]}): close it first")
    # without CPU virtualization the emulator stalls at boot instead of failing: say so first
    accel = subprocess.run([sdk.emulator_exe, "-accel-check"], capture_output=True, text=True, env=sdk.env)
    if accel.returncode != 0:
        sys.exit("the emulator has no hardware acceleration here:\n  " + accel.stdout.strip().replace("\n", "\n  ")
                 + "\n(turn on virtualization (SVM / VT-x) in the firmware, then the Windows Hypervisor Platform"
                 " feature or the Android Emulator hypervisor driver)")
    log("starting the emulator (no window)")
    logf = open(os.path.join(OUT, "emulator.log"), "w")
    proc = subprocess.Popen([sdk.emulator_exe, "-avd", AVD, "-no-window", "-no-audio", "-no-boot-anim",
                             "-no-snapshot", "-gpu", "host", "-port", "5584"],
                            stdout=logf, stderr=subprocess.STDOUT, env=sdk.env, cwd=os.path.expanduser("~"))
    sdk.serial = "emulator-5584"
    end = time.time() + timeout
    while time.time() < end:
        if proc.poll() is not None:
            sys.exit(f"the emulator quit (code {proc.returncode}); see {logf.name}")
        try:
            if sdk.shell("getprop sys.boot_completed", timeout=15).strip() == "1":
                log(f"booted in {int(timeout - (end - time.time()))} s")
                return proc
        except subprocess.TimeoutExpired:
            pass
        time.sleep(5)
    proc.kill()
    sys.exit(f"the emulator didn't boot in {timeout} s (no hardware acceleration?); see {logf.name}")


def shutdown(sdk, proc):
    try:
        sdk.adb("emu", "kill", timeout=30)
    except Exception:
        pass
    try:
        proc.wait(60)
    except Exception:
        proc.kill()


# --- displays and pictures -------------------------------------------------

def displays(sdk):
    """{logical display id: (width, height, density)}"""
    out = {}
    text = sdk.shell("dumpsys display")
    for m in re.finditer(r"mBaseDisplayInfo=DisplayInfo\{(.*)", text):
        info = m.group(1)
        did = re.search(r"displayId (\d+)", info)
        size = re.search(r"real (\d+) x (\d+)", info)
        dens = re.search(r"density (\d+)", info)
        if did and size and dens:
            out[int(did.group(1))] = (int(size.group(1)), int(size.group(2)), int(dens.group(1)))
    return out


def second_display(sdk):
    for did, d in sorted(displays(sdk).items()):
        if did != 0:
            return did, d
    return None, None


class Picture:
    """A raw screencap (RGBA after a 12- or 16-byte header), also saved as a PNG."""

    def __init__(self, data):
        self.w, self.h, _ = struct.unpack("<III", data[:12])
        self.off = len(data) - self.w * self.h * 4
        if self.w <= 0 or self.h <= 0 or self.off not in (12, 16):
            raise ValueError("not a screencap")
        self.data = data

    def pixel(self, x, y):
        x, y = max(0, min(self.w - 1, int(x))), max(0, min(self.h - 1, int(y)))
        i = self.off + (y * self.w + x) * 4
        return tuple(self.data[i:i + 3])

    def save(self, path):
        stride = self.w * 4
        raw = b"".join(bytes(1) + self.data[self.off + y * stride:self.off + (y + 1) * stride] for y in range(self.h))

        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

        signature = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
        open(path, "wb").write(signature + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def read_png(path):
    """An 8-bit RGB/RGBA PNG (the emulator's screenshots) as a Picture."""
    data = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError("not an 8-bit RGB(A) PNG")
            bpp = 4 if ctype == 6 else 3
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            line = bytearray((a + b) & 0xFF for a, b in zip(line, prev))
        elif f == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - bpp] if i >= bpp else 0) + prev[i]) // 2) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b, c = prev[i], (prev[i - bpp] if i >= bpp else 0)
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        if bpp == 4:
            out[y * w * 4:(y + 1) * w * 4] = line
        else:
            for x in range(w):
                out[(y * w + x) * 4:(y * w + x) * 4 + 3] = line[x * 3:x * 3 + 3]
        prev = line
    return Picture(struct.pack("<III", w, h, 1) + bytes(out))


def shot(sdk, display, name):
    """A display's picture, saved as NAME.png; or None. display None: the main one (screencap);
    else the emulator console's id of an added display: screencap can't capture those (virtual
    displays), the console's screenshot can."""
    path = os.path.join(OUT, name + ".png")
    try:
        if display is None:
            pic = Picture(sdk.adb("exec-out", "screencap", binary=True, timeout=60))
            pic.save(path)
            return pic
        tmp = os.path.join(OUT, "console-shot")
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        sdk.adb("emu", "screenrecord", "screenshot", "--display", str(display), os.path.abspath(tmp), timeout=60)
        made = wait_for(lambda: glob.glob(os.path.join(tmp, "*.png")), 10, 1)
        if not made:
            return None
        time.sleep(0.5)  # (written by the emulator: let it finish)
        os.replace(made[0], path)
        shutil.rmtree(tmp, ignore_errors=True)
        return read_png(path)
    except Exception:
        return None


def near(c, want, tol=40):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def menu_layout(w, h, density):
    """TtydCompanion.Page.drawMenu's buttons: {name: (x, y)} in the display's pixels."""
    dp = density / 160.0
    bar_h = min(96 * dp, h * 0.3)
    pw, ph = w, w * 0.75
    if ph > h - bar_h:
        ph = h - bar_h
        pw = ph / 0.75
    top = max(0, (h - bar_h - ph) / 2)
    rad = min(bar_h * 0.3, w / 30.0)
    cy = min(h - bar_h / 2, top + ph + bar_h / 2)
    step, x = rad * 2.5, rad * 1.6
    right = w - rad * 1.6
    return {"L": (x, cy), "R": (x + 5 * step, cy), "A": (right, cy), "B": (right - step, cy), "rad": rad,
            "picture": ((w - pw) / 2, top, (w + pw) / 2, top + ph)}


def is_menu(pic, density):
    """The second screen shows the Start menu's picture: its A button drawn where drawMenu puts it."""
    if pic is None:
        return False
    lay = menu_layout(pic.w, view_height(pic), density)
    (ax, ay), rad = lay["A"], lay["rad"]
    # around the button, inside it: its middle is the white A
    ring = [(ax + 0.6 * rad * math.cos(k * math.pi / 4), ay + 0.6 * rad * math.sin(k * math.pi / 4))
            for k in range(8)]
    return sum(near(pic.pixel(x, y), A_GREEN) for x, y in ring) >= 3


def view_height(pic):
    """The page's height: the display's, less a navigation bar the system puts on a
    second display that shows system decorations (the emulator's does): the night
    background ends there."""
    for y in range(pic.h - 1, pic.h // 2, -1):
        if near(pic.pixel(2, y), NIGHT, 12):
            return y + 1
    return pic.h


def changed(a, b, step=12):
    """How many sampled pixels differ between two pictures."""
    if a is None or b is None or (a.w, a.h) != (b.w, b.h):
        return 0
    n = 0
    for y in range(0, a.h, step):
        for x in range(0, a.w, step):
            if any(abs(p - q) > 8 for p, q in zip(a.pixel(x, y), b.pixel(x, y))):
                n += 1
    return n


# --- the app ---------------------------------------------------------------

class Log:
    """The app's log (logcat, tag ttyd) since the test began."""

    def __init__(self, sdk):
        self.sdk = sdk
        sdk.adb("logcat", "-G", "16M")  # (offsets into it stay put for the whole run)
        sdk.adb("logcat", "-c")
        self.text = ""

    def read(self):
        self.text = self.sdk.adb("logcat", "-d", "-v", "brief", "-s", "ttyd:*", "SDL:*", "AndroidRuntime:E",
                                 "DEBUG:*", timeout=60)
        open(os.path.join(OUT, "logcat.txt"), "w", encoding="utf-8").write(self.text)
        return self.text

    def wait(self, pattern, timeout, after=0):
        """The first match of pattern past character `after`, or None."""
        end = time.time() + timeout
        rx = re.compile(pattern)
        while True:
            m = rx.search(self.read(), after)
            if m:
                return m
            if time.time() > end:
                return None
            time.sleep(3)


def main_rotation(sdk):
    """Display 0's rotation (0-3), from the window manager; or None."""
    text = sdk.shell("dumpsys window displays")
    part = re.search(r"Display: mDisplayId=0\b(.*?)(?:\n  Display: |\Z)", text, re.S)
    m = re.search(r"\n    mRotation=(\d)", part.group(1)) if part else None
    return int(m.group(1)) if m else None


def alive(sdk):
    return sdk.shell(f"pidof {PACKAGE}").strip() != ""


def wait_for(fn, timeout, every=3):
    end = time.time() + timeout
    while True:
        v = fn()
        if v or time.time() > end:
            return v
        time.sleep(every)


def add_second(sdk):
    w, h, dpi = SECOND
    sdk.adb("emu", "multidisplay", "add", str(SECOND_ID), str(w), str(h), str(dpi), "0", timeout=60)


def remove_second(sdk):
    sdk.adb("emu", "multidisplay", "del", str(SECOND_ID), timeout=60)


def start_app(sdk):
    sdk.shell(f"monkey -p {PACKAGE} -c android.intent.category.LAUNCHER 1", timeout=60)


def main():
    global OUT
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--apk")
    ap.add_argument("--disc", default=os.path.join(ROOT, "orig", "G8ME01", "Paper Mario - The Thousand-Year Door.iso"))
    ap.add_argument("--keep", action="store_true", help="leave the emulator running")
    ap.add_argument("--recreate", action="store_true", help="make the AVD again")
    ap.add_argument("--boot-timeout", type=int, default=600)
    ap.add_argument("--timeout", type=int, default=600, help="the longest wait for each step in the game")
    ap.add_argument("--out", default=os.path.join(ROOT, "build-android-dualscreen-test"))
    a = ap.parse_args()
    OUT = a.out
    os.makedirs(OUT, exist_ok=True)

    apk = a.apk
    if apk is None:
        for kind in ("release", "debug"):
            found = sorted(glob.glob(os.path.join(ROOT, "port", "android", "app", "build", "outputs", "apk", "us",
                                                  kind, "*.apk")))
            if found:
                apk = found[0]
                break
    if apk is None or not os.path.exists(apk):
        sys.exit("no US APK: build it (port/android: gradle assembleUsRelease) or pass --apk")
    if not os.path.exists(a.disc):
        sys.exit(f"no disc image at {a.disc} (--disc)")

    sdk = Sdk()
    sdk.ensure()
    ensure_avd(sdk, a.recreate)
    proc = boot(sdk, a.boot_timeout)
    try:
        run(sdk, apk, a)
    finally:
        if not a.keep:
            log("shutting the emulator down")
            shutdown(sdk, proc)
    failed = [n for n, ok in results if not ok]
    log(f"{len(results) - len(failed)}/{len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed or not results else 0


def push_lines(sdk, values, dest):
    """NAME=value lines into a file on the emulator."""
    local = os.path.join(OUT, os.path.basename(dest))
    with open(local, "w", newline="\n", encoding="utf-8") as f:
        f.writelines(f"{k}={v}\n" for k, v in values.items())
    sdk.adb("push", local, dest, check=True)


def run(sdk, apk, a):
    T = a.timeout
    sdk.shell("settings put system accelerometer_rotation 0; settings put system user_rotation 0;"
              " settings put global stay_on_while_plugged_in 7; svc power stayon true; input keyevent WAKEUP;"
              " wm dismiss-keyguard")
    log(f"installing {os.path.basename(apk)}")
    sdk.adb("install", "-r", "-g", apk, timeout=600, check=True)
    files = f"/sdcard/Android/data/{PACKAGE}/files"
    sdk.shell(f"am force-stop {PACKAGE}; mkdir -p {files}")
    disc = f"{files}/disc.iso"
    size = os.path.getsize(a.disc)
    if sdk.shell(f"stat -c %s {disc} 2>/dev/null").strip() != str(size):
        log("copying the disc into the emulator (once)")
        sdk.adb("push", a.disc, disc, timeout=1800, check=True)
    user = f"{files}/test-user"
    sdk.shell(f"rm -rf {user}; mkdir -p {user}")
    # a fresh user folder with the house settings (testpass.py's): the intro skipped
    push_lines(sdk, {"skip_intro": 1, "discord_status": 0, "settings_hint_shown": 1}, f"{user}/settings.txt")
    env = {
        "TTYD_DISC": disc,
        "TTYD_USER_DIR": user,
        "TTYD_NO_LAUNCHER": "1",
        "TTYD_MAP": "gor_01",
        # the warp's own limits (trace_pc.c): no exit 600 frames after the arrival, and
        # the emulator's boot to the title takes longer than the 60 s default
        "TTYD_EXIT_FRAMES": "100000000",
        "TTYD_WARP_LIMIT": str(max(T, 600)),
        "TTYD_FAST": "1",
        "TTYD_COMPANION_DUMP": "1",
        "TTYD_COMPANION_ANY_DISPLAY": "1",
    }
    push_lines(sdk, env, f"{files}/env.txt")

    add_second(sdk)
    did, d2 = wait_for(lambda: second_display(sdk)[0] is not None and second_display(sdk), 60) or (None, None)
    if not check("display2-made", did is not None, f"display {did} {d2 if d2 else ''}"):
        return
    w2, h2, dpi2 = d2
    lc = Log(sdk)
    start_app(sdk)

    m = lc.wait(r"companion on display (\d+)", T)
    check("presentation", m is not None and int(m.group(1)) == did, m.group(0) if m else "not in the log")
    if m is None:
        crash = re.search(r"(Fatal signal.*|FATAL EXCEPTION.*)", lc.read())
        if crash:
            log("  " + crash.group(1))
        return

    # the field up (TTYD_MAP), then the menu's picture on the second screen
    # (the warp's own line: env.txt's TTYD_MAP=gor_01 is in the log from the start)
    warp = lc.wait(r"\[warp\] (frame \d+: to gor_01|FAIL.*)", T)
    if warp is None or "FAIL" in warp.group(0):
        log("  " + (warp.group(0) if warp else "no warp to gor_01 in the log"))
    img = wait_for(lambda: (lambda i: i if is_menu(i, dpi2) else None)(shot(sdk, SECOND_ID, "second_menu")), T, 5)
    check("menu", img is not None, "the Start menu's picture and buttons" if img else "panels or nothing (second_menu.png)")

    # a tap the game acts on: the second screen's menu opening asks for the Mailbox SP (a new file has none),
    # and the game adds it at the point its own Start menu could open (companion_pc.c)
    sp = lc.wait(r"\[companion\] menu: the Mailbox SP (in the pouch|asked for)", T)
    had = sp is not None and sp.group(1) == "in the pouch"
    added = had or (sp is not None and lc.wait(r"\[companion\] the Mailbox SP: added to the pouch", T) is not None)
    check("tap", added, ("the game had it" if had else "asked, and the game added it") if added
          else (sp.group(0) + ", never added" if sp else "the menu never asked"))

    # touch: R then L on the second display; the menu's tab follows
    # (the page's height from its picture: a navigation bar may take the display's bottom)
    lay = menu_layout(w2, view_height(img) if img else h2, dpi2)
    mark = len(lc.read())
    sdk.shell(f"input -d {did} tap {int(lay['R'][0])} {int(lay['R'][1])}")
    # PAD_TRIGGER_R 0x20, PAD_TRIGGER_L 0x40
    r = lc.wait(r"\[companion\] menu input 20: active \d+ tab (\d+)", T, mark)
    time.sleep(2)  # the tab's change settles first: a press during it does nothing
    mark = len(lc.read())
    sdk.shell(f"input -d {did} tap {int(lay['L'][0])} {int(lay['L'][1])}")
    l = lc.wait(r"\[companion\] menu input 40: active \d+ tab (\d+)", T, mark)
    check("touch", r is not None and l is not None and r.group(1) != l.group(1),
          f"R -> tab {r.group(1) if r else '?'}, L -> tab {l.group(1) if l else '?'}")
    shot(sdk, SECOND_ID, "second_after_touch")

    # the game keeps running on the main display
    a0 = shot(sdk, None, "main_1")
    time.sleep(10)
    a1 = shot(sdk, None, "main_2")
    check("game", alive(sdk) and changed(a0, a1) > 20, f"{changed(a0, a1)} sampled pixels changed in 10 s")

    # rotation of the main display
    # upside down: the app is sensorLandscape, which ignores user_rotation and goes by the sensor,
    # and the emulator's sensor doesn't turn it; fixed-to-user-rotation makes the display follow
    # the user's rotation whatever the app asks (the screenshot stays upright: screencap turns it)
    sdk.shell("cmd window fixed-to-user-rotation enabled; cmd window user-rotation lock 2")
    turned = wait_for(lambda: main_rotation(sdk) == 2, 15, 1)
    time.sleep(5)
    shot(sdk, None, "main_rotated")
    rotated_menu = is_menu(shot(sdk, SECOND_ID, "second_rotated"), dpi2)
    sdk.shell("cmd window user-rotation lock 0; cmd window fixed-to-user-rotation default")
    wait_for(lambda: main_rotation(sdk) == 0, 15, 1)
    time.sleep(5)
    ok = turned and rotated_menu and alive(sdk) and \
        wait_for(lambda: is_menu(shot(sdk, SECOND_ID, "second_after_rotation"), dpi2), T, 5)
    check("rotation", bool(ok), f"turned {bool(turned)}, the menu while turned {rotated_menu}, after "
          + ("the app alive, the menu on the second screen" if ok else "lost (second_after_rotation.png)"))

    # the second display removed and added back
    mark = len(lc.read())
    remove_second(sdk)
    time.sleep(10)
    gone = alive(sdk) and wait_for(lambda: second_display(sdk)[0] is None, 30)
    crash = re.search(r"Fatal signal|FATAL EXCEPTION", lc.read()[mark:])
    check("display2-gone", bool(gone) and crash is None, "the app alive" if gone and not crash else "crashed or still there")
    mark = len(lc.text)
    add_second(sdk)
    did, d2 = wait_for(lambda: second_display(sdk)[0] is not None and second_display(sdk), 60) or (None, None)
    m = did is not None and lc.wait(r"companion on display (\d+)", T, mark)
    ok = bool(m) and wait_for(lambda: is_menu(shot(sdk, SECOND_ID, "second_back"), d2[2]), T, 5)
    check("display2-back", bool(ok), m.group(0) if m else "no presentation on the new display")

    # the app to the background and back
    mark = len(lc.read())
    sdk.shell("input keyevent HOME")
    time.sleep(10)
    bg_alive = alive(sdk)
    start_app(sdk)
    m = lc.wait(r"companion on display (\d+)", T, mark)
    ok = bg_alive and bool(m) and d2 is not None and \
        wait_for(lambda: is_menu(shot(sdk, SECOND_ID, "second_resumed"), d2[2]), T, 5)
    b0 = shot(sdk, None, "main_resumed_1")
    time.sleep(10)
    b1 = shot(sdk, None, "main_resumed_2")
    check("background", bool(ok) and changed(b0, b1) > 20,
          f"alive in the background {bg_alive}, second screen back {bool(m)}, main moving {changed(b0, b1)}")


if __name__ == "__main__":
    OUT = ""
    sys.exit(main())
