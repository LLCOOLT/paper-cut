# Super Luigi: The Marvelous Compass

In the game, after each of Mario's chapters, Luigi tells him about his own adventure in the Waffle Kingdom, and the five Super Luigi books retell it. In the game it's only text. Paper Cut makes it playable: Luigi's story as seven chapters, built on the game's own maps, characters and battles, with text of our own.

It plays exactly like Mario's chapters:
- rooms to explore, puzzles that need a partner's ability, and enemies in the field to hit first or avoid;
- coin, item and hidden blocks, Save Blocks, Heart Blocks, shops, badges, Star Pieces;
- Star Points and level-ups, a boss at the end of each chapter, and the chapter's title card and ending screen.

All of its animation work in Blender shares one style: the game's own paper-cutout look, timing and frame rate.

This is the plan, and the record of how it's built.

## Two ways in

- **From the inn:** when Luigi finishes telling a tale, a new choice, "Show me", plays that chapter. Afterwards you're back at the table, with your game as you left it.
- **The Super Luigi campaign:** a game of its own, from the launcher's Play tab and the settings page's Modes tab. It plays the prologue through Hatesong Tower in order, with its own save file, apart from Mario's. Luigi keeps his levels, badges and items from one chapter to the next, and Save Blocks save the campaign.

Chapters played from the inn unlock as Luigi tells them (the stages in `gor_luigi_talk_*`'s tags). The campaign has no locks.

## Keeping Mario's game safe

- **From the inn:** on entry, an in-memory save of the running game is made (`portCardSnapshot`, as quick resume does). On exit it's loaded back as the player's own slot (`portWarpRequestResume`). Nothing the chapter does to flags, the pouch or the party survives.
- **The campaign:** a save file of its own (`superluigi_<game ID>.gci` in the user folder), never the player's card: Save Blocks in a chapter write it instead.
- **During any chapter:** quick resume and the Gameplay+ retry slot are paused. Play as Luigi is on, whatever the setting says.

## The chapters

Luigi's telling, played straight. Each chapter ends on a short tag scene with his friend's version of what happened, which never matches.

| # | Chapter | Maps it borrows | Luigi's friend | The finale |
|---|---|---|---|---|
| 0 | The Letter | `aaa_00` (the brothers' house); a castle hall for the Waffle Kingdom | | Minister Crepe hands over the Marvelous Compass's base |
| 1 | Rumblebump Volcano | Keelhaul Key's jungle and caves (`muj_*`), tinted toward lava | Blooey joins in the town | The walking statue: throw Blooey at the red gem on its forehead |
| 2 | Plumpbelly Village | Fahr Outpost (`bom_*`) as the village | Jerry joins after | Hizza, the two-headed snake: dodge so the heads crash into each other. Luigi in a bride's veil |
| 3 | Circuit Break Island | The Pit and Glitz Pit's arena dressed as a race day (`tou_*`, `yuu_*`) | Torque joins | The race: Big Green 01 starts in reverse, and everyone else crashes |
| 4 | Jazzafrazz Town | Glitzville and its stage (`tou_*`) | Hayzee joins | The Drama Slam: Luigi plays the grass, as a stage battle with the audience |
| 5 | Rapturous Ruins | Boggly Woods (`win_*`) and a white ruin | Screamy | Cranberry, last of the Luffs, wakes, gives the piece and fades away. No battle |
| 6 | Hatesong Tower | Creepy Steeple (`jin_*`), up to its top | Blooey, burnt | The Chestnut King, then Minister Crepe; the tiara's piece shows Crepe's next move, as Luigi tells it |

The details of each map, its characters and its fights are worked out chapter by chapter, below, as they're built.

### Chapter 2 notes

- **Hizza** is two models in battle, `c_sl_hizza` (the coil and Hiss) and
  `c_sl_hizza_tail` (Zah, rising out of the floor behind Luigi), and one,
  `c_sl_hizza_whole`, for the field's scenes (docs/LUIGI_ANIMS.md, "Hizza":
  their poses, and where to put the two parts so the heads meet on Luigi).
- **Luigi in the veil** is the model `c_luigi_bride` (docs/LUIGI_ANIMS.md,
  "Luigi's bride veil"): every pose of Luigi's with the veil on. In scenes,
  NPCs use it by name. For the player, the bride section switches the
  stand-in's model in `port/src/common/luigi_pc.c` (`LUIGI_MODEL`, now
  `c_luigi_pc`) to `c_luigi_bride` (`portLuigiSetCostume`, `evt_luigi_costume`), and back after it.

## Luigi's friends

Blooey (`c_lp_gesso`), Jerry (`c_lp_bom`), Torque (`c_lp_met`), Hayzee (`c_lp_pansy`) and Screamy (`c_lp_moa`) are in the game, standing at the inn. Each has only stand, talk and idle poses. Paper Cut gives each one walk, run and jump animations, made in Blender with the Luigi animation tools ([LUIGI_ANIMS.md](LUIGI_ANIMS.md)), and shipped the same way: no disc data in the repository.

They're real partners, as Goombella and the others are for Mario: in Luigi's party menu, with a field ability, and in battle with HP, moves, action commands and upgrades as the campaign goes on. During a chapter each friend takes over one of Mario's partner slots (its field and battle entries point at the friend), and the slot is put back afterwards. Each friend gets a battle set as well as the field one: attacks, being hit, guarding, dazed, victory.

| Friend | In the field | In battle |
|---|---|---|
| Blooey | Swim: carries Luigi across lava and water gaps | Torpedo (the "White Torpedo" tackle), Ink Squirt (dizzy), Tentacle Hold (holds a foe for a turn) |
| Jerry | Blast: blows open cracked walls, like Bobbery | Lit Fuse (a thrown bomb), Big Bang (hits every foe), Short Fuse (charges the next attack) |
| Torque | Tune-up: fixes broken machines and moving platforms | Shell Slide (hits the front foes in a row), Wrench Work (heals Luigi), Spike Guard (blocks a jump attack) |
| Hayzee | Sing: makes crowds dance and sleepers stir | Lullaby (sleep), Encore (Luigi acts again), Showstopper (the audience joins in) |
| Screamy | Float: drifts over gaps and sees what's hidden | Swarm (several small hits), Scream (scares foes into fleeing), Shade (Luigi dodges the next hit) |

Luigi fights with his own jump and hammer, and his badges.

## How it's built

Super Luigi is port code, like the modes (`port/src/aurora/modes_aurora.c`), not a built-in mod: its scripts are C, in the decomp's own script macros, compiled into the game on every platform (iPhone included), and its text is set by tag (`portModsSetMessage`), so a mod can still replace any of it.

