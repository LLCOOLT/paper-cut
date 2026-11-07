# The built-in randomizer

A single-player item randomizer built into Paper Cut. From a seed, it shuffles the game's collectible items among the places the game hands them out. It needs no Archipelago server, no patched disc and no seed file. The same seed with the same choices gives the same game on every computer and phone, so seeds can be shared.

It works on the **US version only**. Where each item lies in the game, and in which script or table its item number sits, comes from the TTYD apworld's location data, which was made for the US disc. The JP and EU builds don't have those places mapped, so on them the Randomizer tab only says so and the randomizer stays off.

## Playing a seed

1. Open the settings page (F1, a controller's Back, or a three-finger tap) and go to the **Randomizer** tab. In the game, the Start menu's Settings tab has the same settings under **Randomizer** (the seed typed on the game's own text page).
2. Turn on **Randomize the game from the next start**.
3. Type a seed, or press **New seed** for a random one. Leave it empty and one is made at the start and kept.
4. Choose what is shuffled (below). **Check this seed** makes the seed then and there and says how many places take part and how many items moved, without changing anything.
5. Start the game (or restart it, if it is running) and begin a **new file**.

The seed applies to the whole run, so any file played while the randomizer is on gets its items. Start a new file for it, and turn the randomizer off again before going back to your other files. The tab shows what this run was given: the seed, what moved and where the spoiler log went.

An Archipelago seed chosen on the Archipelago tab takes precedence. The Randomizer tab says so and offers to clear it.

The settings are kept in `randomizer.json` in the user folder.

Online, the host's settings are the session's: they travel with its setup (a seed is made first if it has none), so a new file started there gets the same open world on both sides, and a guest whose game hasn't started yet starts with the host's seed. A guest's game that already started with other randomizer settings is turned away with a notice, to start again and join from the launcher. Nothing of the host's is written into the guest's `randomizer.json`. A replay keeps the recording's settings the same way ([REPLAYS.md](REPLAYS.md)). While online or in a replay, the page's randomizer settings can't be changed.

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

The story stays exactly as the game has it, so nothing it depends on moves. That rules out key items, partners, curses, upgrades and Crystal Stars, and also items the apworld marks as needed somewhere (a Coconut for a trade, ingredients for a recipe a trouble asks for). Only badges, items nothing needs, Star Pieces and Shine Sprites move, and only among places where the game's own script gives the item (a block, a chest, a hidden panel, a character handing it over). Shops, Dazzle's and the Pit's rewards, troubles, cooking and tattles are left alone. Those places are tables or flows that the TTYD AP mod normally looks after. So are Whacka's bumps and the Excess Express madam's 30 coins: for those the apworld's data points at script words that aren't the item (the mod reworks those scenes). A Shine Sprite block, or a block of coins, gives the item placed in it: the game leaves those blocks' item word empty, and Paper Cut reads it when something is written there. That leaves about 230 places.

Since nothing the story needs ever moves, every seed can be finished, and no mod is needed. This is the default.

The Briefcase on the Excess Express is the one exception among those kinds: the apworld gives it an item number of the TTYD AP mod's own, while the game's Zip Toad scene doesn't end until Mario hands over the game's own Briefcase. So it never moves in the game's own story.

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

### Paper Cut's open world (chapters 1 to 8, no mod)

**Paper Cut's open world** on the Randomizer tab is an open world of Paper Cut's own: no mod, written from the game's own map scripts. It covers Rogueport and both sides of its sewers, and all eight chapters. After the prologue (the game's own), each chapter opens by what its way in needs, in whatever order the items allow:

| Chapter | Way in | Needs |
|---|---|---|
| 1, Petal Meadows | tik_02's pipe | the Plane Curse (the prologue's) |
| 2, Boggly Woods | tik_03's pipe | the Paper Curse |
| 3, Glitzville | the blimp at the station (gor_04) | the Blimp Ticket |
| 4, Twilight Town | tik_08's pipe, in the west sewers | Yoshi (or the Ultra Boots, the way below) |
| 5, Keelhaul Key | Flavio's ship at the harbor (gor_00) | the Old Letter |
| 6, the Excess Express | the train at the station (gor_04) | the Train Ticket |
| 7, Fahr Outpost | tik_07's pipe, in the west sewers | the Ultra Hammer, and Yoshi or the Ultra Boots |
| 8, the Palace of Shadow | the Thousand-Year Door (tik_05) | the goal's Crystal Stars |

