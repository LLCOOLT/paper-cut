#!/usr/bin/env python3
"""Generate port/archipelago/ttyd_rando_logic.h: the logic the built-in
randomizer (docs/RANDOMIZER.md) places key items by, from the TTYD apworld
(github.com/jamesbrq/ArchipelagoTTYD, MIT):

- each location's region (its tags, as the apworld's Regions.py groups them),
  its tags (shop, trouble, star_piece...) and its rule (json/rules.json);
- the regions and the rules between them (Regions.py and StateLogic.py, here
  written out as the same rules, with the apworld's defaults the built-in
  randomizer uses: blue pipes on, Westside not open, no palace skip, the
  Shadow Queen as the goal with all seven Crystal Stars).

A rule is a tree of nodes: has (an item, a count), all, any, at least n of,
reach a location, reach a region. The apworld's StateLogic functions are
expanded into those.

  gen_ttyd_rando.py --apworld ../ArchipelagoTTYD [--check]

The apworld is read, not run (its JSON). Keep it at the version
gen_ttyd_ap.py read: the locations are matched to ttyd_ap_data.h by id.
"""
import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "port", "archipelago", "ttyd_rando_logic.h")

# Regions.py's get_regions_dict: region -> the tag its locations carry
REGIONS = [
    ("Menu", None),
    ("Rogueport", "rogueport"),
    ("Rogueport (Westside)", "rogueport_westside"),
    ("Rogueport Sewers", "sewers"),
    ("Rogueport Sewers Westside", "sewers_westside"),
    ("Rogueport Sewers Westside Ground", "sewers_westside_ground"),
    ("Petal Meadows (Left)", "petal_left"),
    ("Petal Meadows (Right)", "petal_right"),
    ("Hooktail's Castle", "hooktails_castle"),
    ("Boggly Woods", "boggly_woods"),
    ("Great Tree", "great_tree"),
    ("Glitzville", "glitzville"),
    ("Twilight Town", "twilight_town"),
    ("Twilight Trail", "twilight_trail"),
    ("Creepy Steeple", "creepy_steeple"),
    ("Keelhaul Key", "keelhaul_key"),
    ("Pirate's Grotto", "pirates_grotto"),
    ("Excess Express", "excess_express"),
    ("Riverside Station", "riverside"),
    ("Poshley Heights", "poshley_heights"),
    ("Fahr Outpost", "fahr_outpost"),
    ("X-Naut Fortress", "xnaut_fortress"),
    ("Palace of Shadow", "palace"),
    ("Palace of Shadow (Post-Riddle Tower)", "riddle_tower"),
    ("Pit of 100 Trials", "pit"),
    ("Shadow Queen", None),
]

# Tags the randomizer looks at (bits of a location's tags)
TAGS = ["shop", "trouble", "cooking", "dazzle", "pit_floor", "star_piece", "panel", "shine", "star", "partner", "key"]

STAR_LOCATIONS = [
    "Hooktail's Castle Hooktail's Room: Diamond Star",
    "Great Tree Entrance: Emerald Star",
    "Glitzville Arena: Gold Star",
    "Creepy Steeple Upper Room: Ruby Star",
    "Pirate's Grotto Cortez' Hoard: Sapphire Star",
    "Poshley Heights Sanctum Altar: Garnet Star",
    "X-Naut Fortress Boss Room: Crystal Star",
]
GOAL_STARS = 7


def has(item, count=1):
    return {"has": item, "count": count}


def all_(*r):
    return {"and": list(r)}


def any_(*r):
    return {"or": list(r)}


# StateLogic.py, as rules
F = {}
F["super_hammer"] = has("Progressive Hammer", 1)
F["ultra_hammer"] = has("Progressive Hammer", 2)
F["super_boots"] = has("Progressive Boots", 1)
F["ultra_boots"] = has("Progressive Boots", 2)
F["tube_curse"] = all_(has("Paper Mode"), has("Tube Mode"))
F["westside"] = any_(has("Contact Lens"), has("Bobbery"), F["tube_curse"], F["ultra_hammer"])
F["petal_left"] = has("Plane Mode")
F["hooktails_castle"] = all_(has("Sun Stone"), has("Moon Stone"), any_(has("Koops"), has("Bobbery")))
F["boggly_woods"] = has("Paper Mode")
F["great_tree"] = has("Flurrie")
F["glitzville"] = has("Blimp Ticket")
F["sewer_westside"] = any_(F["tube_curse"], has("Bobbery"), all_(has("Paper Mode"), has("Contact Lens")),
                           all_(F["ultra_hammer"], any_(has("Paper Mode"), all_(F["ultra_boots"], has("Yoshi")))))
