# The built-in randomizer

A single-player item randomizer built into Paper Cut. From a seed, it shuffles the game's collectible items among the places the game hands them out. It needs no Archipelago server, no patched disc and no seed file. The same seed with the same choices gives the same game on every computer and phone, so seeds can be shared.

It works on the **US version only**. Where each item lies in the game, and in which script or table its item number sits, comes from the TTYD apworld's location data, which was made for the US disc. The JP and EU builds don't have those places mapped, so on them the Randomizer tab only says so and the randomizer stays off.

## Playing a seed

1. Open the settings page (F1, a controller's Back, or a three-finger tap) and go to the **Randomizer** tab.
2. Turn on **Randomize the game from the next start**.
3. Type a seed, or press **New seed** for a random one. Leave it empty and one is made at the start and kept.
4. Choose what is shuffled (below). **Check this seed** makes the seed then and there and says how many places take part and how many items moved, without changing anything.
5. Start the game (or restart it, if it is running) and begin a **new file**.

The seed applies to the whole run, so any file played while the randomizer is on gets its items. Start a new file for it, and turn the randomizer off again before going back to your other files. The tab shows what this run was given: the seed, what moved and where the spoiler log went.

An Archipelago seed chosen on the Archipelago tab takes precedence. The Randomizer tab says so and offers to clear it.

The settings are kept in `randomizer.json` in the user folder.

## What is shuffled

- **Badges**
- **Items**: consumables, cooking ingredients and, in the open world, coins.
- **Star Pieces**
- **Shine Sprites**
- **Key items**: keys and story items, and in the open world also the partners, the curses, the hammer and the boots. This needs the TTYD AP mod (below).

Each place whose own item is of a chosen kind takes part, and gets one of the chosen kinds' items. With badges and Star Pieces both chosen, for example, a badge can be where a Star Piece was and the other way round. The number of each item stays as the game has it.

## The logic

Two kinds of game come out of the settings, depending on whether key items are shuffled.

### Without key items: the game's own story

The story stays exactly as the game has it, so nothing it depends on moves. That rules out key items, partners, curses, upgrades and Crystal Stars, and also items the apworld marks as needed somewhere (a Coconut for a trade, ingredients for a recipe a trouble asks for). Only badges, items nothing needs, Star Pieces and Shine Sprites move, and only among places where the game's own script gives the item (a block, a chest, a hidden panel, a character handing it over). Shops, Dazzle's and the Pit's rewards, troubles, cooking and tattles are left alone. Those places are tables or flows that the TTYD AP mod normally looks after. That leaves about 230 places.

Since nothing the story needs ever moves, every seed can be finished, and no mod is needed. This is the default.

### With key items: the open world (needs the TTYD AP mod)

The game's story gates chapters on events as well as items, and only the TTYD AP mod changes that. With the mod, chapters open by items: a ticket opens the blimp, a curse opens a path. The apworld's logic describes that world: which items open which regions and places. A key item placed by that logic in the game's own story could end up where the story never lets Mario go before he needs it.

So shuffling key items means playing the mod's open world. The randomizer then writes the same options block an Archipelago seed would, with the apworld's defaults:

- the Shadow Queen as the goal, all seven Crystal Stars for the Palace;
- Goombella as the starting partner;
- blue pipes on and Westside not open;
- shops, troubles, cooking, tattles, the Pit's and Dazzle's rewards as the game has them;
- Crystal Stars where the game has them;
- intermissions and cutscenes skipped and fast travel on;
- every item this player's own, given where it lies, so no server is involved.

The mod is GPL and isn't part of Paper Cut. It is built from its source with the porting tools and added as a code mod (see [MODDING.md](MODDING.md#archipelago)). The tab warns when no enabled code mod looks like it. Its messages come from an installed `ttyd.apworld`, as for Archipelago seeds. The randomizer also looks for one in the user folder.

The key items, partners, curses, hammer and boots are always shuffled together in the open world, because the apworld never keeps them where the game has them: there they would lock each other away. Goombella stays at the start. At least one of badges, items or Star Pieces must be shuffled with them, since the key items' own places are too few early on for all of them.

Placing them works the way Archipelago's generator does ("assumed fill"):

1. The progression items are those any rule asks for.
2. They are placed one at a time, in a random order.
3. Each one goes to a random empty place that can be reached while every progression item not yet placed is assumed to be in hand.

Whichever order they are found in, each item's place opens before that item is needed. The rest of the items fill the remaining places at random. Finally the seed is checked from nothing, collecting what each reachable place holds until no more opens. If that sweep can't reach the Shadow Queen, the seed is drawn again (up to 100 tries; in practice one or two).

### Paper Cut's open world (chapters 1 and 2, no mod)

**Paper Cut's open world** on the Randomizer tab is an open world of Paper Cut's own: no mod, written from the game's own map scripts. This first stage covers Rogueport, the sewers and chapters 1 and 2. After the prologue (the game's own), Petal Meadows and Boggly Woods are both open, and either can come first.

