# Director's Cut

Modes > **Director's Cut** (off by default) puts back content the game's code still has but never uses. Each restored thing has a page in the **Director's Cut journal** (Modes, on the settings page and in the Start menu). Code: `port/src/aurora/directors_cut_aurora.c`, `port/settings/features/directors_cut.cpp`.

## What comes back

- **Eight cut badges.** The battle code has their moves in full, but nothing in the game gives them, and the item table lists them hidden with no name: Mega Jump, Mega Smash, Mega Quake, Super Charge, Super Charge P, Triple Dip (on its own; normally only two Double Dips give it), Lucky Start P and Auto Command (every Action Command, Mario's and the partner's, done for you). Dazzle sells them for Star Pieces. Their names and texts were never finished, so Paper Cut gives them its own; Auto Command never had an icon either and shows the Simplifier's. They sort after the other badges. Remix's badge draft ([MODES.md](MODES.md)) may offer them too, only while Director's Cut is on.
- **Boggly Woods' other enemy groups.** The area's data has a second set of enemy groups for its field enemies, Clefts with Pale Piranhas among others, chosen by a flag nothing ever sets. With Director's Cut on, the field enemies draw from that set; the flag itself isn't written to the save.
- **Field enemies with battles but no place.** Areas' battle data still has fights no map ever starts:
  - Hooktail Castle: the room with the floating blocks names its two Dull Bones "2" and "3"; a first one, with its own battle (two Dull Bones), walks the east half of the room again.
  - Creepy Steeple: two Swoopers on the stairs and two Boos in the room with the big chest (two or three Boos a fight), with the battles named for those rooms.
  - Keelhaul Key: the jungle's Green Fuzzies come in ones, twos and threes; the group of four after them in the data is picked too (as the Steeple's Boos pick one to four).
- **The level-up's confirmation box.** After choosing HP, FP or BP at a level up, "Is this OK?" (its text is on every disc) asks before the raise; No goes back to the choice.
- **The Red Buzzy family.** The game has three red Buzzies that never fight anywhere: the Red Buzzy, the Red Parabuzzy and the Red Spiky Parabuzzy, their units left only in the European release's test module. Paper Cut builds them into the Glitz Pit's module for every release, and with Director's Cut the Tiny Spinies' match (two Red Spike Tops) is the whole family. A winged one that loses its wings turns into its red grounded form (the test module's code turned it into a plain one). Goombella has a Tattle of ours for each (the disc's are a placeholder); they stay out of the Tattle Log, which never had pages for them.
- **Three lost pipe rooms in the sewers.** In the room before the Pit of 100 Trials is a pipe Mario only ever comes out of ("You can't enter from here!"). Its rooms are on every disc, maps, text and scripts, but left out of the area's map list: with Director's Cut the pipe leads down into them (a pipe that rises out of the floor, two chests with a Mushroom each) and back out.

Turning it off hides the badges from Dazzle again (badges already bought stay yours), puts the enemies and battles back as they were, closes the box and the pipe again.

## Save data

No new save flags. The restored field enemies count as defeated the way the room's others do. The sewer rooms' chests use the flags the game set aside for them (`GSWF_TIK10_CHEST_OPENED`, `GSWF_TIK14_CHEST_OPENED`, 1360 and 1361), which nothing else uses. Tattling a Red Buzzy sets the Tattle flag the game keeps for its kind (172 to 174 past the Tattle base), unused otherwise; the Tattle Log doesn't list those kinds, so its count is unchanged.

## Online and replays

The setting is one of the host's settings online already sends, and everything above follows from it and the game's own state (the badge stock, the enemies' random choice through the game's own random numbers), so both games of a session play the same.

## How it is put back

- The badges' item-table fields are changed in place while the setting is on and restored when it turns off.
- Each area's REL hands its cut battles to the port from its `_prolog` (a `TARGET_PC` block at the end of its battle database: `rels/gon`, `rels/jin`, `rels/muj`, `rels/tou2`): group lists with the cut groups added, used in place of the area's own while the setting is on (`npcSetupBattleInfo`), and field enemies, added to the map's enemy setup in the setup file's own format so the game's own code (`setupDataBase`) makes them, their battles given after.
- The sewer rooms get their map entries from a hook on `mapDataPtr`; the sewers' REL already registers their scripts. tik_06's pipe entry is changed as its script sets up its exits.

## Not restored

- **Keelhaul Key's three Goombas** (`btlgrp_muj_muj_00_01`): no Goomba is anywhere on the island and the group has no item drops; it reads as a placeholder.
- **The digest movie** (`dig_00`, Japanese and European releases): its movie, `demo.thp`, isn't on any disc.
- Test maps and test units (Goomba-model placeholders, stage stand-ins), the Hyper and Ultra Bob-omb (only Tattle rows, no unit), and two Defend badges with no effect at all.
