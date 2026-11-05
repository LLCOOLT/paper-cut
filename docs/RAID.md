# The raid boss

One of the game's own bosses, much stronger, for a full party: two to four players online, with controllers on this computer (Multiplayer > Second player, and players 3 and 4: [PLAYERS.md](PLAYERS.md)), or one player and Brothers mode's Luigi ([BROTHERS.md](BROTHERS.md)). It fights in four phases that need the duo moves ([DUO_MOVES.md](DUO_MOVES.md)) and everyone's Superguards, and its attacks borrow other bosses'. A new boss every Monday (UTC), the same for everyone, with or without a network.

## Starting it

With a file in play, in the field, and two players or more:

- the Start menu's Settings tab, **Modes > Raid boss** (and **This week's raid**, the week's boss and its phases on a page);
- online, also the Start menu's Online tab, **Raid boss of the week**;
- offline, the full settings page's Modes tab, **Start this week's raid**.

It begins once the menu is closed and Mario is free. Mario goes to the boss's own battle stage (a small map of the raid's own in the boss's area, with the area's audience and music), fights, and afterwards is back in the room he left, win, lose or flee: no Game Over.

For the fight Mario's side is lent the boss rush's late-game loadout: level 30, 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, every partner at Ultra rank (so both duo moves of each), all the Star Powers and eight items. The badges stay as they are. When it's over Mario is as he was; a win pays 50 coins on top.

## The week's raid

The bosses take turns, a week each:

| | Boss | HP (two players) |
|---|---|---|
| 1 | Bonetail | 260 |
| 2 | Hooktail | 160 |
| 3 | Magnus von Grapple 2.0 | 220 |
| 4 | Gloomtail | 240 |
| 5 | Macho Grubba | 180 |
| 6 | Smorg | 200 |

With three players the HP is 125 %, with four 150 %. Each week also draws, from the week's number, the two attacks of other bosses its phases borrow (never its own):

| Borrowed | Its hits can |
|---|---|
| Hooktail's flame | burn (50 %, 2 turns) |
| Gloomtail's poison breath | poison (60 %, 3 turns) |
| Bonetail's sleepy breath | put to sleep (35 %, 2 turns) |
| Macho Grubba's Power Lift | hit 2 harder |
| Smorg's miasma | make dizzy (40 %, 2 turns) |
| Magnus 2.0's lasers | pierce defence |
| Beldam's Ice Blast | freeze (30 %, 1 turn) |
| Doopliss's tricks | confuse (40 %, 2 turns) |
| Sir Grodus's staff | stop (25 %, 1 turn) |
| the Shadow Queen's curse | shrink (50 %, 3 turns) |

A guard keeps a borrowed status off; a Superguard keeps the whole hit off, as ever.

## The phases

The boss's HP bar is over the picture, with a mark at 75 %, 50 % and 25 %; each mark is a phase's end. While a phase's condition isn't met the boss's HP stops at its mark, so no hit skips a phase.

| Phase | HP | What it does | To end it |
|---|---|---|---|
| 1, the warm-up | 100 to 75 % | the boss as it fights, its hits 1 stronger | bring it to 75 % |
| 2, the shell | 75 to 50 % | every hit on it does 1 damage; its hits 2 stronger, with the first borrowed attack | a **duo move** breaks the shell (its hit lands whole); then bring it to 50 % |
| 3, the fury | 50 to 25 % | its hits 3 stronger and piercing defence; it takes half damage | **everyone Superguards**: each hero standing (Mario, Luigi, the partner) Superguards one of its hits (or twice as many Superguards as there are heroes, from whoever the fury picks on). Then it's staggered and takes 1.5 times the damage |
| 4, the last stand | 25 to 0 % | its hits 2 stronger, with both borrowed attacks | beat it; duo moves do double damage |

With Duo moves off in this fight (Multiplayer > Duo moves; online, the host's setting), the shell wears through after 10 hits instead.

Who plays what is as in any battle with more players ([PLAYERS.md](PLAYERS.md)): player 1 Mario, player 2 the partner (or Luigi in Brothers mode), players 3 and 4 the partner when it's their companion's. The duo moves' two commands are players 1 and 2's.

## Online

The raid is the same in every game of a session: the week comes from the game clock, which online is the host's (from the session's start), so a session that began on Sunday night fights Sunday's boss even after midnight; the players present are the frame's controllers as played; the fight's counters (phases, Superguards, the shell) all happen in the game's frame; its timer counts frames. The Start menu runs in step in every game, so its rows start the raid in all of them at once; the settings page's button (one player's) is off online. Watchers see it too.

## The board

Each week's clears are kept on the relay, by party name: the fewest turns first, then the quickest. After a win, the Modes tab's **Send our clear** sends it (online, with the session's player names; else the name typed there); **Show the board** shows the week's. The relay knows the rotation too: when its week's boss isn't the one this computer has, the page says the computer's date may be off. A clear counts for its week or the week after (a fight that ended after Monday came).

## Rewards

- The achievements **Raid Party** (beat a raid boss), **Weekly Regulars** (beat five) and **Full House** (beat one with four players), and **Every Boss in Turn** (beat each of the rotation's bosses);
- a raid plaque in the trophy room, with the raids won ([POSTGAME.md](POSTGAME.md)).

## How it works

`port/src/aurora/raid_aurora.c`, `port/settings/features/raid.cpp`, `port/include/port/raid.h`; the relay's `Raid` class in `port/relay/src/index.js`.

- **The maps**: `jon_r0` with `jon_06`'s files (Bonetail), `gon_r0` with `gon_11`'s, `aji_r0` with `aji_00`'s, `las_r0` with `las_26`'s, `tou_r0` with `tou_03`'s, `rsh_r0` with `rsh_06_a`'s, added to the mods' new maps as the boss rush's are ([MODES.md](MODES.md)); each one's script starts the boss's own battle setup from a hidden stand-in NPC, with losing allowed and fleeing possible.
- **The boss**: its HP set as it enters (`BtlUnit_Entry`); its side's attacks go through `BattleCheckDamage` as a changed copy of the weapon (the game's weapon data is never written); the damage on the boss and from it is changed after `BattleCalculateDamage`, where the phase moves on.
- **The week**: weeks since Monday 1969-12-29 (UTC), from `port/include/port/date_seed.h`'s day; the boss is the week's number modulo the six, the borrowed attacks drawn from the week's seed.
- While a raid runs, autosave and Remix: enemies leave it alone.

Tests: `TTYD_RAID_TEST=1` asks for the raid 30 seconds in; `TTYD_RAID_SOLO=1` lets one player start it.