| File | What |
|---|---|
| `port/include/port/superluigi.h` | the framework's API: the chapter and friend tables, field points, battle pieces, our art |
| `port/include/port/superluigi_evt.h` | the game's script functions the scripts call, and every `evt_sl_*` |
| `port/src/aurora/superluigi_aurora.c` | entering and leaving, the campaign and its save, the chapter card and ending, the friends' slots, field points, the shared script functions |
| `port/src/aurora/superluigi_battle_aurora.c` | per-chapter unit stats, target gates, forced commands, prompts |
| `port/src/aurora/sl_assets_aurora.c` | our PNGs (textures, title cut-outs, the friends' faces) built in, as GX textures; the face icons; the card's cut-outs |
| `port/src/common/map_look_pc.c` | map looks: tint, glow or wash a room or one of its objects (`port/include/port/map_look.h`) |
| `port/src/aurora/superluigi_ch01_aurora.c` | the prologue and chapter 1: rooms, scenes, text, the two chapter tables |
| `port/src/aurora/superluigi_blooey_aurora.c` | Blooey as a partner: field, Swim, party menu, battle unit and moves |
| `port/src/aurora/superluigi_statue_aurora.c` | chapter 1's boss unit, its gem's gate, and the lava |
| `port/include/port/superluigi_sjis.h` | the game's Japanese names the scripts use (tribes, AI types), as Shift-JIS (the port's own sources aren't converted as the game's are) |

Hooks in the game, each a small `TARGET_PC` block: `rels/gor/src/gor.c` ("Show me" after a tale), `src/manager/cardmgr.c` (`cardWrite`: a save in a chapter goes to the campaign's file, or nowhere from the inn), `src/database.c` (a room on borrowed geometry without the borrowed map's setup file), `src/driver/msgdrv.c` (the chapters' own text isn't name-swapped), `src/driver/animdrv.c` (`portAnimAlias`: a borrowed move script's pose names, as the friend's own), `rels/sys/src/sys.c` (the chapter card goes on to the chapter's first room, and stands its cut-outs up), `src/driver/icondrv.c` (`portIconTexOverride`: a friend's face in a partner's icons), `src/driver/mapdrv.c` (`portMapLookTev`: a map look's stages), `src/battle/battle_detect.c` (`portBtlTargetFilter`: target gates), `src/battle/battle_seq_command.c` (`portBtlActFilter`, `portBtlWeaponFilter`: forced commands). Each has a weak no-op in `port/src/common/gameplus_pc.c` for the headless build. Elsewhere: Play as Luigi is on while a chapter runs (`portLuigiOn`), quick resume's `safe()` and the Gameplay+ retry slot are paused.

### Entering and leaving

