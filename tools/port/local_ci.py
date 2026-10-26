#!/usr/bin/env python3
"""The port's CI, run on this machine: every check, both versions, one report.

Stages (each logs to build-ci/<stage>.log; independent ones run at once):
  gamecube   both matching builds (configure.py + ninja: SHA-1 OK), in turn
  build      the PC builds: headless and Aurora, JP and US (configured if new)
  unit       unit tests (ctest -LE slow), both versions
  audits     proto_audit, noproto_calls, proto_mismatch, gen_bedecode --check, gen_reflect --check,
             gen_hooks --check, diag_census (fatal classes)
  sweep      map_sweep with golden audio, both versions
  cards      new file, Save Block save and load, headless and Aurora, both
             versions; the headless card then loads on Aurora
  mods       gen_game_all, gen_evt_table and gen_statics (both versions) --check; the
             US Aurora build with examples/mods: the examples' messages are used, their
             data patches apply, the code mod's services and a damage hook work, the
             script mod's new map loads and runs its scripts
  archipelago  the TTYD apworld's offsets as the PC's (exact, against the disc), and a
             session with a test seed against a stand-in server
  android    both debug APKs, when the Android SDK, JDK and Gradle are there
  linux      headless build and unit tests in WSL, when a distribution is there;
             else compiled and linked for Linux against stand-in headers
Data: the extracted discs in data/<version>, the images in orig/<version>/*.iso;
stages that need them skip without.

usage: local_ci.py [--quick] [--only STAGE,...] [--skip STAGE,...]
  --quick: no sweep, no cards (minutes instead of most of an hour)
Exit status 1 if any stage failed.
"""
import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSIONS = [("G8MJ01", "jp"), ("G8ME01", "us")]
# $TTYD_BUILD_SUFFIX: build folders (and logs) of their own, for a checkout
# another system builds in too (the Linux container over the Mac's: "-linux");
# $TTYD_CI_HEADLESS_ONLY: no Aurora builds or runs (a machine with no display)
SUFFIX = os.environ.get("TTYD_BUILD_SUFFIX", "")
HEADLESS_ONLY = bool(os.environ.get("TTYD_CI_HEADLESS_ONLY"))
LOGS = os.path.join(ROOT, "build-ci" + SUFFIX)
BUILDS = [(v, short, backend) for v, short in VERSIONS
          for backend in (("headless",) if HEADLESS_ONLY else ("headless", "aurora"))]


def build_dir(short, backend):
    return os.path.join(ROOT, f"build-pc-{short}" + ("-aurora" if backend == "aurora" else "") + SUFFIX)


def data_dir(v):
    """The extracted disc: data/<version>, or orig/<version> when the disc
    was extracted beside its image (files/ and sys/ there)."""
    for d in (os.path.join(ROOT, "data", v), os.path.join(ROOT, "orig", v)):
        if os.path.isdir(os.path.join(d, "files")):
            return d
    return None


def aurora_exe(short):
    """The Aurora executable: inside the .app bundle on macOS."""
    d = build_dir(short, "aurora")
    if sys.platform == "darwin":
        return os.path.join(d, f"ttyd-{short}.app", "Contents", "MacOS", f"ttyd-{short}")
    return os.path.join(d, f"ttyd-{short}")


def tool_env():
    """PATH with clang, CMake and Ninja (tools/port/winenv.sh, for Windows)."""
    env = dict(os.environ)
    if os.name == "nt":
        vs = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake"
        extra = [r"C:\Program Files\LLVM\bin", os.path.join(vs, "CMake", "bin"), os.path.join(vs, "Ninja")]
        env["PATH"] = os.pathsep.join(p for p in extra if os.path.isdir(p)) + os.pathsep + env["PATH"]
        env["Python3_ROOT_DIR"] = os.path.dirname(sys.executable)
    # every warp gets 5 minutes: a locked Windows session presents at about
    # 30 fps, and a warp from boot is about 4,000 frames
    env.setdefault("TTYD_WARP_LIMIT", "300")
    return env


