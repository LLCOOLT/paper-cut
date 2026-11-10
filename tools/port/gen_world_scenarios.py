#!/usr/bin/env python3
"""Writes the test pass's remake map scenarios (tests/port/testpass/maps_*.json):
each placed thing of port/src/common/remake_maps_pc.c (pipes, blocks, jump
stands, merchants, ambushes) captured from beside it with its switch on, and a
few rooms again with every switch off (nothing placed).

    python3 tools/port/gen_world_scenarios.py
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "tests", "port", "testpass")

MAP_KEYS = ["remake_warp_pipes", "remake_shortcut_pipes", "remake_express_skip", "remake_chapter_shortcuts",
            "remake_keelhaul", "remake_steeple_stairs", "remake_twilight", "remake_blocks", "remake_ambushes",
            "remake_merchants"]

# (name, map, x, floor y, z, what, extra env) -- the places as in remake_maps_pc.c's kThings
THINGS = [
    ("gon_05_pipe", "gon_05", 215, 915, 150, "pipe to gon_01", {"TTYD_GSWF": "1503"}),
    ("jin_08_pipe", "jin_08", -60, 0, 115, "pipe to usu_00", {}),
    ("las_27_pipe", "las_27", 500, 0, 100, "pipe to las_00", {}),
    ("usu_01_pipe", "usu_01", 330, 0, 130, "pipe to gra_06", {}),
    ("gra_06_pipe", "gra_06", -220, 0, 160, "pipe to usu_01", {}),
    ("las_10_pipe", "las_10", -150, 0, 250, "pipe to las_00", {}),
    ("muj_01_jump", "muj_01", 320, 0, -20, "jump_stand", {"TTYD_GSW": "2030:1"}),
    ("muj_05_jump", "muj_05", 20, 0, -110, "jump_stand", {}),
    ("win_06_heart", "win_06", 205, 0, -50, "heart_block", {}),
    ("gra_03_blocks", "gra_03", 5, 0, -100, "save_block", {}),
    ("jin_11_heart", "jin_11", -5, 0, -40, "heart_block", {"_input": ["wait 120"]}),
    ("dou_00_heart", "dou_00", -45, 0, -75, "heart_block", {"_input": ["wait 120"]}),
    ("rsh_07_a_heart", "rsh_07_a", 140, 0, -145, "heart_block", {"TTYD_STORY": "290"}),
    ("eki_01_blocks", "eki_01", -115, 0, 50, "save_block", {}),
    ("aji_18_blocks", "aji_18", 0, 0, 40, "save_block", {}),
    ("las_04_heart", "las_04", -950, 80, 100, "heart_block", {}),
    ("las_19_blocks", "las_19", 165, -540, 20, "save_block",
     {"_input": ["wait 5", "goto 75 -530 40", "wait 20"]}),  # (before the hall's enemy reaches Mario)
    ("mri_00_charlieton", "mri_00", 330, 40, 330, r"\[remake\] mri_00", {}),
    ("muj_10_toad", "muj_10", -430, 25, -20, r"\[remake\] muj_10",
     {"TTYD_MAP": "muj_10,w_bero", "_input": ["wait 120"]}),
    ("hei_06_ambush", "hei_06", -250, 0, 0, None, {}),
    ("dou_02_ambush", "dou_02", -225, 25, 25, None, {"TTYD_MAP": "dou_02,w_bero_1", "TTYD_MARIO_SHIP": "1"}),
    ("aji_18_ambush", "aji_18", 0, 0, -20, None, {}),
]
OFF = {"gra_03_blocks", "muj_05_jump", "jin_08_pipe", "mri_00_charlieton"}


def scenario(name, m, x, y, z, what, extra, on):
    env = {"TTYD_MAP": m, "TTYD_STORY": "405", "TTYD_EXIT_FRAMES": 3000}
    env.update(extra)
    # Mario a step to the thing's left (the camera looks along -z): the thing beside him; or walked in
    # from a door ("_input"), where a goto would leave the camera behind
    walk = env.pop("_input", None)
    inp = (walk or ["wait 5", "goto %d %d %d" % (x - 90, y + 10, z + 20), "wait 120"]) + ["where", "shot near", "end"]
    s = {
        "desc": "%s with %s: %s placed, captured from beside it" % (m, "every remake map switch on" if on else "every switch off", what or "ambush"),
        "groups": ["world", "maps_remake"],
        "versions": ["us", "jp", "eu"] if on else ["us"],
        "env": env,
        # (the ambushes only in their own scenarios: an ambush by a block starts a fight before the capture)
        "settings": {k: (1 if on and (k != "remake_ambushes" or what is None) else 0) for k in MAP_KEYS},
        "input": inp,
        "expect_shots": ["near"],
        "timeout": 200,
    }
    if what:
        pat = what if what.startswith("\\") else r"\[map edits\] %s: %s" % (m, what.split()[0])
        if on:
            s["log_must"] = [pat]
        else:
            s["log_must_not"] = [pat]
    return s


def main():
    n = 0
    for t in THINGS:
        for on in (True, False):
            if not on and t[0] not in OFF:
                continue
            name = "maps_%s%s" % (t[0], "" if on else "_off")
            with open(os.path.join(OUT, name + ".json"), "w", newline="\n") as f:
                json.dump(scenario(*t, on), f, indent=1, ensure_ascii=False)
                f.write("\n")
            n += 1
    print("%d scenarios" % n)


if __name__ == "__main__":
    main()
