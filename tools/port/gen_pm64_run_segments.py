#!/usr/bin/env python3
"""Writes tests/port/full_run/pm64/9Np_*.txt, the PM64 chain's boss segments after Jr. Troopa (full_run.py --pm64):
each from a start generated at the story value where Mario is back in Rogueport with the boss's TTYD counterpart
joined (full_run.py --pm64 --gen gaps), the earlier bosses beaten and their partners told (GSW 2024..2025, 2011,
2019: port/src/common/pm64_partner_pc.c). Each walks into its boss (port/src/aurora/pm64_story_aurora.c's spot),
plays the fight, checks the partner's join (log, notice, out in the host's place, the Party tab), then takes them
down Frankly's pipe into the sewers (tik_01) for a battle where they must hit something.
Then run tools/port/gen_fullrun_scenarios.py for the test pass's pm64run_* scenarios.

usage: gen_pm64_run_segments.py
"""
import os

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tests", "port",
                   "full_run", "pm64")

# routes from a Rogueport room to gor_02's west door, then Frankly's yard pipe into the sewers (tik_01)
PLAZA_EAST = ["walk_to 0 50 1200                # the middle of the plaza (02_frankly's way east)",
              "walk_to e_bero 1800", "wait_map gor_02 900"]
TO_GOR02 = {
    "gor_01": PLAZA_EAST,
    "gor_02": [],
    "gor_03": ["walk_to e_bero 1500", "wait_map gor_01 900", "press_until_control a 3000"] + PLAZA_EAST,
    "gor_00": ["walk_to 330 -380 600              # up the harbour stairs (01_goombella's way north)",
               "walk_to 230 -480 300", "walk_to 120 -560 300", "walk_to n_bero 1500", "wait_map gor_01 900",
               "press_until_control a 3000"] + PLAZA_EAST,
}
# the side Mario comes up to a boss from (east of it unless said): the Crystal King's east is Star Haven's board
WEST_OF = {"crystal_king"}
SEWERS = [
    "press_until_control a 3000",
    "walk_to -250 200 1200           # to the gap in Frankly's fence (x -300..-200)",
    "walk_to -250 345 600            # through it",
    "walk_to -232 340 300",
    "hold a+right 6                  # jump east onto the pipe (-190 329; 30 high)",
    "stick 100 26 14",
    "wait 20",
    "walk_to -190 329 120            # the pipe's middle",
    "hold down 30                    # down it",
    "press_until_map a tik_01 900",
    "press_until_control a 3000",
    "where npcs",
]

# n, file, fight name (log), host, PM64 partner (name, id), map, x, z of the boss, story, party, level, hp, fp,
# items, gsw (earlier bosses beaten, their partners told)
BOSSES = [
    (90, "koopa_bros", "Koopa Bros\\.", 2, "Kooper", 10, "gor_03", -210, 330, 65, "2,1", 4, 15, 10, "", "2024:1,2011:2",
     "chapter 1 back in Rogueport (Frankly's house, story 65), Koops joined"),
    (91, "huff_n_puff", "Huff N\\. Puff", 5, "Lakilester", 15, "gor_02", -500, 190, 121, "5,1,2", 7, 20, 15, "",
     "2024:3,2011:6", "chapter 2 back in Rogueport (story 121), Flurrie joined"),
    (92, "tutankoopa", "Tutankoopa", 4, "Parakarry", 12, "gor_03", -360, 320, 175, "4,1,2,5", 10, 25, 20,
     "ITEM_SUPER_BOOTS,ITEM_SUPER_HAMMER", "2024:67,2011:134", "chapter 3 back in Rogueport (story 175), Yoshi joined"),
    (93, "tubba_blubba", "Tubba Blubba", 6, "Bow", 16, "gor_02", -250, 130, 222, "6,1,2,4,5", 13, 30, 20,
     "ITEM_SUPER_BOOTS,ITEM_SUPER_HAMMER", "2024:71,2011:150", "chapter 4 back in Rogueport (story 222), Vivian joined"),
    (94, "lava_piranha", "Lava Piranha", 6, "Sushie", 14, "gor_00", 440, -290, 222, "6,1,2,4,5", 13, 30, 20,
     "ITEM_SUPER_BOOTS,ITEM_SUPER_HAMMER", "2024:71,2011:150", "chapter 4 back in Rogueport (story 222), Vivian joined"),
    (95, "general_guy", "General Guy", 7, "Watt", 13, "gor_03", -300, 430, 222, "6,1,2,4,5", 13, 30, 20,
     "ITEM_SUPER_BOOTS,ITEM_SUPER_HAMMER", "2024:111,2011:214,2019:1",
     "after chapter 4 (story 222, Frankly's house): Ms. Mowz not recruited, Watt in her slot"),
    (96, "koopa_bros_rematch", "Koopa Bros\\. \\(rematch\\)", 3, "Bombette", 11, "gor_01", -200, 470, 272, "3,1,2,4,5,6", 15, 35, 25,
     "ITEM_SUPER_BOOTS,ITEM_SUPER_HAMMER", "2024:127,2011:246,2019:1",
     "chapter 5 back in Rogueport (story 272), Bobbery joined"),
    (97, "crystal_king", "Crystal King", 0, "", 0, "gor_01", 180, 420, 378, "1,2,3,4,5,6", 20, 40, 30,
     "ITEM_ULTRA_BOOTS,ITEM_ULTRA_HAMMER", "2024:127,2025:4,2011:254,2019:1",
     "after chapter 7 (story 378): the hideout gone"),
    (98, "kammy_koopa", "Kammy Koopa", 0, "", 0, "gor_01", 425, 355, 382, "1,2,3,4,5,6", 21, 40, 35,
     "ITEM_ULTRA_BOOTS,ITEM_ULTRA_HAMMER", "2024:255,2025:4,2011:254,2019:1",
     "chapter 8 (story 382): the Palace of Shadow reached"),
    (99, "bowser", "Bowser", 0, "", 0, "gor_00", 400, -350, 405, "1,2,3,4,5,6", 25, 45, 40,
     "ITEM_ULTRA_BOOTS,ITEM_ULTRA_HAMMER", "2024:255,2025:5,2011:254,2019:1", "after the credits (story 405)"),
]

