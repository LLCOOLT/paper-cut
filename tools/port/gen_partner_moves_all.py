#!/usr/bin/env python3
"""Writes the partner move sweep as test pass scenarios: tests/port/testpass/partner_moves_all_<who>_<assist>.json,
group "partner_moves_all" (a playtest: Bow's Spook crashed the game in a texture-plane draw).

The short list: the Paper Mario 64 moves that have no animation or effect of their own, but run in a host TTYD
move's slot, on another host move's weapon and targets, or on the port's shared helping script
(pm64_partner_pc.c's sMoves: a "base" move, a BUFF_*, or Bow's four on Vivian's slots). Each scenario fights the
Rogueport Sewers' three Spinias (tik_01, battle 25) with one PM64 partner at Ultra rank (every move on its list)
and uses its short-list moves through the battle menu itself (story_test_aurora.c, $TTYD_MOVE_SWEEP and
$TTYD_MOVE_SWEEP_ONLY: the list, the cursor, A on the row and on the targets, with "battle_immune_cross" on so the
targets' immunities are drawn). Mario Defends; both sides are made whole each command. Twice per partner: action
commands off with foes that take no status ($TTYD_MOVE_SWEEP_IMMUNE: every immunity cross drawn, nobody runs or is
carried off), and Automatic with statuses landing as in play.

Each run must not crash (exit 0), must say "all N moves used", and must never log a skipped texture
("[tex] draw skipped", src/sdk/texPalette.c), animation ("[anim] ...", src/driver/animdrv.c) or effect
("[eff] effect skipped", src/driver/effdrv.c), nor a stuck menu.

usage: gen_partner_moves_all.py [--check]   (--check: exit 1 if a written scenario is out of date)
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "tests", "port", "testpass")
INPUTS = os.path.join(REPO, "tests", "port", "input")
GROUP = "partner_moves_all"
INPUT = "partner_moves_all.txt"

# (key, name, PM64 number (port/include/port/pm64.h; wheel id 8 + it), host party (pm64_partner_pc.c's kWho),
#  the short list as the menu names them)
PM64 = [
    ("goombario", "Goombario", 1, 1, ["Charge"]),
    ("kooper", "Kooper", 2, 2, ["Dizzy Shell"]),
    ("bombette", "Bombette", 3, 3, ["Bomb", "Power Bomb"]),
    ("parakarry", "Parakarry", 4, 4, ["Air Lift"]),
    ("watt", "Watt", 5, 7, ["Power Shock", "Turbo Charge"]),
    ("sushie", "Sushie", 6, 6, ["Squirt", "Water Block"]),
    ("lakilester", "Lakilester", 7, 5, ["Spiny Flip", "Spiny Surge", "Cloud Nine"]),
    ("bow", "Bow", 8, 6, ["Smack", "Outta Sight", "Spook", "Fan Smack"]),
]
ASSISTS = [("assist_off", 0), ("assist_auto", 2)]
HOSTS = ["", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"]


def party_list(first):
    return ",".join(str(p) for p in [first] + [p for p in range(1, 8) if p != first])


def script():
    # two battles: a foe carried off or scared away ends the first early
    lines = ["wait 160", "maxfp 99", "fp 99", "rank 2"]
    for _ in range(2):
        lines += ["battle 25", "wait_seq 4 2500", "wait_seq 2 30000", "wait 30"]
    return lines + ["end"]


def scenario(name, number, host, moves, assist):
    immune = assist[1] == 0
    s = {
        "desc": f"Partner move sweep: {name}'s (Paper Mario 64, on {HOSTS[host]}'s place) moves that run on a host "
                f"move or the shared helping script ({', '.join(moves)}), at Ultra rank, action commands "
                + ("off, the foes taking no status" if immune else "Automatic")
                + ", used through the battle menu in the Rogueport Sewers (tik_01, battle 25), their targets shown "
                + "with immunities crossed out; no crash, no skipped texture, animation or effect, back to the menu "
                + "after each. Written by tools/port/gen_partner_moves_all.py",
        "groups": [GROUP, "pm64"],
        "versions": ["us"],
        "new": True,
        "env": {
            "TTYD_MAP": "tik_01",
            "TTYD_STORY": 405,
            "TTYD_PARTY": party_list(host),
            "TTYD_PARTY_RANK": 2,
            "TTYD_WHEEL_TEST": f"{8 + number},90",
            "TTYD_PM64_ROM": "{pm64rom}",
            "TTYD_AUTO_BATTLE": 1,
            "TTYD_AUTO_BATTLE_ASSIST": assist[1],
            "TTYD_AUTO_BATTLE_TURNS": 200,
            "TTYD_AUTO_TEXT": 1,
            "TTYD_MOVE_SWEEP": 1,
            "TTYD_MOVE_SWEEP_ONLY": ",".join(moves),
            "TTYD_GOD": 1,
            "TTYD_FOE_HP": 999,
        },
        "settings": {"battle_immune_cross": 1, "pm64_join": 0, "pm64_partners": 1, "pm64_met_all": 1},
        "input": "tests/port/input/" + INPUT,
        "log_must": ["\\[pm64\\] TTYD_PM64_ROM: linked", "^\\[movesweep\\] rank 2: all [1-9]\\d* moves used$"]
                    + [f"^\\[movesweep\\] \\S+ '{m}' \\(row \\d+ of \\d+, rank 2\\): used$" for m in moves],
        "log_must_not": [
            "\\[tex\\] draw skipped",
            "^\\[anim\\] ",
            "\\[eff\\] effect skipped",
            "\\[movesweep\\] .*STUCK",
            "\\[story\\] STUCK",
        ],
        "timeout": 900,
    }
    if immune:
        s["env"]["TTYD_MOVE_SWEEP_IMMUNE"] = 1
    return s


def all_scenarios():
    return {f"{GROUP}_{key}_{assist[0]}.json": scenario(name, number, host, moves, assist)
            for key, name, number, host, moves in PM64 for assist in ASSISTS}


def main():
    check = "--check" in sys.argv[1:]
    files = {os.path.join(OUT, name): json.dumps(s, indent=1, ensure_ascii=False) + "\n"
             for name, s in all_scenarios().items()}
    files[os.path.join(INPUTS, INPUT)] = "\n".join(script()) + "\n"
    stale = []
    for path, text in files.items():
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != text:
            stale.append(os.path.basename(path))
            if not check:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
    gone = [os.path.join(OUT, n) for n in sorted(os.listdir(OUT))
            if n.startswith(GROUP + "_") and os.path.join(OUT, n) not in files]
    gone += [os.path.join(INPUTS, n) for n in sorted(os.listdir(INPUTS))
             if n.startswith(GROUP) and os.path.join(INPUTS, n) not in files]
    for path in gone:
        stale.append(os.path.basename(path) + " (no longer made)")
        if not check:
            os.remove(path)
    if check:
        if stale:
            print("out of date: " + ", ".join(stale))
            return 1
        print(f"{len(files) - 1} scenarios up to date")
        return 0
    print(f"{len(files) - 1} scenarios; written or removed: {len(stale)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
