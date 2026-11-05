# Modes and streaming

Opt-in extras over the game's own battles. All are off until started or turned on, and none changes the game's logic otherwise.

The built-in item randomizer, a mode of its own for a whole new file, is in [RANDOMIZER.md](RANDOMIZER.md). Versus battles (Mario's side against Luigi's, two players on one screen, in the Glitz Pit's ring; the Start menu's Settings tab) are in [VERSUS.md](VERSUS.md).

The raid boss (one of the game's bosses, much stronger, for two to four players, a new one every week) is in [RAID.md](RAID.md).

Master Quest (the story's fights remixed from each area's own enemies, with the game's own statuses on their attacks, harder chapter by chapter; your own sets in the Challenges tab) is in [MASTER_QUEST.md](MASTER_QUEST.md).
After the story: the Paper Tower (a 30-floor climb of remixed rooms), the trophy room, daily shop specials, area maps and the new modes' achievements are in [POSTGAME.md](POSTGAME.md).

## Boss rush (Modes tab)

Every boss in story order, one fight after another, timed: Lord Crump, Blooper, Gold Fuzzy, Hooktail, the Shadow Sirens, Magnus von Grapple, Macho Grubba, Doopliss, Cortez, Lord Crump again, Smorg, Magnus 2.0, Gloomtail, Sir Grodus, Bowser and Kammy, and the Shadow Queen. Start it from the Modes tab while a file is in play, with a **loadout**:

| Loadout | |
|---|---|
| As you are | Mario as he is in the file |
| Mid-game | level 15, 30 HP, 25 FP, 21 BP, Super Boots and Hammer, every partner, four Star Powers, eight items |
| Late game | level 30, 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, every partner upgraded, all Star Powers, eight items |

The loadout is lent: Mario's stats and items are put back when the run ends or is stopped (badges stay as they are). Fleeing a fight starts it again; a Game Over ends the run. The time and the boss show at the top right; the best time per loadout is kept in `modes.txt` in the user folder.

A mid-game or late-game rush (and a run of the Pit of 100 Trials) can also be recorded for the online leaderboards, with its replay, and checked by playing it back: Modes tab > Leaderboards (docs/LEADERBOARDS.md).

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

- **The start:** the file's story state is kept (the saved words and flags, the area's own, and the key items, partners and Star Powers the story gives); the story is set to the end of the chapter before, and the chapter's areas' flags are cleared (`port/src/aurora/chapters_aurora.c`, the flag blocks of 150 per area). The party is the one the story has at the chapter's start (Goombella and Koops for chapter 2, ...; Ms. Mowz from chapter 7), since the chapter's scenes have no lines for partners who join later; the others come back at the end.
- **The end:** when the story reaches the chapter's last step (on the way to the next chapter), the kept state is put back and Mario returns to the room he left. Levels, items, badges and coins won stay. **Leave the chapter** ends it early the same way.
- **Saving during it:** the kept state is also in `chapter_replay_<slot>.bin` in the user folder, so a save made during the replay goes on with it when loaded.

## Remix modes (Modes tab)

Settings that change play, so they're in settings presets, challenge codes and replays:

| Setting | |
|---|---|
| Remix: enemies | The fight behind each field enemy is drawn from all its area's ordinary fights (`npcSetupBattleInfo`'s setup swapped). Bosses and scripted fights (no escape, losing allowed, no Jump or Hammer...) and the Glitz Pit's and the Pit of 100 Trials' stay. The same for a room, an enemy and the seed. |
| Remix: badge draft | At each chapter's title card, three badges to choose one from (left and right, A). The card waits for A anyway; that A takes the badge. The seven cut badges are among them only with Director's Cut on ([DIRECTORS_CUT.md](DIRECTORS_CUT.md)). |
| Remix seed | 0-99999: the same seed, the same remix. |

**Hardcore:** with "The next new file is a hardcore file", the next new file is marked (GSW 2040-2042) and has one life: no Retry on Game Over, no save state loads, and its Game Over ends it for good (its id goes in `hardcore.txt`; loading it again goes back to the title). **Restart and record a hardcore run** starts the game again at the title, recording a replay; the Game Over saves it, a ghost to race.

## Pit of 100 Trials in co-op (Modes tab)