- **Key items and partners move:** Koops, Flurrie, Yoshi, Vivian and Bobbery, the Paper, Tube and Boat Curses, the keys, the tickets, the stones and orbs, the Super Boots and Hammer and the rest of the chapters' story items. The badges, items, Star Pieces and Shine Sprites move too, as chosen. Goombella, the Plane Curse and its Black Key stay where the prologue has them, since the prologue can't be finished without them. The Crystal Stars stay too. Ms. Mowz stays where the game has her: her place is a trouble, and troubles aren't shuffled.
- **The goal** is a number of Crystal Stars, from any chapters: all seven by default, or fewer (**Crystal Stars for the goal**). The logic makes sure that many can be reached, and a notice says when they are. The Thousand-Year Door opens with that many, as it opens with all seven in the game: Frankly waits in the sewers, the stars Mario has rise from the pedestal (and light their marks on the door; the others stay dark) and chapter 8 begins. The palace is the way to the end; the Shadow Queen ends the game as always. The stars past the goal, and the palace, may need items that lie past the goal; the logic still makes sure every chapter can be reached.
- **The file keeps the seed.** With the randomizer on and this chosen, each new file takes the seed (a new one when the box is empty) and the goal. The seed, the goal and each chapter's story are saved in the file, so it plays the same after a restart, and files started without it stay as the game has them, also when loaded after an open-world file in the same run (the items it wrote into the areas' scripts are put back). The settings only matter when a new file is started.

How it works:

