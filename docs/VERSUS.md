# Versus battles

Mario and his partners against Luigi and his, two players on one screen, online, or against the computer, in the Glitz Pit's ring or on a battle stage from one of the chapters. Opt-in: nothing changes in the game until a versus fight is started, and the game's own battles are as they were.

## Starting one

With a file in play, in the field: the Start menu's Settings tab, Modes → **Versus battle**. The set-up is a page in the game's windows, Mario's side on the left and Luigi's on the right:

| Row | |
|---|---|
| Level | 1 to 30. Each level-up above 1 goes to HP (+5), FP (+5) or BP (+3), as at a level-up in the story; all of them must be shared out |
| HP, FP, BP | left and right put a level-up in or take one back |
| Boots, hammer | Normal, Super or Ultra: the jump and hammer moves they give |
| Partner (three rows) | the first comes out; the others swap in from Tactics. Each partner fights on one side only |
| Partner rank | Normal, Super or Ultra for all three |
| Badges | A opens the list: every badge that does something in a fight, as many as the BP allow |
| Items | A opens the list: up to six, or none |

Then the rules both sides play by: the **stage** (below), a **turn timer** (off, 15, 30 or 60 seconds a command; when it runs out the turn is passed), the **luck badges** (Close Call, Pretty Lucky, Lucky Day and Lucky Start, which leave hits to chance; off by default), **Showstopper knocks out** (off by default: below) and **Luigi's side**: a player, or the computer, easy, normal or hard. **Fight!** closes the menu and goes to the ring; afterwards the game is back in the room it was started from, and Mario is as he was: the fight lends both sides their stats, partners, badges and items.

Up and down choose a row, left and right change it, X goes across to the other side. With a second controller, Luigi's player has a cursor of their own on his side, so both set up at once. The set-up is kept in `versus.txt` in the user folder.

**Fight!**'s row also has, with left and right, **the ladder** (below).

### Stages

Each stage is one of the game's own battle stages, with the audience and music of a battle there:

| Stage | From |
|---|---|
| Glitz Pit | the ring of the Glitz Pit's first match (its own music) |
| Petal Meadows | chapter 1's field |
| Hooktail Castle | chapter 1's castle |
| Boggly Woods | chapter 2 |
| Creepy Steeple | chapter 4 |
| Keelhaul Key | chapter 5's jungle |
| Palace of Shadow | chapter 8 |
| Rogueport Sewers | the sewers under Rogueport |

The names are the game's own (the Journal's map), in its language.

The Modes tab of the full settings page has **Fight with the last set-up** and **Climb the ladder**, and says who won the last fight and how far the ladder went.

## The fight

Mario's side acts in the player phase, Luigi's in the enemy phase, each through the game's own command menu: jumps, hammers, the badges' moves, the partners' moves, items, Star Powers, Tactics (swapping partners, Defend, Appeal). Mario's player is the first controller, Luigi's the second; while Luigi's side acts, the second controller plays the menu and the action commands.

The defender guards and Superguards every hit from the other side with the normal timing, on their own controller, so both players play every turn. With one controller only, the two players take turns on it, and the side being hit guards on it too.

The stage's audience comes and reacts as at a battle there (the Glitz Pit's as at a match); each side fills its own Star Power. Luigi's HUD is drawn at the bottom of the screen, with his face (the audience count a little higher, above it); the turn timer's seconds show in the middle.

As the curtain goes up, each side makes its entrance: Mario and his partner run in from the left and strike a pose for the audience (Mario with his cry), then Luigi and his from the right; the first turn comes once both stand in their places, about three seconds in.

The fight ends when Mario or Luigi is down: no Game Over, no Star Points or coins.

Fair play: no Life Shroom (no revival in a fight), Fright Mask or Mystery; no luck badges unless the rules allow them; no badges that only work in the field.

Showstopper: in the story it knocks out each enemy by chance (50% with no round of buttons cleared, 10% more a round), and every hero and partner can be knocked out by it, so in versus one landed Showstopper would often end the fight for 2 SP. With **Showstopper knocks out** off (the default), it instead takes a share of each foe's max HP, rounded up and through defence: a sixth with no round cleared, up to a third with all five (the knock-out chance it earned, over 3). Mario with 30 HP loses 5 to 10. With the rule on it knocks out by chance, as in the story.