for (n, name, fight, host, who, wid, mp, x, z, story, party, level, hp, fp, items, gsw, when) in BOSSES:
    lines = [f"#start map={mp} story={story} party={party} level={level} hp={hp} fp={fp} coins=50"
             + (f" items={items}" if items else "") + f" flags=@rogueport_open gsw={gsw}",
             "#end story={} map={}".format(story, "tik_01" if who else mp),
             "#gap the TTYD chain stops at segment 05 (chapter 1's start); generated start",
             "#env TTYD_GOD_POWER=4",
             f"# PM64 chain: {when}. The PM64 boss waiting in {mp} ({x} {z}, port/src/aurora/pm64_story_aurora.c):",
             "# Mario walks into it, its fight (an ordinary battle, scaled to his level; the auto battler, god mode)"
             + (f", then {who}" if who else ", its star") + ".",
             ]
    if who:
        lines += [f"# joins: the notice, out in the partner's place (the second screen's partner tap {wid}, as the",
                  "# Partner wheel), the Party tab; then down into the sewers (tik_01) for a battle with them."]
    lines += [f"#must \\[pm64\\] story boss: {fight} waits in {mp}",
              f"#must \\[pm64\\] story boss: {fight} met \\([a-z]+\\) in {mp}",
              f"#must \\[pm64\\] story boss: {fight} won"]
    if who:
        lines += [f"#must \\[pm64\\] {who} joins with partner {host}, its boss beaten",
                  f"#must \\[pm64\\] notice: {who} joined too",
                  f"#must \\[pm64\\] {who} in {host}'s place",
                  f"#must \\[pm64\\] Party tab: {who}'s description: ",
                  f"#must \\[pm64\\] {who}'s move \\d+ hit"]
    lines += ["#must_not Game Over", "",
              "wait_seq 2 3000", "press_until_control a 3000", "where npcs",
              f"walk_to {x - 110 if x >= 300 or name in WEST_OF else x + 110} {z} 1500   # beside it",
              f"shot {name}_waits",
              f"walk_to {x} {z} 300            # into it: the battle (the walk ends once it is over; the boss",
              "                                # engine's key-moment captures, pm64_*, show it)",
              "wait_seq 2 40000", "press_until_control a 6000", "wait 60"]
    if who:
        lines += [f"shot {who.lower()}_joins            # the join notice", "wait 400",
                  f"companion partner {wid}", "wait 120", f"shot {who.lower()}_out",
                  "press start", "wait 45", "press right", "wait 30", "press a", "wait 40",
                  f"shot {who.lower()}_party_tab", "press b", "wait 30", "press b", "wait 30", "press b", "wait 60",
                  "press_until_control b 600"]
        lines += TO_GOR02[mp] + SEWERS
        lines += ["battle 25                       # tik_20's three Spinias, on the nearest field enemy",
                  "wait_seq 4 3000", "wait 200", f"shot {who.lower()}_first_battle", "wait 300",
                  f"shot {who.lower()}_first_battle2", "wait_seq 2 30000", "press_until_control a 6000"]
    else:
        lines += [f"shot {name}_after"]
    lines += ["where", "save", "end", ""]
    with open(os.path.join(OUT, f"{n}p_{name}.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
print("ok")
