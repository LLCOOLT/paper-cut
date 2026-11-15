#!/usr/bin/env python3
"""Writes the test pass's open-world way-in scenarios (tests/port/testpass/rando_ow_way_*.json, group
"open_world_ways"): every chapter's way in played on headless with an open-world file, at two of Rogueport's
stages, failing when the gate or the trigger the game waits for isn't there (#65: tik_03's passage to Boggly
Woods stayed shut, its switch being Punio's scene, which an open-world file never plays).

Each chapter not begun reads as the story its way in waits for (open_world_pc.c's kChapterStart), and each way
in is the room the game has it in: the sewer pipes of chapters 1, 2, 4 and 7, the blimp (3), Flavio's ship (5),
the train (6) and the Thousand-Year Door's Frankly in the underground town (8). The stages:

  first  Rogueport just past the prologue (story 21), no chapter done
  late   five other chapters done, Rogueport at the rest of its fifth stretch (story 273, Don Pianta ill)

A pipe is walked into or gone down, and the chapter's first room (through its title card) must be reached.
The ship and the train ask first whether the one-way trip can be finished from what Mario holds
(portEvtOpenTripReady): their scenario shows the attendant's or Flavio's question and the guard's answer, the
ride's own trigger. The blimp is ridden. Chapter 8's way in is Frankly's scene as Mario leaves the teleporter
house in tik_00, once the goal's stars are held: the story must move on to 378.

    python3 tools/port/gen_open_world_ways.py
    python3 tools/port/testpass.py run open_world_ways --run <name> --versions us
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "tests", "port", "testpass")

SEED = "OWWAYS1"
# each chapter's story when done (open_world_pc.c's kChapterDone), for the late stage's chapters done
DONE = {1: 63, 2: 119, 3: 172, 4: 219, 5: 268, 6: 346, 7: 377}
STAGES = {
    "first": {"story": 21, "done": []},
    "late": {"story": 273, "done": None},  # (five chapters other than the one played)
}

ITEM_BLIMP_TICKET, ITEM_TRAIN_TICKET, ITEM_OLD_LETTER = 0x34, 0x35, 0x45


def pipe(room, hit, card_to, first_room):
    """down a sewer pipe: its chapter's title card, then the chapter's first room"""
    return {
        "map": room,
        "input": ["wait 120", "press_until_control a 3000", "where", f"goto {hit}", "wait 30", "hold down 40",
                  "wait 240", f"press_until_map a {first_room} 6000", "press_until_control a 6000", "where", "end"],
        "log_must": [f"\\[input\\] where: map '{first_room}'"],
    }


WAYS = {
    1: dict(pipe("tik_02", "dokan", "stg_1", "hei_00"),
            what="tik_02's pipe: chapter 1's title card, then Petal Meadows (hei_00)"),
    2: {
        "map": "tik_03,s_bero_1",
        "what": "tik_03 from tik_04, along the secret passage Punio opens in the game, down the pipe at its end: "
                "chapter 2's title card, then Boggly Woods (win_06)",
        "input": "tests/port/input/rando_ow_way_ch2.txt",
        "log_must": ["\\[open\\] chapter 2's way in: flag 1334 set \\(tik_03\\)",
                     "\\[input\\] where flag GSWF_TIK03_SECRET_DOOR_OPEN \\(1334\\) = 1",
                     "\\[input\\] where: map 'win_06'"],
    },
    3: {
        "map": "gor_04",
        "what": "the station's blimp attendant, the Blimp Ticket held: aboard, the blimp to Glitzville (tou_20, whose "
                "arrival scene goes on to the square, tou_01: chapter 3 begun)",
        "items": [ITEM_BLIMP_TICKET],
        "kick": "talk:rel_gor__hikosenkakari_talk",
        "input": ["wait 600", "press_until_control a 6000", "where", "goto A_noriba", "wait 30", "press a",
                  "press_until_map a tou_20 6000", "press_until_control a 9000", "where", "end"],
        "log_must": ["\\[open\\] chapter 3: story 125 -> 127"],
    },
    4: dict(pipe("tik_08", "dokan", "stg_4", "usu_00"),
            what="tik_08's pipe: chapter 4's title card, then Twilight Town (usu_00)"),
    5: {
        "map": "gor_00,n_bero",
        "what": "the harbor with the Old Letter: Flavio waits to sail (story 228) and asks; the one-way trip's guard "
                "answers (Keelhaul Key's way in; what the trip needs is the logic's, not held here)",
        "items": [ITEM_OLD_LETTER],
        "kick": "talk:*rel_gor__port_static__rels_gor_src_gor_00__marco_talk",
        "input": ["wait 900", "press_until_control a 6000", "where", "end"],
        "log_must": ["\\[input\\] where: map 'gor_00', Mario at -?\\d+ -?\\d+ -?\\d+, story 228,",
                     "\\[open\\] trip 0 \\(the ship\\): "],
    },
    6: {
        "map": "gor_04",
        "what": "the station's train attendant, the Train Ticket held: he offers the Excess Express (story 280) and "
                "the first ride's guard answers (chapter 6's way in; the ride's needs are the logic's)",
        "items": [ITEM_TRAIN_TICKET],
        "kick": "talk:rel_gor__ekiin_talk",
        "input": ["wait 600", "press_until_control a 6000", "where", "end"],
        "log_must": ["\\[open\\] trip 1 \\(the train\\): "],
    },
    7: dict(pipe("tik_07", "dokan__1", "stg_7", "bom_00"),
            what="tik_07's pipe past the Ultra Hammer's stone: chapter 7's title card, then Fahr Outpost (bom_00)"),
    8: {
        "map": "tik_00",
        "what": "the goal's Crystal Stars held: the teleporter house in the underground town opens from outside, "
                "and leaving it plays Frankly's scene (story 377 to 378), the Thousand-Year Door's way",
        "goal": True,
        "input": ["wait 120", "press_until_control a 3000", "where", "walk_to a_door_4 stop 900", "press a",
                  "wait 120", "press_until_control a 3000", "where", "walk_to a_door_4_N stop 900", "press a",
                  "press_until_story a 378 9000", "press_until_control a 6000", "where", "end"],
        "log_must": ["\\[open\\] chapter 8: story 377 -> 378"],
    },
}


def scenario(chapter, stage):
    w = WAYS[chapter]
    st = STAGES[stage]
    done = st["done"]
    if done is None:
        done = [c for c in range(1, 8) if c != chapter][:5]
    env = {
        "TTYD_MAP": w["map"],
        "TTYD_STORY": str(st["story"]),
        "TTYD_PARTY": "1",
        "TTYD_OPEN_SEED": SEED,
        "TTYD_AUTO_TEXT": "1",
        "TTYD_GOD": "1",
        "TTYD_EXIT_FRAMES": 20000,
    }
    if done:
        env["TTYD_OPEN_TRACKS"] = ",".join(f"{c}:{DONE[c]}" for c in done)
    if w.get("items"):
        env["TTYD_OPEN_ITEMS"] = ",".join(hex(i) for i in w["items"])
    if w.get("goal"):
        stars = done if done else [1]
        env["TTYD_OPEN_GOAL"] = str(len(stars))
        env["TTYD_OPEN_STARS"] = ",".join(str(c) for c in stars)
    if w.get("kick"):
        env["TTYD_STORY_KICK"] = w["kick"]
        env["TTYD_STORY_KICK_AFTER"] = "60"
    stage_text = ("Rogueport just past the prologue, no chapter done" if stage == "first" else
                  f"chapters {', '.join(map(str, done))} done, Rogueport at story {st['story']}")
    return {
        "desc": f"Open world, chapter {chapter}'s way in ({stage_text}): {w['what']} (gen_open_world_ways.py)",
        "groups": ["rando", "open_world_ways"],
        "versions": ["us"],
        "new": True,
        "backend": "headless",
        "env": env,
        "input": w["input"],
        "log_must": ["\\[open\\] file stamped: seed '" + SEED + "'", "open world: seed " + SEED + ": \\d+ places"]
        + w["log_must"],
        "log_must_not": ["walk_to \\S+: not reached", "\\*\\*\\* crash", "\\[input\\] FAIL"],
        "timeout": 420,
    }


def main():
    n = 0
    for chapter in WAYS:
        for stage in STAGES:
            path = os.path.join(OUT, f"rando_ow_way_ch{chapter}_{stage}.json")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(scenario(chapter, stage), f, indent=1, ensure_ascii=False)
                f.write("\n")
            n += 1
    print(f"{n} scenarios written to {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