Stop: Luigi's side acts in the enemies' phase, after which the turn's statuses count down, so a stop from his side (Clock Out, a Stopwatch) lasts one turn longer than the move says: a 1-turn stop costs Mario's side one turn, as Mario's 1-turn stop costs Luigi's side one.

The Gameplay tab's difficulty doesn't scale Luigi's hits, and the Accessibility tab's command help is the same for both players.

## Against the computer

With **Luigi's side** set to the computer, Luigi and his partners play themselves, through the same command menu a player would use: the menu comes up for a moment, then the computer's pick. It weighs each jump, hammer, badge move, partner move and item by the damage it would do (the move's power less the target's defence), more on Luigi's foe Mario (whose fall wins) and on a knock-out, against the FP it costs; it heals with an item when HP run low, swaps in another partner from Tactics when the one out is down, and Defends when nothing else will do (or Appeals, while its Star Power isn't full). Its guards, Superguards and action commands come up by chance, more often the harder it is:

| Skill | Action commands | Guards | Superguards | Picks at random |
|---|---|---|---|---|
| Easy | 45% | 20% | none | 35% of turns, and any target |
| Normal | 75% | 55% | 6% | 12% of turns |
| Hard | 95% | 80% | 20% | never |

Luigi uses the Star Powers too, from the Special menu, once his Star Power has filled: Sweet Treat and Sweet Feast when his HP run low, Earth Tremor, Art Attack and Supernova by the damage they would likely do to the other side (they pierce defence), Clock Out on foes that can still act, Power Lift once, Showstopper by the knock-outs it would likely make (or, with **Showstopper knocks out** off, the HP it would take); each weighed against the Star Power it costs, which is slow to fill. The harder the computer, the more turns it looks at them (easy 40%, normal 85%, hard every turn). With the Charge badge (Charge P for a partner), Tactics' Charge comes first when no hit now would win the fight and the charge isn't high already, keeping FP for the move after it.

It plays each Star Power's game itself, on its own random numbers (so an online session's games and a replay play it the same), easy poorly, normal decently, hard nearly perfectly but never to the frame:

| Star Power | How it plays | Easy | Normal | Hard |
|---|---|---|---|---|
| Sweet Treat, Sweet Feast | fires when a shot would hit an item, judged a few frames off; a pause between shots | 4 frames off, 30 between; doesn't see the poison | 2 off, 14 between | mostly on time, 8 between |
| Earth Tremor | A as the cursor passes each star, somewhere inside its window; a miss is too early | 82% right | 93% | 99% |
| Clock Out | mashes the button that throws the bomb to the foes (which fills the gauge too); a moment to find it when the buttons swap; now and then the wrong one | a press every 11-15 frames, 45 to react, 6% wrong | 8-11, 25, 2% | 5-7, 12, never |
| Power Lift | the cursor to the nearest arrow (attack on a tie), then A; a moment each step; the poison mushroom now and then taken for an arrow | 16-24 frames a step, 15% | 8-12, 4% | 4-6, never |
| Art Attack | a loop round all the foes it can hit, again and again until time is up: an ellipse round their boxes, overshot so the line crosses itself | slow, loose loops, off-centre, some missing a foe | quicker, mostly round them | quick, sure loops |
| Showstopper | each button after a moment, right by chance | 80% | 92% | 98% |
| Supernova | mashes A | a press every 12-15 frames (a cut or two) | 10-13 (about three) | 5-7 (all five) |

It weighs the statuses a move gives too, by their chance on the target (its own resistance, Feeling Fine and the like, none it has already): on a foe, the turns lost to sleep, stop and freezing (half for dizzy, confused and slow), poison's and burn's damage, attack and defence lost, a knock-out; on its own side, an extra move, attack and defence up, dodging, going unseen, healing over turns, Payback and Hold Fast. So it uses the moves played for their statuses alone (Sleepy Stomp, Tease, Rally Wink, Veil, a Stopwatch or Sleepy Sheep) where they do the most.

