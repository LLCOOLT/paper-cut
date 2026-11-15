#!/usr/bin/env python3
"""The online test matrix: generated online scenarios over every axis that has broken online play, run through
testpass.py's online runner (run_online: the games, the relay, the checks every online scenario gets), each a
number of times (a pass rate under 100% is a failure: a flake is a bug), with a seed so any combination reruns
exactly. docs/TESTING.md ("The online matrix") has the axes, the tiers and how to read a report.

  netmatrix.py plan  [--seed N] [--tier local|real|offscreen] [--limit N]   the combinations, nothing run
  netmatrix.py run   [--seed N] [--tier local] [--run NAME] [--only ID,...] [--repeat N] [--repeat-risky N]
                     [--games 6] [--limit N] [--axis K=V,...]                 play them; report.txt ranked
  netmatrix.py real  [--smoke] [--budget 2000] [--yes]                       the real relay: cost first, then play
  netmatrix.py repeat SCENARIO [SCENARIO ...] [-n 10] [--games 6]            existing scenarios N times: pass rates
  netmatrix.py report RUN                                                    the ranked report again

Coverage: every pair of two axes' values in at least one combination (pairwise, greedy, seeded), and every
combination of the risky groups' axes in full (RISKY_FULL below: who joins when x what happens next, leaving x the
network, the play-as picks x the battle kinds, players x leaving). A combination that can't be (a party game on
headless, a network rule only the relay has, by address) is constrained to one that can, never left out quietly.

The local tier: headless games, the local relay (tools/port/sync_test_relay.py, its --net shaping) or by address,
the game's own $TTYD_NET_FAULTS. The real tier: the same harness through tools/port/relay_bridge.py to the real
relay, a few chosen combinations at real pace (60 fps), every request counted against a hard budget. The offscreen
tier: the party games (versus, raid, boss rush, quiz, cards, bingo, dungeon master), which exist in the Aurora
build only (port/src/aurora/*_aurora.c; headless.c stubs them): generated here, run with --backend offscreen by a
tester with an Aurora build.
"""
import argparse
import concurrent.futures
import itertools
import json
import os
import random
import re
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import testpass  # noqa: E402
import full_run  # noqa: E402

REPO = testpass.REPO
OUT = os.path.join(REPO, "local-docs", "netmatrix")
CODE = "2TST-RXYM"

# --- the axes -------------------------------------------------------------------------------------------------
AXES = {
    # how many play: 2 to 4, or 2 with a watcher
    "players": ["2", "3", "4", "2+watch"],
    # the host's and the others' play-as pick (playable_luigi: 0 Mario, 1 Luigi, 2 Peach, 3 Bowser)
    "host_as": ["mario", "luigi", "peach", "bowser"],
    "guest_as": ["mario", "luigi", "peach", "bowser"],
    # Paper Mario 64 looks and partners from the tester's ROM ($PAPERCUT_PM64_ROM; unset: skipped)
    "pm64": ["off", "both"],
    # the disc each plays: all the same, or the guest's another release (refused, clearly, on both)
    "version": ["us", "jp", "eu"],
    # one player's Paper Cut settings unlike the others' (the session plays the host's rules)
    "settings": ["same", "remake", "restorations", "difficulty", "randomizer", "graphics"],
    # when the guests come in
    "join": ["start", "title", "field", "walk", "menu", "scene", "battle", "room"],
    # what the host does once they're in (the story tier: the full run's segments instead, below)
    "action": ["walk", "room", "battle_script", "battle_enemy_first", "battle_host_first", "battle_guest_first",
               "menu", "save", "pit"],
    # how it ends
    "leave": ["stay", "host_leaves", "guest_leaves", "rejoin", "guest_drops", "reconnect", "halfopen"],
    # how they meet
    "link": ["relay", "address"],
    # the network between them
    "net": ["clean", "lat100", "lat400", "jitter", "loss1", "loss10", "dupreorder", "burst", "stall5", "stall30",
            "restart"],
    # the session: playing together (the adventure, co-op) or each their own game (sync what matters)
    "mode": ["coop", "own"],
}
# the offscreen tier's own: the party games (Aurora only)
PARTY = ["versus", "raid", "bossrush", "quiz", "cards", "bingo", "dm2"]

# the risky groups: every combination of their axes, not just pairs (pairwise covers join x action already)
RISKY_FULL = [("leave", "net"), ("host_as", "guest_as"), ("players", "leave"), ("action", "mode"),
              ("host_as", "action")]
RISKY_ACTIONS = re.compile(r"^(battle|seg_|pit)")  # a battle, a story segment, the Pit: repeat-risky times

AS = {"mario": 0, "luigi": 1, "peach": 2, "bowser": 3}
SEGMENTS = {"seg_sewers": "03_sewers", "seg_blooper": "05_blooper", "seg_petalburg": "06_petalburg",
            "seg_hooktail": "19_hooktail", "seg_interludes": "20_interludes", "seg_punio": "22_punio",
            "seg_punies": "31_punies", "seg_bubble_room": "32_bubble_room", "seg_jabbies": "33_jabbies",
            "seg_minors": "41_minors", "seg_yoshi": "44_yoshi_hatches", "seg_storage_room": "47_storage_room",
            "seg_doopliss": "55_doopliss", "seg_keelhaul": "61_keelhaul", "seg_express": "73_excess_express"}
# what each segment is there for (the report says it)
SEGMENT_WHY = {"seg_sewers": "a pipe, the goons fought together", "seg_blooper": "a boss (Blooper)",
               "seg_petalburg": "a town: shops, Star Pieces", "seg_hooktail": "a boss (Hooktail)",
               "seg_interludes": "the Peach and Bowser interludes (a playable Bowser)",
               "seg_punio": "Punio following", "seg_punies": "the Punies following, battles with them",
               "seg_bubble_room": "the Punies following", "seg_jabbies": "battles with the Punies following",
               "seg_minors": "the Glitz Pit's scripted battles", "seg_yoshi": "Yoshi hatching, menus",
               "seg_storage_room": "Ms. Mowz", "seg_doopliss": "a boss (Doopliss), Vivian",
               "seg_keelhaul": "a scene-heavy chapter start", "seg_express": "the Excess Express (followers, text)"}

# worlds the synthetic actions play in (TTYD_MAP and the rest)
WORLD = {
    "rogue": {"TTYD_MAP": "gor_01"},
    # the full run's segment 3 checkpoint: Frankly's, gor_02 story 11, the pipe down to the sewers (a save loaded)
    "frankly": {"TTYD_LOAD_SAVE": "{out}/user/start.gci", "TTYD_STORY_WATCH": 300},
    "meadow": {"TTYD_MAP": "hei_03", "TTYD_STORY": 11, "TTYD_PARTY": "1"},
    "sewer": {"TTYD_MAP": "tik_01", "TTYD_STORY": 21, "TTYD_PARTY": "1"},
    "pit": {"TTYD_MAP": "gor_01", "TTYD_STORY": 130},
    "scene": {"TTYD_EVENT": 7},  # the X-Nauts at the harbour (online_scene)
}
ACTION_WORLD = {"walk": "rogue", "room": "frankly", "menu": "rogue", "save": "rogue", "battle_script": "meadow",
                "battle_enemy_first": "sewer", "battle_host_first": "sewer", "battle_guest_first": "sewer",
                "pit": "pit"}
