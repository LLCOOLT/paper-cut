#!/usr/bin/env python3
"""Writes the full run's segments as test pass scenarios: tests/port/testpass/fullrun_<segment>.json (group
"fullrun", the TTYD set, headless) and pm64run_<segment>.json (group "pm64run", full_run.py --pm64's set: Paper
Mario 64's content on with the tester's ROM, Aurora off screen). Each plays its segment's script from its
checkpoint (tools/port/full_run_card.py makes the card at test time) with the hooks full_run.py gives it, and
checks what full_run.py does: the script's end, the save at its '#end' (story and map) and its '#must' lines.
Run it again after adding or changing a segment; the test round then runs them side by side (testpass.py run
fullrun pm64run -j 8).

usage: gen_fullrun_scenarios.py [--check]   (--check: exit 1 if a written scenario is out of date)
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import full_run  # noqa: E402

REPO = full_run.ROOT
OUT = os.path.join(REPO, "tests", "port", "testpass")
CARD = "USA/Card A/01-G8ME-mariost_save_file.gci"  # (US: full_run.card_path's)
# the last TTYD segment whose PM64 run (full_run.py --pm64) was recorded: the chapter work after it recorded only
# the TTYD chain, so the PM64 set's later TTYD segments wait in group "pm64run_pending", out of the round, until
# someone records them (raise this then); the PM64 side trips (pm64/NNp_*) have their own runs and stay in
PM64_RECORDED_THROUGH = "25_flurrie_request"


def scenario(idx, name, script, pm64):
    d = full_run.directives(script)
    env = full_run.segment_env(script)
    env = {k: (int(v) if re.fullmatch(r"-?\d+", v) else v) for k, v in env.items() if v != ""}
    env.update(TTYD_WATCHDOG=60)
    rel = os.path.relpath(script, REPO).replace(os.sep, "/")
    s = {"desc": f"The full run{' with Paper Mario 64 on' if pm64 else ''}, segment {name} ({rel}), from its "
                 "checkpoint" + (f" (a gap in the chain: {d['gap']})" if d["gap"] else "")
                 + ": its script played to its end and its save at "
                 + " ".join(f"{k} {v}" for k, v in d["end"].items()) + "; written by tools/port/gen_fullrun_scenarios.py"}
    s["groups"] = ["pm64run" if pm64 else "fullrun"]
    if pm64 and os.path.dirname(os.path.abspath(script)) != os.path.abspath(full_run.PM64_DIR) and \
            name > PM64_RECORDED_THROUGH:
        s["groups"] = ["pm64run_pending"]
        s["desc"] += ("; PENDING: its PM64 run was never recorded (the chain's PM64 set is recorded through "
                      f"{PM64_RECORDED_THROUGH}), so it is out of the round")
    s["versions"] = ["us"]
    if not pm64:
        s["backend"] = "headless"
    if idx > 0:
        args = [name] + (["--pm64"] if pm64 else [])
        s["user_gen"] = [{"script": "tools/port/full_run_card.py", "args": args, "dest": CARD},
                         {"script": "tools/port/full_run_card.py", "args": args, "dest": "start.gci"}]
        env["TTYD_LOAD_SAVE"] = "{out}/user/start.gci"
    if pm64:
        env.update(TTYD_PM64_ROM="{pm64rom}", TTYD_PM64_LOG=1)
        s["settings"] = {"pm64_partners": 1, "pm64_join": 1, "pm64_met_all": 0, "pm64_bosses": 1,
                         "pm64_story_bosses": 1}
    if d["setting"]:  # the segment's '#setting' lines (fast_travel=1)
        s.setdefault("settings", {}).update({k: int(v) if re.fullmatch(r"-?\d+", v) else v
                                             for k, v in d["setting"].items()})
    s["env"] = env
    s["input"] = rel
    end = d["end"]
    save = r"^\[input\] save: slot \d+, map '" + re.escape(end.get("map", "")) + "'" if end.get("map") else \
        r"^\[input\] save: slot \d+"
    if end.get("story"):
        save += r".*, story " + end["story"] + r"\b"
    s["log_must"] = [save, r"^\[input\] end"] + d["must"]
    if d["must_not"]:
        s["log_must_not"] = d["must_not"]
    # (a walk that ran out of frames: full_run.py's walks_short; the story watch's STUCK on a story value held a
    # while, as a PM64 boss segment's whole run: neither is a failure there, its end checks decide)
    s["allow_failures"] = ["input", "stuck"]
    s["timeout"] = 1800 if pm64 else 900
    return s


def main():
    check = "--check" in sys.argv
    stale = 0
    want = set()
    for pm64 in (False, True):
        full_run.PM64 = pm64
        for idx, name, script in full_run.segments():
            if pm64 and os.path.dirname(script) != full_run.PM64_DIR and not full_run.directives(script)["must"]:
                pass  # (the PM64 set's TTYD segments run too: PM64 content must not break the story)
            fn = os.path.join(OUT, f"{'pm64run' if pm64 else 'fullrun'}_{name}.json")
            want.add(os.path.basename(fn))
            text = json.dumps(scenario(idx, name, script, pm64), indent=1, ensure_ascii=False) + "\n"
            old = open(fn, encoding="utf-8").read() if os.path.exists(fn) else None
            if old != text:
                stale += 1
                if not check:
                    with open(fn, "w", encoding="utf-8", newline="\n") as f:
                        f.write(text)
    for fn in os.listdir(OUT):
        if re.match(r"(fullrun|pm64run)_\d\d.*\.json$", fn) and fn not in want:
            stale += 1
            if not check:
                os.remove(os.path.join(OUT, fn))
    print(f"gen_fullrun_scenarios: {stale} {'out of date' if check else 'written'}")
    sys.exit(1 if check and stale else 0)


if __name__ == "__main__":
    main()
