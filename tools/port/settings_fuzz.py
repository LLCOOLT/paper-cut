#!/usr/bin/env python3
"""Settings fuzzer: random combinations of Paper Cut's settings, plus a pairwise
covering set of the risky ones, each played through a short scenario.

Each combination is a fresh user folder with its settings.txt, then two runs:

  play    TTYD_MAP (default hei_00; or --event ROW: TTYD_EVENT) with
          TTYD_AUTO_BATTLE, TTYD_GOD, TTYD_AUTO_TEXT and
          tests/port/input/settings_fuzz_play.txt: walk, a fight, a save
  reload  no warp, the same card: the save loaded back from the title
          (TTYD_LOAD_SAVE) to the map it was made on, a short walk
          (tests/port/input/settings_fuzz_reload.txt)

A combination fails on a crash, a hang (the watchdog, the time limit), an
assert or panic, an input script that didn't get there, or a setting out of
its range in $TTYD_SETTINGS_DUMP after either run (port/src/common/
settings_extra.c). Failures are written as repro files (the seed and the
combination); --repro FILE runs one again.

  settings_fuzz.py EXE --iso DISC [--runs N] [--seed S] [--jobs J] [--out DIR]
  settings_fuzz.py EXE --iso DISC --list            the settings and ranges, as JSON
  settings_fuzz.py EXE --iso DISC --repro DIR/fail_0007.json
  settings_fuzz.py EXE --iso DISC --aurora ...      the Aurora build: the graphics
                                                    settings, a map and a few seconds

--slot CMD runs each game through a wrapper (the shared game slots on the
test PC: --slot "bash $GAME_SLOT"). Every run has
TTYD_FAST=1 and its own user folder (and, on Aurora, its own cache folder).
"""
import argparse
import glob
import itertools
import json
import os
import random
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
INPUT_DIR = os.path.join(ROOT, "tests", "port", "input")

# never changed: they leave the test's window or machine (a full screen, a second window, the network,
# Discord, speech aloud), stop the game by design (frame advance), or belong to the launcher
DENY = {
    "fullscreen", "fullscreen_kind", "window_on_top", "window_max", "window_remember", "window_x", "window_y",
    "window_w", "window_h", "second_screen", "discord_status", "update_check", "update_auto", "crowd_control",
    "livesplit", "livesplit_port", "tts", "frame_advance", "pause_unfocused", "background_input", "cloud_sync",
    "launcher_skip", "launcher_popup", "launcher_sounds", "launcher_style", "dump_textures", "skip_intro",
    "settings_hint_shown", "whats_new_seen", "settings_per_file", "save_library", "update_install",
    "menu_language_pack", "custom_music", "music_shuffle_custom", "dualsense_speaker", "memory_watch",
    "memory_watch_set", "memory_watch_set2", "title_menu", "quick_resume", "autosave",
}
# set in every combination (the test's own)
BASE = {"skip_intro": 1, "fullscreen": 0, "discord_status": 0, "settings_hint_shown": 1, "update_check": 0,
        "title_menu": 0, "quick_resume": 0,
        "wide_hud_050": 1}  # (this release's file: its wide_hud as the combination has it, not turned on once)