# joins that put the host somewhere first: the actions that can follow there
JOIN_ACTIONS = {"scene": {"walk", "menu", "save"}, "room": {"walk", "menu", "save"},
                "battle": {"walk", "menu", "save", "battle_script"}}


# the room change: gor_02's pipe down to the sewers (the full run's segment 3, mp_online_host_goons.txt)
PIPE = ["walk_to -250 200 900", "walk_to -250 345 600", "walk_to -232 340 300", "hold a+right 6", "stick 100 26 14",
        "wait 20", "walk_to -190 329 120", "hold down 30", "press_until_map a tik_01 900",
        "press_until_control a 12000"]  # (the goons' scene and battle at the bottom, story 11 to 12)


def valid(c):
    """whether a (partial) combination can be played; the generator never makes one that can't"""
    j, act, lv, link, net = c.get("join"), c.get("action"), c.get("leave"), c.get("link"), c.get("net")
    if j in JOIN_ACTIONS and act is not None and act not in JOIN_ACTIONS[j]:
        return False
    if act is not None and act.startswith("seg_") and j in ("scene", "room", "battle", "walk"):
        return False  # (a segment starts from its own checkpoint: the host's first steps are its script's)
    if act == "pit" and j not in (None, "start", "field", "title"):
        return False
    if c.get("version") in ("jp", "eu") and (act == "room" or j == "room" or (act or "").startswith("seg_")):
        return False  # (the full run's checkpoints are US saves: tests/port/full_run/checkpoints/us)
    if link == "address" and (net in ("restart", "stall5", "stall30") or lv == "halfopen"):
        return False  # (only a relay restarts, stalls a connection or leaves one half open)
    if c.get("mode") == "own" and (lv in ("rejoin",) or (act or "").startswith("battle_guest")):
        return False  # (rejoin's seat, the guest's own swing: co-op's)
    if (str(c.get("version")).startswith("mismatch") or c.get("pm64") in ("host", "guest")) and (c.get("players") not in (None, "2") or lv not in (None, "stay")):
        return False  # (a refusal: two games, nothing after it)
    if c.get("pm64") in ("guest", "both") and c.get("players") == "2+watch" and False:
        return False
    return True


# the story tier: the host plays a full-run segment from its checkpoint with the others along (followers, bosses,
# shops, scenes, text, pipes, saves); fewer axes, the rest held to what the local tier covers
STORY_AXES = {
    "players": ["2", "3", "4"],
    "host_as": ["mario", "luigi", "peach", "bowser"],
    "guest_as": ["mario", "bowser"],
    "pm64": ["off"],  # (the full run's scripts were recorded without the PM64 partners: pm64run_* is its own chain)
    "version": ["us"],
    "settings": ["same", "remake"],
    "join": ["start", "field"],
    "action": list(SEGMENTS),
    "leave": ["stay", "host_leaves", "reconnect"],
    "link": ["relay", "address"],
    "net": ["clean", "lat100", "dupreorder"],
    "mode": ["coop"],
}
STORY_FULL = [("action", "players")]
TIERS = {"local": (AXES, RISKY_FULL), "story": (STORY_AXES, STORY_FULL)}


def all_tuples(axes, full):
    """what must be covered: every pair of values of two axes, and the risky groups in full (valid ones)"""
    need = set()
    for a, b in itertools.combinations(list(axes), 2):
        for va in axes[a]:
            for vb in axes[b]:
                if valid({a: va, b: vb}):
                    need.add(((a, va), (b, vb)))
    for group in full:
        for vals in itertools.product(*(axes[g] for g in group)):
            t = tuple(zip(group, vals))
            if valid(dict(t)):
                need.add(t)
    return need


def covers(c, t):
    return all(c.get(k) == v for k, v in t)


def generate(seed, limit=None, fixed=None, tier="local"):
    """the combinations: greedy (each the best of a few seeded candidates at covering what's still uncovered)"""
    axes, full = TIERS[tier]
    rng = random.Random(seed)
    fixed = fixed or {}
    need = {t for t in all_tuples(axes, full) if all(fixed.get(k, v) == v for k, v in t)}
    by = {}
    for t in need:
        for p in t:
            by.setdefault(p, set()).add(t)
    combos = []
    while need and (limit is None or len(combos) < limit):
        best, best_n = None, -1
        for t0 in rng.sample(sorted(need), min(12, len(need))):
            c = dict(fixed)
            c.update(dict(t0))
            for k in rng.sample(list(axes), len(axes)):
                if k in c:
                    continue
                opts = [v for v in axes[k] if valid(dict(c, **{k: v}))]
                rng.shuffle(opts)
                # (the value completing most uncovered tuples among the axes chosen so far)
                score = {v: sum(1 for t in by.get((k, v), ()) if t in need and
                                all(c.get(k2) == v2 for k2, v2 in t if k2 != k and k2 in c)) for v in opts}
                c[k] = max(opts, key=lambda v: score[v]) if opts else axes[k][0]
            if not valid(c):
                continue
            n = sum(1 for p in c.items() for t in by.get(p, ()) if t in need and covers(c, t))
            if n > best_n:
                best, best_n = c, n
        if best is None or best_n <= 0:
            break
        combos.append(best)
        need -= {t for p in best.items() for t in by.get(p, ()) if covers(best, t)}
    if tier == "local" and not fixed:
        # another release's disc: refused, clearly, on both (each pair of releases, by relay and by address)
        base = dict(players="2", host_as="mario", guest_as="mario", pm64="off", settings="same", join="start",
                    action="walk", leave="stay", net="clean", mode="coop")
        for hv, gv in (("us", "jp"), ("jp", "eu"), ("eu", "us")):
            for link in ("relay", "address"):
                combos.append(dict(base, version=f"mismatch:{hv}/{gv}", link=link))
        # the PM64 partners on in one game only: refused, clearly, on both
        for side in ("host", "guest"):
            for link in ("relay", "address"):
                combos.append(dict(base, version="us", pm64=side, link=link))
    for i, c in enumerate(combos):
        c["id"] = f"{tier[0]}{seed}-{i:03d}"
    return combos, len(need)


# --- a combination as a testpass online scenario --------------------------------------------------------------

def nplayers(c):
    return {"2": 2, "3": 3, "4": 4, "2+watch": 3}[c["players"]]


