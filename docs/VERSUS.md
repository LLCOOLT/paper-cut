# Versus battles

Mario and his partners against Luigi and his, two players on one screen, online, or against the computer, in the Glitz Pit's ring. Opt-in: nothing changes in the game until a versus fight is started, and the game's own battles are as they were.

## Starting one

With a file in play, in the field: the Start menu's Settings tab, **Versus battle** (beside Photo mode). The set-up is a page in the game's windows, Mario's side on the left and Luigi's on the right:

| Row | |
|---|---|
| Level | 1 to 30. Each level-up above 1 goes to HP (+5), FP (+5) or BP (+3), as at a level-up in the story; all of them must be shared out |
| HP, FP, BP | left and right put a level-up in or take one back |
| Boots, hammer | Normal, Super or Ultra: the jump and hammer moves they give |
| Partner (three rows) | the first comes out; the others swap in from Tactics. Each partner fights on one side only |
| Partner rank | Normal, Super or Ultra for all three |
| Badges | A opens the list: every badge that does something in a fight, as many as the BP allow |
| Items | A opens the list: up to six, or none |

Then the rules both sides play by: the **stage** (the Glitz Pit for now), a **turn timer** (off, 15, 30 or 60 seconds a command; when it runs out the turn is passed), the **luck badges** (Close Call, Pretty Lucky, Lucky Day and Lucky Start, which leave hits to chance; off by default) and **Luigi's side**: a player, or the computer, easy, normal or hard. **Fight!** closes the menu and goes to the ring; afterwards the game is back in the room it was started from, and Mario is as he was: the fight lends both sides their stats, partners, badges and items.

Up and down choose a row, left and right change it, X goes across to the other side. With a second controller, Luigi's player has a cursor of their own on his side, so both set up at once. The set-up is kept in `versus.txt` in the user folder.

**Fight!**'s row also has, with left and right, **the ladder** (below).

The Modes tab of the full settings page has **Fight with the last set-up** and **Climb the ladder**, and says who won the last fight and how far the ladder went.

## The fight

Mario's side acts in the player phase, Luigi's in the enemy phase, each through the game's own command menu: jumps, hammers, the badges' moves, the partners' moves, items, Star Powers, Tactics (swapping partners, Defend, Appeal). Mario's player is the first controller, Luigi's the second; while Luigi's side acts, the second controller plays the menu and the action commands.

The defender guards and Superguards every hit from the other side with the normal timing, on their own controller, so both players play every turn. With one controller only, the side being hit can't guard.

The Glitz Pit's audience comes and reacts as at a match; each side fills its own Star Power. Luigi's HUD is drawn at the bottom of the screen, with his face (the audience count a little higher, above it); the turn timer's seconds show in the middle.

The fight ends when Mario or Luigi is down: no Game Over, no Star Points or coins.

Fair play: no Life Shroom (no revival in a fight), Fright Mask or Mystery; no luck badges unless the rules allow them; no badges that only work in the field. The Gameplay tab's difficulty doesn't scale Luigi's hits, and the Accessibility tab's command help is the same for both players.

## Against the computer

With **Luigi's side** set to the computer, Luigi and his partners play themselves, through the same command menu a player would use: the menu comes up for a moment, then the computer's pick. It weighs each jump, hammer, badge move, partner move and item by the damage it would do (the move's power less the target's defence), more on Luigi's foe Mario (whose fall wins) and on a knock-out, against the FP it costs; it heals with an item when HP run low, swaps in another partner from Tactics when the one out is down, and Defends when nothing else will do. Its guards, Superguards and action commands come up by chance, more often the harder it is:

| Skill | Action commands | Guards | Superguards | Picks at random |
|---|---|---|---|---|
| Easy | 45% | 20% | none | 35% of turns, and any target |
| Normal | 75% | 55% | 6% | 12% of turns |
| Hard | 95% | 80% | 20% | never |

Mario's player guards the computer's hits on the first controller. Star Powers, Charge and moves for their effects alone are left to players for now.

## The ladder