- **Key items and partners move:** Koops and Flurrie, the Paper Curse, the Castle Keys, the Black Key, the Sun and Moon Stones, the Necklace, the Puni Orb, the Red and Blue Keys and the Super Boots. The badges, items, Star Pieces and Shine Sprites move too, as chosen. Goombella, the Plane Curse and its Black Key stay where the prologue has them, since the prologue can't be finished without them. The Crystal Stars stay too.
- **The goal** of this stage is both chapters' Crystal Stars. Chapters 3 to 8 come in later versions; until then the story past chapter 2 doesn't open.
- **The file keeps the seed.** With the randomizer on and this chosen, each new file takes the seed (a new one when the box is empty). The seed and each chapter's story are saved in the file, so it plays the same after a restart, and files started without it stay as the game has them. The settings only matter when a new file is started.

How it works:

- **Each chapter has its own story.** The game keeps one story value, and every map's scripts check it, so normally a chapter can only begin once the one before has ended. In an open-world file, the maps of Petal Meadows, Petalburg and Hooktail's Castle read and write chapter 1's own value. Boggly Woods and the Great Tree read and write chapter 2's. Rogueport keeps the game's value, which stays where the prologue left it. The two sewer rooms with a chapter's pipe read that chapter's value, so the pipe shows the chapter's title card the first time and goes straight in after. The title cards and Peach's and Bowser's intermissions read the chapter last played. A chapter not yet begun reads as the value its first room waits for, so it begins as the game begins it, and inside a chapter the story goes in the game's own order.
- **Partners are items.** A partner's item is one of the game's unused key items, named and drawn as that partner. Picking it up makes the partner join. A partner's own joining scene gives whatever was placed there instead: another partner (the scene plays for that partner), or an item, handed over the way an NPC hands one over.
- **Keys:** the four Castle Keys count as one key that opens any of the castle's doors, as the logic counts them. A second Super Boots is the Ultra Boots.
- **The logic** is the apworld's rules for the places inside a chapter, plus Paper Cut's own ways between regions and the story's conditions, in `port/archipelago/pc_world.h`. For example, Hooktail's Castle needs both stones placed and a shell for the timed switches. Koops only waits on the bridge once both stones are taken, and the Great Tree opens after Flurrie's scene. In Rogueport and the sewers, whose story stands still, a key item only goes to a room's own items (its blocks, chests and the items lying in it), not to a scene that waits for a later chapter.

## The spoiler log

With **Write a spoiler log** on (the default), the start writes `randomizer/spoiler_<seed>.txt` in the user folder. It has the seed, its 64-bit hash, the logic, what was shuffled and every place that took part with the item it got, grouped by region. In the open world it also has a playthrough: the key items in the spheres the logic expects them to be found in (sphere 1 is what can be reached at the start, sphere 2 what those items open, and so on).

## How it is built