def net_rules(c, start_s):
    """(relay_args, game faults) for the network axis and the drop kinds of leaving"""
    relay = c["link"] == "relay"
    args, faults = [], None
    n = c["net"]
    if relay:
        spec = {"lat100": "lat:40-60", "lat400": "lat:150-250,jitter:40", "jitter": "lat:10-20,jitter:120",
                "loss1": "loss:1", "loss10": "loss:10", "burst": "burst:3000/600/40"}.get(n)
        if spec:
            args += ["--net", spec]
        if n == "stall5":
            args += ["--net-role", f"guest=stall:{start_s:.0f}+5"]
        if n == "stall30":
            args += ["--net-role", f"guest=stall:{start_s:.0f}+30"]
        if n == "restart":
            args += ["--restart", f"{start_s:.0f}:3"]
    else:
        faults = {"lat100": "delay:40-60", "lat400": "delay:150-250", "jitter": "delay:0-120", "loss1": "drop:1",
                  "loss10": "drop:10", "burst": "drop:5,delay:0-120"}.get(n)
    if n == "dupreorder":
        faults = "dup:3,reorder:3"
    return args, faults


def build(c, rep=0):
    """the scenario (a testpass dict) for one combination and repeat; rep varies the timing and fault seeds"""
    rng = random.Random(f"{c['id']}/{rep}")
    n = nplayers(c)
    coop = c["mode"] == "coop"
    pick = 1 if coop else -1
    act, join, lv = c["action"], c["join"], c["leave"]
    seg = SEGMENTS.get(act)
    # the guests' join frame (field, walk, menu, battle, room, scene): game frames from the start, where the
    # scripts count from the map's load (LOAD frames in), so a join "at the host's step N" is LOAD + N
    LOAD = 690
    J = 600 + rng.randrange(-60, 61)
    env = {"TTYD_AUTO_BATTLE": 1, "TTYD_GOD": 1, "TTYD_AUTO_TEXT": 1, "TTYD_EXIT_FRAMES": 60000,
           "TTYD_SYNC_BATTLE_LOG": 1}
    user_gen = []
    world = None
    if seg:
        user_gen = [{"script": "tools/port/full_run_card.py", "args": [seg], "dest": "start.gci"}]
        env.update(TTYD_LOAD_SAVE="{out}/user/start.gci", TTYD_STORY_WATCH=300, TTYD_GOD_POWER=2)
        # (the segment's own '#env' and '#setting' lines, as full_run.py plays it: its choices answered
        # (TTYD_AUTO_CHOICE), its battle hooks. Read as comments, they were lost: 05_blooper's Frankly asked
        # "a lesson on badges?" and got yes (option 0), a lecture the script never meant to sit, its A presses
        # read by nothing it waited on: st51 s51-002/025 stuck in gor_02 at story 19)
        d = full_run.directives(os.path.join(REPO, "tests", "port", "full_run", seg + ".txt"))
        env.update(d["env"])
        env.update({f"TTYD_SET_{k}": v for k, v in d["setting"].items()})
    else:
        world = {"scene": "scene", "battle": "meadow", "room": "frankly"}.get(join) or ACTION_WORLD[act]
        env.update(WORLD[world])
        if world == "frankly":
            user_gen = [{"script": "tools/port/full_run_card.py", "args": ["03_sewers"], "dest": "start.gci"}]
    seats = n - (1 if c["players"] == "2+watch" else 0)  # (a watcher has no seat: never a co-op guest)
    waitc = f"coop{seats - 1}" if coop else f"here{seats - 1}"
    # the host's script: its first steps (as the guests join), the action, the end
    host, guest = [], []
    hen, gen = {"TTYD_SYNC_PICK": pick}, {}
    if seg or world == "frankly":
        host += ["wait_seq 2 3000", "press_until_control a 3000"]
    at = {"field": LOAD + 150, "walk": LOAD + 520, "menu": LOAD + 450, "battle": LOAD + 600, "room": LOAD + 560,
          "scene": 400}.get(join)
    if at is not None:
        gen["TTYD_SYNC_AT"] = at + rng.randrange(-60, 61)
    if join == "walk":
        host += ["wait 400", f"stick 100 0 {rng.randrange(250, 400)}"]
    elif join == "scene":
        host += ["press_until_control a 9000"]  # (the scene played to its end, the text through)
    elif join == "battle":
        host += ["wait 200", "give 148", "battle 4", "wait_seq 4 3000", "wait_seq 2 12000",
                 "press_until_control a 6000"]
    elif join == "room":
        host += ["wait 450"] + PIPE
    host += [f"wait_peer {waitc} 12000", "wait 120"]
    gscript = ["wait_seq 2 4000" if join != "title" else "wait 60", "wait_peer here 12000"]
    if join == "title":
        gen.update(TTYD_MAP=None, TTYD_STORY=None, TTYD_PARTY=None, TTYD_LOAD_SAVE=None, TTYD_EVENT=None)
        gscript = ["wait_peer here 12000"]
    own_file = []
    if join == "title" and not coop and c["version"] == "us":
        # (each their own game, joined at the title: the guest then plays a file of its own (a card's file 1, loaded
        # from the file menu). With none, its "game" was the headless title's idle demo (dmo_00): the host's
        # progress merged into that, and once the demo ended (a long row: halfopen, stall30) the title's zeros
        # were its progress: "progress differs", nm51f l51-035. Rows that passed did so only while the demo lasted)
        own_file = ["press_until_seq start 6 3000", "wait 120", "press_until_seq a 2 3000", "wait_seq 2 6000"]
    if join == "menu":
        # (the guest's own Start menu up as it joins: from its step 300 to 800, the join at about 450)
        gscript = ["wait_seq 2 4000", "wait 300", "press start", "wait 500", "press start", "wait 60",
                   "wait_peer here 12000"]
    gscript_watch = list(gscript)
    gscript = gscript + own_file
    # the action
    if seg:
        lines = [ln.split("#")[0].strip() for ln in open(os.path.join(REPO, "tests", "port", "full_run", seg + ".txt"),
                                                        encoding="utf-8")]
        lines = [ln for ln in lines if ln and ln != "end"]
        # (its own first steps are the load's, done above)
        while lines and lines[0].split()[0] in ("wait_seq", "press_until_control") and len(host) < 40:
            lines.pop(0)
            break
        host += lines
        fr = json.load(open(os.path.join(testpass.SCENARIOS, f"fullrun_{seg}.json"), encoding="utf-8"))
        last_map = re.search(r"map '(\w+)'", json.dumps(fr.get("log_must", [])))
        final = last_map.group(1) if last_map else None
        # (judged as the full run judges it alone: its allow_failures (a walk_to that ends a step short of its point
        # and the script goes on: 55_doopliss's -100 -60 on the ledge, 03_sewers' -395 -40 under the walkway, 61's
        # e_bero; the story watch's 300 s with no story change in a long room: 32's Punies), and its own end, the
        # segment's save at its last map and story. Those lines failed rows the single-player full runs pass with
        # the very same lines: st51 s51-005/007/008/012/016/019. A hard FAIL still ends a script (its exit), and a
        # guest still waits for the host's last map)
        seg_allow = list(fr.get("allow_failures", []))
        seg_must = [rx for rx in fr.get("log_must", []) if "save:" in rx]
    else:
        seg_allow, seg_must = [], []
        final = None
        host += {
            "walk": ["hold right 90", "hold left 150", "hold right 60", "wait 60"],  # (no up or down: a pipe)
            "room": PIPE + ["wait 60"],
            "menu": ["press start", "wait 300", "press b", "press_until_control b 600", "wait 60"],
            "save": ["save", "wait 120"],
            "battle_script": ["give 148", "battle 4", "wait_seq 4 3000", "wait_seq 2 12000",
                              "press_until_control a 6000"],
            "battle_enemy_first": ["goto 340 -50 38", "wait 160", "goto 405 -50 38", "hold right 40",
                                   "wait_seq 4 1500", "wait_seq 2 12000", "press_until_control a 6000"],
            "battle_host_first": ["where npcs", "walk_to npc 3 900", "press b", "wait_seq 4 1500",
                                  "wait_seq 2 12000", "press_until_control a 6000"],
            "battle_guest_first": ["wait_seq 4 4000", "wait_seq 2 12000", "press_until_control a 6000"],
            "pit": ["wait 1500", "press_until_control a 3000"],
        }[act]
        if act == "battle_guest_first":
            # (the guest's own character seeks the nearest enemy from soon after it joins and walks into it; the host
            # puts its Spiky Goomba before that character a little later: online_act_hammer's. No B: these games are
            # headless, where Luigi's hammer never reaches an enemy (its fbatHitCheck hook is the Aurora build's:
            # brothers_aurora.c; online_act_hammer plays the swing offscreen), so a swing 26 away missed and the
            # battle came only if the Goomba drifted into him: 9 of 11 once, none once Luigi stood elsewhere, nl51)
            hen["TTYD_SYNC_ENEMY_TO_SEAT"] = f"{(at or LOAD) + 900}:2:0:60"
            gen["TTYD_SYNC_OWN_SEEK"] = f"{(at or LOAD) + 600}"
            env["TTYD_SET_brothers_mode"] = 1
        if act == "pit":
            hen["TTYD_SYNC_MENU_AT"] = f"{J + 300}:pit"
    # the end: everyone settled and in the same place (where coop, during the host's pause), then the leave kind.
    # (Everyone still meant to be here is heard from first, and again a while into the pause: a guest whose link
    # stalled or went half-open (stall30, restart, halfopen, a rejoin) as the host paused reads as here for AWAY_MS
    # of wall time, and the host's TTYD_FAST frames run on: it paused and left while that guest couldn't hear it,
    # the guest waited for a pause that was over, then was told the host had left (nm51s l51-005/008/012/016). The
    # pause held past AWAY_MS (2400 frames, seconds at any speed) and then till they're back, they see it)
    stay = n - 1 - (1 if lv == "guest_leaves" else 0)
    here = [f"wait_peer here{stay} 60000"] if stay > 0 else []
    away = "wait 2400" if c["id"].startswith("l") else "wait 240"  # (the real tier plays at 60 fps)
    # (the Start menu opens only in the field with control: pressed while the host fought a battle (tik_01's, story
    # 11) it opened nothing, every guest waited for a pause that never came and was left at the host's leave:
    # nm51f l51-020/034/052/059. So the battle's end, the field's fade-in (a Start the frame after the battle
    # ended opened nothing either) and the host's control first)
    # ("online last_pause": this pause is marked the host's last in its words, the guests' "wait_peer paused_last")
    # (Peach's interludes (aji_*) have no Start menu: Start there opened nothing, every guest waited for a last pause
    # that never came and was left at the host's end, st51e s51-009 (seg_hooktail ends in aji_16). There the host
    # reads as paused by the test's own word, online hold_pause)
    no_menu = bool(final and final.startswith("aji_"))
    pause_on = ["online hold_pause on"] if no_menu else ["press start"]
    pause_off = ["online hold_pause off", "wait 120"] if no_menu else ["press b", "press_until_control b 600", "wait 120"]
    held = ["wait_seq 2 30000", "wait 300", "press_until_control b 3000", "online last_pause"] + pause_on + [
            "wait 400"] + (
        [away] + here + ["wait 1200"] if here else []) + pause_off + here + ["wait_peer !last_seen 60000"]
    # ("!last_seen": no guest still sees that pause up, as it last heard the host. A guest whose link to the host
    # stalled through the pause's end (stall30) never heard it end; the host, still hearing that guest, went on and
    # left, and the guest waited for an end that never came: nm51f l51-097. Everyone staying heard first: that guest
    # was back in the room as a new socket, its key not yet agreed (the stall), and read as gone it held nothing)
    pause = ["wait 300", "where coop 900"] + held
    host += here
    # (each their own game: the host's own Start menu opened and closed all the same, a moment every guest waits
    # for: its link's blips (a half-open link stays "here" for QUIET_MS of wall time, many TTYD_FAST frames) never
    # read as the host gone, which ended a guest's script mid-battle without the battle's progress, l51-013)
    host += pause if coop else ["wait 1500", "where coop 600"] + held
    gtail = []
    if final:
        gtail.append(f"wait_peer map={final} 60000")
    # (the host's last pause is marked so ("online last_pause", wait_peer paused_last): the pauses before it (a menu
    # action's) never counted. Counted, a guest away for one of them (a rejoin over a relay restart) took the last
    # for it, waited for one more that never came and ended on dmo_00 once the host left: nm51f l51-053)
    # (the last pause seen out to its end: "!paused" also holds while this guest's link is out, the host unheard,
    # and a guest that ended then left the host's pause waiting for it, l51-004's half-open player 4. So once the
    # host is heard again, its pause's end is waited for once more; unless the host leaves right after it)
    # (where coop waits up to 2400 frames, still inside the host's pause (400 + AWAY 2400 + 1200 with anyone staying):
    # a guest whose link stalled mid-battle follows the host's steps behind it and was still in the battle, its
    # end 640 frames on, when 300 ran out: "battle 1, host's 0", nm51f l51-062. Settled sooner, it prints sooner)
    last = ["wait_peer paused_last 60000", "wait 120", "where coop 2400", "wait_peer !paused 60000"] + (
        ["wait_peer host 60000", "wait_peer !paused 60000"] if lv != "host_leaves" else [])
    gtail += last if coop else [
                  "wait 1200", "wait_peer here 12000", "wait_peer paused_last 60000", "wait_peer !paused 60000",
                  "wait_peer !here 30000", "wait 600", "wait_peer !here 30000"]
    # (a dropped one back first; and a link that drops and resumes later on (a relay's kill, a stall) isn't the
    # host gone: once it's back the second wait holds till the host really goes. A guest that took its link's
    # blip for the end left mid-battle without the battle's changes: "progress differs", l51-065/110)
    if lv == "host_leaves":
        host += ["online leave", "wait 1500"]
        gtail += ["wait_peer !here 8000", "wait 900"]
    else:
        host += ["wait 600"]
        gtail += ["wait 300"]
    gen.setdefault("TTYD_SYNC_END_PICK", "own")
    # the players
    players = [{"role": "host", "env": dict(hen, TTYD_SET_playable_luigi=AS[c["host_as"]]), "input": host + ["end"]}]
    if seg_must:
        players[0]["log_must"] = players[0].get("log_must", []) + seg_must
    for i in range(1, n):
        role = "guest" if i == 1 else f"guest{i}"
        pe = dict(gen, TTYD_SET_playable_luigi=AS[c["guest_as"]])
        script = list(gscript) + gtail
        pl = {"role": role, "env": pe, "input": script + ["end"]}
        if own_file:
            code, folder = full_run.REGIONS["us"]
            pl["user_gen"] = [{"script": "tools/port/full_run_card.py", "args": ["03_sewers"],
                               "dest": f"{folder}/Card A/01-{code[:4]}-{full_run.CARD_FILE}.gci"}]
        if i > 1:
            pe.pop("TTYD_SYNC_OWN_SEEK", None)
            if "TTYD_SYNC_AT" in pe:
                pe["TTYD_SYNC_AT"] = pe["TTYD_SYNC_AT"] + 60 * i
        if c["players"] == "2+watch" and i == n - 1:
            role = pl["role"] = "watch"
            pe["TTYD_SYNC_WATCH"] = 1
            pe.pop("TTYD_SYNC_OWN_SEEK", None)
            pl["input"] = [x for x in gscript_watch + gtail if not x.startswith("where")] + ["end"]
            pl.pop("user_gen", None)  # (a watcher plays no file: never its own)
            pl["checked"] = False  # (a watcher: no seat; its own checks only, crash, exit, screen)
        players.append(pl)
        pl["start_delay"] = round(rng.uniform(0.2, 2.0), 2)
    for p in players:
        if seg_allow:
            p["allow_failures"] = list(seg_allow)
    # the leaving guest (the last player, not a watcher)
    leaver = next((p for p in reversed(players[1:]) if p["role"] != "watch"), None)
    start_s = 4 + J / 300.0 + rng.uniform(0, 3)  # seconds after the leaver's first open: mid-action
    relay_args, faults = net_rules(c, start_s)
    musts = {}
    if leaver is not None:
        if lv == "guest_leaves":
            # (in its game first, settled (where coop): joined at the title, "here" comes as it links, before the host's world is
            # loaded, and a leave then is a join given up, not "You left the room": l51-012)
            # (and only once no game is busy: the host still in its scene when this guest left never saw it as a
            # co-op guest, its "wait_peer coop1" ran out: nm51f l51-040)
            # (a battle action: it leaves mid-battle, once the host's has begun. Left 600 frames after settling, it was
            # gone before the host's battle, which nobody shared: "shared battle begins" never came, nm51f l51-038
            # (2+watch), l51-082 (2 players, battle_guest_first))
            # (for a battle, no "where coop" first: this guest is in the shared battle itself, so it settled only as the
            # battle ended and its "wait_peer battle" then waited for one that was over: nl51r l51-049, l51-095. The
            # host in battle means its world is loaded and this guest linked)
            calm = ["wait_peer battle 12000", "wait 300"] if act.startswith("battle_") else [
                "wait_peer !busy 60000", "wait 600"]
            settle = [] if act.startswith("battle_") else ["where coop 3000"]
            leaver["input"] = gscript + settle + calm + ["online leave", "wait 900", "end"]
            leaver["checked"] = False
            if coop:
                musts.setdefault("host", []).append(r"co-op: seat \d's player \d is gone|player \d (left|is gone)")
                leaver["log_must"] = [r"end: You left the room\."]
            else:  # (each their own game: the leaver's link closed, the host told by its close)
                musts.setdefault("host", []).append(r"net: connection to player \d lost|player \d (left|is gone)")
                leaver["log_must"] = [r"net: off \(this player left\)"]
        elif lv == "rejoin":
            # (by address: the input script's "online join port" is [::1] on $TTYD_SYNC_PORT, the host's; a bare
            # "[::1]" joined the default port 47700, where no test host listens: l51-005/012 never came back)
            target = CODE if c["link"] == "relay" else "port"
            if c["link"] != "relay":
                leaver["env"]["TTYD_SYNC_PORT"] = "{port}"
            leaver["input"] = gscript + ["where coop 3000", "wait_peer !busy 60000", "wait 600", "online leave", "wait 600",
                                         f"online join {target}",
                                         "wait_peer here 12000", "where coop 3000"] + gtail + ["end"]
            leaver["log_must"] = [r"end: You left the room\.", r"\[input\] online join .*: on"]
        elif lv in ("guest_drops", "reconnect", "halfopen"):
            kind = {"guest_drops": "--kill", "reconnect": "--quiet", "halfopen": "--halfopen"}[lv]
            r = leaver["role"].replace("guest2", "peer0").replace("guest3", "peer1")
            if c["link"] == "relay":
                relay_args += [kind, f"{r}:{start_s:.1f}" + (":3" if lv == "reconnect" else "")]
            else:
                fr = J + 600 + rng.randrange(0, 300)
                leaver["faults"] = (faults + "," if faults else "") + (
                    f"disconnect_at:{fr}" if lv == "guest_drops" else f"offline_ms:{fr}-4000")
    for p in players:
        if p["role"] in musts:
            p["log_must"] = p.get("log_must", []) + musts[p["role"]]
    check = {"linked": True, "drift": True, "names": True, "progress": not coop, "same_where": coop,
             "battle_ended": act.startswith("battle")}
    if join == "title" and not coop and not own_file:
        # (each their own game, joined at the title, on a disc with no checkpoint card (the full run's are US saves):
        # the guest has no file of its own, its "game" the headless title's idle demo, whose progress is the title's
        # once the demo ends (nm51f l51-035, JP). Nothing of its own to compare: the link, names and crashes only)
        check["progress"] = False
    if lv in ("guest_leaves",):
        check["names"] = False
    if act in ("pit",) or seg:
        check["battle_ended"] = False
    if check["battle_ended"] and (lv not in ("stay", "host_leaves") or c["net"] in ("restart", "stall30")):
        # (a player gone mid-battle ends it another way, "the other went on alone"): it began, and it ended
        check["battle_ended"] = False
        musts.setdefault("host", [])
        own = ""
        if seats == 2 and (lv in ("guest_drops", "reconnect", "halfopen") or c["net"] in ("restart", "stall30")):
            # (the only seated guest's link out as the host's battle began (the outage's time is the relay's or the
            # faults', not the battle's): the host fights it alone, as its own, and the guest, back mid-way, waits for
            # its end (a late join needs a shared battle): nm51f l51-057. That's an outcome too)
            own = r"|player \d's progress kept for after this game's own battle"
        players[0]["log_must"] = players[0].get("log_must", []) + [r"battle \d+: the shared battle begins" + own,
                                                                   r"battle \d+: the shared battle ends" + own]
    s = {"name": f"nm_{c['id']}_r{rep}", "desc": describe(c), "versions": ["us"], "backend": "headless",
         "env": env, "timeout": 1500 if seg else 420,
         "online": {"players": players, "link": c["link"], "check": check, "seed": rng.randrange(1, 1 << 30),
                    "pace": 300}}
    if relay_args:
        s["online"]["relay_args"] = relay_args + ["--seed", str(s["online"]["seed"])]
    if faults:
        s["online"]["faults"] = faults
    if user_gen:
        s["user_gen"] = user_gen
    # settings differences: the last player's own
    other = {"same": {}, "remake": {"remake_balance": 1, "remake_field_hammer": 1, "remake_guard_stance": 1},
             "restorations": {"rs_chalk_outline": 1, "rs_bunny_boo": 1, "rs_xnaut_crowd": 1},
             "difficulty": {"difficulty": 3}, "randomizer": {},
             "graphics": {"texture_upscale": 4, "msaa": 0, "widescreen": 1}}[c["settings"]]
    if other:
        players[-1]["settings"] = other
    if c["settings"] == "randomizer":
        # (co-op: the last player's own file a randomizer, which it leaves for the host's. Each their own game:
        # every file the same seed, as players who play a randomizer together have; another kind of file is
        # refused there by design, online_own_files_refused: nm51s l51-011's guest seed against a vanilla host)
        seed = f"NM{rng.randrange(10**6)}"
        for p in (players if not coop else players[-1:]):
            p["env"]["TTYD_OPEN_SEED"] = seed
    if c["settings"] == "difficulty":
        players[0]["settings"] = {"difficulty": 0}
    # Paper Mario 64 looks and partners (the tester's ROM)
    pm = {"TTYD_PM64_ROM": "{pm64rom}", "TTYD_SET_pm64_partners": 1, "TTYD_SET_pm64_met_all": 1}
    for i, p in enumerate(players):
        if c["pm64"] == "both" or (c["pm64"] == "host" and i == 0) or (c["pm64"] == "guest" and i > 0):
            p["env"].update(pm)
    # versions: the same disc for all (another release: their own builds), or a refusal
    if c["version"] in ("jp", "eu"):
        s["versions"] = [c["version"]]
    if c["version"].startswith("mismatch") or c["pm64"] in ("host", "guest"):
        if c["pm64"] in ("host", "guest"):
            refuse = r"Paper Mario 64 partners are on in one game"
        else:
            hv, gv = c["version"].split(":")[1].split("/")
            s["versions"] = [hv]
            refuse = r"Different disc release \(yours [A-Z?]+, your friend.s [A-Z?]+\): both need the same release\."
            players[1]["version"] = gv
        s["env"] = {"TTYD_MAP": "gor_01", "TTYD_EXIT_FRAMES": 1200}
        s.pop("user_gen", None)
        for p in players:
            p["input"] = ["wait 1100", "end"]
            p["log_must"] = [refuse]
            p.pop("checked", None)
            for k in ("TTYD_SYNC_AT", "TTYD_SYNC_OWN_SEEK", "TTYD_SYNC_ENEMY_TO_SEAT", "TTYD_SYNC_MENU_AT",
                      "TTYD_MAP", "TTYD_STORY", "TTYD_PARTY", "TTYD_LOAD_SAVE", "TTYD_EVENT"):
                p["env"].pop(k, None)
        s["online"]["check"] = {"linked": False, "progress": False, "names": False, "drift": False}
        s["timeout"] = 240
    return s