Mario's side as set up against the computer's Luigi, fight after fight in the same ring, until Mario falls. Each rung's Luigi is a level higher (from three below Mario's level, up to 30), and better geared: his level-ups shared out, boots, hammer and partner rank that go with his level, three partners Mario's side hasn't, badges as his BP allow, more items every two rungs (the strongest from the fifth), and the computer easy for the first two rungs, normal to the fifth and hard after. Each fight starts afresh (both sides' HP and items as set up). The rung shows at the top of the screen; the most rungs won is kept in `versus_ladder.txt` in the user folder. Each rung's Luigi comes from its number alone, so rung 7 is the same Luigi every time (against the same Mario).

## Online

In an online session (the Online tab; rooms: [ONLINE_RELAY.md](ONLINE_RELAY.md)), the host's player is Mario's and the guest's is Luigi's. The session's games play the Start menu in step (the host's controller drives it), so the host opens Settings > **Versus battle** and both set up their side on the page at once, the guest with the second cursor on Luigi's side; **Fight!** starts the fight in both games, which play it out in step as they play the rest of the game. A room's session works the same; watchers see the fight too.

- The set-up both games start from is the host's: it goes with the session's start (with the host's settings and memory card), so the page shows the same in every game. The guest's own `versus.txt` isn't touched in a session.
- The turn timer counts frames (60 a second), not the clock, so it runs out on the same frame in both games.
- **Fight with the last set-up** in the settings page is off online (one player's button would start the fight in one game only).
- The computer's random numbers are its own, from the same start each fight, so a computer Luigi also plays the same in every game (the two players then both fight it as Mario's side's: the host's controller).

## How it works

`port/src/aurora/versus_aurora.c`, `versus_setup_aurora.c` (the page), `port/include/port/versus.h`.

- **The map:** `tou_m2`, a map the fight adds to the mods' new maps with `tou_03`'s files (the Glitz Pit's area and REL), as the modes' maps do ([MODES.md](MODES.md)). Its script starts the battle from a hidden stand-in NPC with a battle setup of its own: the Glitz Pit's first match's stage, audience and music, and no enemies.
- **Luigi's side** is the enemy side made of player units: after the game enters Mario's side (`Btl_UnitSetup`), a second Mario unit and Luigi's first partner are entered on the right, from Luigi's pouch. Units of the enemy side face left by themselves. The Mario unit on the enemy side is drawn as Luigi (Play as Luigi's model and animations, whatever that option is).
- **The side acting:** the turn's phases decide it (Mario's in the player phase, Luigi's from the phase after it through the enemy phase). While Luigi's side acts, `BattleGetMarioPtr` and `BattleGetPartyPtr` give his units, the pouch is his (the two pouches are swapped), and the controllers are swapped, so the game's command menu, action commands, Star Power and partner swaps work for him as they do for Mario. A unit for the player's side entered then (a partner swapped in, Bobbery's bombs, Koops's shell) goes to Luigi's side. The game's HUD stays Mario's.
- **Guards:** before each hit from the other side (`BattleCheckDamage`), the defender's guard is judged as an enemy's attack script judges Mario's (`BattleActionCommandCheckDefence`), on the defender's controller's record.
- **The computer** (`versus_cpu_aurora.c`): while its side chooses (`BattleCommandInput`), it lists what the menu would offer as the menu does (`_btlcmd_MakeSelectWeaponTable`'s lists, the partner's own move list), samples each move's targets as the menu does (`BattleSamplingEnemy`), and puts its pick where the menu keeps one: the list's row and cursor, the targets and the chosen one, and the menu's last state, whose step sets the attack (`_btlcmd_SetAttackEvent`) as an A press on the target would. Its guards are a `BattleActionCommandCheckDefence` hook for its units; its action commands run as with the Auto Command badge when their roll (at `BattleActionCommandStart`) came up, and with no input otherwise. The first controller reads nothing while it acts.
- **The ladder:** at the end of a fight Mario won, the map is entered again and its script starts the next rung's fight; Luigi's side is the rung's (`ladderSide`), the rest the set-up's.
- **Online:** `port/settings/features/online.cpp` sends the set-up's text (`portVersusSetupText`) after the resume save, and every game of the session takes it for the run (`portVersusSetupForSession`); both games read the guest as the second controller (`twoPads`, the page's `secondPad`) whatever is plugged in here.
- All of this is the game's hook slots (as code mods use), each returning at once outside a versus fight.

Tests: `TTYD_VERSUS_TEST=1` starts a fight with the set-up as it is, 30 s in (`TTYD_VERSUS_TEST=ladder` the ladder); `TTYD_P2_PRESS` (the second controller's presses, as for co-op) plays Luigi's side.

Not done yet: other stages, the computer's Star Powers, the side's own intros (both sides are already standing when the fight starts).