- **Each chapter has its own story.** The game keeps one story value, and every map's scripts check it, so normally a chapter can only begin once the one before has ended. In an open-world file each chapter's maps read and write that chapter's own value: Petal Meadows, Petalburg and Hooktail's Castle chapter 1's, Boggly Woods and the Great Tree chapter 2's, Glitzville chapter 3's, Twilight Town, its trail and the steeple chapter 4's, Keelhaul Key and the grotto chapter 5's, the train, Riverside Station and Poshley Heights chapter 6's, Fahr Outpost, the moon and the X-Naut Fortress chapter 7's, the palace chapter 8's. The rooms with a chapter's way in read that chapter's value too, so the way in shows the chapter's title card the first time and goes straight in after: the sewers' pipe rooms; the station, which is the blimp's (chapter 3) except while Mario boards the train or comes off it (chapter 6); the harbor once Mario holds the Old Letter (where Flavio waits to sail) or chapter 5 has begun (the ship's return, and Cortez's ferry after it); the Thousand-Year Door's rooms once Mario has the goal's stars. The title cards read the chapter last played. Peach's and Bowser's intermissions read the chapter they follow, wherever they go, from its boss to its last scene; after the Shadow Queen every room reads chapter 8's, so the ending and the world after it are the game's own. A chapter not yet begun reads as the value its way in waits for, so it begins as the game begins it, and inside a chapter the story goes in the game's own order.
- **General White's hunt.** In chapter 7 Mayor Dour sends Mario after General White, and the game has him ask in Petalburg, Glitzville, the Great Tree and Twilight Town before White turns up asleep in Fahr Outpost, while Goldbob in Poshley Heights must agree to the cannon. While chapter 7's story is in that hunt, the rooms where those questions are asked (and Goldbob's station) read chapter 7's story, as the whole world does in the game; once White is woken and Goldbob agreed they go back to their own chapters. So the moon needs those places to be reachable; the Goldbob Guide itself is never asked for by the game.
- **Rogueport's own story follows the chapters done.** Rogueport keeps the game's value. Once more chapters are done than it has caught up with, the next Rogueport room entered moves it to its next stretch: the scenes the game plays there after a chapter (the Thousand-Year Door's star, Frankly's, Zess T., Don Pianta and the Blimp Ticket, Darkly...). The n-th stretch follows the n-th chapter done, whichever chapter that was. The file notes the order the chapters were done in, and the star that rises from the pedestal is that chapter's; the lines in the stretch are the game's and may still be about another chapter's star. A stretch's story never goes past its end: what the game has after it is the next chapter's way in, which is that chapter's own here. So the stretch after the fourth chapter stops at Frankly's, before Flavio's search for a sailor; the one after the fifth at Don Pianta falling ill; the one after the sixth at Frankly's research, before the Ultra Hammer and chapter 7's pipe. The seventh chapter done opens no stretch of its own: what follows it in the game is the Thousand-Year Door.
- **Partners are items.** A partner's item is one of the game's unused key items, named and drawn as that partner. Picking it up makes the partner join. A partner's own joining scene gives whatever was placed there instead: another partner (the scene plays for that partner), or an item, handed over the way an NPC hands one over. Either way, the lines the party speaks in the rest of that scene are left out: they are the joining partner's words, and the partner who is out would say them. A scene that sends the party away and brings it back (Doopliss in the steeple, the poisoned cake in Glitzville) brings back only the partners who were there, not ones not found yet.
- **The black chests** (the Paper, Tube and Boat Curses') give what was placed in them. When that is the chest's own curse, the Paper Mage appears and teaches it as in the game; anything else is handed over as an item, and the Paper Mage doesn't come. The Plane Curse's chest is the prologue's and keeps its curse.
- **Keys:** the four Castle Keys count as one key that opens any of the castle's doors, as the logic counts them; so do the steeple's two Steeple Keys, the palace's three Palace Keys and the riddle tower's eight. A second Super Boots is the Ultra Boots, a second Super Hammer the Ultra Hammer.
- **Crystal Stars** give 100 Star Power each, whichever order they come in (the game sets the maximum by the last star's number).
- **The logic** is the apworld's rules for the places inside a chapter, plus Paper Cut's own ways between regions and the story's conditions, in `port/archipelago/pc_world.h`. For example, Hooktail's Castle needs both stones placed and a shell for the timed switches; Koops only waits on the bridge once both stones are taken; the Storage Key 2 falls only once the storage room was opened with the first; Vivian joins only after Doopliss's first battle and the Superbombomb; Bobbery joins only after Flavio's Chuckola Cola; on the train the ghost's diary waits for the Autograph and Vivian, and the train only reaches Riverside once Zip Toad is caught, which needs the Briefcase handed over. Rogueport's west side opens once a chapter is done (the Contact Lens Zess T. waits for comes then; it has no place of its own and never moves), or by the apworld's other ways through the sewers. In Rogueport and the sewers a key item only goes to a room's own items (its blocks, chests and the items lying in it), or to a scene of a stretch the logic counts the chapters for (Don Pianta's Blimp Ticket, after two). The Ultra Hammer's chest on a Rogueport roof is there from the start, and stays whatever hammer Mario has.
- **The Old Letter** opens chapter 5 here, as in the apworld. The game's Podley hands it over in a Rogueport scene after the fourth chapter, past where Rogueport's stretch stops, so its own place is out of the shuffle and the letter is placed among the other places instead of one of their items.
- **The Train Ticket** opens chapter 6 likewise. In the game Don Pianta hands it over once Frankie and Francesca are home from Keelhaul Key with Francesca's lost wedding ring: a chain that runs between Rogueport and Keelhaul Key after the fifth chapter. Here Rogueport's stretch after the fifth chapter stops before it and chapter 5's story stops at the ship's return, so that chain doesn't play: the ticket's own place is out and the ticket is placed elsewhere, and the wedding ring that only shows in the grotto during that chain is out too. Nothing else asks for the ring.
- **Trips with no way back.** Three trips don't come back until their chapter's scene ends: Flavio's ship to Keelhaul Key (it sails home after Cortez; the jungle's pipe to the sewers only shows once its switch was pressed from the sewers' side), the train's first ride (it has no stop before Riverside Station, and the first way back is from Poshley Heights) and the cannon to the moon (no way back before the X-Naut Fortress is done). The logic takes such a trip only once it can be finished from what Mario holds and what lies on the way, and the moon, which can't be gone back to, only once nothing else opens. In the game Flavio, the train's attendant and Fahr Outpost's mayor won't set off until then: they say there's no way back and to get ready first, without saying what's missing.
- **The logic's version** is kept in the file. A file started before the one-way trips came into the logic keeps the placements it was made with. From that version on the same seed gives other placements than before, and the spoiler log's "Logic version" line says which one made it.

## The spoiler log

With **Write a spoiler log** on (the default), the start writes `randomizer/spoiler_<seed>.txt` in the user folder. It has the seed, its 64-bit hash, the logic, what was shuffled and every place that took part with the item it got, grouped by region. In the open world it also has a playthrough: the key items in the spheres the logic expects them to be found in (sphere 1 is what can be reached at the start, sphere 2 what those items open, and so on).

## How it is built

- `port/archipelago/rando_gen.cpp` is the generator. It takes a seed text and options and returns the placements and the spoiler text. It has no game, file or global state. Its random numbers come from its own xoshiro256** seeded through splitmix64 from the seed text's FNV-1a hash, with no `<random>` distributions, so a seed gives the same placements on every compiler and platform.
- `port/archipelago/ttyd_rando_logic.h` is generated by [`tools/port/gen_ttyd_rando.py`](../tools/port/gen_ttyd_rando.py) from the apworld. It holds each place's region, tags (shop, trouble, Star Piece...), game item and rule from `json/rules.json`. It also holds the regions and the rules between them from `Regions.py`, with the functions of `StateLogic.py` written out as rules, and every item's name for the log. A rule is a small tree: has an item (a number of times), all of, any of, at least n of, can reach a place or a region. The apworld is read, not run. Regenerate it from the same apworld version as `ttyd_ap_data.h` (`gen_ttyd_ap.py`): places are matched by id, and the generator refuses a mismatch.
- `port/archipelago/ttyd_rando.cpp` keeps the settings, makes the seed at the game's start and writes the spoiler log.
- `ttyd_ap.cpp`, the Archipelago support, applies the seed. With no `.apttyd` chosen and the randomizer on, the placements become the loaded seed's, as if read from a seed file. Each item is written where the game gives it: in the DOL's tables at start, and in an area's scripts as its REL is linked. In the open world the options block is written too.
- `port/settings/features/randomizer.cpp` is the Randomizer tab.
- `port/settings/features/randomizer_menu.cpp` is the same settings as a Start menu form (`port/include/port/pause_forms.h`). An on/off is one line in its `kToggles` (a `PortRandoSettings` field, and when it is shown); another kind of row is an id with a case in its `rows` and `act`.
- Paper Cut's open world: `port/archipelago/pc_world.h` is its world, stage by stage (regions, the ways between them, the story's conditions, the places the prologue keeps, the goal). The generator's `kPaperCut` logic places by it. `port/src/common/open_world_pc.c` is the game side: the file's stamp (GSW 1920 on, which the game never uses: the stage, the seed, the goal, each chapter's story), each chapter's own story and Rogueport's stretches behind GSW 0 (`swByteGet`/`swByteSet`), the partners' items, the keys counted as one, the partners' own scenes (at `CALL_CHILD`), the party a scene sends away (`evt_pouch_party_left`/`_join`), the lines a joining scene leaves out (`evt_msg.c`), the black chests (`portEvtOpenChest`, called from their scenes), the stars the door and the pedestal show (`tik_05.c`) and the Star Power maximum (`pouchGetStarStone`). `port/archipelago/open_world.cpp` makes the file's placements again from its seed and writes them into each area's scripts as the area is linked, and puts those words back for a file that isn't an open-world one.

`tests/port/rando_test.cpp` (`rando_test` in CMake; or `clang++ -std=c++20 tests/port/rando_test.cpp port/archipelago/rando_gen.cpp`) runs the generator on the host over many seeds and every combination of kinds. It checks that:

- the same seed gives the same placements;
- the items placed are exactly the items taken;
- in the vanilla story no key item moves and no shop, tattle or table place takes part;
- in the open world the Shadow Queen is reachable, by a sweep of the test's own over the same tables.

It runs Paper Cut's open world over many seeds too. It checks that the prologue's places stay as they are, that the Old Letter's and the Train Ticket's own places stay out and both are placed, that the five partners are all placed and Koops lands in different places from seed to seed, that each seed reaches its goal's Crystal Stars and every chapter's way in (the generator's own final sweep), and that every goal from one star to seven makes a seed. In the game's own story it checks that the Briefcase never moves. `TTYD_OPEN_SEED=<seed>` with a `TTYD_MAP` warp stamps the warped-in file with that seed; `TTYD_OPEN_TRACKS=<chapter>:<story>,...` sets chapters' stories in it and `TTYD_OPEN_ITEMS=<item>,...` puts items in hand (item ids, as `0x34`); `TTYD_OPEN_GOAL=<n>` sets its goal and `TTYD_OPEN_STARS=<1-7>,...` gives it those Crystal Stars. Adding `TTYD_OPEN_SELFTEST=1` also gives items in an odd order on the spot (Flurrie's before Koops's, the third Castle Key first, a Steeple Key for the other lock, a Palace Key for another, the Super Boots twice), sends the party away and back, checks the chapters' stories, the station's two chapters, General White's hunt, the shut door, the door's stars, the order of the chapters done and the harbor without Cortez and, at Rogueport's first rest, its first stretch, and prints PASS and FAIL lines.

It also pins one seed's placements, so a change to the generator or its data shows up. Such a change means a seed shared before no longer gives the same game.

`tests/port/rando_solve.cpp` (`rando_solve` in CMake) plays the generator's seeds over every setting (the logic, which kinds are shuffled, every goal) from their placements alone, with two models of its own: the generator's tables played again, and a model of the game's one-way trips written from the map scripts apart from `pc_world.h`'s. It reports a seed the goal, every chapter's way in, the Thousand-Year Door or the Shadow Queen can't be reached in, with the key items left out and where they lie; a trip that can be taken too early (each key item left lying in turn, every trip whose way in opens without it finished or not); a place given twice, a place written where the game gives nothing, and the items placed not being the ones taken; and the same seed not giving the same placements. `rando_solve 20` runs 20 seeds per combination; `rando_solve --seed <text> [--pools <n>] [--goal <n>]` one seed, in detail; `rando_solve --digests <n>` a digest per seed, to compare two machines (online, every side makes the placements itself).

## Limits and what is not verified yet

- **US only**, as above.
- **The open world has not been played with the TTYD AP mod.** The mod isn't part of this repository. Its options and placements are written the way an `.apttyd` seed's are, through code that a seed and a server session already exercise. Whether the mod plays a solo, serverless seed through to the end is untested.
- **The vanilla story's places** are the apworld's places whose item sits in a script word of a REL. The patcher writes ordinary item numbers there for Archipelago too, and `TTYD_AP_VERIFY=1` checks that those words hold the disc's items before a seed changes them. No full game has been played through on a vanilla-story seed yet.
- A seed applies to every file played in that run. Nothing in the save says which seed it was made with. This doesn't apply to Paper Cut's open world, whose files keep their seed.
- **Paper Cut's open world** has been checked by the host test, not played through; chapters 3 to 8 and Rogueport's stretches haven't been run in the game yet. Some things are known to look odd: a partner found before the chapter where the game has them as a character (Bobbery on Flavio's ship, Vivian in Twilight Town) is there twice, in the party and in the scenes. Rogueport's stretches follow the count of chapters done, not which ones: the pedestal's star is the right one, but the lines may name another chapter's star. Files started with an earlier stage (chapters 1 and 2, or 1 to 5) take this stage's placements and chapters when loaded. Keelhaul Key's wedding ring chain after chapter 5 doesn't play (above). During General White's hunt the rooms where he is asked about show their chapter as it is after the game's chapter 7, until the hunt is over. The harbor stays Rogueport's while Rogueport's stretch after the second chapter waits for Don Pianta's daughter there, so Flavio's ship waits until that scene has played.
- Shops, troubles, cooking, tattles, Dazzle's and the Pit's rewards aren't shuffled. Neither are the Crystal Stars, or Goombella's place.