def describe(c):
    seg = SEGMENTS.get(c["action"])
    what = f"the full run's {seg} ({SEGMENT_WHY[c['action']]})" if seg else c["action"]
    return (f"{c['players']} players ({c['mode']}), host as {c['host_as']}, others as {c['guest_as']}, PM64 "
            f"{c['pm64']}, disc {c['version']}, settings {c['settings']}; join at {c['join']}, then {what}; "
            f"{c['leave']}; {c['link']}, net {c['net']}")


def risky(c):
    return bool(RISKY_ACTIONS.match(c["action"])) or c["leave"] != "stay" or c["join"] == "battle"


# --- running --------------------------------------------------------------------------------------------------

class Games:
    """at most N of this tool's games at once (a scenario takes as many as it plays)"""

    def __init__(self, n):
        self.n, self.used, self.cv = n, 0, threading.Condition()

    def take(self, k):
        with self.cv:
            self.cv.wait_for(lambda: self.used + k <= self.n)
            self.used += k

    def give(self, k):
        with self.cv:
            self.used -= k
            self.cv.notify_all()


def runner_args(run, real=False, budget=2000):
    return argparse.Namespace(run=run, tree=REPO, backend=None, iso_dir=None, data_root=None, timeout_scale=1.0,
                              keep_user=False, part=time.strftime("%Y%m%d-%H%M%S"), real_relay=real,
                              real_budget=budget, ui_shots=False, user_sources=set())


