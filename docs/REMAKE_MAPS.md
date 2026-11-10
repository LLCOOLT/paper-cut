# The remake's map changes

Settings → **Remake** → Maps and places: map changes in the spirit of the 2024 remake, each its own switch, all off by default and locked by Console-accurate mode. Only the disc's own models are used (the field's pipes, jump stands, Save and Heart Blocks, its NPCs), and everything is placed by the story's own flags, so a save made with a switch on loads fine with it off. Code: `port/src/common/remake_maps_pc.c`, `port/settings/features/remake_maps.cpp`. The new pipes, blocks, jump stands and the Steeple's fences are a built-in set of [map edits](MAP_EDITOR.md), placed as a mod's are.

| Switch (settings key) | What it does |
|---|---|
| Warp pipes in the sewers (`remake_warp_pipes`) | The Director's Cut sewer pipe rooms ([DIRECTORS_CUT.md](DIRECTORS_CUT.md)) open from the room before the Pit of 100 Trials, with or without Director's Cut. Their last pipe (the side pipe in the third room) asks where to: Rogueport's plaza, then each chapter's town once its Crystal Star is in (Petalburg, Boggly Woods, Glitzville, Twilight Town, Keelhaul Key, Poshley Heights, Fahr Outpost; the town's own sewer pipe where it has one). The names are the game's own, in the game's language. Fast travel's menu "Travel to" stays. |
| The remake's shortcut pipes (`remake_shortcut_pipes`) | Hooktail Castle: a pipe where Last Stand P was (once it's taken), down to the castle's gate courtyard. The Creepy Steeple: one in the parrot's room in the basement (at the front, left of the perch), to Twilight Town's pipe. The Palace of Shadow: one in the hall before the throne rooms, back to the gate. |
| Excess Express skips Riverside (`remake_express_skip`) | After chapter 6, saying yes to sleep in the cabin of the car that would stop at Riverside Station next asks "Stop at Riverside Station on the way?"; riding straight through goes on as the train does when it leaves Riverside. |
| Chapter shortcuts (`remake_chapter_shortcuts`) | Twilight Town's east gate to the end of Twilight Trail and back (once the Steeple has been reached); the Palace of Shadow's riddle tower back to the gate. Chapter 5's are Keelhaul Key's jump stands, chapter 6's the Excess Express. |
| Keelhaul Key and the Grotto (`remake_keelhaul`) | A blue jump stand at Skull Rock (once Bobbery has joined) that throws Mario over to the castaways' camp, and one in the camp back to the rock, switched on by that first jump (the shortcut opens from its far end, as the remake's do); the Pirate's Grotto's floating barrel and bottle by the floodgate come back after chapter 5; the jungle's Fuzzies stay after chapter 5. |
| Jump down the Steeple's stairs (`remake_steeple_stairs`) | The stairs in the Steeple's stair room lose the collision fences along their sides. |
| A shorter hunt for General White (`remake_twilight`) | In chapter 7, the hint in Petalburg is enough for the villager in Twilight Town to send Mario back to Fahr Outpost (Glitzville and Boggly Woods are skipped). |
| New Save and Heart Blocks (`remake_blocks`) | Boggly Woods (a Heart Block by the pipe's Save Block), Twilight Trail (both, halfway), the Steeple's basement (a Heart Block between its Save and Power-up Blocks), the Pirate's Grotto (a Heart Block at the entrance, left of its Save Block), the Excess Express (a Heart Block in the engineer's cab), Riverside Station (both, in the stairs hall), the X-Naut Fortress (both, before the door with three locks), the Palace of Shadow (a Heart Block in the fire-bar hall, both in the hall after the riddle tower, on its floor at the foot of the stairs). |
| New ambush battles (`remake_ambushes`) | Field enemies added the Director's Cut way, with fights from their areas' own data: three Paragoombas on the last field before Shhwonk Fortress, an Ember in the Pirate's Grotto's water room, an Elite X-Naut before the X-Naut Fortress's door with three locks. |
| Charlieton and a Grotto salesman (`remake_merchants`) | Charlieton outside the Great Tree by its sewer pipe (after chapter 2), selling from the same stock as in Rogueport; a Toad on the hidden dock at the end of the Pirate's Grotto with healing and battle items. Their lines are Paper Cut's. |

## Not done

- **Twilight Town's pumpkin patch**: neither town room (usu_00, usu_01) has a model, texture or collision object named for pumpkins or a field on the disc, and the floor collision covers the whole town, so what keeps Mario out is walls. The ground-level spaces walled off with no door are three in usu_00, behind its houses: the north-west back yard (x -360..-150, z -490..-250), the north-east one (x 120..240, z -490..-310) and one by the east exit (x 390..480, z -220..-100). Their walls belong to the houses' own meshes, not to a fence of their own, so which one is the patch, and which wall to lift, needs a look in the game.
- **Pipes entered with A and a prompt**: not part of this set.

## Save data

One save value of Paper Cut's own: GSW 2030 (which the game never uses) is 1 once Keelhaul Key's Skull Rock jump stand has been used, so the camp's is there. A save with it loads fine with the switch off or in a build without it. Everything else is placed from the story's flags as a room loads; the new blocks and pipes are not written anywhere. The Toad's and Charlieton's sales use the game's own coins and Charlieton's own stock.

## Online and replays

The switches are part of the settings online's host sends, and everything follows from them and the game's own state.