Mario's player guards the computer's hits on the first controller.

Its turn can't hang: if the command menu is ever somewhere the computer doesn't choose from for 5 seconds (300 frames, the same in every game online), the menus are put away and it Defends.

## The ladder

Mario's side as set up against the computer's Luigi, fight after fight, until Mario falls: the first on the set-up's stage, each after it on the next stage of the list (round again after the last). Each rung's Luigi is a level higher (the first two below Mario's level, up to 30), and better geared: his level-ups shared out, boots, hammer and partner rank that go with his level, three partners Mario's side hasn't, badges as his BP allow, more items every two rungs (the strongest from the fifth), and the computer easy for the first two rungs, normal to the fifth and hard after. Each fight starts afresh (both sides' HP and items as set up). The rung shows at the top of the screen; the most rungs won is kept in `versus_ladder.txt` in the user folder. Each rung's Luigi comes from its number alone, so rung 7 is the same Luigi every time (against the same Mario).

## Online

In an online session (the Online tab; rooms: [ONLINE_RELAY.md](ONLINE_RELAY.md)), the host's player is Mario's and the guest's is Luigi's. The session's games play the Start menu in step (the host's controller drives it), so the host opens Settings → Modes → **Versus battle** and both set up their side on the page at once, the guest with the second cursor on Luigi's side; **Fight!** starts the fight in both games, which play it out in step as they play the rest of the game. A room's session works the same; watchers see the fight too.

**Versus room:** the host's option for a session that's all about versus: in the launcher's Online tab, the **Versus room** box under Host a game (or once the other player is there, beside where it starts from); in the Start menu's Online tab, the **Versus room** row in the options (and while hosting). With it on, every game of the session opens the Start menu straight on the versus set-up page as soon as the menu could open in the field with a file in play (so from the title, once a file is loaded; with Continue where you left off, at once), on the same frame in each. Once a session: after the fight, or B out of the page, the session goes on as any other. It goes with the session's start (the host's), and isn't taken again when a session starts again to recover.