def play(jobs, run, games_cap, real=False, budget=2000):
    """[(combo id, rep, scenario)] -> results (each run_one_safe's, with its id and rep)"""
    a = runner_args(run, real, budget)
    # (a combination with more players than the cap could never take its games: the run waited for ever, idle)
    largest = max((len(s["online"]["players"]) for _, _, s in jobs), default=0)
    if games_cap < largest:
        print(f"netmatrix: --games {games_cap} raised to {largest}, the most players a combination here has", flush=True)
        games_cap = largest
    gate = Games(games_cap)
    lock = threading.Lock()
    results = []
    log_path = os.path.join(OUT, run, "results.jsonl")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)

    def one(job):
        cid, rep, s = job
        k = len(s["online"]["players"])
        gate.take(k)
        try:
            res = testpass.run_one_safe(s, s.get("versions", ["us"])[0], a)
        finally:
            gate.give(k)
        res.update(combo=cid, rep=rep)
        with lock:
            results.append(res)
            with open(log_path, "a", encoding="utf-8") as f:
                f.write(json.dumps({k2: res.get(k2) for k2 in ("combo", "rep", "scenario", "version", "ok", "skipped",
                                                              "why", "seconds")}) + "\n")
            mark = "SKIP" if res.get("skipped") else "PASS" if res["ok"] else "FAIL"
            print(f"{mark} {s['name']} ({res.get('seconds', '-')} s)" +
                  ("" if res["ok"] else f" - {str(res.get('why'))[:300]}"), flush=True)
        return res

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, games_cap // 2 + 1)) as pool:
        list(pool.map(one, jobs))
    return results