ENV = tool_env()

# A step that downloads (CMake's FetchContent, Cargo, Gradle, apt, GitHub's
# API) is tried again when it fails: those servers fail now and then (a 504
# from GitHub once cost a release run). A real error fails every time, after
# these waits (seconds).
RETRY_WAITS = (30, 120)


def retrying(attempt, what="", log=print):
    """attempt() until it returns something true, waiting RETRY_WAITS between
    tries; its last result."""
    result = attempt()
    for wait in RETRY_WAITS:
        if result:
            break
        log(f"({what or 'a step'} failed: trying again in {wait}s, in case a download did)")
        time.sleep(wait)
        result = attempt()
    return result


class Stage:
    def __init__(self, name):
        self.name, self.ok, self.note, self.secs = name, True, "", 0.0
        self.log = open(os.path.join(LOGS, name + ".log"), "w", encoding="utf-8", errors="replace")

    def run(self, cmd, cwd=ROOT, env=None, ok_codes=(0,), downloads=False):
        """downloads: the step may fetch something, so a failure is retried
        (RETRY_WAITS)."""
        env = env or ENV
        # Windows looks a bare command up on this process's PATH, not env's.
        cmd = [shutil.which(cmd[0], path=env.get("PATH")) or cmd[0]] + list(cmd[1:])
        runs = []

        def attempt():
            self.log.write(f"\n$ {' '.join(cmd)}\n")
            self.log.flush()
            r = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
            self.log.write(r.stdout)
            self.log.flush()
            runs.append(r)
            return r.returncode in ok_codes
        if downloads:
            retrying(attempt, os.path.basename(cmd[0]), lambda m: self.log.write(m + "\n"))
        else:
            attempt()
        r = runs[-1]
        if r.returncode not in ok_codes:
            self.fail(f"{os.path.basename(cmd[0])} {' '.join(cmd[1:3])}: exit {r.returncode}")
        return r

    def fail(self, why):
        self.ok = False
        self.note = self.note or why

    def skip(self, why):
        self.note = "skipped: " + why


def stage_gamecube(s):
    if not glob.glob(os.path.join(ROOT, "orig", "*", "sys", "main.dol")):
        return s.skip("no orig/<version>/sys/main.dol")
    for v, _ in VERSIONS:
        s.run([sys.executable, "configure.py", "--version", v])
        r = s.run(["ninja"])
        if r.returncode == 0:
            s.note += f"{v} OK "


def configure_args(v, backend):
    args = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo", f"-DTTYD_VERSION={v}", f"-DTTYD_BACKEND={backend}"]
    if os.name == "nt":
        args.append("--toolchain=" + os.path.join(ROOT, "cmake", "toolchains", "windows-clang.cmake"))
    elif sys.platform.startswith("linux"):
        args.append("--toolchain=" + os.path.join(ROOT, "cmake", "toolchains", "linux-clang.cmake"))
    return args


def stage_build(s):
    for v, short, backend in BUILDS:
        d = build_dir(short, backend)
        # (configuring fetches the dependencies; building, Cargo's crates)
        if not os.path.exists(os.path.join(d, "CMakeCache.txt")):
            s.run(["cmake", "-S", ROOT, "-B", d] + configure_args(v, backend), downloads=True)
        else:
            s.run(["cmake", d], downloads=True)
        s.run(["ninja", "-C", d, "-k", "0"], downloads=True)


UNIT_TARGETS = ["bedecode_test", "anim_test", "loaders_test", "map_test", "dtk_test", "musyx_swap_test",
                "musyx_mixer_test", "disc_sweep", "musyx_seq_sweep"]


def stage_unit(s):
    for _, short in VERSIONS:
        d = build_dir(short, "headless")
        s.run(["ninja", "-C", d] + UNIT_TARGETS)
        s.run(["ctest", "--test-dir", d, "-LE", "slow", "--output-on-failure"])


