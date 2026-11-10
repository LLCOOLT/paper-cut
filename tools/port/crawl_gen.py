#!/usr/bin/env python3
"""The dead-end crawl (testpass group "dead"): every page and pop-up has a way back with every input a
player has. Each scenario enters a run of screens and leaves each one with a single way out, pressed until
the screen is gone, through the device flows (port/src/common/flows_pc.c: real devices' events, not the
input script's controller 1):

  b      the button labelled B on an Xbox controller (a virtual SDL controller: "dev pad 1 cancel", its right
         face button; never the game's B, which the bindings put on its X: the one back rule,
         port/include/port/menu_buttons.h)
  switch the button labelled B on a Switch Pro controller (its bottom face button)
  esc    the keyboard's Esc ("dev key esc")
  mouse  a right click ("back": the mouse's back in the game's menus)
  touch  the touch controls' B ("dev touch b", the touch build)

A screen counts as left when the flows' check holds after at most a few presses ("menu closed": the Start
menu and its port tabs, categories and pages; "page closed": the settings page; "editor closed": the touch
layout editor). A screen that stays is "[flows] FAIL" (testpass fails the run). The title's pages are checked
by their own log lines (title_menu_aurora.c, pause_online_aurora.c).

Kept fast: one game per way and family (about a dozen runs), the screens chained in it.

  crawl_gen.py            writes tests/port/testpass/dead_*.json (then: testpass.py run dead --run NAME)
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "tests", "port", "testpass")

FIELD = {"TTYD_MAP": "gor_01", "TTYD_STORY": "21", "TTYD_PARTY": "1", "TTYD_AUTO_TEXT": "1"}
SETTINGS = {"background_input": 1}
PRESSES = 6  # the most presses a screen may take to leave (a page four levels deep, a list in it)

# the Start menu's screens: (name, steps after the menu opens, from its first tab; "right", "down" and "a" as
# the way's own device presses them)
SETTINGS_TAB = 7
PAUSE = [
    ("the Start menu", []),
    # (the game's own tabs: Mario, Party, Items (its lists), Badges)
    ("the Mario tab", ["a"]),
    ("the Party tab", ["right", "a"]),
    ("the Items tab", ["right"] * 2 + ["a"]),
    ("the Items tab's list", ["right"] * 2 + ["a", "a"]),
    ("the Badges tab", ["right"] * 3 + ["a"]),
    ("the Journal tab", ["right"] * 4 + ["a"]),
    ("the Wardrobe tab", ["right"] * 5 + ["a"]),
    ("the Multiplayer tab", ["right"] * 6 + ["a"]),
    # (its rows: Brothers mode, Online..., Party..., Who's who..., Multiplayer settings...)
    ("Multiplayer > Multiplayer settings", ["right"] * 6 + ["a"] + ["down"] * 4 + ["a"]),
    ("the Settings tab's categories", ["right"] * SETTINGS_TAB + ["a"]),
    ("Settings > Gameplay", ["right"] * SETTINGS_TAB + ["a", "a"]),
    ("Settings > Enhancements > Field", ["right"] * SETTINGS_TAB + ["a", "down", "down", "a", "a"]),
    ("Settings > Controls", ["right"] * SETTINGS_TAB + ["a"] + ["down"] * 6 + ["a"]),
]


def press(way, key):
    """one press of key (a b start right down ...) on the way's device; the mouse's way moves with keys"""
    if way in PADS:
        return f"dev pad 1 {'confirm' if key == 'a' else key} 4"
    if way == "touch":
        return f"dev touch {key} 4" if key in ("a", "b", "start") else f"dev touch stick-{key} 4"
    return f"dev key {key} 4"


def out(way):
    """the way out, once (east: the controller's own back on Paper Cut's ImGui pages, an Xbox pad's B; b: the
    game's B as the port maps it, for the game-drawn pages)"""
    return {"b": "dev pad 1 cancel 4", "switch": "dev pad 1 cancel 4", "east": "dev pad 1 east 4",
            "esc": "dev key esc 4", "touch": "dev touch b 4", "mouse": "dev back"}[way]


# the ways that are a virtual controller: its kind
PADS = {"b": "xbox", "switch": "switch"}
NAMES = {"b": "an Xbox controller's B", "switch": "a Switch controller's B", "esc": "Esc", "mouse": "a right click",
         "touch": "the touch controls' B"}


def leave(way, check):
    """the way out until check holds (at most PRESSES), then checked"""
    steps = [out(way), "dev wait 30"]
    for _ in range(PRESSES - 1):
        steps += [f"dev ifnot {check} then {out(way)[4:]}", "dev wait 30"]
    return steps + [f"dev expect {check}"]


def base(desc, way, steps, log_must, build=None, env=None, timeout=300):
    s = {"desc": desc, "groups": ["dead"], "versions": ["us"], "backend": "offscreen",
         "env": dict(FIELD, **(env or {})), "settings": dict(SETTINGS),
         "input": steps + ["end"], "timeout": timeout, "log_must": log_must, "log_must_not": ["STUCK"]}
    if build:
        s["build"] = build
    return s


def pause_scenario(way):
    """the Start menu's screens, each entered and left with this way"""
    steps = ["dev watch 240", "dev wait 120"]
    if way in PADS:
        steps.insert(0, f"dev pad 1 attach {PADS[way]}")
    for name, nav in PAUSE:
        steps += [f"dev state {name}", press(way, "start"), "dev until menu 120", "dev wait 30"]
        for k in nav:
            steps += [press(way if way != "mouse" else "esc", k), "dev wait 20"]
        steps += ["dev wait 30"] + leave(way, "menu closed") + ["dev wait 40"]
    return base(f"Dead ends: the Start menu, its game and port tabs, categories and pages, each left with {NAMES[way]} "
                f"alone (pressed until the menu is closed; tools/port/crawl_gen.py)", way, steps,
                [r"^\[flows\] ok: menu closed"] * 1, build="touch" if way == "touch" else None)


def start_scenario(way):
    """the Start menu's screens, each closed by one Start (a controller's, or the keyboard's Start key), as the
    game's own tabs always were: Paper Cut's tabs and pages too"""
    pad = way == "pad"
    steps = ["dev watch 240", "dev wait 120"]
    if pad:
        steps.insert(0, "dev pad 1 attach xbox")
    for name, nav in PAUSE:
        start = "dev pad 1 start 4" if pad else "dev key start 4"
        steps += [f"dev state {name}", start, "dev until menu 120", "dev wait 30"]
        for k in nav:
            steps += [press("b" if pad else "esc", k), "dev wait 20"]
        steps += ["dev wait 30", start, "dev wait 40", "dev expect menu closed", "dev ifnot menu closed then key esc 4",
                  "dev wait 30", "dev ifnot menu closed then key esc 4", "dev wait 30",
                  "dev ifnot menu closed then key esc 4", "dev wait 30", "dev ifnot menu closed then key esc 4",
                  "dev wait 40"]
    return base(f"Start closes the Start menu at once from each of its game and port tabs, categories and pages, "
                f"{'a controller' if pad else 'the keyboard'}'s Start (tools/port/crawl_gen.py)", way, steps,
                [r"^\[flows\] ok: menu closed"])


def page_scenario(way):
    """the settings page (F1): opened and left"""
    steps = ["dev wait 120"]
    if way == "b":
        steps.insert(0, "dev pad 1 attach")
    steps += ["dev key f1 4", "dev wait 40", "dev expect page open"] + leave("east" if way == "b" else way,
                                                                               "page closed")
    return base(f"Dead ends: the settings page (F1) left with {'a controller' if way == 'b' else 'Esc'}'s back",
                way, steps, [r"^\[flows\] ok: page open", r"^\[flows\] ok: page closed"])


def editor_scenario(way):
    """the touch layout editor, no touch: opened ($TTYD_TOUCH_EDIT) and left with a key or a controller's button
    (b and esc: Cancel; start: Done). The editor holds the game, so the device flows' frames stop: the open and
    the press go by drawn frame ($TTYD_TOUCH_EDIT_DO), the editor's own log says it closed"""
    press_ = {"b": "pad=b", "esc": "key=esc", "start": "pad=start"}[way]
    how = "saved" if way == "start" else "cancelled"
    word = {"b": "a controller's B (Cancel)", "esc": "Esc (Cancel)", "start": "a controller's Start (Done)"}[way]
    s = base(f"Dead ends: the touch layout editor left with {word}, no touch", way, ["wait 900"],
             [r"\[touch\] layout editor open", rf"\[touch\] test: {press_}", rf"\[touch\] layout editor closed \({how}\)"],
             build="touch", env={"TTYD_TOUCH_DEVICE": "1920x1080@370", "TTYD_TOUCH_EDIT": "400",
                                 "TTYD_TOUCH_EDIT_DO": f"480:{press_}"})
    return s


def title_scenario(way):
    """the title's Multiplayer pages (TTYD_OPEN_MULTIPLAYER), Online... open, left to the title's buttons"""
    # (Online... is the second row: under Brothers mode)
    steps = ["wait_seq 1 2000", "wait 420", "press down", "wait 20", "press a", "wait 60"]
    if way == "mouse":
        steps += ["back", "wait 30"] * 6
    else:
        steps += ["dev touch b 4", "dev wait 30"] * 6
    s = base(f"Dead ends: the title's Multiplayer pages left with {'a right click' if way == 'mouse' else 'the touch controls B'}"
             " (B and Esc: mp_pages_back_b, mp_pages_back_esc)", way, steps,
             [r"^\[title\] the Multiplayer page at once", r"^\[online\] page: front",
              r"^\[title\] the pages closed: back to the title's buttons"],
             build="touch" if way == "touch" else None)
    s["env"] = {"TTYD_OPEN_MULTIPLAYER": 1, "TTYD_TITLE_MENU": 1}
    if way == "touch":
        s["env"]["TTYD_TOUCH_DEVICE"] = "1920x1080@370"
    return s


def choice_scenario(way):
    """a talk's choice (the Rogueport inn's: stay the night?), read up to and left with this way: its cancel"""
    steps = ["dev wait 60"]
    if way in PADS:
        steps.insert(0, f"dev pad 1 attach {PADS[way]}")
    nxt = press(way, "a") if way != "mouse" else "dev key a 4"
    steps += ["dev state the inn's choice", f"dev mash {nxt[4:-2]} choice 900", "dev wait 20",
              "dev expect choice open"] + leave(way, "choice closed")
    s = base(f"Dead ends: a talk's choice (the Rogueport inn's) left with {NAMES[way]}: its cancel", way, steps,
             [r"^\[flows\] ok: choice open", r"^\[flows\] ok: choice closed"],
             env={"TTYD_STORY": "380", "TTYD_STORY_KICK": "talk:kinopio_house_talk_evt",
                  "TTYD_STORY_KICK_AFTER": "60"})
    del s["env"]["TTYD_AUTO_TEXT"]
    return s


def scenarios():
    yield from ((f"dead_pause_{w}", pause_scenario(w)) for w in ("b", "switch", "esc", "mouse", "touch"))
    yield from ((f"dead_choice_{w}", choice_scenario(w)) for w in ("b", "esc", "mouse"))
    yield from ((f"dead_start_{w}", start_scenario(w)) for w in ("pad", "key"))
    yield from ((f"dead_page_{w}", page_scenario(w)) for w in ("b", "esc"))
    yield from ((f"dead_editor_{w}", editor_scenario(w)) for w in ("b", "esc", "start"))
    yield from ((f"dead_title_{w}", title_scenario(w)) for w in ("mouse", "touch"))


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        # (its own families only: dead_rowf.json and the like are written by hand)
        if f.startswith(("dead_pause_", "dead_page_", "dead_editor_", "dead_title_", "dead_choice_", "dead_start_")):
            os.remove(os.path.join(OUT, f))
    n = 0
    for name, s in scenarios():
        with open(os.path.join(OUT, name + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(s, f, indent=1)
            f.write("\n")
        n += 1
    print(f"{n} scenarios in {OUT}")


if __name__ == "__main__":
    main()