SEVERITY = [("crash", r"crash|panic|abort"), ("hang", r"ran too long|stall|not reached|could not be stopped"),
            ("screen", r"screen"), ("exit", r"exit -?\d"), ("apart", r"NOT SETTLED|units from|story \d+, host|"
                                                                     r"map \w+, host|partner|battle \d, host"),
            ("drift", r"DRIFT|progress differs|hits differ"), ("link", r"never linked|names"),
            ("log", r"log lacks|log has"), ("harness", r"testpass error|no build|no disc")]


def classify(why):
    text = " ".join(why) if isinstance(why, list) else str(why or "")
    for name, rx in SEVERITY:
        if re.search(rx, text):
            return name
    return "other"


def write_report(run, combos_by_id=None):
    path = os.path.join(OUT, run, "results.jsonl")
    rows = [json.loads(l) for l in open(path, encoding="utf-8")] if os.path.exists(path) else []
    plan = os.path.join(OUT, run, "plan.json")
    meta = json.load(open(plan, encoding="utf-8")) if os.path.exists(plan) else {}
    combos_by_id = combos_by_id or {c["id"]: c for c in meta.get("combos", [])}
    by = {}
    for r in rows:
        by.setdefault(r["combo"], []).append(r)
    fails = []
    for cid, rs in by.items():
        ran = [r for r in rs if not r.get("skipped")]
        if not ran:
            continue
        ok = sum(1 for r in ran if r["ok"])
        if ok < len(ran):
            bad = [r for r in ran if not r["ok"]]
            kinds = sorted({classify(r["why"]) for r in bad}, key=lambda k: [s[0] for s in SEVERITY + [("other", "")]]
                           .index(k))
            fails.append((kinds[0], ok, len(ran), cid, bad[0]))
    order = [s[0] for s in SEVERITY] + ["other"]
    fails.sort(key=lambda f: (order.index(f[0]), f[1] / f[2]))
    lines = [f"netmatrix run {run}: seed {meta.get('seed')}, {len(combos_by_id)} combinations, "
             f"{meta.get('uncovered', '?')} tuples left uncovered; {len(rows)} runs, "
             f"{sum(1 for r in rows if r.get('skipped'))} skipped",
             f"combinations failing (any run): {len(fails)} of {sum(1 for rs in by.values() if any(not r.get('skipped') for r in rs))}",
             ""]
    for kind, ok, n, cid, r in fails:
        c = combos_by_id.get(cid, {})
        lines.append(f"[{kind}] {cid}: passed {ok} of {n}  ({describe(c) if c else ''})")
        why = r["why"] if isinstance(r["why"], list) else [r["why"]]
        for w in why[:6]:
            lines.append(f"      {str(w)[:240]}")
        lines.append(f"      logs: local-docs/testpass/{run}/{r['scenario']}/{r['version']}/")
        if isinstance(meta.get("seed"), int):
            extra = (f" --tier {meta.get('tier', 'local')}" + (f" --axis {meta['axis']}" if meta.get("axis") else "") +
                     (f" --limit {meta['limit']}" if meta.get("limit") else ""))
            lines.append(f"      rerun: tools/port/netmatrix.py run --plan {run} --only {cid} "
                         f"--repeat {n} --repeat-risky {n}   (or --seed {meta['seed']}{extra})")
        else:
            lines.append(f"      rerun: tools/port/netmatrix.py repeat {cid} -n {n}")
    txt = "\n".join(lines) + "\n"
    open(os.path.join(OUT, run, "report.txt"), "w", encoding="utf-8").write(txt)
    return txt


