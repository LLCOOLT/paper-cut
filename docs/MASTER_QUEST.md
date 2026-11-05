# Master Quest

Master Quest remixes the story's fights: more enemies, mixed from each area's own, and attacks that carry the game's own statuses, harder chapter by chapter, from the prologue to chapter 8, and the Pit of 100 Trials. Nothing new is drawn or animated: every enemy, move and status is one the game already has. Turn it on with **Modes > Master Quest**; it applies from the next room.

## What changes

- **Fights.** A field enemy's fight is picked as the game picks it, then replaced by one of its area's Master Quest formations for the same first enemy: one to five enemies from that area's own fights. They keep the game's own entries for them (heights, item tables, what they do), stand where the game puts one to five enemies, and roll their held items as the game rolls them. The formation is the same each time for a room, an enemy and the remix seed (**Modes > Remix seed**), so a fight can be learned.
- **Enemies.** An enemy can have more (or less) HP, hit a little harder (attacks with a fixed power), give a status with its attacks on Mario's side (sleep, stop, dizzy, poison, confusion, burn, freeze, tiny, attack or defense down, slow; a guard still stops them, as in the game), attack with an element or pierce defense, and start a fight with up to two statuses of its own (dodgy, huge, attack or defense up, charged, invisible, fast, payback, hold fast, HP regeneration, electrified). The game's weapon data is never written: an attack with changes goes through the damage check as a changed copy.
- **What stays.** Bosses and their parts, scripted fights (no running away, losing allowed, no Jump or Hammer, no partner change), tutorials, fights with an enemy outside a set's list, the boss rush and the endless Glitz Pit modes, and versus battles. In the Glitz Pit's story matches the teams stay as the story names them, and only their moves change.

Master Quest works with the other remix modes: with **Remix: enemies** on, a field enemy's fight is first drawn from its area's fights, then remixed.

## The curated set

Balanced for a new file, without the Gameplay tab's help. Fights grow more than enemies do, so more Star Points come in and Mario's level keeps up:

| Part | Areas | What it does |
|---|---|---|
| Prologue | the sewers (tik) | an enemy more, Goombas mixed with Paragoombas and Spiky Goombas; no stat changes |
| Chapter 1 | Petal Meadows, Shhwonk Fortress (hei), Hooktail Castle (gon) | three to five enemies, the area's kinds mixed; Spiky Goombas and the Bones a hit point more; Paragoombas and Paratroopas can make Mario dizzy |
| Chapter 2 | Boggly Woods (win), the Great Tree (mri) | four-enemy fights mixing Puffs, Clefts, Piranhas, Piders, X-Nauts and Yuxes; Pale Piranhas can poison, Yuxes start with defense up |
| Chapter 3 | the Glitz Pit (tou) | the teams as they are; their moves change: Bandits start dodgy, Hammer Bros start fast, Fire Bros can burn, the Dark Koopatrol has 5 HP more |
| Chapter 4 | Twilight Trail (gra), Creepy Steeple (jin) | Hyper Goombas, Dayzees and Clefts mixed, sometimes an Amazy Dayzee; Boos start invisible; most enemies a hit point more |
| Chapter 5 | Keelhaul Key (muj), Pirate's Grotto (dou) | Fuzzy and Piranha packs, Embers and Lava Bubbles that can burn, Bill Blasters with Bulky Bob-ombs |
| Chapter 6 | Riverside Station (eki) | Ruff Puffs, Spiky Parabuzzies and Poison Pokeys together, two more HP |
| Chapter 7 | Fahr Outpost (bom), the Moon (moo), the X-Naut Fortress (aji) | five-enemy fights; Ice Puffs can slow, X-Nauts and Moon Clefts hit one harder, the Yuxes start shielded |
| Chapter 8 | the Palace of Shadow (las) | Swoopula swarms, Dry Bones with Phantom Embers and Dark Wizzerds, Chain Chomps one harder |
| The Pit | jon | each ten floors' enemies in bigger groups, with that tier's changes |

## Your own sets

**Challenges > Master Quest sets**: start from the curated set or from an empty one, edit the text (it is checked as you type), save it, and **Play this set**, or choose it in **Modes > Master Quest set**. **Copy the code** shares a set as a `PCMQ1:` code; **Add from the code** takes one in. The sets are kept in `master_quest_sets.txt` in the user folder, a code a line.

A set is text, a line each (`#` starts a note):

```
name My Master Quest
fight hei GOOMBA = GOOMBA PARAGOOMBA GOOMBA SPIKY_GOOMBA
fight hei GOOMBA = GOOMBA KOOPA_TROOPA GOOMBA
enemy hei SPIKY_GOOMBA hp+1 hit=poison:30:2:1
enemy * KOOPA_TROOPA start=defense_up:2:1
```

- `fight <area> <lead> = <enemy> ...`: in that area (the first three letters of its map names: `tik`, `hei`, `gon`, `win`, `mri`, `gra`, `jin`, `muj`, `dou`, `eki`, `bom`, `moo`, `aji`, `las`, `jon` for the Pit), an ordinary fight whose first enemy is `<lead>` becomes one of the area's lines for that lead: one to five enemies. An enemy the area has no fight with is left out (its data isn't loaded there).
- `enemy <area or *> <enemy> <changes>`: `hp+N`, `hp-N`, `hp*P` (P percent, before `hp+N`), `atk+N`, `atk-N`, `hit=<status>:<chance>:<turns>[:<strength>]` (twice at most), `start=<status>:<turns>[:<strength>]` (twice at most), `element=fire|ice|explosion|electric`, `pierce`, `variant=PCFOE1:...` (an enemy variant's code from the Enemy maker, [BATTLE_EXTRAS.md](BATTLE_EXTRAS.md): its stats, colour, size, borrowed move, coins, name and Tattle, for that enemy whatever kind the code names, in the fights it is in, with the line's other changes; 32 in a set). `*` is every area; an area's own line wins.

The enemy names are under **How a set is written > The enemies a set can use** (the decomp's names: `GOOMBA`, `KOOPA_TROOPA`, `X_NAUT_PHD`...). A **Master Quest** rule is in the challenge editor's rule sets too, so a challenge can turn it on.

## Online

Master Quest changes play, so it follows the host: the host's settings say whether it's on and which set, and the set's text goes with the host's setup, so the guest plays the same fights whatever it has. Every choice comes from the settings, the area's data and the game's own random numbers, so both games stay in step.

## For scripts

`port/include/port/master_quest.h` has a table of hooks (`portMasterQuestSetHooks`) a script can fill: one sees each field fight's enemies before it is set up and can change them (the area's enemies only), the other sees each enemy's changes as it enters a fight and can change them. They run even with Master Quest off, and like every script they are off online unless both players run the same ones.

## Tests

The log says each remixed fight: `[master quest] hei_01 <enemy>: GOOMBA PARAGOOMBA GOOMBA SPIKY_GOOMBA`. `TTYD_SET_master_quest=1` turns it on for a run.
