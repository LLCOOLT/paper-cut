# Link your Paper Mario 64 ROM

Paper Cut can use your own copy of **Paper Mario** for the Nintendo 64 to bring its partners into The
Thousand-Year Door: Goombario, Kooper, Bombette, Parakarry, Bow, Watt, Sushie and Lakilester.

Nothing from Paper Mario comes with Paper Cut. You point it at a ROM you made from your own cartridge, and
Paper Cut reads what it needs from that file on your device.

## Linking

**From the launcher.** The Play tab has a **Paper Mario 64** entry under your discs: it says whether a ROM is
linked (and which release). **Link Paper Mario 64...** opens the file picker; **Add a disc image...** takes a ROM
too, and so does dropping one on the launcher's window (or on the game's). A ROM in Paper Cut's user folder or
in Downloads gets a **Link** button of its own, so a controller alone can link it. **Settings** opens the
Paper Mario 64 page, where the partners are turned on.

**From the settings:**

1. Settings > Enhancements > Paper Mario 64 > **Link Paper Mario 64**.
2. **Choose your Paper Mario ROM...** and pick the file (`.z64`, `.v64`, `.n64`; any byte order).
3. Paper Cut checks it by its SHA-1. It knows the four unmodified releases:

   | Release | SHA-1 (as a .z64) |
   | --- | --- |
   | US | `3837f44cda784b466c9a2d99df70d77c322b97a0` |
   | Japan | `b9cca3ff260b9ff427d981626b82f96de73586d3` |
   | Europe | `2111d39265a317414d359e35a7d971c4dfa5f9e1` |
   | iQue | `5c724685085eba796537573dd6f84aaddedc8582` |

   A modified ROM or a hack isn't linked.
4. Turn on **Paper Mario 64 partners**.

The file picker is the same as the disc picker's, so this works on Windows, macOS, Linux, Android and iOS.

## What is read, and where it goes

Linking copies these parts of the ROM into one file in your user folder, `pm64/rom_cache.bin`:

- the sprite section (the characters' pictures, palettes and animations),
- the message archive (names and descriptions; the European ROM's four languages),
- the audio archive (the soundtrack and the sound effects),
- the move table (each move's FP cost and texts),
- the item table and the item icons (for the badges and Tayce T.'s recipes, docs/PM64_EXTRAS.md).

The ROM itself isn't kept: after linking you can move or delete it. Nothing is uploaded or downloaded, and
nothing from the ROM is in Paper Cut, its source or its releases.

## Unlinking

Settings > Enhancements > Paper Mario 64 > Link Paper Mario 64 > **Unlink** deletes `pm64/rom_cache.bin`. You can
also delete the `pm64` folder yourself. With no cache the partners are gone and everything is as before.

## The partners

- **In the launcher.** With the ROM linked, Paper Mario's partners spring up along the stage's front edge after
  Mario and his, their faces made from your ROM's sprites as the launcher opens. On a narrow screen they stand
  in a second row behind. Unlinking takes them off again.
- **Their TTYD counterparts.** Each belongs with a TTYD partner: Goombario with Goombella, Kooper with Koops,
  Bombette with Bobbery, Parakarry with Yoshi, Lakilester with Flurrie, Sushie and Bow with Vivian; Watt comes
  after chapter 4 and takes Ms. Mowz's place even if she hasn't joined (she isn't on the wheel then, and her own
  quest and joining are as ever: once she joins, both are there).
- **Joining: won from their bosses.** Settings > Enhancements > Paper Mario 64 > **Win Paper Mario partners from
  their bosses** (on at first): each Paper Mario partner joins once you beat its Paper Mario boss, Goombario's
  Jr. Troopa, Kooper's the Koopa Bros., Bombette's their rematch, Parakarry's Tutankoopa, Bow's Tubba Blubba,
  Sushie's Lava Piranha, Lakilester's Huff N. Puff and Watt's General Guy. With Settings > Modes > **Paper Mario
  64 bosses** on, each boss waits in Rogueport once its partner's TTYD counterpart has joined (General Guy once
  chapter 4 is over); walk into it or talk to it to fight. Any time, the same fights are on the **Paper Mario 64
  boss list**. Off, each joins with its TTYD counterpart instead, no boss needed. Either way a notice at the top
  of the screen says so ("Kooper joined too!", the name from your ROM) once Mario is free to move again. On a
  file where that partner joined already (an older save, or the ROM linked mid-game), they're there at once,
  with the notice as you play on; one that joined before the setting was turned on stays. In the open world
  they follow their counterpart whenever it is received. (**Paper Mario 64 partners from the start** has them
  join with their counterparts without the notices, when they aren't won from their bosses.)
- **The bosses' strength.** A boss fought in Rogueport scales to your level: its HP and attack follow Mario's
  level against its chapter in Paper Mario, from half of Paper Mario's numbers for a low level up to exactly
  them at that level or past it, never more. From the boss list, the board or Boss refights it scales the same
  way with **Paper Mario 64 bosses: Scale to your level** (on at first); off, it's as strong as the game's
  post-game.
- **Choosing them.** Hold Partner quick-swap for the **Partner wheel**: the Paper Mario partners are on it after
  your own. Picking one puts it in a TTYD partner's place; picking that TTYD partner brings them back. The
  partner's face by your HP is theirs then (their sprite's head, from your ROM, with the white edge the game's own faces have), and Watt floats as he does in
  Paper Mario.
- **Whose place.** Each takes the place of the partner it joined with, and uses that partner's field ability,
  HP and rank, with a little of its own:

  | Paper Mario partner | Takes the place of | In the field |
  | --- | --- | --- |
  | Goombario | Goombella | Tattle |
  | Kooper | Koops | the shell |
  | Bombette | Bobbery | the blast |
  | Parakarry | Yoshi | ridden, he carries Mario: the flutter (hold A in the air) flies three times as far * |
  | Watt | Ms. Mowz | finding hidden things; a dark room's gloom pushed back; hidden blocks near him spark |
  | Sushie | Vivian | the shadow; Mario swims on her back over deep water instead of falling in, and a jump off her back is high enough to climb back onto the shore (not in the boat's water: Pirate's Grotto and the rooms with boat panels) * |
  | Lakilester | Flurrie | his own: the button puts Mario on his cloud and takes him off it (not over spikes or lava); riding, Mario crosses spiked and burning floors unhurt and stops at spikes he walks into unhurt * |
  | Bow | Vivian | Outta Sight: Vivian's Veil (enemies don't notice Mario, and whatever passes over a hidden Mario passes him), with Mario standing see-through and Bow over him instead of the two sinking into a shadow |

- **Paper Mario field abilities** (Settings > Enhancements). The three marked * reach places their TTYD partner
  can't, so they can skip parts of the game. **Full** (the default) keeps them. Every room of the game was checked
  for where they reach (its floors, walls, doors, pipes, blocks and items, and the story scripts that watch where
  Mario stands): none of those places holds a door or pipe out of the room or a story event the game needs, so
  what they skip is a climb, a puzzle's way round, a block or an item early. The few that would leave Mario stuck
  with no way back are taken out:
  - Parakarry: in the Palace of Shadow's entrance hall, his long flutter ends as Yoshi's would on the way to the
    low pillar at the back on the right.
  - Lakilester: he only carries Mario onto spikes or lava he could jump back up from (from a ledge higher up Mario
    lands on them as ever: hurt, and the game puts him back). In the Palace of Shadow's spike room Mario stops at
    the spike clusters in the two pits unhurt and walks back out. Pirate's Grotto's spear traps hurt as ever.
  - Sushie: she only takes Mario swimming in water he could jump back out of (from a ledge higher up he falls in
    as ever, and the game puts him back).

  **Like their partner** leaves the three out: Parakarry flutters as far as Yoshi, Lakilester does Flurrie's
  Gale Force and Mario is hurt by spikes and lava as with her, and Mario falls into deep water with Sushie out as with Vivian. Watt's light and sparks
  open nothing, and stay either way. Online it's the host's choice.

- **In battle** they fight with the TTYD partner's action commands, and every move on their list is one of
  their own: each of the TTYD partner's four moves stands for one of theirs, with the Paper Mario name,
  description and FP cost (read from your ROM), unlocked at the rank the TTYD move is. Where the TTYD move
  hits the same way, it also does Paper Mario's damage for the partner's rank, with and without the action
  command. Merlon ranks them up to Super and Ultra with Shine Sprites, as he does the TTYD partner, and each
  rank raises their damage as in Paper Mario.

  | Partner | TTYD move | Paper Mario move | What it does |
  | --- | --- | --- | --- |
  | Goombario | Headbonk | Headbonk | Paper Mario's damage by rank |
  | | Tattle | Tattle | as TTYD's |
  | | Multibonk | Multibonk | Paper Mario's damage by rank |
  | | Rally Wink | Charge | he charges himself (TTYD's Charge: +2, +3 at Ultra rank) |
  | Kooper | Shell Toss | Shell Toss | Paper Mario's damage by rank |
  | | Power Shell | Power Shell | Paper Mario's damage by rank |
  | | Shell Shield | Dizzy Shell | Shell Slam's run through the enemies on the ground, no damage, dizzy for 2 turns |
  | | Shell Slam | Fire Shell | fire (no burn: Paper Mario has none) |
  | Bombette | Bomb | Body Slam | |
  | | Bomb Squad | Bomb | |
  | | Hold Fast | Power Bomb | Bob-ombast's blast on the enemies on the ground |
  | | Bob-ombast | Mega Bomb | |
  | Parakarry | Ground Pound | Sky Dive | |
  | | Mini-Egg | Shell Shot | |
  | | Gulp | Air Lift | |
  | | Stampede | Air Raid | |
  | Watt | Love Slap | Electro Dash | electric, through defense |
  | | Kiss Thief | Power Shock | Tease's gauge at one enemy: paralyzed (TTYD's Stop, 3 turns) at the gauge's chance |
  | | Tease | Mega Shock | electric; every enemy paralyzed (TTYD's Stop, 3 turns) at the gauge's chance |
  | | Smooch | Turbo Charge | Mario's attack +1 for 3 turns |
  | Sushie | Shade Fist | Belly Flop | water (no fire, no burn), Paper Mario's damage by rank |
  | | Veil | Water Block | |
  | | Fiery Jinx | Tidal Wave | water |
  | | Infatuate | Squirt | Shade Fist's punch at one enemy, water |
  | Lakilester | Body Slam | Spiny Flip | |
  | | Lip Lock | Spiny Surge | |
  | | Dodgy Fog | Cloud Nine | Mario dodgy (TTYD's 50% evasion) for 3 turns |
  | | Gale Force | Hurricane | |
  | Bow | Shade Fist | Smack | she slaps one enemy, mash A (Bomb's gauge): one slap, and up to 3, 4 or 5 by rank as the gauge fills, 1 damage each, no fire |
  | | Veil | Outta Sight | Veil's buttons: Mario invisible for the enemies' turn (every attack misses), Bow out of reach; she skips her next turn |
  | | Fiery Jinx | Spook | Fiery Jinx's buttons: a Fright Mask's scare on every enemy, each may flee (half the chance after a missed command); no damage |
  | | Infatuate | Fan Smack | Smack with her fan at one enemy: up to 5 slaps, 2 damage each |

  The TTYD list's order and ranks stay: Watt's Mega Shock comes at Super rank and Turbo Charge at Ultra, as
  Tease and Smooch do. The helping moves (Charge, Turbo Charge, Cloud Nine) have no action command. Bow and Sushie both take
  Vivian's place: the one picked on the wheel is the one out. In
  battle the partners face where the partner's unit does: the enemies, and back when a move turns it round.
- **Their names** follow the game's language: a European ROM's in the European release's text language
  (English, German, French or Spanish; Italian shows English), the Japanese release's own names on the Japanese
  disc. With a Japanese or iQue ROM the move names and descriptions stay TTYD's (their text isn't read yet).

## Sound effects

With the ROM linked, Audio > **Paper Mario sound effects** (on by default) gives the Paper Mario partners,
enemies, Star Spirits and the Star Haven corner Paper Mario's own sounds, played from your ROM by Paper Cut's
own player for its sound effect scripts:

- **The partners**, by the TTYD partner's sounds they stand in for: Goombario's jump, Headbonk, Tattle and
  Charge; Kooper's steps, shell spin-up, kick and swirl, and Fire Shell; Bombette's steps, fuse, Body Slam and
  her three blasts; Parakarry's wing flaps (for his steps and jumps), Sky Dive, Shell Shot and Air Raid; Watt
  floating without a sound, his charge, Power Shock, Turbo Charge and Mega Shock; Sushie's steps, dive, Belly
  Flop, Squirt, Water Block and Tidal Wave; Lakilester's cloud, Spiny Flip, Spiny Surge, Cloud Nine and
  Hurricane; Bow's vanishing and reappearing, Smack, Fan Smack, Outta Sight and Spook. In the field too: their
  steps and jumps, Kooper's shell, Bombette's fuse and blast, the rides, Sushie's dive.
- **The Paper Mario enemies** (the Pit, Master Quest, the Enemy maker): Goombas' and Shy Guys' steps,
  Paragoombas' wings and dive, the Koopas' shell, Bob-ombs' fuse and blast, Fuzzies' hops, Pokeys, Dry Bones,
  Magikoopas, Hammer Bros., Lakitus, Spinies and Bullet Bills.
- **A Star Spirit** appearing over a Special move, and **the corner's partners** as you talk to them.

Every other sound is the game's. The game's own sound still runs underneath, silently, so its timing, fades
and 3D placement carry over to Paper Mario's; the Sound effects volume applies. Only you hear it: online and
replays play the same with it on or off, and with no ROM linked nothing changes.

Audio > **Paper Mario sounds for everyday actions** (off by default) goes further: Mario's jump (and the
Stylish jump combo), his hammer, coins, hearts, flowers, Star Points, items, saving and the menus' cursor
take Paper Mario's sounds too.

### For scripts and mods: Paper Mario's sounds by name

Any of the game's sound calls (`evt_snd_sfxon`, `btlevtcmd_snd_se`, `psndSFXOn`, `psndSFXOn_3D`, an NPC's
step, jump and landing sounds) can name a Paper Mario sound with the game's own sound as its stand-in:

```c
CALL(evt_snd_sfxon, STRING("SOUND_BOW_VANISH|SFX_AUDIENCE_TELESA_INVISIBLE1"), 0)
```

With a ROM linked and Paper Mario sound effects on, `SOUND_BOW_VANISH` plays, with the stand-in's timing, 3D
placement, volume, fades and stop (the stand-in runs silently underneath). Otherwise the stand-in plays as it
always did, so the name is safe with no ROM. The names are the Paper Mario decompilation's `SOUND_` names
(its `include/enums.h`), all listed in `port/include/port/pm64_sound_ids.h` (written by
`tools/port/gen_pm64_sound_ids.py`); a `SOUND_LOOP_` name plays its looping `SOUND_LRAW_` script until the
game's sound is stopped. From C, with no stand-in: `portPm64SfxPlayName("SOUND_BOW_SPOOK", 100)` or
`portPm64SfxPlay(PM64_SOUND_BOW_SPOOK, 100)` (centred, volume 0-127; `port/include/port/pm64_sfx.h`).

## Online

Every player needs the same: their own ROM linked and the partners on, or the partners off. A game that
differs is refused when it joins, with the reason. Each player picks their own partners on the wheel, and
the others see each player's partner as that player's pick; a shared battle plays with the partners of the
player whose battle it is. Turning the setting on or off, or linking, takes effect from the next session.

## Replays

A replay with Paper Mario partners plays back the same only where a ROM is linked.

## Credits

The [Paper Mario decompilation](https://github.com/pmret/papermario) and its contributors are the reference
for reading the ROM's data and for how the partners, enemies and bosses move and fight. Paper Cut's own code
re-creates that behaviour; none of the decompilation's code or data is included. Each re-created move notes
which part of the decompilation it follows.