def cmd_plan(a):
    combos, left = generate(a.seed, a.limit, fixed_of(a), a.tier)
    combos = [c for c in combos if not a.only or c["id"] in a.only.split(",")]
    by_name = {x["name"]: x for x in testpass.all_scenarios()}
    linted = 0
    for c in combos:
        print(f"{c['id']}  {'R' if risky(c) else ' '}  {describe(c)}")
        bad = testpass.lint_scenario(build(c, 0), by_name)  # (as written: fields, regexes, a positive check)
        for b in bad:
            print(f"      LINT: {b}")
        linted += bool(bad)
    need = all_tuples(*TIERS[a.tier])
    print(f"\n{len(combos)} combinations; {len(need)} tuples to cover ({len(need) - left} covered"
          f"{', ' + str(left) + ' left: raise --limit' if left else ''}); risky {sum(map(risky, combos))}"
          f"; runs at --repeat {a.repeat} / --repeat-risky {a.repeat_risky}: "
          f"{sum(a.repeat_risky if risky(c) else a.repeat for c in combos)}; lint problems in {linted}")
    return 1 if linted else 0


def fixed_of(a):
    return dict(kv.split("=", 1) for kv in (a.axis or "").split(",") if kv)


def cmd_run(a):
    if a.plan:
        # (an earlier run's combinations exactly as it played them, whatever the generator makes now)
        meta = json.load(open(os.path.join(OUT, a.plan, "plan.json"), encoding="utf-8"))
        combos, left = meta["combos"], meta.get("uncovered", 0)
        a.seed, a.tier = meta.get("seed", a.seed), meta.get("tier", a.tier)
    else:
        combos, left = generate(a.seed, a.limit, fixed_of(a), a.tier)
    if a.only:
        combos = [c for c in combos if c["id"] in a.only.split(",")]
    run = a.run or f"nm-{a.tier}-{a.seed}-{time.strftime('%m%d-%H%M')}"
    os.makedirs(os.path.join(OUT, run), exist_ok=True)
    json.dump({"seed": a.seed, "tier": a.tier, "axis": a.axis or "", "limit": a.limit, "uncovered": left,
               "combos": combos}, open(os.path.join(OUT, run, "plan.json"), "w"),
              indent=1)
    jobs = []
    for c in combos:
        for rep in range(a.repeat_risky if risky(c) else a.repeat):
            jobs.append((c["id"], rep, build(c, rep)))
    # (every combination's first run before any's second: a report of every combination early, and games of
    # every size mixed, so the cap packs; the slowest (story segments) first within a round)
    jobs.sort(key=lambda j: (j[1], -j[2]["timeout"]))
    print(f"netmatrix: {len(combos)} combinations, {len(jobs)} runs, at most {a.games} games at once -> "
          f"local-docs/netmatrix/{run}/ (logs in local-docs/testpass/{run}/)", flush=True)
    t0 = time.time()
    play(jobs, run, a.games)
    print(f"netmatrix: {time.time() - t0:.0f} s")
    print(write_report(run, {c["id"]: c for c in combos}))
    return 0


# --- the real relay -------------------------------------------------------------------------------------------

