# Versus battles

Mario and his partners against Luigi and his, two players on one screen, in the Glitz Pit's ring. Opt-in: nothing changes in the game until a versus fight is started, and the game's own battles are as they were.

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

Then the rules both sides play by: the **stage** (the Glitz Pit for now), a **turn timer** (off, 15, 30 or 60 seconds a command; when it runs out the turn is passed) and the **luck badges** (Close Call, Pretty Lucky, Lucky Day and Lucky Start, which leave hits to chance; off by default). **Fight!** closes the menu and goes to the ring; afterwards the game is back in the room it was started from, and Mario is as he was: the fight lends both sides their stats, partners, badges and items.

Up and down choose a row, left and right change it, X goes across to the other side. With a second controller, Luigi's player has a cursor of their own on his side, so both set up at once. The set-up is kept in `versus.txt` in the user folder.

The Modes tab of the full settings page has **Fight with the last set-up**, and says who won the last fight.

## The fight

Mario's side acts in the player phase, Luigi's in the enemy phase, each through the game's own command menu: jumps, hammers, the badges' moves, the partners' moves, items, Star Powers, Tactics (swapping partners, Defend, Appeal). Mario's player is the first controller, Luigi's the second; while Luigi's side acts, the second controller plays the menu and the action commands.

The defender guards and Superguards every hit from the other side with the normal timing, on their own controller, so both players play every turn. With one controller only, the side being hit can't guard.

The Glitz Pit's audience comes and reacts as at a match; each side fills its own Star Power. Luigi's HUD is drawn under Mario's, with his face; the turn timer's seconds show in the middle.

The fight ends when Mario or Luigi is down: no Game Over, no Star Points or coins.

Fair play: no Life Shroom (no revival in a fight), Fright Mask or Mystery; no luck badges unless the rules allow them; no badges that only work in the field. The Gameplay tab's difficulty doesn't scale Luigi's hits, and the Accessibility tab's command help is the same for both players.

## How it works

`port/src/aurora/versus_aurora.c`, `versus_setup_aurora.c` (the page), `port/include/port/versus.h`.

- **The map:** `tou_m2`, a map the fight adds to the mods' new maps with `tou_03`'s files (the Glitz Pit's area and REL), as the modes' maps do ([MODES.md](MODES.md)). Its script starts the battle from a hidden stand-in NPC with a battle setup of its own: the Glitz Pit's first match's stage, audience and music, and no enemies.
- **Luigi's side** is the enemy side made of player units: after the game enters Mario's side (`Btl_UnitSetup`), a second Mario unit and Luigi's first partner are entered on the right, from Luigi's pouch. Units of the enemy side face left by themselves. The Mario unit on the enemy side is drawn as Luigi (Play as Luigi's model and animations, whatever that option is).
- **The side acting:** the turn's phases decide it (Mario's in the player phase, Luigi's from the phase after it through the enemy phase). While Luigi's side acts, `BattleGetMarioPtr` and `BattleGetPartyPtr` give his units, the pouch is his (the two pouches are swapped), and the controllers are swapped, so the game's command menu, action commands, Star Power and partner swaps work for him as they do for Mario. A unit for the player's side entered then (a partner swapped in, Bobbery's bombs, Koops's shell) goes to Luigi's side. The game's HUD stays Mario's.
- **Guards:** before each hit from the other side (`BattleCheckDamage`), the defender's guard is judged as an enemy's attack script judges Mario's (`BattleActionCommandCheckDefence`), on the defender's controller's record.
- All of this is the game's hook slots (as code mods use), each returning at once outside a versus fight.

Tests: `TTYD_VERSUS_TEST=1` starts a fight with the set-up as it is, 30 s in; `TTYD_P2_PRESS` (the second controller's presses, as for co-op) plays Luigi's side.

Not done yet: online versus, a computer Luigi, other stages, the side's own intros (both sides are already standing when the fight starts).