def stage_audits(s):
    s.run([sys.executable, "tools/port/proto_audit.py"])
    s.run([sys.executable, "tools/port/noproto_calls.py"])
    s.run([sys.executable, "tools/port/proto_mismatch.py"])
    s.run([sys.executable, "tools/port/gen_bedecode.py", "--check"])
    s.run([sys.executable, "tools/port/gen_reflect.py", "--check"])
    s.run([sys.executable, "tools/port/gen_hooks.py", "--check"])
    s.run([sys.executable, "tools/port/diag_census.py"] + [build_dir(short, "headless") for _, short in VERSIONS])


def stage_sweep(s):
    for v, short in VERSIONS:
        data = data_dir(v)
        if data is None:
            s.note += f"{v}: no extracted disc "
            continue
        exe = os.path.join(build_dir(short, "headless"), f"ttyd-{short}")
        r = s.run([sys.executable, "tools/port/map_sweep.py", exe, data, "--golden",
                   os.path.join("tests", "port", "golden", f"audio_{v}.json")])
        last = [l for l in r.stdout.splitlines() if l.startswith("map_sweep:")]
        s.note += f"{v}: {last[-1][11:] if last else '?'}; "


CARD_SCRIPTS = (("save_new_file", None), ("save_block", "gor_01"), ("load_file", None))


def card_run(s, v, short, backend, user, script, warp):
    """One card script on one backend with its own card folder; True if it reached its end."""
    env = dict(ENV, TTYD_FAST="1", TTYD_INPUT=os.path.join(ROOT, "tests", "port", "input", script + ".txt"),
               TTYD_USER_DIR=user, TTYD_WATCHDOG="30")
    env.pop("TTYD_MAP", None)
    if warp:
        env["TTYD_MAP"] = warp
    exe = aurora_exe(short) if backend == "aurora" else os.path.join(build_dir(short, backend), f"ttyd-{short}")
    if backend == "headless":
        env["TTYD_DATA_DIR"] = data_dir(v)
        cmd = [exe]
    else:
        env.update(TTYD_WINDOW_POS="-4000,0", TTYD_CACHE_DIR=os.path.join(LOGS, "cards", "cache"),
                   TTYD_SHOT_DIR=os.path.join(user, "shots"))
        cmd = [exe, sorted(glob.glob(os.path.join(ROOT, "orig", v, "*.iso")))[0]]
    r = s.run(cmd, env=env)
    if r.returncode == 0 and "[input] end" not in r.stdout:
        s.fail(f"{v} {backend} {script}: no [input] end")
    return r.returncode == 0 and "[input] end" in r.stdout


def stage_cards(s):
    """New file, Save Block save and load, on each backend with its own card
    folder; then the headless card loads on Aurora (same .gci files)."""
    for v, short in VERSIONS:
        have = {"headless": data_dir(v) is not None,
                "aurora": not HEADLESS_ONLY and bool(glob.glob(os.path.join(ROOT, "orig", v, "*.iso")))}
        for backend in ("headless", "aurora"):
            if not have[backend]:
                why = ("no extracted disc" if backend == "headless" else
                       "not here (headless only)" if HEADLESS_ONLY else "no disc image")
                s.note += f"{v} {backend}: {why} "
                continue
            user = os.path.join(LOGS, "cards", f"{short}-{backend}")
            shutil.rmtree(user, ignore_errors=True)
            os.makedirs(user)
            ok = all([card_run(s, v, short, backend, user, script, warp) for script, warp in CARD_SCRIPTS])
            s.note += f"{v} {backend} {'ok' if ok else 'FAIL'} "
        if have["headless"] and have["aurora"]:
            user = os.path.join(LOGS, "cards", f"{short}-cross")
            shutil.rmtree(user, ignore_errors=True)
            shutil.copytree(os.path.join(LOGS, "cards", f"{short}-headless"), user,
                            ignore=shutil.ignore_patterns("shots"))
            ok = card_run(s, v, short, "aurora", user, "load_file", None)
            s.note += f"{v} headless->aurora {'ok' if ok else 'FAIL'} "


