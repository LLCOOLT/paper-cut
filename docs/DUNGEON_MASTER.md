# Dungeon master

Another player plays the enemies' side in the story's battles. On each enemy's turn they pick which of that enemy's own moves it uses and on whom, from a menu in the game's look, and they can hold its wind-up before the strike; Mario's side plays as ever (the first controller, and the co-op partner's player when co-op is on). Off by default.

## Turning it on

The full settings page's **Multiplayer** tab (also in the Start menu's Settings tab, under Multiplayer): **Dungeon master**, Off or the player who plays the enemies, **Player 2**, **Player 3** or **Player 4**. That player needs a controller of their own (the Controls tab's Players section picks player 2's). Without one, the enemies play themselves and the battle says so once.

It changes how battles play, so online it's the host's setting (below). Versus battles ([VERSUS.md](VERSUS.md)) are left alone: there Luigi's player already commands a side.

With co-op on and the dungeon master on player 2, player 2 plays both the partner's turns (Mario's side) and the enemies' turns. No player guards against their own enemies' hits: while player 2 is the dungeon master, the partner's guards go back to the first controller (in Brothers mode, Luigi's too). With player 3 or 4 as the dungeon master, co-op is untouched.

## An enemy's turn

When an enemy's turn comes, the dungeon master's menu opens at the right, in the battle menus' windows: the enemy's name with the player's tag (2P, 3P or 4P), then a row for each move it can make this turn, with the damage it would do (to Mario, or the target the enemy would pick), and the game's hand cursor. The help window under it says what the selected move does: its damage, whether it hits Mario's whole side, the statuses it can cause, and how often the enemy's own AI would have picked it.

| Button | |
|---|---|
| Up, down | choose a move |
| A | this move; for a move with one target out of several, then choose the target (left and right, up and down, or L and R), A again |
| B | back from the target to the moves |
| Y | the enemy's choice: it picks its move and target itself, as it would |

The last row, **Enemy's choice**, does the same as Y.

The moves are the enemy's own: each enemy's battle script, read as it is about to run (below). Their names come from what each move does, in the menu's language: Basic attack, Fire attack, Sleep attack, Explosion, Charge, Call for help and so on; an item the enemy holds shows as the item, by its own name. Two moves of the same kind are numbered. The damage is before guards: the move's power with the enemy's attack changes and charge, less the target's defence.

### What the script keeps

Some of an enemy's turn isn't the dungeon master's to choose, and the menu says so:

- **A set move:** when the script has only one move it can make this turn (Hooktail's opening fire breath and stomp, a boss's phase that always does the same), the move stands and the screen says **"Its script picks this move this turn."**; its target is still the dungeon master's when it has a choice of them.
- **A turn the script decides:** when the move comes from the enemy's own code rather than its script's chances, the screen says **"Its script decides this turn."** and the enemy acts as it would; any target it picks from several is still asked for.
- **Confused:** a confused enemy acts on its own (**"Confused: it acts on its own."**).
- **Everything else in battle:** the bosses' phase events, the story's scenes in battle, the stage's hazards, the enemies' reactions to being hit, all as the game has them.

A second hit of the same move (an enemy that strikes again and picks again) asks for its target again.

## The wind-up

Once an enemy has declared its attack and moved up to its target, the dungeon master can hold its wind-up: holding A keeps it waiting at its first pause before the hit, up to three quarters of a second, once a turn. The hit itself then plays at the game's own speed, so Mario's player reads the strike as ever and guards and Superguards keep their windows; the hold only changes when the strike starts. A move without damage, or one on the enemies' own side, has nothing to hold. While it can be held, the bottom of the screen says **"Hold A: the wind-up"**.

## The enemies' side of the screen

While a battle has a dungeon master, a panel at the top right lists each enemy: its name (red for the one whose turn it is), its HP and an HP bar, and the game's icons for its statuses (asleep, poisoned, charged, defence up...). The enemies have no Star Power or audience of their own, so there's none on their side.

## Online

In an online session the guest is player 2 (a room's seats are players 3 and 4), so with the host's Dungeon master on **Player 2** the guest plays the enemies. The setting is the host's: it goes with the host's settings at the session's start, as every setting that changes play does. Each game reads the dungeon master's controller as the session played it, and the look ahead reads only the battle's own state, so both games offer the same moves and play the same pick on the same frame. Watchers see the menu as the guest does.

## How it works

`port/src/aurora/dungeon_master_aurora.c`, `port/include/port/dungeon_master.h`, the setting in `port/settings/features/dungeon_master.cpp`.

- **The turn:** an enemy's turn is its attack event (the battle's act sequence starts it, `battle_seq.c`). Two hooks in the script interpreter (`evtmgr_cmd.c`) see each user function of that event and the events it waits on, and each wait it starts; with no dungeon master they are a load and a branch.
- **The look ahead:** at the event's first user function, the event and those waiting on it are copied into scratch entries and run on without touching the battle: the flow and the arithmetic by the game's own command handlers (`portEvtLookStep`), the battle's getters (HP, statuses, unit work, the battle's flags) for real on the copy, the target sampling for real (the battle's target list put back afterwards), anything else skipped with what it might have written marked unknown. Every chance the script takes (`evt_sub_random`, `btlevtcmd_GetRandomValue`, `btlevtcmd_DrawLots`, `btlevtcmd_EnemyItemUseCheck`) is followed each way it can go; a random number is split by the numbers the script compares it with, so 0-99 against 50 is two ways, not a hundred. Each way ends at the move it makes: the weapon it declares, pays for or hits with (`btlevtcmd_AttackDeclare`, `PayWeaponCost`, `WeaponAftereffect`, `PreCheckDamage`, `CheckDamage`), or a call for help (`btlevtcmd_SpawnUnit`). The ways that end on the same move are one row; their share of the chances is the AI's own odds. A branch on something unknown ends that way as the enemy's own (the menu's last row covers it).
- **The pick:** the move's way is played back as the script runs: each chance it asks for gets the value that way took, checked against the command the look ahead saw ask it (if the script goes elsewhere, the rest is its own), and the `btlevtcmd_ChoiceSamplingEnemy` that picks the move's target gets the dungeon master's target, if that target is still there. Nothing else in the script changes.
- **The wind-up:** after `btlevtcmd_AttackDeclare` and a move toward the target, the first wait before `PreCheckDamage` or `CheckDamage` is held while the player holds A (45 frames at most).
- **Guards:** `second_player_pc.c`'s guard pad leaves a guard on the first controller when its player is the dungeon master.
- **Determinism:** the controller read is the frame's as played (online, the session's; a replay, the recording's), the look ahead and the menu run in the game's frame, the look ahead puts the game's random numbers back as they were, and the drawing keeps them too (the font takes them per letter). With the setting off, nothing runs.

Tests: `TTYD_SET_dungeon_master=1` (or `dungeon_master=1` in the settings), `TTYD_P2_PRESS` for the second controller's presses; the log prints `[dm]` lines: the moves found, the pick.