- **From the inn:** after a tale whose chapter is built, Luigi offers "Show me!". `evt_sl_enter` makes an in-memory save of Mario's game (`portCardSnapshot`), gives Luigi the chapter's pouch (`PortSlChapter`: level, HP, FP, BP, boots, hammer, coins, items, badges, and the friends in `party`; no Star Powers), and plays the chapter card. At the chapter's end (or a Game Over, or "Back to Mario's game" on the Modes tab) the save is loaded back as the player's own slot, the card's copy of that slot untouched. Saves inside the chapter are taken and dropped.
- **The campaign:** "New campaign" (Modes tab) starts a fresh game (`stg0_00_init`, `swInit`, the name Luigi) at the prologue's card, from the title or the field. Save Blocks write `superluigi_<game ID>.gci` in the user folder (the GCI layout of quick resume's file); "Continue the campaign" loads it. At a chapter's end the next chapter's card plays and the chapter is saved as its first room comes up; after the last chapter built, the title.
- **The Modes tab** has the campaign, each built chapter Luigi has told (playable from the field as from the inn), and leaving the running chapter.
- **Tests:** `TTYD_SL_TEST=modes:campaign`, `modes:continue`, `modes:chapter:N` (what the Modes tab's buttons call) and `TTYD_SL_QUIT_AT=N` (its "leave the chapter", N frames in); `TTYD_SL_DEBUG=1` logs the battle menus' acts and moves; `TTYD_SL_TEST=campaign`, `continue`, or `chapter:N` (from the loaded save's field, as "Show me", card first), or `chapter:N@map@bero` (straight into one of its rooms, no card, the chapter's friend joined). With `TTYD_LOAD_SAVE` for the inn's way in.

### The chapter card and the ending

Both are the game's own. **The card** is Mario's chapter card, the map `sys_01` (`rels/sys/src/sys.c`): entered at `prologue` or `stg_N`, it flies in "Prologue" / "Chapter N" over the spinning medallion, with the chapter's title as its subtitle (`msg_stg_seq_stgN`, set to ours while a chapter runs), waits for a button as Mario's does, and goes on to the chapter's `firstMap` (`evt_sl_title_next`). It shows nothing else, as Mario's doesn't. A chapter's opening scene no longer calls a card: `evt_sl_title_card` is kept and does nothing.

**The first rooms (2026-11-03, from the playtest's "dropped me on the left side of a map that had a way to walk left... felt disjointed").** Each chapter's first room reads as an arrival, and no way out of it leads nowhere. A borrowed room's spare exit with no map and no exit script is disarmed by `evt_bero_pc_leads` (Luigi just walks to the room's edge); the rule now is to give such an exit an exit script that says why and sets `LW(0) = 1` (the game's own way to refuse an exit: `bero_case_init_check`), with the game's signboard (`evt_mobj_signboard`) on the path past it when it's a road.

| Chapter | First room, way in | Arrival | The way behind |
|---|---|---|---|
| 0 | `aaa_l0`, placed in the yard | Home: the mail scene | The road east runs to the room's edge: past x 440 Luigi says it's Mario's road to town and his way's the pipe (`aaa_l0_road_watch`) |
| 1 | `muj_l1`, `w_bero` (walk-in from the west) | Up from the ferry dock: the ship sound, he turns back, "there goes the ferry, next one's not for a week", then his line and Blooey | The dock's sign on the west path; walking into `w_bero` shows it and he turns back (`muj_l1_dock_back`) |
| 2 | `bom_20`, `dokan` | Up out of the well (the harbour tunnel), Blooey after him | The well's pipe leads nowhere and is disarmed; it's the way he came |
| 3 | `tou_l31`, `a_door_mon` | Black, a ship sound, the racket, the narration, the plaza's west corner | The plaza is closed: only real doors |
| 4 | `pik_l40`, `sl_arrive` (placed) | The train's bell and departure, the narration, the friends | The platform door (`n_bero`, up the station steps) says the train's gone (`pik_l40_platform`) |
| 5 | `win_l50`, `dokan1` | Out of the hollow stump with the friends | The stump's pipe is the way he came, disarmed |
| 6 | `gra_60`, `sw_bero` (then placed) | Black, the narrator, the friends shouting over the wind | The cliff's sign at the head of the way down; walking into `sw_bero` shows it and he turns back (`gra_60_cliff_back`) |

The card's spinning coin shows Luigi on Super Luigi's cards and his first two friends and the Marvelous Compass on its other faces (`art/titles/medallion/`, in place of `effect_sys.tpl`'s images 0 to 3: Mario, Peach, Bowser, the Crystal Star emblem): a texture set registered for the map name `sys_01@sl`, which `mod_game_aurora.c` gives the map-scoped sets in place of `sys_01` while a chapter runs, so Mario's own cards keep Mario.

**The ending** (`evt_sl_chapter_end`) is what Mario's chapters do when a Crystal Star is won (`evt_sub_starstone`): the stage-clear music, the curtain comes down behind Luigi, "Chapter Clear!" (the game's `stageclear` effect) and his cheer, the curtain closes, and the chapter's `diary` page (a message) sums it up. Then on: back to the inn, or the next chapter's card. A chapter's tag scene goes before it.

## The API

What a chapter or friend builder uses. Everything is registered from the chapter's or friend's own file: nothing shared is edited but a line in `portSlChapterN()`/`portSlFriendX()` (already declared and weakly defined in the framework, so a file that defines one is picked up as it's built).

### A chapter

One `PortSlChapter`, returned by `const PortSlChapter* portSlChapterN(void)` in the chapter's own file:

| Field | What |
|---|---|
| `title`, `firstMap`, `firstBero`, `tale` | the title (the card's subtitle), the first room and entrance, the inn tale's message |
| `friendId` | the friend who joins in it (in his slot from the start of the chapter; in the party from `evt_sl_friend_join`) |
| `level`, `hp`, `fp`, `bp`, `boots`, `hammer`, `coins`, `items`, `badges` | Luigi coming in from the inn |
| `party` | the friends with him coming in from the inn, a `PORT_SL_BIT(id)` each, or-ed (joined, with their `hp` and `rank`) |
| `maps` | its rooms: `{map, init, borrowed map, noSetup}`, ended by `{NULL}` |
| `text` | tag, text, ..., `NULL` |
| `stats` | its unit overrides (below), ended by `{0}` |
| `setup` | called once at start: its texture sets, target gates, hooks |
| `diary` | the ending's diary page (a tag) |

### Script functions

| | |
|---|---|
| `evt_sl_chapter_end` | the ending, then on (blocks while it plays) |
| `evt_sl_friend_join(id)` | a friend joins: in his slot, in the party, with his HP and rank; then `evt_mario_set_party_pos(0, slot, ...)` |
| `evt_sl_friend_slot(id, LW)` | the partner slot a friend takes |
| `evt_sl_enemy(name, tribe, ai, x, y, z, roamX, roamZ, "BTLNO_...")` | a field enemy as a setup file makes one (it wanders, chases, First Strikes both ways), for rooms with `noSetup` |
| `evt_sl_point(ability, id, x, y, z, radius, value, script)`, `evt_sl_point_end(ability, id, x2, y2, z2)`, `evt_sl_point_off(ability, id)` | a field ability's point in this room (`PORT_SL_ABILITY_*`, up to 8 each; cleared with every map change) |
| `evt_sl_swim_point(i, x, y, z, x2, y2, z2)` | Blooey's Swim: two shores (a `PORT_SL_ABILITY_SWIM` point) |
| `evt_sl_ability_done` | the end of a friend's field-ability script |
| `evt_sl_map_look(name, mul, add)` | a room's look: the object or group `name` (0 for the whole map) drawn as colour × `mul` + `add` (`0xRRGGBB`): a tint, a glow |
| `evt_sl_map_wash(name, rgb, percent)` | washed toward a colour: `evt_sl_map_wash(0, 0xFFFFFF, 70)` is chapter 5's white ruin |
| `evt_sl_lava_zone(x0, z0, x1, z1, top, sx, sy, sz)` | lava that burns: Luigi under `top` in the rectangle (not riding Blooey) is thrown back to (sx, sy, sz), 1 HP |
| `evt_sl_boss_arm(1)` | the next field battle is replaced by chapter 1's boss group (`Btl_UnitSetup` host hook; a chapter with its own boss does the same in its file) |
| `evt_sl_force_command(actor, act, weapon, prompt)` | the finishing blow (below); `act` -1 clears |
| `evt_sl_prompt(tag, ms)` | a prompt in the game's announcement window, over the battle or the field |
| `evt_sl_npc_fly(name, x, y, z, ms)` | a smooth flight for any NPC (the game's glide wants a tribe's NPC) |
| `evt_sl_mode`, `evt_sl_buttons`, `evt_sl_where` | inn or campaign; the buttons pressed; Luigi's position to the log (building rooms) |

### Map-scoped textures

`portSlMapTextures(pattern, folder)` (in a chapter's `setup`) makes every `tex1_` PNG under an asset folder a texture set that applies only while the map's name matches `pattern` (`"muj_l1,muj_l2,muj_l3"`; `*` and `?` work): chapter 1's `textures/ch1_lava/` turns Keelhaul Key's jungle volcanic in `muj_l1`-`muj_l3` and nowhere else. Underneath, `portModsAddMapTexture` (port/include/port/mods.h): the sets are registered with Aurora as the map changes to one they match and dropped as it leaves; they sit above every mod's global textures, and a mod's own `textures/@<pattern>/` folder above them ([MODDING.md](MODDING.md)). The PNGs under `port/assets/super_luigi/textures/` and `art/titles/`, `art/icons/` are built into the executable (`generated/sl_assets.c`, `portSlAsset`).

### Map looks

The game's map blend and object colour only multiply: an orange over a dark pool is a darker pool, which is why chapter 1's lava stayed dark. A look (`port/include/port/map_look.h`, `evt_sl_map_look` / `evt_sl_map_wash`) adds two TEV stages after an object's own material, colour × `mul` + `add`, so it can glow and wash as well as tint; an object with a look is drawn through `mapSetMaterialTev` rather than its prerecorded display list. Looks are cleared with every map change. `$TTYD_MAPOBJ_DUMP=1` lists every map object (and its group) as a map comes up, to find the names. The fog still applies after. Looks touch the map's own geometry only: Luigi, partners, NPCs, items and effects keep their colours (a 100% white wash leaves the characters as they are); a room's sky backdrop and the area's background aren't map objects and keep theirs too. `$TTYD_SL_WASH=RRGGBB,percent` washes every room, to try one out.

### Unit stats per chapter

`stats`: `{kind, hp, attack, attackAdd, defense, level}` per unit kind (`UNIT_*`), applied to each unit of that kind as it enters a battle while the chapter runs; the game's data is never changed. `hp` 0, `attack` 0, `attackAdd` 0, `defense` -1 and `level` -1 keep the unit's own. `attack` makes every damaging attack of the unit do that much (before badges and guards); `attackAdd` instead adds to each (minimum 1), keeping their spread. `defense` sets each part's normal defense (99s and immunities stay). `level` is what Star Points count.

### Target gates and forced commands

A **target gate** (`portSlAddTargetGate`, in a setup) makes a part reachable only by some moves, and only while a unit work word holds a value: the statue's gem (`{&kStatue, GEM_PART, {Blooey Toss}, UW_GEM_SHUT, 0, only = 1}`: only the Toss reaches it, only while it shows, and the Toss reaches nothing else of the statue). It filters where the game builds a weapon's targets, so the menu greys a move with nothing to hit.

A **forced command** (`portSlForceCommand(actor, act, weapon, prompt)` or `evt_sl_force_command`) is the finishing blow: on that actor's turns (0 Luigi, 1 the partner) only `act` (`PORT_SL_ACT_JUMP`, `_HAMMER`, `_PARTNER`, ...) is offered in the act ring and only `weapon` in its list (the rest greyed, the cursor on it, as the game's tutorial battles do), with `prompt` shown in the game's announcement window while he picks. It lasts until cleared or the battle ends. For a defensive moment (Hizza's "Lunge left!"), `evt_sl_prompt` shows a prompt for a time while the builder's own action command runs.

### A friend

One `PortSlFriend`, returned by `const PortSlFriend* portSlFriendX(void)` in his own file (Blooey, Jerry, Torque, Hayzee, Screamy):

| Field | What |
|---|---|
| `id`, `partyId` | `PORT_SL_FRIEND_*`; the Mario partner slot he takes (ch2.md's conventions: Blooey 1 Goombella, Jerry 3 Bobbery, Torque 2 Koops, Hayzee 5 Flurrie, Screamy 6 Vivian) |
| `name`, `model`, `menuText`, `menuPose` | his name (it replaces the slot's in the party menu, the battle and the partner switch), his model, the party menu's line, its pose |
| `fieldPoses` | 15 pose names, `partyPoseTbl`'s columns |
| `fieldUse`, `fieldPost` | the slot's use and post functions while he's in it: `fieldUse` calls `portSlFieldUse(party, script)`, which runs his ability's script with the field held and hands him back when it calls `evt_sl_ability_done`; the script finds its point with `portSlPointNear(ability, x, z, &end)` and runs it with `portSlPointRun` |
| `battleUnit` | makes his `BattleUnitKind` (the slot's unit type, his parts, pose table, and a unit data table whose tag 1 is his move menu) |
| `menuMoves` | the party menu's move list |
| `iconMenu`, `iconHud` | his faces (`art/icons/<friend>/<friend>_menu.png`, 40x40, the menus' full-body icon, and `<friend>_hud.png`, 48x48, the HUD sticker), drawn at the game's own icon sizes wherever the game draws the slot's partner's face (the HUD, the party menu, the battle's partner command and switch), through `icondrv.c`'s `portIconTexOverride`; a round face in his colour if the art is missing. `portSlFriendIcons(id, menu, hud)` / `evt_sl_friend_icons` swaps them for a while (chapter 6's sooty Blooey: `blooey_soot_menu.png`, `blooey_soot_hud.png`).|
| `hp`, `rank` | as he joins |
| `text` | his own text |
| `swapped(on)` | optional, after he's put in or taken out |

The framework swaps every friend wanted (the joined ones, the chapter's `party`, its `friendId`) into his slot as the chapter starts and puts the slots back as it ends: `partyDataTbl`, `partyPoseTbl`, `entryunit_party`, `winPartyDt` and the slot's name texts.

### Rules learnt building them

An NPC entered with `evt_npc_entry` needs `evt_npc_set_tribe` before it can be talked to, and `evt_npc_change_interrupt(name, 6, script)` for its talk (`evt_npc_setup` wants a tribe's NPC); a room's bero table must never be empty, and an exit with no `nextMapName` is shut; `evt_mario_key_onoff` counts, so every `0` needs its `1`; a message printed fresh must not start with `<p>`; Mario faces only left and right, so a `CASE_COND_FRONT_A` object is approached from the side; a big chest (`evt_mobj_itembox` kind 1, as every chest) opens only from its front face (`mobjHitEntry` type 2: the front face is the A-able part, the other five are plain wall), so place it where Luigi walks up to it, or walk him round in the scene; `evt_npc_change_interrupt` takes its script as written (`PTR(script)`), not a variable's value, as the game's own calls do; a script run from a map object's event (a chest's, a block's) runs with the field held at system level 1, so a script it starts with plain `evtEntry` stays stopped: start it with the caller's type (`evtEntryType(code, event->priority, 0, event->type)`, as `evt_sl_chapter_end` does).

**Entrances.** A room entered by a name that isn't in its bero table (aaa_l0's `sl_start`, a test's `@nowhere`) is entered through the game's `bero_null`: Luigi is put at the map's origin with no walk-in, and `bero_case_entry` still arms every exit (it doesn't depend on the entrance); the scene that follows places him. Name an entrance that's in the table when the room has one.

**Field abilities.** A friend's ability script runs (type 26) with the field at system level 1 and the partner in his use state (`portSlFieldUse`, until `evt_sl_ability_done`), by design: the partner's own motion code isn't running, so the game's partner moves (`evt_party_move_pos`, `evt_party_run` and the like) wait on it and never finish, and any script the ability starts with plain `evtEntry` is stopped by the system level. Inside an ability, move the friend with `evt_sl_npc_fly` or by setting his position, run helpers with `CALL_CHILD` (or `evtEntryType` with type 26), or call `evt_sl_ability_done` first. `evt_sl_ability_done` itself puts the field back at system level 0 and the friend back to following: Swim's own partner moves took Blooey out of his use state, his use function was never called again, and the field stayed at level 1 after every Swim, with every exit, chest and sign in the room dead (case events and map objects need level 0). That was the chest after the statue too, for a player who'd swum in dou_l1.

### Flags

Super Luigi's story switches are plain GSWF from a bank the game never touches. The game's table is `GlobalWork.gswf[0x100]`, flags 0-8191, saved with the file; the game's own end at 5666 (`GSWF_AJI08_STAR_PIECE`), and the computed ones stay below it (the Tattle Log 4474-4689, the trouble work 5405 on, badges and recipes in the low hundreds; scanned over `rels/`, `src/` and `port/`). So 7000-7799 is ours: chapter N's 100 at 7000 + 100 N, the campaign's own at 7700 (`port/include/port/superluigi.h`; static asserts in `superluigi_aurora.c` that it fits the table and stays above the game's).

| | |
|---|---|
| `SL_FLAG(chapter, n)` | the switch, for `swSet` / `swGet` in C |
| `SL_GSWF(chapter, n)` | the same as a script variable: `IF_INT_EQUAL(SL_GSWF(2, F_X), 0)`, `SET_VALUE(SL_GSWF(2, F_X), 1)`, a block's, chest's or item's flag argument |
| `SL_FLAG_CAMPAIGN(n)` | the campaign's own (the bank's chapter 7) |

`n` (0-99) and the chapter (0-7) are checked at compile time: an `n` out of range is an array of negative size. Being GSWF they're saved in the campaign's file and survive its Save Blocks and Continue; from the inn they live in Mario's running game until the snapshot taken at entry is put back, which drops them. A chapter keeps its flags in an enum and a short macro (chapter 1: `#define SLF(n) SL_GSWF(..., n)`). Tests set them with `TTYD_SL_FLAGS=7115,7117` (with `TTYD_SL_TEST=chapter:N@map@bero`). GSW bytes 2000-2002 (the game's own end at 1474 of 2048) hold the campaign marker, the chapter and the friends who joined. A chapter's own story words (GSW bytes, 0-255) come the same way, four each: `SL_WORD(chapter, n)` for `swByteSet`/`swByteGet`, `SL_GSW(chapter, n)` in a script (2008 + 4 N + n; chapter 3's race keeps its practice pay-outs in `SL_WORD(3, 0)`).

Map names: `<area>_l<chapter><n>` (`aaa_l0`, `muj_l1`). The plans' `<area>_<chapter><n>` collides with the game's own maps from chapter 1 on (`muj_10`-`muj_12`, `dou_10`-`dou_13`, `tou_10`...), so the `l` stays. Text tags: `sl<N>_*` for a chapter, `sl_<friend>_*`, `sl_*` shared; the plans' `sl_c<N>_*` works the same (any `sl` tag is ours and isn't name-swapped).

Sounds and music are looked up by name, and a name the game doesn't have plays nothing, silently: `python tools/port/sl_sounds.py` lists every `SFX_`/`BGM_`/`ENV_` name in `superluigi_*.c` that `src/pmario_sound.c` lacks (run it before committing a chapter). A friend's `menuText` is also his line in the battle's partner switch (`btl_hlp_cmd_operation_partyN`).

### Blooey's moves

His own move scripts (`superluigi_blooey_aurora.c`), each on one of the game's action commands, the model for the other friends':

| Move | FP | Command | What |
|---|---|---|---|
| Torpedo | 0 | the charge gauge (`SetupAC` 19, Koops's Shell Toss): hold left, let go on the star | winds up (`A_1A`), shoots head first at one foe on the ground (`A_1B`), bounces off and rights himself (`A_1C`), a stylish window on the rebound. 1/2 at normal rank, 2/3 Super, 3/4 Ultra (failed/succeeded) |
| Ink Squirt | 3 | mash A (`SetupAC` 5, Goombella's Rally Wink) | a breath (`A_2A`), then a squirt of the Blooper's black ink (`A_2B`) over every foe on the ground: dizzy 2 turns on a full gauge, else a 50% chance of 1. No damage |
| Tentacle Hold | 2 (Super rank) | a timed A (`SetupAC` 1) as the tentacles close | lunges (`A_3A`), squeezes (`A_3B`), lets go (`A_3C`): 1 damage, and on time the foe can't move for a turn |
| Blooey Toss | 0 (the statue's fight) | as Torpedo | the Torpedo high in an arc onto a part no jump reaches (the gem): 2/3, 3/4, 4/5 |

The statue's fight: the gem's target gate opens on its resting turns; once one Toss can end it (5 HP or less with the gem showing) the Toss is a forced command, "The gem is cracking! Finish it: Blooey Toss!". The statue has 12 HP; its stomp does 2, twice a turn once it's angry. His Swim plays `F_1`.

A chapter's own battle group needs heart and flower drop tables (`battle_heart_drop_param_default`, `battle_flower_drop_param_default`): the game reads them unchecked when the battle ends. The framework fills in the defaults for a group without them.

## Chapter 0: The Letter

| Room | Borrowed | What happens |
|---|---|---|
| `aaa_l0` | `aaa_00`, the brothers' house | The title card. Luigi alone ("left behind again"); a mail Paratroopa flies in and drops a letter; Luigi reads Minister Crepe's plea and leaves Mario a note. The pipe out back now goes to the Waffle Kingdom. Save Block in the yard. |
| `gon_l0` | `gon_04`, a castle hall, warmed (map blend) | Minister Crepe (`c_kino_ji`) and two guards. The Marvelous Compass, its seven parts and the tiara; the compass base glows and points south. The ending card. |

**The plan (2026-11-02, from the Mac playtest: "The prologue is just some text").** A real playable opening of 30 to 45 minutes, as Mario's is, on rooms no other chapter borrows:

| Room | Borrowed | What it's for |
|---|---|---|
| `aaa_l0` | `aaa_00` | Home: the mail, Luigi left behind, things to look at. The yard's pipe. |
| `hei_l0` | `hei_00` | The Waffle Road: Luigi comes up out of a real pipe, music changing in step. A cadet Toad teaches fights: the first field Goombas, Jump, Hammer and the commands; a ? block, coins, a Save Block. |
| `nok_l0` | `nok_00` | Griddle, a town: people with Luigi's sort of humour, a stall, an inn, a Heart Block. It tells of the princess and the minister. |
| `hei_l1` | `hei_01` | The Maple Wood: exploring, hidden finds in the grass, a blue switch and its bridge (a badge across it), more fights, first level. |
| `gon_l1` | `gon_00` | The castle's gate: "So this is the Waffle Kingdom", said outside looking up; a guard, a Save and a Heart Block. Luigi walks in through the door himself. |
| `gon_l0` | `gon_04` | The hall: Crepe, the compass base. Then something goes wrong: the Chestnut King's raiders (Red Bones and his Dull Bones, the area's own fight) burst in for the base. Crepe stays oddly calm, and knew a little too much. The ending. |

## Chapter 1: Rumblebump Volcano

| Room | Borrowed | What's in it |
|---|---|---|
| `muj_l1` | `muj_01`, the harbour town | The title card. Blooey heard Luigi's "sea-monster screaming" and runs up; he joins. The town's shop (our goods), its inn clerk, a Save Block. East: the jungle. |
| `muj_l2` | `muj_02`, jungle | Green and Flower Fuzzies (the area's own battles). A badge block (Power Smash), a ? block (Fire Flower), a Star Piece. |
| `muj_l3` | `muj_03`, cliffs | Putrid Piranhas and a Flower Fuzzy. A coin block, a hidden block (Super Shroom), a Heart Block, a badge block (Close Call), a Star Piece. |
| `dou_l1` | `dou_00`, the volcano's mouth | Lava light: a breathing orange map blend, red fog, the pool as lava that burns; Embers. Save and Heart Blocks, a Honey Syrup. Blooey's Swim crosses the lava to the ledge and the way on. |
| `dou_l2` | `dou_13`, the statue's hall | The statue wakes and steps forward; the fight; it shatters; the chest holds the second compass piece, which points west. Blooey's version ("I hurled LUIGI at the gem"). The ending card. |

The boss, the walking statue (`c_lp_statue`, the friends' animation work): 20 HP (Hooktail's), two parts, played like one of Mario's bosses: hit the armour to open the weak point. The stone body has defense 1, so Luigi's blows do less (a jump's stomps nothing, a hammer with its command 1, a Power Smash or a Fire Flower more); the red gem has none but can't be reached by jumps, hammers or shells, so only Blooey Toss hits it, and only while it shows. The statue alternates a resting turn (one stomp, then it rests with the gem aglow, `A_3A`, open until its next turn) and a guarding turn (the arm over the gem, a stomp; two once it's down to a third of its HP, with a line when it gets angry). A solid blow on the stone while the gem's covered staggers it (`statue_struck`: a hit that still does damage after the defense, or a jump's second stomp, both landed with the command): it reels (`X_1`), drops its guard, the gem shows at once ("The statue staggers!", and Blooey's "Quick, throw me at the gem!" the first time), so Luigi first and Blooey after gets a Toss in on a guarding turn too. A Toss on the gem reels it back (`D_2`, the gem flashing); at a third of its HP it stands cracked and wobbling (`K_1`, the eyes failing). Once one Toss can end it (4 HP or less, the gem showing) the Toss is the forced finishing blow. The stomp is a jump-like 3, guardable (2 guarded); angry, two of 2. Its level is 60, so it gives Star Points as Hooktail does (about 30 to a level-2 Luigi: a level). Measured against Hooktail (20 HP, bites of 5 and 4) and the Mario who meets it (the save library's "Hooktail Castle Before Hooktail": level 2, 10 HP): a stomp takes a fifth of a level-2 Luigi's 15 HP, a bite half of that Mario's, so the statue is the gentler of the two, as a first boss with a new partner should be.

## Chapter 2: Plumpbelly Village

Files: `superluigi_ch02_aurora.c` (rooms, scenes, text, the disguise rule, the
chapter table), `superluigi_jerry_aurora.c` (Jerry), `superluigi_hizza_aurora.c`
(the formations, Sergeant Bulkhead, Hizza). Flags: the bank's chapter 2, `SL_GSWF(2, n)` (GSWF 7200-7299).

| Room | Borrowed | What's in it |
|---|---|---|
| `bom_20` | `bom_00` | Luigi and Blooey pop out of the well (the chapter's entrance is its pipe); Save Block, coin block, hidden Star Piece, a flip panel; the pearls (Blooey's Swim at the well: a swim point with a script is a dive, he goes alone); the dawn and Jerry's tag |
| `bom_21` | `bom_03` | Goombas and an Ice Puff; blocks, Ice Smash in a hidden block; the hollow pine's packed ice (Blast): Shine Sprite and the Snowmoth Silk |
| `bom_22` | `bom_04` | the gate soldier; a Frost Piranha and Ice Puffs; Fire Flower, Heart Block, HP Plus in a hidden block |
| `bom_23` | `bom_01` | the square's six villagers (three moods), the banner over the dome, the feast, the proposal, the voice |
| `bom_24` | `bom_02` | the mayor's house (White's), Jerry at its door, the Blast tutorial (ice wall before the shop), the tailor's corner and Plumpbelly Provisions (ie2), the Full Belly inn (our words), Old Bertha's pie salute, the Bridal Road |
| `dou_20`-`dou_30` | `dou_00`..`dou_13` | the Hollow, washed cold; Hissguards (the disguise rule: run, jump or hammer in a guard's cone and he charges, his first strike); jump stands, Swim crossings, cracked ice nooks, the frozen winch and the water gate, the Super Boots, the side wall (Blast) to Sidewind Passage, Bulkhead on the bridge, the bomb wall to the den |
| `gon_20` | `gon_11` | Hizza's den: the heap, six brides in the cage, the faint, the fight, the piece |

## Chapter 3: Circuit Break Island

Built from docs/super_luigi/ch3.md: `superluigi_ch03_aurora.c` (rooms and scenes), `superluigi_torque_aurora.c` (Torque), `superluigi_revvo_aurora.c` (the Stadium, the bouts, King Revvo), `superluigi_race_aurora.c` (the Grand Prix, Kart Mode, the small scene pieces), `superluigi_ch03_text_aurora.c` (generated by `tools/port/sl_ch3_text.py`), `port/include/port/superluigi_ch03.h`. Flags: the bank's chapter 3, `SL3F(n)` = `SL_GSWF(3, n)` (GSWF 7300-7399).

| Room | Borrowed | What's in it |
|---|---|---|
| `tou_l31` | `tou_01` | Victory Plaza: the arrival (the plan's cliff station on tou_00 has no floor: its sign, Save Block and hidden Star Piece block are at the plaza's west end), the awards, Charlie's Parts & Snacks, fans, blocks, a Star Piece panel; later the crowning (Luigi's crown), the voice, Torque comes along, Torque's tag |
| `tou_l32` | `tou_02` | Race HQ: Checkers (sign-up, Timing Tutor after Bout 1, the stamp), the reporter, the Heart Block; Revvo's and the racing posters (texture sets) |
| `tou_l33` | `tou_03` (tou2) | The Stadium: the bouts in the ring, a Star Piece, the track gate once Big Green 01 is built |
| `tou_l34` | `tou_04` | Pit Lane: Torque joins after Bout 2, the jammed shutter (Tune-up tutorial), Lug and Nut after Bout 4, crews; the Suite's door locked till the Suite scene |
| `tou_l35` | `tou_05` | Gasket's office: the bouts' desk, the vent (Tune-up) to the duct |
| `tou_l36` | `tou_06` | Parts Depot: the hoist (Tune-up) up onto the right-hand containers, where the Spark Plug glints in an open crate; the left-hand container (Blast, Shine Sprite); a Super Shroom block (tou_06's floor is x -190 to 190: everything is on it) |
| `tou_l37` | `tou_07` | The King's Suite: the Steering Wheel and Revvo in a bathrobe, FP Plus, a hidden Star Piece |
| `tou_l38` | `tou_08` | Team Revvo's garage: red karts, drivers, the locker (Spin Jump) with Spike Shield |
| `tou_l39` | `tou_10` | Torque's garage: meeting him, the build, the night before, his bench (Thunder Rage), a Save Block |
| `tou_l40`, `tou_l41` | `tou_09`, `tou_11` | Garage Row B (the Blastards, coin blocks, the lockers Jerry blasts) and the Hidden Bay (Shine Sprite, the Turbo Muffler, a Star Piece) |
| `tou_l42`, `tou_l43` | `tou_12`, `tou_13` | The Back Lot (the crane lifts the giant block off the Gearbox; Multibounce) and the Air Duct |
| `yuu_l30`, `yuu_l31` | `yuu_00`, `yuu_02` | The Start Grid (practice; the reverse start, the pile-up, the scoreboard, the pit stop) and the Circuit (Kart Mode, the Lone Lap) |

## Chapter 4: Jazzafrazz Town

The plan is [super_luigi/ch4.md](super_luigi/ch4.md); the files are `superluigi_ch04_aurora.c` (rooms, scenes, text, shops), `superluigi_hayzee_aurora.c` (Hayzee) and `superluigi_slam_aurora.c` (the Choir and the Drama Slam). Flags: the bank's chapter 4, `SLF4(n)` = `SL_GSWF(4, n)` (GSWF 7400-7499).

| Room | Borrowed | What's in it |
|---|---|---|
| `pik_l40` | `pik_00`, the station plaza | The arrival (Blooey, Jerry and Torque as NPCs, the dancing Dayzee); a singing porter, a posh couple; Save Block; a hidden block (Star Piece); the station clock (page 3, Torque's Tune-up). |
| `pik_l41` | `pik_04`, Limelight Lane | The poster; the Lane Chorus on the studio's steps (Sing tutorial: Shake a Leaf, they conga off); the planter (page 2, Jerry's Blast); coin blocks, Honey Syrup block; the Pianta busker (3 tips: Super Appeal). At night: the fans (paper grass, the "WE ♥ THE GRASS" banner), the voice, Hayzee comes along, his version (the tag). |
| `pik_l42` | `pik_01`, the square | Hayzee at the fountain (he joins); Curtain Call Sundries (a list shop); the Society's clerk (the inn, 15 coins; after the show, the patron's Shine Sprite); the fountain (page 1, Blooey dives); the sleeping Dayzee doorman (page 4, Rise and Shine); the awning's spring (page 5); Heart Block, Star Piece; the pipe to the hills. The theatre's door opens with the casting. |
| `pik_l43` | `pik_02`, the long hall: the Grand Dramalama | The usher; a stagehand at the side door (backstage from the casting); the orchestra pit's hidden block (Sleepy Stomp). Slam night: the MC and the rivals, the huddle, the costume, the stage battle, the verdict, the review's reward, the after-party. |
| `pik_l44` | `pik_03`, washed blue-grey | The Super Hammer's big chest; two stone blocks (Lv1BigBig: the charged swing), Quake Hammer chest; coin blocks over a crate spring; Bruno at the corridor door (Shake a Leaf). |
| `win_l40` | `win_04` | Hayzee's studio: the casting, the three problems; Save Block, piano, hidden block (Star Piece). |
| `win_l41` | `win_05` | The dressing room: the bed's spring up to the canopy, the Fiery Hat (`c_sl_fiery_hat`), Shine Sprite, Dazzlina's entrance; the vanity's chest (Happy Flower). |
| `gra_l40`-`gra_l45` | `gra_00`-`gra_05` | The hills: the sign, Save Block and Old Thimble at the shed (he hears only songs: his shop, Lucky Day after the Choir); field enemies (the area's own battles, ch4's levels); Super Shroom block, the stone and FP Plus; the pond's stump spring, Shine Sprite, Heart Block; Dayzee Dell's Choir (the mid-boss); the Snoring Woods: Petunia (Rise and Shine), the echo stone (Echo), gra_05's own way past the boulder (below), Peekaboo past it. |

## Chapter 5: Rapturous Ruins

`port/src/aurora/superluigi_ch05_aurora.c` (the rooms, scenes, text) and `superluigi_screamy_aurora.c` (Screamy). Flags: the bank's chapter 5, `SLF5(n)` = `SL_GSWF(5, n)` (GSWF 7500-7599). The plan is `docs/super_luigi/ch5.md`; its map names are `<area>_l5<n>` here.

| Room | Borrowed | What's in it |
|---|---|---|
| `win_l50` Grimble Edge | `win_06` | Luigi hops down off the hollow stump (once the pipe's done); the four friends stand round him for the arrival (beat 1). Save Block; a hidden block (Star Piece) that Look shows. At the end, night: the dark wood, the compass and the voice, Screamy's version (beats 12-14), Chapter Clear. |
| `win_l51` Hushed Clearing | `win_00` | Old Mossbeard (Petalburg's bearded old mayor, `c_nokoson`, greyed) and his Buttons trade (Maple Syrup; a Shine Sprite, or Jammin' Jelly if the Glimmerfuzz's was taken; Feeling Fine). Dark Puffs, a Cleft. ? block (Honey Syrup), coin block. The dark pond: Blooey's Swim there is a dive (a swim point with a script) and he brings up Button 1. |
| `win_l52` Old Root Path | `win_01` | A Super Hammer stone (`MOBJ_Lv1Block`) in the hollow stump's mouth: the stump is a pipe to the ledge behind, and Defend Plus on it (the ledge is too narrow for a chest's front). The game's crow (`c_f_karas`) asleep on Button 2, in front of the hedge: Hayzee's Rise and Shine wakes it (checking it says so). Pale Piranhas, a Dark Puff. |
| `win_l53` Thorn Walk | `win_02` | A Dark Bristle (`c_togedaruma_b`) curled asleep across the path: Jerry's Blast throws it off into the trees; coin blocks, a chest (Flower Saver), Button 3. Heart Block. Pale Piranhas, a Cleft. win_02 flew to its far bank by a plane panel: a blue spring at the bank's foot (480, 0, 0) takes Luigi up; the pillar's pipe pair stays as the game has it. |
| `win_l54` Grey Stumps | `win_03` | The old house on the hill, where the compass points. The Luff signpost (`c_sl_signpost`, x2.5, Tune-up): it points, the stump opens (Star Piece, 20 coins). Save Block. The stump pipe goes up to the hill path; A at the house's door (as win_03's own) goes in, to the White Gate. win_03's hidden hole and its wall stay hidden, as its own init has them. The pipe stands behind the hedge and the hill path has a break no jump clears (win_03 has Mario through them in forms Luigi lacks): the game's red springs leap the hedge (in at (-67, 0, 85), out at (-20, 0, -80)) and the break (-25 and 75 on the path), each way. |
| `hei_l50` White Gate | `hei_07` | The Sentinel's three questions (the quiz statue, `BGM_EVT_QUIZ2`, our choices; a wrong one drops Clefts for a battle); passing slides the statue aside and raises the pipe down. Look: a hidden block (Star Piece). |
| `tik_l50` Sleeper's Chamber | `tik_05` | Cranberry asleep on the dais (`c_sl_cranberry`); Luigi walks down the steps to the dais, Screamy's shriek and his joining (beat 5); the tutorial (Float up to the west ledge, Look shows the real door, walked through once shown). The three sockets (`c_sl_socket_*`): each lens set, its beam rises; the first opens the great door (to the White Lake) and brings a Heart Block; the third, the sneeze, Cranberry, the piece and his fading (beats 10-11). Save Block. |
| `hei_l51` Hall of Still Water | `hei_10` | ? block (Super Shroom); Look: a hidden block (Shine Sprite). Fuzzies. |
| `hei_l52` Gallery of Murals | `hei_09` | The six murals (`c_sl_mural` in the bays, 1 to 6 from the door in); Screamy reads them; the cracked sixth (Blast) opens on a chest (Refund). A Bald Cleft, a Fuzzy. |
| `hei_l53` Moon Room | `hei_08` | The gear-gate (`c_sl_gear_gate`): Tune-up cleans the gear, then four pushes of A in a row raise the gate (it slips back if he stops); the Moon Lens. Look: a Star Piece in the floor. A Cleft. |
| `hei_l54` Long Echo | `hei_11` | Three echo stones: each sings a phrase (A-A-B-A, X-B-A-Y, Y-X-Y-B-A) and Luigi sings it back on the buttons (Hayzee's lines when she's out); the stones are Luff stone stands (the sun socket, empty) that light their beam when sung back; a gate of three bars of light (`c_sl_look_edge_100_v`) holds him back, one bar fading per stone; the chest beyond (FP Plus). Coin blocks. A Bristle, a Bald Cleft. |
| `hei_l55` Sun Room | `hei_12` | The pedestal and the Sun Lens (the floor's flat: no chasm); the Glimmerfuzz (a Gold Fuzzy, the area's own battle with its Horde) on the Shine Sprite; a chest (Damage Dodge). |
| `tik_l51` White Lake | `tik_02` | Deep cold water (a drop back to where he set out); Float (or Blooey's Swim) from the shore to the island; Look there shows a line of old stone blocks (`MOBJ_Lv3Block`, unbreakable at Luigi's hammer) to the far shore, a Star Piece on the way; Float from the far shore to the pipe's ledge. |
| `tik_l52` Room of Nothing | `tik_15` | Washed white, no music. Look lifts the white for 8 seconds and lights the edges (`c_sl_look_edge_200`); the first Look shows the chest (Shine Sprite), a Star Piece and the white stone in the way (hammer it). Float up to the walkway (its floor at 125); the Tear Lens at its end. In by tik_15's west door, `w_bero_2`, on the floor (its east way is over the water and threw him about, 2 HP). The east stairs reach the walkway without Float; the lens wants Look first. tik_15's own pipe in the alcove under the walkway is left unarmed. |

## Chapter 6: Hatesong Tower

The plan is [super_luigi/ch6.md](super_luigi/ch6.md); the build is three files: `superluigi_ch06_aurora.c` (the rooms, scenes, the friends' puzzles, the chapter table), `superluigi_ch06_boss_aurora.c` (the formations, the Wailer, the Chestnut King, Minister Crepe and the Nightmare, Future Sight's overlay) and `superluigi_ch06_text_aurora.c` (every line, ours). Flags: the framework's bank, `SL_GSWF(6, n)`. Coming in from the inn: level 16, all five friends in the party (each who's built).

| Room | Borrowed | What's in it |
|---|---|---|
| `gra_60` | `gra_06` | Arrival in the dark (the narrator, then the five friends shouting over the wind), the card. Save Block, Heart Block, a hidden Star Piece. At the end: the goodbyes at dawn, Eclair and King Marron by the door. |
| `jin_60` | `jin_00` | The King's ambush (a scene camera keeps him in frame); his goo eats the middle of the hall (`c_sl_goo_pit` tiles filling exactly the rectangle that burns: the lava zone). Two Boos. The hall's doors on, the stair door and the Crown Door, are on its gallery (y 200), which the ground floor doesn't reach: both chapels' switches raise a spring at its back (`HALL_SPRING_*`) up to it. A Heart Block in the open just inside the west door (-450, 60, 0), beside the Save Block, so FP can be refilled between the chapels. The Crown Door (the Steeple Key's lock; the key is named the Crown Key while the chapter runs) with the last Save and Heart Blocks; the Vault's opening in the back wall, ground floor (Look). |
| `jin_61` | `jin_01` | Torque's Tune-up frees the rusted-down switch. Swoopers, Jammin' Jelly, a hidden HP Plus. |
| `jin_62` | `jin_02` | Six Boos in a line; Hayzee sets them dancing: the switch, the Fire Drive chest. Boo and Buzzy, two coin blocks. |
| `jin_63` | `jin_03` | The kururin tile, a Star Piece; the Wailer (the area's Atomic Boo battle, tinted, with Hatesong). |
| `jin_64` | `jin_05` | The Vault: Power Plus, Ultra Shroom, a Star Piece, Shine Sprite #1. |
| `jin_65` | `jin_06` | Jerry blasts the cracked wall (the room's peel-away panel, the way on); Last Stand hangs over the spring, which throws Luigi through it; Spiky Buzzies. |
| `jin_66` | `jin_07` | The Choir: one torch burns, three are dark. Hayzee's Echo at a dark torch: the lit one hums its phrase (the Echo's pitches), Luigi sings it back (A-B-A-Y, X-X-B-A, Y-A-X-B-B); all lit opens both doors. Shine Sprite #2, a Star Piece. The borrowed room has four torches, not the plan's six. |
| `jin_67` | `jin_08` | The parrot's four lines (Crepe's), then the Crown Key. It sits on its perch above the game's talk check, so an unseen twin at the perch's foot takes the talk. Mega Rush, Maple Syrup, a hidden Star Piece. |
| `jin_68` | `jin_11` | The door's side reaches the stairs only by a slot 20 high at x 70 behind the black pillar (Tube Mode's in Mario's game): pushing into it takes Luigi through, either way (`jin_68_slot`), and the roof over the stairs is off (`a_gawa04`). The rotten floor at the room's end (it has no far side): Screamy's Look from the stairs' top sees the shaft and a ledge below; he floats Luigi down to it (Shine Sprite #3) and on to the Winch Gate. Walk onto it without him and it gives way: the same room, no Shine. Save Block, Super Shroom. |
| `jin_69` | `jin_10` | Torque frees the winch; the gate slides. A fall from jin_68 lands on the slope past the gate. Tornado Jump. |
| `jin_70` | `jin_09` | The goo moat between the way in and the spring: Blooey swims it and is burnt from here on (`SOOT_*`, his field row and battle table); the spring (moved off the moat) straight up its shaft to the hall's gallery. Life Shroom, coins. |
| `jin_71` | `jin_04` | The King's fight; the dots; "STOP!"; the truth; Crepe; his fight; the curse breaking (`c_sl_marron`); the compass, the choice, the seven lights. |
| `aaa_60` | `aaa_00` | Home at night, the armchair, the dream (each friend fades in and out, then the figure in cream and gold), the tag (Blooey, burnt), the ending. |