def mod_platform():
    """A code mod's lib/<platform>/ folder for this machine (CMakeLists.txt, TTYD_MOD_PLATFORM)."""
    import platform
    arch = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    if sys.platform == "win32":
        return f"windows-{arch}"
    return "macos" if sys.platform == "darwin" else f"linux-{arch}"


def stage_mods(s):
    """examples/mods in a fresh user folder, the code mod's library from the
    US Aurora build in it: the build lists the examples, uses their messages
    in the item menu, applies their data patches, starts the code mod (its
    area, shop and message services), then in a battle its damage hook, and
    warps to the script mod's new map (its scripts and message)."""
    # the generated mod SDK pieces are current: game_all.h and game_loose.h
    # (from the US Aurora build's flags), each version's statics list
    s.run([sys.executable, "tools/port/gen_game_all.py", "--check"])
    s.run([sys.executable, "tools/port/gen_evt_table.py", "--check"])
    for _, short in VERSIONS:
        if os.path.exists(os.path.join(build_dir(short, "aurora"), "compile_commands.json")):
            s.run([sys.executable, "tools/port/gen_statics.py", "--build", build_dir(short, "aurora"), "--check"])
    iso = sorted(glob.glob(os.path.join(ROOT, "orig", "G8ME01", "*.iso")))
    if not iso:
        return s.skip("no US disc image")
    lib = [p for p in glob.glob(os.path.join(build_dir("us", "aurora"), "mods", "hello-native.*"))
           if p.endswith((".dll", ".so", ".dylib"))]
    if not lib:
        return s.fail("no hello-native library in the US Aurora build")
    user = os.path.join(LOGS, "mods", "user")
    shutil.rmtree(os.path.join(LOGS, "mods"), ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "examples", "mods"), os.path.join(user, "mods"))
    lib_dir = os.path.join(user, "mods", "hello-native", "lib", mod_platform())
    os.makedirs(lib_dir)
    shutil.copy(lib[0], lib_dir)
    os.makedirs(os.path.join(LOGS, "mods", "cache"))
    base = dict(ENV, TTYD_FAST="1", TTYD_TRACE="1", TTYD_WATCHDOG="30", TTYD_USER_DIR=user,
                TTYD_WINDOW_POS="-4000,0", TTYD_CACHE_DIR=os.path.join(LOGS, "mods", "cache"),
                TTYD_SHOT_DIR=os.path.join(LOGS, "mods", "shots"))
    runs = (
        ("gor_01", "mods_items.txt",
         ("mods: dev.ttyd.hello-text 1.0: 0 files, 0 textures, 2 messages, 0 patches",
          "mods: dev.ttyd.shroom-tweaks 1.0: 0 files, 0 textures, 0 messages, 3 patches",
          "mods: dev.ttyd.hello-native: code running", "[dev.ttyd.hello-native] entered area gor",
          "[dev.ttyd.hello-native] shop on gor_01: 6 goods",
          "[trace] mod message in_kinoko", "[trace] mod message msg_kinoko", "[input] end")),
        ("hei_01", "battle_jump.txt", ("[dev.ttyd.hello-native] hit: 1 damage, 2 with the mod", "[input] end")),
        ("gor_99", "mods_room.txt", ("mods: dev.ttyd.hello-room 1.0: 0 files, 0 textures, 1 messages, 0 patches, 2 scripts",
                                     "[evt] hello-room: entered gor_99", "[trace] mod message hello_room",
                                     "[evt] hello-room: said hello", "[input] end")),
    )
    for warp, script, wants in runs:
        env = dict(base, TTYD_MAP=warp, TTYD_INPUT=os.path.join(ROOT, "tests", "port", "input", script))
        r = s.run([aurora_exe("us"), iso[0]], env=env)
        for want in wants:
            if want not in r.stdout:
                s.fail(f"{warp}: no \"{want}\"")
        if "mods: dev.ttyd.shroom-tweaks: " in r.stdout:
            s.fail("a data patch of the example was refused")
    s.note += "example mods ok (files, data, code)" if s.ok else ""