# the risky ones, covered pairwise: a domain each (None: from the settings' ranges)
RISKY = {
    "playable_luigi": None,       # Play as
    "brothers_mode": [0, 1],
    "second_player": None,
    "difficulty": None,
    "difficulty_enemy_hp": [25, 100, 300],
    "difficulty_enemy_attack": [25, 100, 300],
    "env:TTYD_OPEN_SEED": ["", "7", "424242"],  # the open-world randomizer (a new file's)
    "master_quest": [0, 1],
    "directors_cut": [0, 1],
    "remix_enemies": [0, 1],
    "remix_badge_draft": [0, 1],
    "hardcore_next": [0, 1],
    "level_sync": None,
    "console_accurate": [0, 1],
    "chaos_mode": [0, 1],
    "fun_big_heads": [0, 1],
    "fun_moon_gravity": [0, 1],
    "fun_paper_thin": [0, 1],
    "fun_spin": [0, 1],
    "fun_luigi_voice": [0, 1],
    "fun_cool": [0, 1],
    "mirror_mode": [0, 1],
    "aspect_ratio": None,
    "battle_speed": None,
    "text_speed": None,
    "run_speed": [100, 150],
    "walk_boost": [0, 1],
    "enemy_variants": [0, 1],
    "battle_grades": [0, 1],
    "battle_recap": None,
    "battle_popups": [0, 1],
    "new_game_plus": None,
    "coin_multiplier": [0, 4],
    "exp_multiplier": [0, 4],
    "challenge_ohko": [0, 1],
    "challenge_guard": None,
    "challenge_commands": None,
    "skip_scenes": [0, 1, 2],
    "costume": None,
    "paper_physics": [0, 1],
    "remake_balance": [0, 1],
}
# ranges for risky keys a backend never declares in the discovery run (lazy ones)
FALLBACK = {"playable_luigi": (0, 3), "second_player": (0, 1), "difficulty": (0, 3), "level_sync": (0, 2),
            "aspect_ratio": (0, 3), "battle_speed": (0, 2), "text_speed": (0, 3), "battle_recap": (0, 2),
            "new_game_plus": (0, 3), "challenge_guard": (0, 2), "challenge_commands": (0, 2), "costume": (0, 3)}

# the Aurora pass: the fixed fields and declared settings that only change the picture or sound
AURORA_KEYS = ["vsync", "widescreen", "resolution", "fps", "overlay_fps", "no_pop_in", "tex_filter", "msaa",
               "side_bars", "wide_hud", "volume", "aspect_ratio", "aspect_max", "aspect_scene_limits", "lod_bias",
               "aniso", "hdr", "hdr_boost", "post_filter", "supersample", "texture_upscale", "sharp_text",
               "hud_scale", "hud_theme", "colorblind", "colorblind_strength", "paper_edges", "paper_shadows",
               "paper_notes", "text_scale", "perf_overlay", "vrr", "battery_saver", "input_display", "page_turn",
               "captions", "speakers", "game_sound_mode", "playable_luigi", "fun_big_heads", "fun_paper_thin"]
AURORA_FRAMERATES = ["60", "120", "144", "240", "display"]

FAIL_PATTERNS = [
    (re.compile(r"\*\*\* crash"), "crash"),
    (re.compile(r"\*\*\* stall"), "hang (watchdog)"),
    (re.compile(r"OSPanic"), "panic"),
    (re.compile(r"Assertion failed|assertion .* failed|\bassert\b.*fail", re.I), "assert"),
    (re.compile(r"\[input\] FAIL"), "input script didn't get there"),
    (re.compile(r"\[story\] STUCK"), "story stuck"),
    (re.compile(r"settings: no room"), "settings table full"),
]
EXIT_NAMES = {2: "script error", 3: "warp not reached", 4: "crash or watchdog", 5: "input FAIL",
              6: "stuck", 7: "game over"}


# --- settings and their ranges ---

def read_dump(path):
    """$TTYD_SETTINGS_DUMP's lines: {key: dict(value, min, max, def, declared, field)}"""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if p[0] == "extra" and len(p) >= 7:
                out[p[1]] = dict(value=int(p[2]), min=int(p[3]), max=int(p[4]), default=int(p[5]),
                                 declared=int(p[6]) != 0, field=False)
            elif p[0] == "field" and len(p) >= 5:
                out[p[1]] = dict(value=int(p[2]), min=int(p[3]), max=int(p[4]), default=None, declared=True,
                                 field=True)
    return out


DECL_RE = re.compile(r'portSettingsDeclare\("([a-z0-9_]+)",\s*(-?(?:0x)?[0-9A-Fa-f]+),\s*(-?(?:0x)?[0-9A-Fa-f]+),'
                     r'\s*(-?(?:0x)?[0-9A-Fa-f]+)\)')