A run down the Pit from its first floor with a second controller on the partner: Multiplayer > Second player is turned on for the run and put back after (`port/src/aurora/pit_coop_aurora.c`). The start is the Pit's own (the floor counter to 0, the pipe's map change to `jon_00`); the floor and the time show over the picture. The run ends when Mario leaves the Pit (its pipe home or the way out after the 100th floor), at a Game Over or at the title; he then goes back to the room he started from if he isn't in Rogueport already. The deepest floor and the best time to the bottom are kept in `pit_coop.txt`. It opens once the file has found the Pit, or after chapter 2. In online co-op both players take the Pit's pipe together instead (one game alone can't jump).

## Bingo (Modes tab)

A 5x5 card of goals made from a seed (`port/src/aurora/bingo_aurora.c`): items, partners, Crystal Stars, the boots and hammer, levels, coins, Star Pieces and Shine Sprites held, the Badge, Recipe and Tattle Logs, troubles solved, a partner's rank, badges worn, the Pit's deepest floor. Each goal has a difficulty from 1 to 5, and every row, column and diagonal holds one of each, so no line is all early goals. The same seed makes the same card everywhere; the goals' items and partners are named in the game's own words. A square is marked by itself as the file reaches its goal, and stays marked. Five in a line is a bingo; the Modes page shows the card, its time and when the first bingo came, and a small tracker sits at the bottom left of the picture (it can be turned off). The Start menu's Settings tab shows the card too (Modes > Show the bingo card). The card, its marks and its clock are kept in `bingo.txt`.

**Lockout:** everyone types the same room code (4 to 8 letters or digits) and a name. The first player in brings the card, the others get it as they join. Each plays on their own game; a square done is claimed from the room on the relay, and the room gives it to whoever claimed it first, so every player sees the same owner. Only your own squares count for your lines; the tracker shows the others' in red. In online co-op only the host's game claims (both games see the same squares done). Bingo only reads the game, so it changes nothing in play or in online co-op's lockstep.

## Quiz party (Modes tab)

Trivia for up to four players on one screen, in the game's own windows with Quizmo's sounds (`port/src/aurora/quiz_aurora.c`). Every question comes from the game's data and is worded with its own names: how much an item restores, what a shop gives for it (only items every shop pays the same for), which of four the shops sell costs the most (from the item shops' own shelves and prices, `tools/port/gen_quiz_shops.py`, and only when it costs more in every shop), a badge's BP, which of four enemies has the most HP, an enemy's Defense or its strongest plain attack (the enemies' numbers come from their battle units in the decomp, `tools/port/gen_tattle_cards.py`), which of four is a badge. Start it from the Modes page or the Start menu's Settings tab (Modes > Start a quiz party); it begins once the menu is closed and Mario is free. Each player presses A on their own controller to join, then Start begins. Answers are A, B, X or Y; a right one is worth 100 points and up to 50 more for speed, ten questions to a round. The field stands still and the game gets no input while the quiz is up; Z on the first controller ends it. Not during online co-op or a replay.

## Tattle cards (Modes tab)

Every enemy in the Tattle Log is a card (`port/src/aurora/cards_aurora.c`) with its own HP, Attack (its strongest plain attack) and Defense, taken from its battle unit in the decomp, and a cost of 1 to 5 stars worked out from them. The strongest (the bosses, and a few like the Amazy Dayzee) are brought down to the top of the five-star scale, their HP first so they keep their Attack, so a five-star card is one of a kind but none wins every round: the Shadow Queen plays with 39 HP rather than 150, still hitting for 7. A deck is up to 8 cards and 20 stars, built on the Modes page from the enemies the file has tattled (**The strongest deck** fills it; without a valid deck the duels use the strongest that fit). It is kept in `cards.txt`.

A duel is best of five rounds. Each round both players play a card from their hand at once, unseen; the two trade hits, each doing the attacker's Attack less the other's Defense (1 at least), the cheaper card striking first (the same cost: both at once), until one falls and its player loses the round. Three rounds win.

- **The card players at Rogueport's Westside:** a rookie, a regular and a champion, each with a deck drawn from the whole Tattle Log and more stars to spend, the champion playing what beats your hand. A win pays a few coins (10, 30, 60). Mario must be in the Westside; not during online co-op. They are three of the Westside's townsfolk: the rookie is a mouse, the regular a Goomba and the champion a Bandit, and talking to one (after their own line) starts a duel with them, as the menu does; **Talking to them starts a duel too** on the Tattle cards page turns that off. Z leaves a duel.
- **A second controller:** both players draft five cards in turn from the same twelve (the file's own tattles first), then duel.
- **Online, the other player:** the same draft. Start it from the Start menu (Settings, Modes > Duel the other player), which both games of a session run together: the pool comes from the game's frame clock and every pick from the controllers through lockstep, so the duel plays out the same in both games. Each player's own hand is drawn at the front.

Start a duel from the Modes page or the Start menu; it begins once the menu is closed and Mario is free. The field stands still and the game gets no input meanwhile; Z on the first controller leaves.

## Daily seed (Modes tab)

Everyone gets the same game each day (`port/settings/features/daily.cpp`): Paper Cut's open world ([RANDOMIZER.md](RANDOMIZER.md)) from the seed `DAILY-YYYYMMDD`, the date in UTC so the day changes at the same moment everywhere (online, the host's date for everyone), with two challenge rules drawn from the date (an enemy remix, a badge draft, no items in battle, Superguards only, harder attack commands or hard enemies) and every other rule at its default, so every player's file plays the same. **Play today's seed** keeps your own randomizer and challenge settings and puts the day's in place; begin a new file from the title screen (Restart now first if a file is in play). **Put my settings back** returns yours.

The goal is any three Crystal Stars, from whichever of the open world's chapters (all of them, while it has fewer than three); the file's own goal is set to it, so the logic makes sure three can be reached. The daily follows the open world: when it opens more chapters, so does the daily, still three stars for one sitting. The clock is the file's own (`daily_YYYYMMDD.txt`): every frame the daily file is played, menus included, kept across restarts and loads. Once done, **Send my time** puts it on the day's results board on the relay under your name (the best per name is kept), and **Show the board** shows the 50 best. The randomizer works on the US version only, so the daily seed does too.

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

## Fun toggles (Modes tab)

Silly switches, each on its own:

- **Big heads**: every character's head drawn nearly twice as big, grown about the neck. Drawing only.
- **Moon gravity**: Mario jumps and walks everywhere as on the Moon (the game's own low gravity, from the Moon's rooms). Changes play.
- **Paper-thin from every side**: everyone a sliver of paper, even from the front. Drawing only.
- **Luigi's voice for Mario**: Mario's steps, jumps and landings sound as Luigi's (as Play as Luigi has them).
- **Spinning paper**: everyone turns round and round. Drawing only.

The drawing-only ones change the matrices models are drawn with, after everything the game keeps, so they never change how the game plays. Online, each player keeps their own Big heads, Paper-thin, Spinning paper and Luigi's voice (only they see or hear them); Moon gravity changes play, so it is the host's.

Most of the game's models name their parts `pPlane1`, `pPlane2`... so Big heads finds a head in a table made from the disc's models by `tools/port/fun_heads.py` (`port/src/common/fun_heads.inc`): each model posed standing, its head picked by shape (the biggest lump of cards high on the model that doesn't reach the floor, with anything sitting on it, such as a ponytail), grown about its neck. It covers Mario, the partners, Luigi, Peach, Bowser and the enemies and people the game names; a model it doesn't have (a mod's) falls back to a group named for a head or a face. `TTYD_FUN_TRACE=1` prints each model's head groups as it is first drawn with Big heads on.

## Chaos mode (Modes tab)

Every 30 seconds of play, something happens. In the field: giant or tiny Mario, flipped controls, the camera upside down, coin rain, a Bob-omb's blast, or one of the fun toggles for 15 seconds (moon gravity, big heads, paper-thin, spinning). In battle: one of Crowd Control's commands (a gift or a rock from the stands, a little Star Power given or taken, Bingo, a cheer), done by the same engine, or a fun toggle. The effect's name shows at the top of the screen.

Offline only: off in online play, while recording or playing a replay, and with console-accurate mode on. It uses random numbers of its own, never the game's.

## Console-accurate mode (Gameplay tab)

One switch for the game as the GameCube plays it. Turned on, every setting marked "changes play" goes back to the original game's value and is locked, along with Text speed, Faster text outside battle, Menu speed, Rewind and the fun toggles. The values they had are kept in `console_accurate.txt` by `settings.txt`, and come back when it is turned off. Paper Cut never patches out the game's glitches, so every speedrun trick is there either way. Online, the host's switch and values travel with its settings. Races and leaderboards can require it: the game side asks `portConsoleAccurate()` (`port/include/port/toggles.h`).

## Level sync (Gameplay tab)

Enemies below Mario's level are brought up to it: a tenth more HP per level below (at most 4x), one more damage per six levels (at most six more), and Star Points as for an even fight. **Post-game and New Game+** applies after the credits, or while Mario is eight or more levels above the chapter's usual level (a New Game+ file); **Always** applies everywhere. Online, the host's setting.

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