def stage_archipelago(s):
    """The US Aurora build's Archipelago support: every item target of the
    TTYD apworld's locations holds what the disc's REL has at the apworld's
    offset (TTYD_AP_VERIFY: the offsets' translation, tools/port/gc_offsets.py);
    then a session with a test seed (tests/port/ap_test_seed.py) against a
    stand-in server (tests/port/ap_mock_server.py): it connects, hands the
    slot's items to the game, places the seed's items and sends a location's
    check once its flag is set. With $TTYD_APWORLD (a checkout of the apworld),
    port/archipelago/ttyd_ap_data.h is checked current too."""
    iso = sorted(glob.glob(os.path.join(ROOT, "orig", "G8ME01", "*.iso")))
    if not iso:
        return s.skip("no US disc image")
    if os.environ.get("TTYD_APWORLD"):
        s.run([sys.executable, "tools/port/gen_ttyd_ap.py", "--apworld", os.environ["TTYD_APWORLD"], "--check"])
    work = os.path.join(LOGS, "archipelago")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "user"))
    base = dict(ENV, TTYD_FAST="1", TTYD_TRACE="1", TTYD_WATCHDOG="30", TTYD_USER_DIR=os.path.join(work, "user"),
                TTYD_WINDOW_POS="-4000,0", TTYD_CACHE_DIR=os.path.join(work, "cache"))
    r = s.run([aurora_exe("us"), iso[0]], env=dict(base, TTYD_AP_VERIFY="1",
                                                   TTYD_INPUT=os.path.join(ROOT, "tests", "port", "input", "wait.txt")))
    if "archipelago: verify: " not in r.stdout or " targets as the disc has them, 0 not" not in r.stdout:
        s.fail("the apworld's targets aren't all as the disc has them")
    seed = os.path.join(work, "test.apttyd")
    s.run([sys.executable, "tests/port/ap_test_seed.py", seed])
    log = os.path.join(work, "server.jsonl")
    server = subprocess.Popen([sys.executable, "tests/port/ap_mock_server.py", "--port", "38281", "--slot", "Mario",
                               "--items", "77772120,77772204", "--log", log], cwd=ROOT)
    try:
        r = s.run([aurora_exe("us"), iso[0]], env=dict(
            base, TTYD_MAP="gor_01", TTYD_AP_SEED=seed, TTYD_AP_SERVER="ws://127.0.0.1:38281", TTYD_AP_NO_MOD="1",
            TTYD_INPUT=os.path.join(ROOT, "tests", "port", "input", "ap_check.txt")))
    finally:
        server.terminate()
        server.wait()
    for want in ("archipelago: connected as Mario", "archipelago: 2 items to the game (from 0)",
                 "archipelago: gor: 5 placements", "[input] end"):
        if want not in r.stdout:
            s.fail(f'no "{want}"')
    sent = open(log, encoding="utf-8").read() if os.path.exists(log) else ""
    for want in ('"cmd": "Connect"', '"value": "gor_01"', '"cmd": "LocationChecks", "locations": [78780004]'):
        if want not in sent:
            s.fail(f"the server got no {want}")
    s.note += "translation exact; session ok" if s.ok else ""