- The set-up both games start from is the host's: it goes with the session's start (with the host's settings and memory card), so the page shows the same in every game. The guest's own `versus.txt` isn't touched in a session.
- The turn timer counts frames (60 a second), not the clock, so it runs out on the same frame in both games.
- **Fight with the last set-up** in the settings page is off online (one player's button would start the fight in one game only).
- The computer's random numbers are its own, from the same start each fight, so a computer Luigi also plays the same in every game (the two players then both fight it as Mario's side's: the host's controller).

## How it works

`port/src/aurora/versus_aurora.c`, `versus_setup_aurora.c` (the page), `port/include/port/versus.h`.

- **The map:** a map of the fight's own for each stage, added to the mods' new maps with the files of a room of the stage's area (so the area's REL), as the modes' maps do ([MODES.md](MODES.md)): `tou_m2` with `tou_03`'s (the Glitz Pit), `hei_v0` with `hei_01`'s, `gon_v0` with `gon_02`'s, `win_v0` with `win_00`'s, `jin_v0` with `jin_00`'s, `muj_v0` with `muj_02`'s, `las_v0` with `las_17`'s, `tik_v0` with `tik_01`'s (`kStages` in `versus_aurora.c`). Its script starts the battle from a hidden stand-in NPC with a battle setup of its own: one of the area's battles' (a battle in that room; the Glitz Pit's first match), for its stage, audience and music, and no enemies. The stage is in the set-up's text (`stage=`), so an online session's games all fight on the host's.
- **Luigi's side** is the enemy side made of player units: after the game enters Mario's side (`Btl_UnitSetup`), a second Mario unit and Luigi's first partner are entered on the right, from Luigi's pouch. Units of the enemy side face left by themselves. The Mario unit on the enemy side is drawn as Luigi (the playable Luigi's model and animations, whatever Gameplay → **Play as** says).
- **The side acting:** the turn's phases decide it (Mario's in the player phase, Luigi's from the phase after it through the enemy phase). While Luigi's side acts, `BattleGetMarioPtr` and `BattleGetPartyPtr` give his units, the pouch is his (the two pouches are swapped), and the controllers are swapped, so the game's command menu, action commands, Star Power and partner swaps work for him as they do for Mario. A unit for the player's side entered then (a partner swapped in, Bobbery's bombs, Koops's shell) goes to Luigi's side. The game's HUD stays Mario's.
- **Guards:** before each hit from the other side (`BattleCheckDamage`), the defender's guard is judged as an enemy's attack script judges Mario's (`BattleActionCommandCheckDefence`), on the defender's controller's record.
- **The computer** (`versus_cpu_aurora.c`): while its side chooses (`BattleCommandInput`), it lists what the menu would offer as the menu does (`_btlcmd_MakeSelectWeaponTable`'s lists, the partner's own move list), samples each move's targets as the menu does (`BattleSamplingEnemy`), and puts its pick where the menu keeps one: the list's row and cursor, the targets and the chosen one, and the menu's last state, whose step sets the attack (`_btlcmd_SetAttackEvent`) as an A press on the target would. Its guards are a `BattleActionCommandCheckDefence` hook for its units; its action commands run as with the Auto Command badge when their roll (at `BattleActionCommandStart`) came up, and with no input otherwise. The first controller reads nothing while it acts, but for its own presses and stick in a Star Power's game (`portVersusCpuSacPad`), from what each game wants now (`portShowstopperWants`, `portEarthTremorWants` and the like in `src/battle/sac/`, read-only). It also stops the BINGO! reels when the slot spins at the end of its own act: they stop only on A, read from its side's controller then, so the fight would otherwise wait forever.
- **Star Powers:** the Star Powers' games (`src/battle/sac/`) pick the units they hit, shake or slide as the enemy alliance, 1; `SAC_FOE_ALLIANCE` (`sac_common.h`) is Mario's side's, 0, while Luigi's side acts (`portSacFoeAlliance`), so his Art Attack, Clock Out and the rest go to the other side, and Sweet Treat's and Sweet Feast's hearts show his face (`portLuigiFaceForce`); a stop from his side gets a turn more (`portVersusStopTime`, from `BattleSetStatusDamageFromWeapon`); Clock Out's bomb game and Art Attack's camera, made for a caster on the left, are mirrored for him (`SIDE_X`, `port_side_x`). A part with no pose for a hurt type (a hero under Earth Tremor) keeps its own (`btlDispChangeAnime`). With **Showstopper knocks out** off, Showstopper's weapon (`set_weapon` in `sac_suki.c`) has no knock-out chance and a damage function of the target's max HP instead (`portVersusShowstopperSoft`); the rule is in the set-up's text (`showko=`).
- **The ladder:** at the end of a fight Mario won, the next stage's map is entered and its script starts the next rung's fight; Luigi's side is the rung's (`ladderSide`), the rest the set-up's.
- **Online:** `port/settings/features/online.cpp` sends the set-up's text (`portVersusSetupText`) after the resume save, and every game of the session takes it for the run (`portVersusSetupForSession`); both games read the guest as the second controller (`twoPads`, the page's `secondPad`) whatever is plugged in here.
- All of this is the game's hook slots (as code mods use), each returning at once outside a versus fight.

Tests: `TTYD_VERSUS_TEST=1` starts a fight with the set-up as it is, 30 s in (`TTYD_VERSUS_TEST=ladder` the ladder); `TTYD_P2_PRESS` (the second controller's presses, as for co-op) plays Luigi's side. `TTYD_VERSUS_RUNG=N` starts the ladder at rung N; `TTYD_VERSUS_WATCH=1` prints the battle's state (its sequences, units and running events) whenever a fight stands still for 30 s.

- **The entrances:** when the battle's opening starts (`SEQ_FIRST_ACT`), every player unit's entry script is the fight's own (`versus_entry_evt`): from 250 behind its place, a wait by its side, the run in, the appeal pose; the opening waits for them as for the game's entries.