F["sewer_westside_ground"] = any_(all_(has("Contact Lens"), has("Paper Mode")), has("Bobbery"), F["tube_curse"],
                                  F["ultra_hammer"])
F["twilight_town"] = any_(all_(F["sewer_westside"], has("Yoshi")), all_(F["sewer_westside_ground"], F["ultra_boots"]))
F["twilight_trail"] = all_(F["twilight_town"], F["tube_curse"])
F["steeple"] = all_(has("Paper Mode"), has("Flurrie"), F["super_boots"])
F["keelhaul_key"] = all_(has("Yoshi"), F["tube_curse"], has("Old Letter"))
F["pirates_grotto"] = all_(has("Yoshi"), has("Bobbery"), has("Skull Gem"), F["super_boots"])
F["excess_express"] = has("Train Ticket")
F["riverside"] = all_(has("Vivian"), has("Autograph"), has("Ragged Diary"), has("Blanket"), has("Vital Paper"),
                      has("Train Ticket"))
F["poshley_heights"] = all_(has("Station Key 1"), has("Elevator Key (Station)"), F["super_hammer"], F["ultra_boots"])
F["fahr_outpost"] = all_(F["ultra_hammer"], F["twilight_town"])
F["moon"] = all_(has("Bobbery"), has("Goldbob Guide"))
F["ttyd"] = any_(has("Plane Mode"), F["super_hammer"],
                 all_(has("Flurrie"), any_(has("Bobbery"), F["tube_curse"], all_(has("Contact Lens"), has("Paper Mode"))))
                 )
F["pit"] = all_(has("Paper Mode"), has("Plane Mode"))
F["pit_westside_ground"] = all_(has("Flurrie"), any_(all_(has("Contact Lens"), has("Paper Mode")), has("Bobbery"),
                                                      F["tube_curse"], F["ultra_hammer"]))
F["palace"] = all_(F["ttyd"], has("stars", GOAL_STARS))
F["riddle_tower"] = all_(F["tube_curse"], has("Palace Key"), has("Bobbery"), has("Boat Mode"), has("Star Key"),
                         has("Palace Key (Tower)", 8))
F["key_any"] = any_(has("Red Key"), has("Blue Key"))
F["super_blue_pipes"] = all_(F["super_hammer"], F["super_boots"])
F["ultra_blue_pipes"] = all_(F["ultra_hammer"], F["super_boots"])


def chapter_completions(count):
    return {"atleast": count, "of": [{"can_reach": l} for l in STAR_LOCATIONS]}


# Regions.py's connections, with blue_pipe_toggle on (its entries replace the
# plain ones), open_westside and palace_skip off, the Shadow Queen the goal
CONNECTIONS = [
    ("Menu", "Rogueport", None),
    ("Rogueport", "Rogueport Sewers", None),
    ("Rogueport", "Rogueport Sewers Westside", F["sewer_westside"]),
    ("Rogueport Sewers Westside", "Twilight Town", has("Yoshi")),
    ("Rogueport", "Rogueport Sewers Westside Ground", F["sewer_westside_ground"]),
    ("Rogueport Sewers Westside Ground", "Pit of 100 Trials", F["pit_westside_ground"]),
    ("Rogueport Sewers Westside Ground", "Rogueport (Westside)", None),
    ("Rogueport Sewers Westside Ground", "Twilight Town", F["ultra_boots"]),
    ("Rogueport Sewers", "Pit of 100 Trials", F["pit"]),
    ("Rogueport", "Palace of Shadow", F["palace"]),
    ("Palace of Shadow", "Palace of Shadow (Post-Riddle Tower)", F["riddle_tower"]),
    ("Palace of Shadow (Post-Riddle Tower)", "Shadow Queen",
     all_({"can_reach": "Palace of Shadow Final Staircase: Ultra Shroom"}, has("stars", GOAL_STARS))),
    ("Rogueport", "Fahr Outpost", F["fahr_outpost"]),
    ("Rogueport", "Keelhaul Key", F["keelhaul_key"]),
    ("Keelhaul Key", "Pirate's Grotto", F["pirates_grotto"]),
    ("Rogueport", "Rogueport (Westside)", F["westside"]),
    ("Rogueport (Westside)", "Glitzville", F["glitzville"]),
    ("Rogueport (Westside)", "Rogueport Sewers Westside", has("Paper Mode")),
    ("Rogueport (Westside)", "Excess Express", F["excess_express"]),
    ("Excess Express", "Riverside Station", F["riverside"]),
    ("Riverside Station", "Poshley Heights", F["poshley_heights"]),
    ("Rogueport Sewers", "Petal Meadows (Left)", F["petal_left"]),
    ("Rogueport Sewers", "Boggly Woods", any_(F["super_blue_pipes"], F["boggly_woods"])),
    ("Twilight Town", "Twilight Trail", F["twilight_trail"]),
    ("Twilight Trail", "Creepy Steeple", F["steeple"]),
    ("Petal Meadows (Left)", "Petal Meadows (Right)", None),
    ("Petal Meadows (Left)", "Hooktail's Castle", F["hooktails_castle"]),
    ("Boggly Woods", "Great Tree", F["great_tree"]),
    ("Fahr Outpost", "X-Naut Fortress", F["moon"]),
    ("Rogueport Sewers", "Petal Meadows (Right)", any_(F["super_blue_pipes"], F["petal_left"])),
    ("Rogueport Sewers", "Keelhaul Key", any_(F["ultra_blue_pipes"], F["keelhaul_key"])),
    ("Rogueport Sewers", "Poshley Heights",
     any_(F["ultra_blue_pipes"], all_({"can_reach_region": "Rogueport (Westside)"}, F["excess_express"],
                                      F["riverside"], F["poshley_heights"]))),
]