def estimate(s, pace, seconds):
    """the real relay's requests one scenario costs at most (docs/ONLINE_RELAY.md): each connection 4 (the Worker,
    the room, the list's reports), and the messages into the relay 20 to 1: every game sends a batch every 3
    frames, the host one to each other player"""
    n = len(s["online"]["players"])
    msgs = (pace / 3.0) * seconds * (2 * (n - 1))
    reconnects = 2 if s["online"].get("relay_args") else 0
    return int(n * 4 * (1 + reconnects) + msgs / 20 + 0.999)


REAL_PICKS = [  # the real tier's chosen few: what the local tier can't prove (TLS, Cloudflare, the real rooms)
    dict(players="2", host_as="mario", guest_as="mario", pm64="off", version="us", settings="same", join="field",
         action="walk", leave="stay", link="relay", net="clean", mode="coop"),
    dict(players="2", host_as="bowser", guest_as="peach", pm64="off", version="us", settings="same", join="start",
         action="battle_script", leave="stay", link="relay", net="clean", mode="coop"),
    dict(players="3", host_as="luigi", guest_as="mario", pm64="off", version="us", settings="remake", join="field",
         action="walk", leave="guest_leaves", link="relay", net="clean", mode="coop"),
    dict(players="2", host_as="mario", guest_as="luigi", pm64="off", version="us", settings="same", join="field",
         action="walk", leave="rejoin", link="relay", net="clean", mode="coop"),
]


def real_scenarios(a):
    out = []
    picks = REAL_PICKS[:1] if a.smoke else REAL_PICKS[:a.count]
    for i, c in enumerate(picks):
        c = dict(c, id=f"real{i}")
        s = build(c, 0)
        s["name"] = f"nm_{c['id']}"
        o = s["online"]
        o["link"] = "real"
        o["pace"] = 60  # (the real pace: what players cost the relay)
        o["code"] = CODE
        o["budget"] = a.budget
        o.pop("relay_args", None)  # (the real relay has none of the stand-in's switches)
        if a.smoke:
            # two players, a minute: they meet, walk a while, agree
            for p in o["players"]:
                p["input"] = (["wait_peer coop1 6000", "wait 120", "hold right 600", "hold left 600", "hold up 600",
                               "hold down 600", "wait 300"] + s["online"]["players"][0]["input"][-9:]
                              if p["role"] == "host" else p["input"])
            s["timeout"] = 240
        seconds = s["timeout"] * 0.6
        out.append((c, s, estimate(s, 60, seconds)))
    return out


def cmd_real(a):
    picked = real_scenarios(a)
    total = sum(e for _, _, e in picked)
    daily = 100000
    print("the real relay tier (relay_bridge.py; rooms TEST*, &test=netmatrix, never listed):")
    for c, s, e in picked:
        print(f"  {s['name']}: {describe(c)}: about {e} requests (budget {a.budget}, a hard stop)")
    print(f"  in all about {total} requests at most {len(picked) * a.budget}: {100.0 * total / daily:.1f}% of the "
          f"free plan's {daily:,} a day (shared with players; the cap for a run is 5%)")
    if len(picked) * a.budget > 0.05 * daily:
        print("refused: the budgets add up to more than 5% of the day's requests; lower --budget or --count")
        return 1
    if not a.yes:
        print("(nothing run: add --yes)")
        return 0
    run = a.run or f"nm-real-{time.strftime('%m%d-%H%M')}"
    os.makedirs(os.path.join(OUT, run), exist_ok=True)
    json.dump({"seed": "real", "combos": [dict(c, id=f"real{i}") for i, (c, _, _) in enumerate(picked)]},
              open(os.path.join(OUT, run, "plan.json"), "w"), indent=1)
    res = play([(f"real{i}", 0, s) for i, (_, s, _) in enumerate(picked)], run, 2, real=True, budget=a.budget)
    for r in res:
        print(f"  {r['scenario']}: relay counts {r.get('relay_counts')}")
    print(write_report(run))
    return 0 if all(r["ok"] for r in res) else 1


def cmd_repeat(a):
    by = {s["name"]: s for s in testpass.all_scenarios()}
    run = a.run or f"nm-repeat-{time.strftime('%m%d-%H%M')}"
    jobs = []
    for name in a.names:
        s = by[name]
        if "online" not in s:
            print(f"{name}: not an online scenario")
            return 1
        for rep in range(a.n):
            s2 = json.loads(json.dumps(s))
            s2["name"] = f"{name}__r{rep}"
            o = s2["online"]
            o["seed"] = o.get("seed", 1234) + 7919 * rep  # (the faults' seeds vary)
            for p in o["players"]:
                p["start_delay"] = round(random.Random(f"{name}{rep}{p.get('role')}").uniform(0.2, 2.0), 2)
            jobs.append((name, rep, s2))
    os.makedirs(os.path.join(OUT, run), exist_ok=True)
    json.dump({"seed": "repeat", "combos": [{"id": n} for n in a.names]}, open(os.path.join(OUT, run, "plan.json"),
                                                                              "w"))
    res = play(jobs, run, a.games)
    for name in a.names:
        rs = [r for r in res if r["combo"] == name and not r.get("skipped")]
        print(f"{name}: passed {sum(r['ok'] for r in rs)} of {len(rs)}")
    print(write_report(run))
    return 0 if all(r["ok"] for r in res) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("plan", "run"):
        p = sub.add_parser(name)
        p.add_argument("--seed", type=int, default=51)
        p.add_argument("--tier", choices=sorted(TIERS), default="local")
        p.add_argument("--limit", type=int)
        p.add_argument("--only", help="combination ids, comma-separated")
        p.add_argument("--axis", help="axes held to one value: join=battle,net=clean")
        p.add_argument("--repeat", type=int, default=2)
        p.add_argument("--repeat-risky", type=int, default=10)
        p.add_argument("--games", type=int, default=6, help="this tool's games at once (the machine's cap is shared)")
        p.add_argument("--run")
        p.add_argument("--plan", help="an earlier run's name: its combinations (plan.json), not generated anew")
    r = sub.add_parser("real")
    r.add_argument("--smoke", action="store_true", help="one: 2 players, a minute")
    r.add_argument("--count", type=int, default=len(REAL_PICKS))
    r.add_argument("--budget", type=int, default=1200)
    r.add_argument("--yes", action="store_true")
    r.add_argument("--run")
    rp = sub.add_parser("repeat")
    rp.add_argument("names", nargs="+")
    rp.add_argument("-n", type=int, default=10)
    rp.add_argument("--games", type=int, default=6)
    rp.add_argument("--run")
    rr = sub.add_parser("report")
    rr.add_argument("run")
    a = ap.parse_args()
    if a.cmd == "plan":
        return cmd_plan(a)
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "real":
        return cmd_real(a)
    if a.cmd == "repeat":
        return cmd_repeat(a)
    print(write_report(a.run))
    return 0


if __name__ == "__main__":
    sys.exit(main())