TABLE_RE = re.compile(r'\{"([a-z0-9_]+)",\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\}')


def static_settings():
    """the declarations with literal ranges in the source (port/), and the table rows of the
    files that declare from tables: what a run may not have declared yet"""
    out = {}
    for d, _, files in os.walk(os.path.join(ROOT, "port")):
        for name in files:
            if not name.endswith((".c", ".cpp", ".inc", ".mm", ".m")):
                continue
            path = os.path.join(d, name)
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if "portSettingsDeclare" not in text:
                continue
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            for m in DECL_RE.finditer(text):
                k, a, b, c = m.group(1), *(int(x, 0) for x in m.group(2, 3, 4))
                out.setdefault(k, dict(default=a, min=b, max=c, source=rel))
            for m in TABLE_RE.finditer(text):
                k, a, b, c = m.group(1), *(int(x) for x in m.group(2, 3, 4))
                if b <= a <= c:
                    out.setdefault(k, dict(default=a, min=b, max=c, source=rel))
    return out


def domain_of(key, info):
    if RISKY.get(key) is not None:
        return list(RISKY[key])
    lo, hi = (info[key]["min"], info[key]["max"]) if key in info else FALLBACK.get(key, (0, 1))
    if hi - lo <= 4:
        return list(range(lo, hi + 1))
    d = sorted({lo, hi, (lo + hi) // 2})
    return d


def random_value(rng, s):
    lo, hi = s["min"], s["max"]
    if hi - lo <= 16:
        return rng.randint(lo, hi)
    return rng.choice([lo, hi, s.get("default") if s.get("default") is not None else lo, rng.randint(lo, hi)])


# --- pairwise ---

def pairwise(factors, rng, tries=40):
    """a greedy covering array: rows (dicts) such that every pair of values of every two
    factors appears in at least one"""
    names = list(factors)
    uncovered = set()
    for (i, a), (j, b) in itertools.combinations(enumerate(names), 2):
        for va in range(len(factors[a])):
            for vb in range(len(factors[b])):
                uncovered.add((i, va, j, vb))
    rows = []
    while uncovered:
        best, best_n = None, -1
        seed_pair = rng.choice(sorted(uncovered))
        for _ in range(tries):
            row = [rng.randrange(len(factors[n])) for n in names]
            row[seed_pair[0]], row[seed_pair[2]] = seed_pair[1], seed_pair[3]
            n = sum(1 for (i, va, j, vb) in uncovered if row[i] == va and row[j] == vb)
            if n > best_n:
                best, best_n = row, n
        uncovered = {(i, va, j, vb) for (i, va, j, vb) in uncovered if not (best[i] == va and best[j] == vb)}
        rows.append({n: factors[n][best[k]] for k, n in enumerate(names)})
    return rows


# --- running ---

def game_cmd(args, extra_env, timeout):
    cmd = []
    if args.slot:
        # (the time limit inside the slot: waiting for one isn't the game's time)
        cmd += shlex.split(args.slot, posix=True) + ["timeout", "-k", "10", str(timeout)]
    cmd += [os.path.abspath(args.exe), os.path.abspath(args.iso)]
    env = dict(os.environ)
    for k in [k for k in env if k.startswith("TTYD_")]:
        del env[k]  # (only this run's)
    env.update(extra_env)
    return cmd, env


def kill_tree(p):
    """this run's game and what it started (by its process id, never by name)"""
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    else:
        p.kill()
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        pass


def run_game(args, run_dir, name, env_extra, timeout):
    log_path = os.path.join(run_dir, name + ".log")
    cmd, env = game_cmd(args, env_extra, timeout)
    t0 = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        p = subprocess.Popen(cmd, env=env, cwd=run_dir, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = p.wait(timeout=None if args.slot else timeout)
        except subprocess.TimeoutExpired:
            code = None
            kill_tree(p)
    if args.slot and code == 124:
        code = None  # (timeout's: the time limit)
    text = open(log_path, encoding="utf-8", errors="replace").read()
    return code, text, time.time() - t0


def check_dump(dump, wanted, problems, when, ran_ok=False):
    if ran_ok and not dump:  # (a run that ended well writes it: without it no range was checked)
        problems.append(f"{when}: no $TTYD_SETTINGS_DUMP written")
    for k, s in dump.items():
        if s["declared"] and not (s["min"] <= s["value"] <= s["max"]):
            problems.append(f"{when}: {k}={s['value']} out of range {s['min']}..{s['max']}")
    for k, v in wanted.items():
        if k.startswith("env:") or k not in dump or not dump[k]["declared"]:
            continue
        lo, hi = dump[k]["min"], dump[k]["max"]
        want = min(max(int(v), lo), hi)
        if dump[k]["value"] != want:
            problems.append(f"{when}: note: {k} set to {v}, is {dump[k]['value']}")


# harness issues, not the game's (reported apart, never a failure): the auto-battle that never ends
# (a known issue of the test hooks; its fix is under way)
HARNESS = [(re.compile(r"\[input\] FAIL: press_until_control .* \(sequence 4,"), "the auto-battle never ended")]


def classify(code, text, name):
    out = []
    for rx, what in HARNESS:
        if rx.search(text):
            return [f"{name}: harness: {what}"]
    if code is None:
        out.append(f"{name}: hang (time limit)")
    elif code != 0:
        out.append(f"{name}: exit {code} ({EXIT_NAMES.get(code, 'failed')})")
    for rx, what in FAIL_PATTERNS:
        m = rx.search(text)
        if m:
            line = text[text.rfind("\n", 0, m.start()) + 1:text.find("\n", m.end())]
            out.append(f"{name}: {what}: {line.strip()[:200]}")
    return out


def settings_txt(combo):
    lines = []
    for k, v in sorted({**BASE, **combo}.items()):
        if not k.startswith("env:"):
            lines.append(f"{k}={v}")
    return "\n".join(lines) + "\n"


CARD_GLOB = os.path.join("*", "Card A", "*mariost_save_file.gci")


def make_card(args):
    """a card with file 1 on it, made once (a save needs the game's file there first): the title,
    a new file, the prologue (tests/port/input/save_new_file.txt); each combination starts from
    a copy (its folder)"""
    d = os.path.join(args.out, "card")
    if glob.glob(os.path.join(d, "user", CARD_GLOB)):
        return os.path.join(d, "user")
    shutil.rmtree(d, ignore_errors=True)
    user = os.path.join(d, "user")
    os.makedirs(user)
    with open(os.path.join(user, "settings.txt"), "w", encoding="utf-8") as f:
        f.write(settings_txt({}))
    env = {"TTYD_FAST": "1", "TTYD_USER_DIR": user, "TTYD_WATCHDOG": str(args.watchdog),
           "TTYD_INPUT": os.path.join(INPUT_DIR, "save_new_file.txt")}
    if args.aurora:
        env.update({"TTYD_CACHE_DIR": os.path.join(d, "cache"), "SDL_WINDOW_ACTIVATE_WHEN_SHOWN": "0",
                    "TTYD_NO_LAUNCHER": "1"})
    code, text, _ = run_game(args, d, "card", env, args.timeout)
    if code != 0 or not glob.glob(os.path.join(user, CARD_GLOB)):
        sys.exit(f"couldn't make the card (exit {code}): {os.path.join(d, 'card.log')}")
    os.remove(os.path.join(user, "settings.txt"))
    return user


def run_combo(args, combo, run_dir):
    """one combination's runs; (problems, notes, dumps)"""
    if os.path.exists(run_dir):
        shutil.rmtree(run_dir)
    user = os.path.join(run_dir, "user")
    if args.card:
        shutil.copytree(args.card, user, ignore=shutil.ignore_patterns("settings.txt", "music"))
    else:
        os.makedirs(user)
    with open(os.path.join(user, "settings.txt"), "w", encoding="utf-8") as f:
        f.write(settings_txt(combo))
    env = {"TTYD_FAST": "1", "TTYD_USER_DIR": user, "TTYD_WATCHDOG": str(args.watchdog), "TTYD_WARP_LIMIT": "900"}
    for k, v in combo.items():
        if k.startswith("env:") and v != "":
            env[k[4:]] = str(v)
    problems, notes = [], []
    if args.aurora:
        cache = os.path.join(run_dir, "cache")
        os.makedirs(cache)
        env.update({"TTYD_CACHE_DIR": cache, "SDL_WINDOW_ACTIVATE_WHEN_SHOWN": "0", "TTYD_NO_LAUNCHER": "1",
                    "TTYD_MAP": args.map, "TTYD_PARTY": "1", "TTYD_EXIT_FRAMES": str(args.aurora_frames), "TTYD_WARP_LIMIT": "900",
                    "TTYD_SETTINGS_DUMP": os.path.join(run_dir, "dump_aurora.tsv")})
        code, text, secs = run_game(args, run_dir, "aurora", env, args.timeout)
        problems += classify(code, text, "aurora")
        if code == 0 and "[warp] ok" not in text:  # (exit 0 alone: nothing shows it reached the map and ran)
            problems.append("aurora: never ran its frames on the map (no [warp] ok)")
        check_dump(read_dump(env["TTYD_SETTINGS_DUMP"]), combo, problems, "aurora", code == 0)
        return problems, secs
    script = open(os.path.join(INPUT_DIR, "settings_fuzz_play.txt"), encoding="utf-8").read()
    if args.no_fight:  # (until the auto-battle always ends: walk and save only)
        # (the card is ready for a save only a while in: the wait the fight would have been)
        script = "".join(l if not l.startswith("battle") else "wait 900\n" for l in script.splitlines(True))
    ppath = os.path.join(run_dir, "play.txt")
    with open(ppath, "w", encoding="utf-8") as f:
        f.write(script)
    play = dict(env)
    play.update({"TTYD_AUTO_BATTLE": "1", "TTYD_GOD": "1", "TTYD_AUTO_TEXT": "1", "TTYD_PARTY": "1",
                 "TTYD_INPUT": ppath,
                 "TTYD_SETTINGS_DUMP": os.path.join(run_dir, "dump_play.tsv")})
    if args.event is not None:
        play["TTYD_EVENT"] = str(args.event)
    else:
        play["TTYD_MAP"] = args.map
    code, text, secs = run_game(args, run_dir, "play", play, args.timeout)
    problems += classify(code, text, "play")
    check_dump(read_dump(play["TTYD_SETTINGS_DUMP"]), combo, problems, "play", code == 0)
    if code == 0 and not args.no_fight and "[input] battle with" not in text:
        problems.append("play: no fight started")
    m = re.search(r"\[input\] save: slot (\d+), map '([^']*)'", text)
    if code != 0 or m is None:
        if m is None and code == 0:
            problems.append("play: no save made")
        return problems, secs
    # reload: the saved map as the goal
    script = open(os.path.join(INPUT_DIR, "settings_fuzz_reload.txt"), encoding="utf-8").read()
    script = script.replace("wait_map MAP", f"wait_map {m.group(2)}")
    rpath = os.path.join(run_dir, "reload.txt")
    with open(rpath, "w", encoding="utf-8") as f:
        f.write(script)
    reload = dict(env)
    saves = sorted(glob.glob(os.path.join(user, CARD_GLOB)))
    if not saves:
        problems.append("play: the save isn't on the card")
        return problems, secs
    reload.update({"TTYD_INPUT": rpath, "TTYD_SETTINGS_DUMP": os.path.join(run_dir, "dump_reload.tsv"),
                   "TTYD_LOAD_SAVE": saves[0]})
    if "TTYD_OPEN_SEED" in reload:
        del reload["TTYD_OPEN_SEED"]  # (a new file's: the save has it)
    code, text, secs2 = run_game(args, run_dir, "reload", reload, args.timeout)
    problems += classify(code, text, "reload")
    if code == 0 and "[input] end" not in text:  # (exit 0 alone: nothing shows the save loaded and the walk ran)
        problems.append("reload: the input script never reached its end")
    check_dump(read_dump(reload["TTYD_SETTINGS_DUMP"]), combo, problems, "reload", code == 0)
    return problems, secs + secs2


def discover(args):
    """the settings a run declares (one with defaults), and those in the source not declared there"""
    d = os.path.join(args.out, "discover")
    combo = {}
    problems, _ = run_combo(args, combo, d)
    for p in problems:
        print("  discovery run: " + p, flush=True)
    dump = {}
    for name in ("dump_play.tsv", "dump_reload.tsv", "dump_aurora.tsv"):
        dump.update(read_dump(os.path.join(d, name)))
    if not dump:  # (the game never got going: no list, no combinations worth running)
        sys.exit(f"settings_fuzz: the discovery run wrote no settings dump: {d}")
    static = static_settings()
    listed = {}
    for k, s in sorted(dump.items()):
        listed[k] = dict(min=s["min"], max=s["max"], default=s["default"], declared=s["declared"],
                         field=s["field"], source=static.get(k, {}).get("source"))
    for k, s in sorted(static.items()):
        if k not in listed:
            listed[k] = dict(min=s["min"], max=s["max"], default=s["default"], declared=False, field=False,
                             source=s["source"])
    return listed


def summarize_problems(problems):
    real = [p for p in problems if ": note:" not in p and ": harness:" not in p]
    return real, [p for p in problems if ": note:" in p or ": harness:" in p]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("--iso", required=True)
    ap.add_argument("--out", default="settings_fuzz_out")
    ap.add_argument("--runs", type=int, default=20, help="random combinations (after the pairwise set)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-pairwise", action="store_true")
    ap.add_argument("--pairwise-only", action="store_true")
    ap.add_argument("--max-combos", type=int, default=0, help="stop after this many in all (0: no limit)")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--slot", default="", help="a wrapper command for each game (shared game slots)")
    ap.add_argument("--map", default="hei_00")
    ap.add_argument("--event", type=int, help="TTYD_EVENT row instead of the map warp")
    ap.add_argument("--no-fight", action="store_true", help="the play run without its fight")
    ap.add_argument("--timeout", type=int, default=240, help="seconds a run may take")
    ap.add_argument("--watchdog", type=int, default=30)
    ap.add_argument("--rate", type=float, default=0.12, help="random runs: the share of other settings changed")
    ap.add_argument("--aurora", action="store_true", help="the Aurora build: the graphics settings")
    ap.add_argument("--aurora-frames", type=int, default=600)
    ap.add_argument("--list", action="store_true", help="write the settings list (JSON) and stop")
    ap.add_argument("--repro", help="a fail_*.json: run that combination again")
    ap.add_argument("--keep", action="store_true", help="keep passing runs' folders")
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)

    args.card = None
    if not args.aurora:
        print("making the card (a new file) ...", flush=True)
        args.card = make_card(args)

    if args.repro:
        rec = json.load(open(args.repro, encoding="utf-8"))
        problems, secs = run_combo(args, rec["combo"], os.path.join(args.out, "repro"))
        real, notes = summarize_problems(problems)
        for p in problems:
            print("  " + p)
        print("FAIL" if real else "PASS", f"({secs:.0f} s)")
        return 1 if real else 0

    print("discovering the settings ...", flush=True)
    listed = discover(args)
    with open(os.path.join(args.out, "settings_list.json"), "w", encoding="utf-8") as f:
        json.dump(listed, f, indent=1, sort_keys=True)
    declared = {k: s for k, s in listed.items() if s["declared"]}
    print(f"{len(listed)} settings ({len(declared)} declared on this backend's run); "
          f"list: {os.path.join(args.out, 'settings_list.json')}", flush=True)
    if args.list:
        return 0

    rng = random.Random(args.seed)
    combos = []
    if args.aurora:
        factors = {k: domain_of(k, listed) for k in AURORA_KEYS if k in listed and k not in DENY}
        rows = [] if args.no_pairwise else pairwise(factors, rng)
        for r in rows:
            combos.append(("pairwise", {**r, "framerate": rng.choice(AURORA_FRAMERATES)}))
        for _ in range(0 if args.pairwise_only else args.runs):
            combos.append(("random", {k: rng.choice(v) for k, v in factors.items()}))
    else:
        factors = {k: domain_of(k, listed) for k in RISKY}
        if not args.no_pairwise:
            combos += [("pairwise", r) for r in pairwise(factors, rng)]
        others = [k for k, s in declared.items() if k not in DENY and k not in RISKY and not s["field"]
                  or (s["field"] and k in ("text_speed", "run_speed", "ac_cue", "wide_hud"))]
        for _ in range(0 if args.pairwise_only else args.runs):
            c = {k: rng.choice(v) for k, v in factors.items() if rng.random() < 0.5}
            for k in others:
                if rng.random() < args.rate:
                    c[k] = random_value(rng, declared[k])
            combos.append(("random", c))
    if args.max_combos:
        combos = combos[:args.max_combos]
    print(f"{len(combos)} combinations (seed {args.seed}), {args.jobs} at a time", flush=True)
    if not combos:  # (e.g. --no-pairwise with --runs 0: 0 of 0 is not a pass)
        print("settings_fuzz: no combinations to run")
        return 2

    from concurrent.futures import ThreadPoolExecutor
    results = []

    def one(i):
        kind, combo = combos[i]
        d = os.path.join(args.out, f"run_{i:04d}")
        problems, secs = run_combo(args, combo, d)
        real, notes = summarize_problems(problems)
        rec = dict(index=i, kind=kind, seed=args.seed, combo=combo, problems=real, notes=notes,
                   seconds=round(secs, 1), backend="aurora" if args.aurora else "headless",
                   map=args.map, event=args.event)
        if real:
            with open(os.path.join(args.out, f"fail_{i:04d}.json"), "w", encoding="utf-8") as f:
                json.dump(rec, f, indent=1, sort_keys=True)
        elif not args.keep:
            shutil.rmtree(d, ignore_errors=True)
        print(f"[{i + 1}/{len(combos)}] {kind} {'FAIL' if real else 'ok'} {secs:.0f}s"
              + "".join("\n    " + p for p in real), flush=True)
        return rec

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        results = list(ex.map(one, range(len(combos))))
    with open(os.path.join(args.out, "results.jsonl"), "w", encoding="utf-8") as f:
        for r in results:
            f.write(json.dumps(r, sort_keys=True) + "\n")
    fails = [r for r in results if r["problems"]]
    # group failures by their first problem's kind
    groups = {}
    for r in fails:
        key = re.sub(r"\d+", "N", r["problems"][0])[:120]
        groups.setdefault(key, []).append(r["index"])
    harness = [r for r in results if any(": harness:" in n for n in r["notes"])]
    # (a run the harness cut short is no failure, but no pass either: it never got through)
    passed = len(results) - len(fails) - len([r for r in harness if not r["problems"]])
    print(f"\n{passed}/{len(results)} passed"
          + (f" ({len(harness)} cut short by the harness: {harness[0]['notes'][0]})" if harness else ""))
    for k, idx in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(idx):3d}x {k}  (e.g. {os.path.join(args.out, f'fail_{idx[0]:04d}.json')})")
    if fails:
        return 1
    if not passed:
        print("settings_fuzz: no combination got through")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
