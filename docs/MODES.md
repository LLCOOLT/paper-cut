# Modes and streaming

Opt-in extras over the game's own battles. All are off until started or turned on, and none changes the game's logic otherwise.

The built-in item randomizer, a mode of its own for a whole new file, is in [RANDOMIZER.md](RANDOMIZER.md).

## Boss rush (Modes tab)

Every boss in story order, one fight after another, timed: Lord Crump, Blooper, Gold Fuzzy, Hooktail, the Shadow Sirens, Magnus von Grapple, Macho Grubba, Doopliss, Cortez, Lord Crump again, Smorg, Magnus 2.0, Gloomtail, Sir Grodus, Bowser and Kammy, and the Shadow Queen. Start it from the Modes tab while a file is in play, with a **loadout**:

| Loadout | |
|---|---|
| As you are | Mario as he is in the file |
| Mid-game | level 15, 30 HP, 25 FP, 21 BP, Super Boots and Hammer, every partner, four Star Powers, eight items |
| Late game | level 30, 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, every partner upgraded, all Star Powers, eight items |

The loadout is lent: Mario's stats and items are put back when the run ends or is stopped (badges stay as they are). Fleeing a fight starts it again; a Game Over ends the run. The time and the boss show at the top right; the best time per loadout is kept in `modes.txt` in the user folder.

## Boss refights (Modes tab)

The Modes tab lists the bosses in story order. You can fight any boss again once this file has beaten it in the story. Story progress decides this (`kBeatenAt` in `port/src/aurora/modes_aurora.c`).

- **The fight:** on the boss rush's map for that boss, with Mario as he is or one of the boss rush's loadouts. A loadout is lent and given back afterwards.
- **Afterwards:** winning or fleeing takes Mario back to the room the refight started in. The time shows over the picture.
- **Losing:** a Game Over, as in the story. Retry on Game Over applies.
- **Autosave:** off while a refight runs.

## Endless Glitz Pit (Modes tab)

Once the story's Glitz Pit is done (chapter 3): the Glitz Pit's teams one after another, each match with a random condition from the Pit's own set ("don't use items", "attack with Jump only"...), shown under the round. Keep it or the run ends. Each round the team is a little stronger and the enemies have 10% more HP and 5% more attack (through the Accessibility tab's custom difficulty, put back afterwards); a won match pays 10 coins and more. The best round is kept in `modes.txt`.

## How the modes work

Each fight is on a small map the mode adds to the mods' new maps (`port/mods/mods.cpp`, host maps): named for the boss's area (`tik_m0`, `tou_m1`...) and reading one of that area's maps' files, model, background and text, so the map change loads the area's REL and its battle table. The map's only script (`port/src/aurora/modes_aurora.c`) starts the battle from the table, as the area's own scripts do (a hidden stand-in NPC, `evt_npc_set_battle_info`, `evt_npc_battle_start`), and after it goes to the next map. A new map with `tou_03`'s files loads the arena's REL (`tou2`), as `tou_03` does (`src/sequence/seq_mapchange.c`).

Tests: `TTYD_MODE_TEST=rush[:loadout]` or `pit` starts a run 30 s in; `TTYD_MODE_STEP=N` starts the rush at the Nth boss.

Not done yet: the bosses' own field scenes (each fight starts straight away), Rawk Hawk (his match is a Glitz Pit story scene), Bonetail, and the Shadow Queen's first phase as the story stages it.

## Crowd Control in chat (Streaming tab)

Viewers' commands in a Twitch channel's chat drive the battle audience. Name the channel (as in `twitch.tv/<name>`) and turn on **Twitch chat drives the audience**. The chat is read as a guest (Twitch's IRC over a WebSocket, `justinfan` names): no account, no partner program, nothing is sent to the chat.

| Command | In battle |
|---|---|
| `!gift` | someone in the stands throws a good item (Mushroom, Honey Syrup, Fire Flower) |
| `!rock` | ... or a rock or a can |
| `!star` / `!drain` | a little Star Power, or a little taken away |
| `!bingo` | the Bingo cards are filled: the spin comes between turns |
| `!cheer` | the crowd cheers and jumps |
| `!leave` / `!join` | a few of the crowd leave, or a few more come |

One command per **cooldown** (5-120 s, default 20); each viewer one per three cooldowns. "Allow the harmful ones" turns `!rock`, `!drain` and `!leave` off when unchecked. Commands outside battles are ignored, and nothing happens in scripted moments (a boss's entrance). Each command taken shows over the picture for a few seconds, with the viewer's name. The commands go through the audience's own functions (`port/src/common/crowd_pc.c`, called from `BattleAudience_Main`). YouTube chat can come later.

`TTYD_CROWD_TEST=1` runs without a chat: a viewer named "test" sends each command in turn. `TTYD_CROWD_CHANNEL` overrides the channel.
