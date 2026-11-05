# Modes and streaming

Opt-in extras over the game's own battles. All are off until started or turned on, and none changes the game's logic otherwise.

The built-in item randomizer, a mode of its own for a whole new file, is in [RANDOMIZER.md](RANDOMIZER.md). Versus battles (Mario's side against Luigi's, two players on one screen, in the Glitz Pit's ring; the Start menu's Settings tab) are in [VERSUS.md](VERSUS.md).

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

## Chapter select (Modes tab)

Once a file has finished the game (saved back in Rogueport after the credits), any chapter's story can be played again with the file's Mario: his level, badges and items. The chapter starts at its title card, as the pipe to it does.

- **The start:** the file's story state is kept (the saved words and flags, the area's own, and the key items, partners and Star Powers the story gives); the story is set to the end of the chapter before, and the chapter's areas' flags are cleared (`port/src/aurora/chapters_aurora.c`, the flag blocks of 150 per area).
- **The end:** when the story reaches the chapter's last step (on the way to the next chapter), the kept state is put back and Mario returns to the room he left. Levels, items, badges and coins won stay. **Leave the chapter** ends it early the same way.
- **Saving during it:** the kept state is also in `chapter_replay_<slot>.bin` in the user folder, so a save made during the replay goes on with it when loaded.

## Remix modes (Modes tab)

Settings that change play, so they're in settings presets, challenge codes and replays:

| Setting | |
|---|---|
| Remix: enemies | The fight behind each field enemy is drawn from all its area's ordinary fights (`npcSetupBattleInfo`'s setup swapped). Bosses and scripted fights (no escape, losing allowed, no Jump or Hammer...) and the Glitz Pit's and the Pit of 100 Trials' stay. The same for a room, an enemy and the seed. |
| Remix: badge draft | At each chapter's title card, three badges to choose one from (left and right, A). The card waits for A anyway; that A takes the badge. |
| Remix seed | 0-99999: the same seed, the same remix. |

**Hardcore:** with "The next new file is a hardcore file", the next new file is marked (GSW 2040-2042) and has one life: no Retry on Game Over, no save state loads, and its Game Over ends it for good (its id goes in `hardcore.txt`; loading it again goes back to the title). **Restart and record a hardcore run** starts the game again at the title, recording a replay; the Game Over saves it, a ghost to race.

## Pit of 100 Trials in co-op (Modes tab)

A run down the Pit from its first floor with a second controller on the partner: Multiplayer > Second player is turned on for the run and put back after (`port/src/aurora/pit_coop_aurora.c`). The start is the Pit's own (the floor counter to 0, the pipe's map change to `jon_00`); the floor and the time show over the picture. The run ends when Mario leaves the Pit (its pipe home or the way out after the 100th floor), at a Game Over or at the title; he then goes back to the room he started from if he isn't in Rogueport already. The deepest floor and the best time to the bottom are kept in `pit_coop.txt`. It opens once the file has found the Pit, or after chapter 2. In online co-op both players take the Pit's pipe together instead (one game alone can't jump).

## Trick practice (Practice tab)

Warps to set-ups: a room with the story where it is for a known trick or fight (the decomp's story names), and the player's own spots, kept in `tricks.txt` in the user folder, a line each: `name|map|entrance|story|x y z` (the entrance and the place may be empty). **Keep this spot** adds where Mario is. A warp sets the story, changes map, and puts Mario at the spot's place once the room is in; it counts as practice (no achievements until a file is loaded). Not during a replay or online co-op.

## Splits for LiveSplit (Practice tab)

Each attempt of the speedrun timer (Any%'s chapters, or `splits.txt`'s) updates `splits/<category>.lss` in the user folder, in LiveSplit's own format (Open Splits): the segments, the personal best's split times, the best segments and the attempt count, in game time (load-free) and real time. The numbers are kept beside it in `<category>.txt`. "Every story step" has no fixed segments, so nothing is kept for it.

## Your challenges (Challenges tab)

Rule sets of your own: the difficulty, the action-command assists, Retry on Game Over, the challenge rules and the remix modes, named and kept in `challenges.txt`, and shared as codes the way settings presets are: `PCRULE1:` and the base64url of `name|key=value;...` (the rules not at their defaults). Putting one in place sets every rule, the others back to their defaults.

Tests: `TTYD_PLAY_TEST=chapter:N[:S]` starts chapter N again 30 s in (left S seconds later); `trick:N` warps to set-up N.

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
