#!/usr/bin/env python3
"""Writes the pm64_choreo_<partner>_<move> test pass scenarios (tests/port/testpass/): a Paper Mario 64
partner in a battle in the Rogueport Sewers (tik_15), after Mario's jump, using one of its four
moves with captures every few frames through it (the stick held left, then A pressed now and then, so every
kind of action command runs), the rest of it and the enemies' turn (now and then at the partner: hurt), and a
last look a while later. Needs $PAPERCUT_PM64_ROM. Re-run after changing the partners' list here."""
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "tests", "port", "testpass")

# name, TTYD_PARTY (the host first), the wheel test's id, its four moves (the battle menu's order)
PARTNERS = [
    ("goombario", "1,2,3,4,5,6,7", 9),
    ("kooper", "2,1,3,4,5,6,7", 10),
    ("bombette", "3,1,2,4,5,6,7", 11),
    ("parakarry", "4,1,2,3,5,6,7", 12),
    ("watt", "7,1,2,3,4,5,6", 13),
    ("sushie", "6,1,2,3,4,5,7", 14),
    ("lakilester", "4,1,2,3,5,6,7", 15),
    ("bow", "6,1,2,3,4,5,7", 16),
]


# partners whose command is PM64's stick-flick mash (Bow's smack: let go of the stick held left, again and again)
FLICK = {"bow"}


def script(move, flick=False):
    s = ["wait 160", "battle", "wait 300", "shot idle",
         # Mario: Jump; then the partner's turn, its move list
         "press a", "wait 30", "press a", "wait 50", "press a", "wait 200",
         "shot front", "press a", "wait 40", "shot moves"]
    s += ["press down", "wait 10"] * move
    s += ["press a", "wait 40", "shot target", "press a"]
    k = 0
    if flick:  # flicks of the stick left (held 4 frames, let go 4) through the whole command
        for _ in range(28):
            s += ["stick -127 0 4", "wait 4", "shot m_%02d" % k]
            k += 1
    else:
        for _ in range(12):  # a held stick (Shell Toss, Spiny Flip...)
            s += ["stick -127 0 8", "shot m_%02d" % k]
            k += 1
    for _ in range(14 if flick else 30):  # then A now and then (timed and mashed commands)
        s += ["press a", "wait 6", "shot m_%02d" % k]
        k += 1
    for i in range(24):  # the rest of it, then the enemies' turn (at the partner, now and then: hurt)
        s += ["wait 12", "shot e_%02d" % i]
    s += ["wait 300", "shot later", "end"]
    return s


def front_script():
    """The partner in front (Y at Mario's menu swaps them), its first move, Mario's jump, then the enemies'
    turn at the one in front: hurt, captured often."""
    s = ["wait 160", "battle", "wait 300", "shot idle", "press y", "wait 60", "shot front",
         "press a", "wait 40", "press a", "wait 20"]
    for k in range(20):
        s += ["press a", "wait 8", "shot m_%02d" % k]
    s += ["wait 120", "press a", "wait 30", "press a", "wait 50", "press a", "wait 160"]
    for i in range(50):
        s += ["wait 8", "shot e_%02d" % i]
    s += ["wait 200", "shot later", "end"]
    return s


def win_script():
    """Turn after turn of Mario's jump and the partner's first move (A through every menu and command), the
    attack badges on, until the battle is won: the victory pose captured."""
    s = ["wait 160", "battle", "wait 300", "shot idle"]
    for i in range(150):
        s += ["press a", "wait 14"]
        if i % 3 == 2:
            s += ["shot v_%03d" % (i // 3)]
    s += ["wait 60", "shot later", "end"]
    return s


def scenario(desc, party, wheel, script, frames, badges):
    return {
        "desc": desc,
        "groups": ["pm64_choreo"],
        "versions": ["us"],
        "env": {
            "TTYD_MAP": "tik_15",
            "TTYD_STORY": 405,
            "TTYD_PARTY": party,
            "TTYD_PARTY_RANK": 2,
            "TTYD_PM64_ROM": "{pm64rom}",
            "TTYD_PM64_LOG": 1,
            "TTYD_WHEEL_TEST": "%d,60" % wheel,
            "TTYD_BADGES": badges,
            "TTYD_EXIT_FRAMES": frames,
        },
        "settings": {"pm64_partners": 1, "pm64_met_all": 1},
        "input": script,
        "expect_shots": ["idle"],
        "log_must": ["\\[pm64\\] TTYD_PM64_ROM: linked US"],
        "timeout": 300,
    }


def write(name, sc):
    with open(os.path.join(OUT, name + ".json"), "w", newline="\n") as f:
        json.dump(sc, f, indent=1, ensure_ascii=False)
        f.write("\n")


def main():
    for name, party, wheel in PARTNERS:
        cap = name.capitalize()
        write("pm64_front_%s" % name, scenario(
            "Paper Mario 64 partners: %s swapped to the front (Y) in a battle in the Rogueport Sewers (tik_15), its "
            "first move, then the enemies' turn at it (hurt, captured often); written by "
            "tools/port/gen_pm64_choreo.py" % cap, party, wheel, front_script(), 2600, "0x118,0x118,0x118"))
        write("pm64_win_%s" % name, scenario(
            "Paper Mario 64 partners: %s and Mario win a battle in the Rogueport Sewers (tik_15), A through every "
            "turn with the attack badges on, the victory pose captured; written by "
            "tools/port/gen_pm64_choreo.py" % cap, party, wheel, win_script(), 3600,
            "0x118,0x118,0x118,0x104,0x104,0x104,0x105,0x105,0x105"))
    for name, party, wheel in PARTNERS:
        for move in range(4):
            sc = {
                "desc": "Paper Mario 64 partners: %s in front in a battle in the Rogueport Sewers (tik_15), its "
                        "move %d captured every few frames (PM64's choreography), then the enemies' turn at it "
                        "(hurt); written by tools/port/gen_pm64_choreo.py" % (name.capitalize(), move + 1),
                "groups": ["pm64_choreo"],
                "versions": ["us"],
                "env": {
                    "TTYD_MAP": "tik_15",
                    "TTYD_STORY": 405,
                    "TTYD_PARTY": party,
                    "TTYD_PARTY_RANK": 2,
                    "TTYD_PM64_ROM": "{pm64rom}",
                    "TTYD_PM64_LOG": 1,
                    "TTYD_WHEEL_TEST": "%d,60" % wheel,
                    "TTYD_BADGES": "0x118,0x118,0x118",
                    "TTYD_EXIT_FRAMES": 2600,
                },
                "settings": {"pm64_partners": 1, "pm64_met_all": 1},
                "input": script(move, name in FLICK),
                "expect_shots": ["idle", "moves"],
                "log_must": ["\\[pm64\\] TTYD_PM64_ROM: linked US"],
                "timeout": 240,
            }
            with open(os.path.join(OUT, "pm64_choreo_%s_%d.json" % (name, move + 1)), "w", newline="\n") as f:
                json.dump(sc, f, indent=1, ensure_ascii=False)
                f.write("\n")


if __name__ == "__main__":
    main()
