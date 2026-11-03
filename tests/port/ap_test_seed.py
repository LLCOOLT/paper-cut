#!/usr/bin/env python3
"""A small TTYD Archipelago seed (.apttyd) for the port's tests: options and
two placements, as the apworld's patch file holds them.

  ap_test_seed.py out.apttyd

- Rogueport Center Shop: Fire Flower -> Ultra Shroom (this slot's), 7 coins
- Rogueport Center: Old Letter       -> Power Bounce (this slot's)
"""
import json
import sys
import zipfile

options = {"world_version": "test", "seed": 1, "seed_name": "test", "player": 1, "player_name": "Mario",
           "generation_time": 0, "goal": 1, "remote_items": 0, "shop_purchase_limit": 1}
locations = {"Rogueport Center Shop: Fire Flower": [77772204, 1, 7],
             "Rogueport Center: Old Letter": [77772140, 1, 0]}
with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.writestr("options.json", json.dumps(options))
    z.writestr("locations.json", json.dumps(locations))
    z.writestr("fallback_locations.json", "{}")
    z.writestr("enemies.bin", b"\0\0")
    z.writestr("bosses.bin", b"\0\0")
    z.writestr("tracker.bin", b"")
    z.writestr("desc.txt", b"")