# Rules.py's additions
EXTRA_RULES = {
    "Palace of Shadow Final Staircase: Ultra Shroom": has("stars", GOAL_STARS),
    "Palace of Shadow Final Staircase: Jammin' Jelly": has("stars", GOAL_STARS),
}


class Writer:
    def __init__(self, loc_index, region_index):
        self.nodes = []  # (op, count, first, n)
        self.kids = []
        self.items = []  # logic item names
        self.item_index = {}
        self.loc_index = loc_index
        self.region_index = region_index

    def item(self, name):
        if name not in self.item_index:
            self.item_index[name] = len(self.items)
            self.items.append(name)
        return self.item_index[name]

    def node(self, r):
        """A rule's node index."""
        if r is None:
            return -1
        if "or" in r or "and" in r or "atleast" in r:
            children = r.get("or") or r.get("and") or r.get("of")
            ids = [self.node(c) for c in children]
            first = len(self.kids)
            self.kids.extend(ids)
            if "or" in r:
                return self.add("RANDO_ANY", 1, first, len(ids))
            if "and" in r:
                return self.add("RANDO_ALL", len(ids), first, len(ids))
            return self.add("RANDO_ANY", int(r["atleast"]), first, len(ids))
        if "has" in r:
            h = r["has"]
            name, count = (h["item"], h.get("count", 1)) if isinstance(h, dict) else (h, r.get("count", 1))
            return self.add("RANDO_HAS", int(count), self.item(name), 0)
        if "function" in r:
            fn = r["function"]
            if isinstance(fn, dict):
                if fn["name"] != "chapter_completions":
                    raise SystemExit("unknown function " + json.dumps(fn))
                return self.node(chapter_completions(int(fn["count"])))
            if fn not in F:
                raise SystemExit("unknown function " + fn)
            return self.node(F[fn])
        if "can_reach" in r:
            return self.add("RANDO_REACH_LOCATION", 0, self.loc_index[r["can_reach"]], 0)
        if "can_reach_region" in r:
            return self.add("RANDO_REACH_REGION", 0, self.region_index[r["can_reach_region"]], 0)
        raise SystemExit("unknown rule " + json.dumps(r))

    def add(self, op, count, first, n):
        self.nodes.append((op, count, first, n))
        return len(self.nodes) - 1


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apworld", required=True, help="a checkout or extracted .apworld of ArchipelagoTTYD")
    ap.add_argument("--check", action="store_true", help="fail if the header differs instead of writing it")
    a = ap.parse_args()
    items = json.load(open(os.path.join(a.apworld, "json", "items.json"), encoding="utf-8"))
    locations = json.load(open(os.path.join(a.apworld, "json", "locations.json"), encoding="utf-8"))
    rules = json.load(open(os.path.join(a.apworld, "json", "rules.json"), encoding="utf-8"))
    item_id = {i["item_name"]: i["id"] for i in items}

    loc_index = {l["name"]: i for i, l in enumerate(locations)}
    region_index = {name: i for i, (name, _) in enumerate(REGIONS)}
    tag_region = {tag: i for i, (_, tag) in enumerate(REGIONS) if tag}
    w = Writer(loc_index, region_index)
    w.item("stars")  # index 0: the Crystal Stars collected

    loc_rows = []
    for l in locations:
        regions = [tag_region[t] for t in l["tags"] if t in tag_region]
        if len(regions) != 1:
            raise SystemExit(f'{l["name"]}: {len(regions)} regions')
        bits = 0
        for t in l["tags"]:
            if t in TAGS:
                bits |= 1 << TAGS.index(t)
        rule = rules.get(l["name"])
        extra = EXTRA_RULES.get(l["name"])
        if extra is not None:
            rule = all_(rule, extra) if rule is not None else extra
        loc_rows.append((l["id"], regions[0], bits, w.node(rule), l["vanilla_item"], l["name"]))
    edge_rows = [(region_index[s], region_index[t], w.node(r)) for s, t, r in CONNECTIONS]
    star_ids = [item_id[n] for n in ["Diamond Star", "Emerald Star", "Gold Star", "Ruby Star", "Sapphire Star",
                                     "Garnet Star", "Crystal Star"]]

    out = ["/* GENERATED by tools/port/gen_ttyd_rando.py from the TTYD apworld",
           " * (github.com/jamesbrq/ArchipelagoTTYD, MIT License, Copyright (c) 2025",
           " * jamesbrq). Do not edit. The logic of the built-in randomizer",
           " * (docs/RANDOMIZER.md): each location's region, tags and rule, the regions",
           " * and the rules between them (json/rules.json, Regions.py, StateLogic.py). */",
           "#pragma once", "", '#include "rando_gen.h"', "",
           "/* the location tags (TtydRandoLocation.tags) */",
           "enum {"]
    for i, t in enumerate(TAGS):
        out.append(f"    RANDO_TAG_{t.upper()} = 1u << {i},")
    out += ["};", "", "static const char* const kTtydRandoRegions[] = {"]
    out += [f"    {c_str(n)}," for n, _ in REGIONS]
    out += ["};", f"enum {{ RANDO_REGION_COUNT = {len(REGIONS)}, RANDO_REGION_MENU = 0, "
                  f"RANDO_REGION_GOAL = {region_index['Shadow Queen']} }};", "",
            "/* the items rules name (index 0: the Crystal Stars collected, counted), by",
            " * name and Archipelago id (0: no such item) */",
            "static const TtydRandoItem kTtydRandoItems[] = {"]
    for n in w.items:
        out.append(f"    {{{c_str(n)}, {item_id.get(n, 0)}}},")
    out += ["};", "", "static const int64_t kTtydRandoStars[] = {" + ", ".join(str(s) for s in star_ids) + "};", "",
            "static const TtydRandoNode kTtydRandoNodes[] = {"]
    for i, (op, count, first, n) in enumerate(w.nodes):
        out.append(f"    {{{op}, {count}, {first}, {n}}}, // {i}")
    out += ["};", "", "static const uint16_t kTtydRandoKids[] = {"]
    for i in range(0, len(w.kids), 16):
        out.append("    " + ", ".join(str(k) for k in w.kids[i:i + 16]) + ",")
    out += ["};", "", "/* in the apworld's order (json/locations.json) */",
            "static const TtydRandoLocation kTtydRandoLocations[] = {"]
    for lid, region, bits, rule, vanilla, name in loc_rows:
        out.append(f"    {{{lid}, {vanilla}, {region}, 0x{bits:X}, {rule}}}, // {name}")
    out += ["};", "", "static const TtydRandoEdge kTtydRandoEdges[] = {"]
    for s, t, r in edge_rows:
        out.append(f"    {{{s}, {t}, {r}}}, // {REGIONS[s][0]} -> {REGIONS[t][0]}")
    out += ["};", "", "/* every item's name, by Archipelago id (for the spoiler log) */",
            "static const TtydRandoItem kTtydRandoItemNames[] = {"]
    for it in sorted(items, key=lambda i: i["id"]):
        out.append(f"    {{{c_str(it['item_name'])}, {it['id']}}},")
    out += ["};", ""]
    text = "\n".join(out)
    if a.check:
        old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if old != text:
            print(f"{OUT} is out of date: run tools/port/gen_ttyd_rando.py --apworld ...")
            return 1
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"{OUT}: {len(loc_rows)} locations, {len(w.nodes)} rule nodes, {len(w.items)} items, {len(edge_rows)} edges")
    return 0


if __name__ == "__main__":
    sys.exit(main())