def stage_linux(s):
    """WSL: build the headless JP version and run its unit tests (Linux CI)."""
    if os.name != "nt" or shutil.which("wsl") is None:
        return s.skip("not on Windows")
    r = subprocess.run(["wsl", "-l", "-q"], capture_output=True)
    if r.returncode != 0 or not r.stdout.replace(b"\0", b"").strip():
        # no Linux to build on: compile and link for it anyway, against
        # stand-in headers (tools/port/linux_xcheck.py; it cannot run)
        for arch, version in (("x86_64", "G8MJ01"), ("aarch64", "G8ME01")):
            s.run([sys.executable, "tools/port/linux_xcheck.py", "--out",
                   os.path.join(LOGS, "linux_xcheck", arch), "--arch", arch, "--version", version])
        s.note = "no WSL: cross-compiled and linked only (x86_64 JP, aarch64 US)"
        return
    root = subprocess.run(["wsl", "wslpath", "-a", ROOT.replace("\\", "/")], capture_output=True, text=True).stdout.strip()
    script = (f"set -e; cd '{root}'; b=$HOME/ttyd-ci/build-linux-jp; "
              "cmake -S . -B $b -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01 "
              "-DTTYD_BACKEND=headless --toolchain cmake/toolchains/linux-clang.cmake; "
              f"ninja -C $b -k 0; ninja -C $b {' '.join(UNIT_TARGETS)}; ctest --test-dir $b -LE slow --output-on-failure")
    s.run(["wsl", "bash", "-lc", script], env=dict(os.environ))


def stage_android(s):
    """Both debug APKs (port/android), when an Android SDK, a JDK 17 and Gradle
    are there: $ANDROID_HOME, $JAVA_HOME and gradle on PATH, else under
    ~/Android (sdk/, jdk-17/, gradle-*/)."""
    home = os.path.expanduser("~/Android")
    sdk = os.environ.get("ANDROID_HOME") or os.path.join(home, "sdk")
    jdk = os.environ.get("JAVA_HOME") or os.path.join(home, "jdk-17")
    gradle = shutil.which("gradle") or next(iter(sorted(glob.glob(os.path.join(home, "gradle-*", "bin",
                                                                             "gradle.bat" if os.name == "nt" else "gradle")))), None)
    if not (os.path.isdir(os.path.join(sdk, "ndk")) and os.path.isdir(jdk) and gradle):
        return s.skip("no Android SDK/NDK, JDK 17 and Gradle")
    env = dict(ENV, ANDROID_HOME=sdk, ANDROID_SDK_ROOT=sdk, JAVA_HOME=jdk)
    s.run([gradle, "--no-daemon", "-p", os.path.join(ROOT, "port", "android"), "assembleJpDebug", "assembleUsDebug"],
          env=env, downloads=True)
    if s.ok:
        s.note = "app-jp-debug.apk, app-us-debug.apk"


STAGES = {"gamecube": stage_gamecube, "build": stage_build, "unit": stage_unit, "audits": stage_audits,
          "sweep": stage_sweep, "cards": stage_cards, "mods": stage_mods, "archipelago": stage_archipelago,
          "linux": stage_linux,
          "android": stage_android}
# what may run at the same time; each group waits for the one before
PLAN = [["gamecube", "build", "linux"], ["unit", "audits", "android"], ["sweep"], ["cards", "mods", "archipelago"]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--skip", default="")
    a = ap.parse_args()
    want = set(a.only.split(",")) if a.only else set(STAGES)
    want -= set(a.skip.split(",")) if a.skip else set()
    if a.quick:
        want -= {"sweep", "cards"}
    os.makedirs(LOGS, exist_ok=True)
    results = []
    start = time.time()
    for group in PLAN:
        names = [n for n in group if n in want]
        with concurrent.futures.ThreadPoolExecutor(len(names) or 1) as pool:
            def go(name):
                s = Stage(name)
                t = time.time()
                try:
                    STAGES[name](s)
                except Exception as e:  # a crashed stage is a failed stage
                    s.fail(f"{type(e).__name__}: {e}")
                s.secs = time.time() - t
                s.log.close()
                print(f"{'PASS' if s.ok else 'FAIL'}  {name:9s} {s.secs:6.0f}s  {s.note.strip()}", flush=True)
                return s
            results += list(pool.map(go, names))
    failed = [s.name for s in results if not s.ok]
    print(f"\nlocal_ci: {len(results) - len(failed)}/{len(results)} stages passed in {time.time() - start:.0f}s"
          + (f"; failed: {', '.join(failed)} (logs: build-ci/<stage>.log)" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