- `port/archipelago/rando_gen.cpp` is the generator. It takes a seed text and options and returns the placements and the spoiler text. It has no game, file or global state. Its random numbers come from its own xoshiro256** seeded through splitmix64 from the seed text's FNV-1a hash, with no `<random>` distributions, so a seed gives the same placements on every compiler and platform.
- `port/archipelago/ttyd_rando_logic.h` is generated by [`tools/port/gen_ttyd_rando.py`](../tools/port/gen_ttyd_rando.py) from the apworld. It holds each place's region, tags (shop, trouble, Star Piece...), game item and rule from `json/rules.json`. It also holds the regions and the rules between them from `Regions.py`, with the functions of `StateLogic.py` written out as rules, and every item's name for the log. A rule is a small tree: has an item (a number of times), all of, any of, at least n of, can reach a place or a region. The apworld is read, not run. Regenerate it from the same apworld version as `ttyd_ap_data.h` (`gen_ttyd_ap.py`): places are matched by id, and the generator refuses a mismatch.
- `port/archipelago/ttyd_rando.cpp` keeps the settings, makes the seed at the game's start and writes the spoiler log.
- `ttyd_ap.cpp`, the Archipelago support, applies the seed. With no `.apttyd` chosen and the randomizer on, the placements become the loaded seed's, as if read from a seed file. Each item is written where the game gives it: in the DOL's tables at start, and in an area's scripts as its REL is linked. In the open world the options block is written too.
- `port/settings/features/randomizer.cpp` is the Randomizer tab.
- Paper Cut's open world: `port/archipelago/pc_world.h` is its world, stage by stage (regions, the ways between them, the story's conditions, the places the prologue keeps, the goal). The generator's `kPaperCut` logic places by it. `port/src/common/open_world_pc.c` is the game side: the file's stamp (GSW 1920 on, which the game never uses), each chapter's own story behind GSW 0 (`swByteGet`/`swByteSet`), the partners' items, the Castle Keys, and the partners' own scenes (at `CALL_CHILD`). `port/archipelago/open_world.cpp` makes the file's placements again from its seed and writes them into each area's scripts as the area is linked.

`tests/port/rando_test.cpp` (`rando_test` in CMake; or `clang++ -std=c++20 tests/port/rando_test.cpp port/archipelago/rando_gen.cpp`) runs the generator on the host over many seeds and every combination of kinds. It checks that:

- the same seed gives the same placements;
- the items placed are exactly the items taken;
- in the vanilla story no key item moves and no shop, tattle or table place takes part;
- in the open world the Shadow Queen is reachable, by a sweep of the test's own over the same tables.

It runs Paper Cut's open world over many seeds too. It checks that the prologue's places stay as they are, that Koops and Flurrie are both placed and land in different places from seed to seed, and that each seed reaches both Crystal Stars (the generator's own sweep). `TTYD_OPEN_SEED=<seed>` with a `TTYD_MAP` warp stamps the warped-in file with that seed. Adding `TTYD_OPEN_SELFTEST=1` also gives items in an odd order on the spot (Flurrie's before Koops's, the third Castle Key first, the Super Boots twice), checks the chapters' stories, and prints PASS and FAIL lines.

It also pins one seed's placements, so a change to the generator or its data shows up. Such a change means a seed shared before no longer gives the same game.

## Limits and what is not verified yet

- **US only**, as above.
- **The open world has not been played with the TTYD AP mod.** The mod isn't part of this repository. Its options and placements are written the way an `.apttyd` seed's are, through code that a seed and a server session already exercise. Whether the mod plays a solo, serverless seed through to the end is untested.
- **The vanilla story's places** are the apworld's places whose item sits in a script word of a REL. The patcher writes ordinary item numbers there for Archipelago too, and `TTYD_AP_VERIFY=1` checks that those words hold the disc's items before a seed changes them. No full game has been played through on a vanilla-story seed yet.
- A seed applies to every file played in that run. Nothing in the save says which seed it was made with. This doesn't apply to Paper Cut's open world, whose files keep their seed.
- **Paper Cut's open world** has been checked by the host test and by short scripted runs, not played through. In Rogueport the story stays where the prologue left it, so the scenes after each chapter there (Zess T., Frankly, the Thousand-Year Door) don't play, and chapter 3 doesn't open yet. After a partner's own scene gives an item instead, the lines that follow are spoken by the partner who is out. The black chest's scene in Hooktail's Castle gives what was placed there without showing it, and still plays the Paper Mage's lesson. Ms. Mowz has no item of her own yet. The notice when a partner joins is in English only.
- Shops, troubles, cooking, tattles, Dazzle's and the Pit's rewards aren't shuffled. Neither are the Crystal Stars, or Goombella's place.
