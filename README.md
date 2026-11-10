<p align="center"><img src="docs/images/banner@2x.png" alt="Paper Cut" width="640"></p>

# Paper Cut

<p align="center"><img src="docs/images/gif-hero-050.gif" alt="Paper Cut 0.5.0 in sixteen seconds: the curtains open on the name; four players play together on Twilight Trail, a Paper Mario 64 partner among them; two games online in Twilight Town; Luigi jumps on Poshley Heights' grand stairs, Peach floats under her parasol in Keelhaul Key's jungle and Bowser breathes fire in Hooktail Castle; Bombette's Mega Bomb; Paper Mario 64's Bowser in Rogueport; Cool Mode's sunglasses and big heads in Glitzville; the bingo card filling on Keelhaul Key; Bonetail after the story; the second screen's live Start menu in Boggly Woods; mirror mode flipping Riverside Station; the curtains close on the name." width="800"></p>

<p align="center"><a href="https://github.com/LLCOOLT/paper-cut/releases/download/v0.5.0/papercut-0.5.0-trailer.mp4"><b>&#9654; Watch the trailer</b></a></p>

**Paper Cut is a fan-made native port of Paper Mario: The Thousand-Year Door** for Windows, macOS, Linux, iOS and Android: the game's own code, rebuilt from its decompilation, with no emulator. Play the whole adventure with up to four friends, on one couch or online on any mix of devices. Bring Paper Mario 64's partners and bosses in from your own ROM, turn on the Switch remake's changes with one switch, play as Luigi, Peach or Bowser, and play it at any screen shape and frame rate. It plays the Japanese (G8MJ01), US (G8ME01) and European (G8MP01) releases.

**No game data is included.** You need your own disc image (and your own Paper Mario ROM for the Paper Mario 64 features).

**Download:** packages are on the [Releases](https://github.com/LLCOOLT/paper-cut/releases) page. One download per platform plays every version: `papercut-<version>-<platform>.zip` (iPhone and Android: `papercut-<version>-<us|jp|eu>-...`). To build it yourself, see [Building and running](#building-and-running).

## Co-op: up to 4 players

Up to four players share one adventure. The first controller (or a handheld's built-in controls) is always player 1; on the couch, another controller's **Start** joins and that player takes the next seat: player 2 plays the partner (or Luigi in Brothers mode), players 3 and 4 the other partners (Settings → Multiplayer → **Co-op (up to 4 players)**, and each player's controller under Players). A row of bars along the bottom shows 1P to 4P, and everyone has **their own card** in the Start menu's style to pick a character, swap seats, emote or check their buttons, without pausing anyone. The camera pulls back to keep everyone in the picture. **Online**, Windows, macOS, Linux, Android and iPhone play together from a room code: the host's world leads, and a bad connection no longer drops you. Players with no one in a fight sit in the audience and throw items; party games, versus battles and the raid boss are one page away. [docs/PLAYERS.md](docs/PLAYERS.md), [docs/STATE_SYNC.md](docs/STATE_SYNC.md)

Everything else is on one **Multiplayer** page: the title menu's Multiplayer button or the Start menu's Multiplayer tab. **To play online:**
1. Everyone: the same Paper Cut release, and a disc of the same release (all US, all JP or all EU).
2. The host: **Multiplayer**, **Online...**, **Host a room**, in a game (Start menu) or from the title. The room's code is on the Online page in large letters; send it to your friends. From the title, the host then picks **Party...** (no save needed) or **Play** for a save file; guests wait in the room until then.
3. Friends: **Multiplayer** (the title's, or the Start menu's), **Online...**, **Join by code**, and type the code with a controller, touch or the keyboard. They never pick a file: they go straight into the host's adventure, in their seat.
4. Or the host picks a **Party** game, and everyone goes in together.

<p align="center">
  <img src="docs/images/gif-coop-start-to-join.gif" alt="Four players on Twilight Trail under a giant yellow moon: Mario swings his hammer with Koops beside him; controller 2 presses Start and takes over Koops, who walks off and fires his shell; player 3 joins and Yoshi steps in, hops and pops an emote; player 4 joins and picks Lakilester from Paper Mario 64 on their card, who floats in on his cloud; then all four run, jump and swing at once, with the 1P to 4P bars along the bottom." width="800">
</p>

<p align="center">
  <img src="docs/images/gif-online-split.gif" alt="Online co-op, two games side by side in Twilight Town: on the left the host's Mario stays put, on the right the guest, on another device, walks Goombella left past the Twilighters and back, each game's camera on its own player" width="800">
</p>

<p align="center">
  <img src="docs/images/shot-coop-cards.jpg" alt="Three players' cards open at once in Poshley Heights, by the Save Block at the foot of the stairs: 2P's Character strip on Koops, 3P's home card as Yoshi, and 4P's Controls with the pad's buttons for Jump, Talk, Partner, Emote, Ping and Menu, while Flurrie, Koops and Yoshi stand beside Mario" width="49%">
  <img src="docs/images/gif-coop-4p-battle.gif" alt="Four players in a Brothers mode battle in Petal Meadows: Mario and Luigi (2P) jump together for Bros. Bounce, player 4 throws a Mushroom to Mario from the audience, and the partner's attack lands a NICE" width="49%">
</p>

## The Switch remake's changes, as options

One switch, **Play it like the Switch remake**, turns on everything the 2024 remake changed that Paper Cut has, or pick them one by one on the Remake page: the remake's battle balance and rules, new Save and Heart Blocks, the sewers' warp pipe and shortcut pipes, merchants, the field hammer and partners' field moves, 9,999 coins, bigger bags and repeated-scene skips. The Remake page has 64 of them, all off by default; the one switch sets every one but Start in Rogueport, which has its own choices. [docs/REMAKE_MAPS.md](docs/REMAKE_MAPS.md)

<p align="center">
  <img src="docs/images/gif-remake-warp-pipe.gif" alt="The remake's warp pipe in the Rogueport Sewers: Where would you like to go? lists Rogueport to Poshley Heights, Petalburg is picked, and Mario goes down the pipe" width="70%">
</p>

<p align="center">
  <img src="docs/images/shot-remake-page.jpg" alt="The Start menu's Remake page: Play it like the Switch remake switched On at the top, the Field changes below it, each On, and the page's help balloon" width="49%">
  <img src="docs/images/gif-remake-heart-block.gif" alt="The remake's new Heart Block in the X-Naut Fortress, beside the Save Block: Mario walks over, jumps into the block marked 20 coins, and hearts burst out of it and rain down over him and Goombella" width="49%">
</p>

## Paper Mario 64, from your own ROM

Link your own Paper Mario ROM and its partners join Mario: **Goombario, Kooper, Bombette, Parakarry, Bow, Watt, Sushie and Lakilester**, each beside a Thousand-Year Door partner, with their own field abilities and their own moves in battle, rebuilt from Paper Mario's own battle scripts. Win them from their bosses, who wait in Rogueport as the story goes on, each with a floating badge of the partner it frees (each one a story battle, its moves with Paper Mario's own effects), and refight **ten Paper Mario 64 bosses**, from Jr. Troopa to Bowser, from the boss list or the board in the Star Haven corner, scaled to your level. Turn on more as you like: Paper Mario 64's enemies in the story, Rowf's badges, Tayce T.'s recipes, the Star Spirits, its soundtrack, or **Everything Paper Mario 64** at once. Goombario Tattles in his own words. **Nothing from the N64 game ships with Paper Cut:** it's read from your ROM, on your device. [docs/PM64_LINK.md](docs/PM64_LINK.md), [docs/PM64_EXTRAS.md](docs/PM64_EXTRAS.md)

<p align="center">
  <img src="docs/images/gif-pm64-bombette-mega-bomb.gif" alt="Bombette's Mega Bomb from Paper Mario 64 in a Thousand-Year Door battle under Rogueport: the mash gauge fills, she walks up to three Spinias, explodes, and NICE pops up over all three" width="49%">
  <img src="docs/images/gif-pm64-partners-moves.gif" alt="Four more Paper Mario 64 partners' moves on the same stage: Kooper's Fire Shell, Watt's Power Shock, Sushie's Tidal Wave and Lakilester's Spiny Surge" width="49%">
</p>

<p align="center">
  <img src="docs/images/shot-pm64-boss-badge.jpg" alt="Paper Mario 64's bosses waiting on Rogueport's west side, each with a floating badge of the partner it frees: General Guy with Watt's, Tutankoopa and the Koopa Bros. with theirs, and Mario and Goombella in front" width="49%">
  <img src="docs/images/shot-pm64-board.jpg" alt="The boss board in the Star Haven corner of Rogueport's plaza: Paper Mario 64's bosses, Who will you fight?, from Jr. Troopa (brings Goombario) and the Koopa Bros. (beaten) to General Guy (brings Watt) and More..." width="49%">
  <img src="docs/images/shot-pm64-boss-general-guy.jpg" alt="General Guy from Paper Mario 64, drawn without his tank, on a Thousand-Year Door battle stage in Rogueport: he stands lit at the right in his white plumed helmet, Bombette walks up to him, and Mario waits at the left" width="49%">
  <img src="docs/images/shot-pm64-boss-bowser.jpg" alt="Paper Mario 64's Bowser at his full size on a Thousand-Year Door battle stage, breathing fire at Mario and Kooper" width="49%">
</p>

<p align="center">
  <img src="docs/images/shot-pm64-goombario-tattle.jpg" alt="Goombario's own Tattle of Merlee in her room, in his words from Paper Mario 64, in the game's speech balloon" width="60%">
</p>

## Play as Luigi, Peach or Bowser

Pick one (Enhancements > Characters) and they replace Mario for the whole story: in the field, in battle, in the scenes and the credits, with their own face on the HUD, their name in every text and the Wardrobe dressed for them. Luigi has 156 animations made for him, jumps a little higher and speaks a little higher than Mario; Peach floats down under her parasol and throws it for a Hammer Throw; Bowser breathes fire in the field and in battle, spits a fireball for a Hammer Throw and stomps for a quake. [docs/PLAY_AS.md](docs/PLAY_AS.md)

<p align="center">
  <img src="docs/images/shot-playas-luigi-castle.jpg" alt="Luigi, played in Mario's place, with Koops on the battlements of Hooktail Castle by its purple door" width="70%">
</p>

<p align="center">
  <img src="docs/images/gif-playas-peach-parasol.gif" alt="Peach, played in Mario's place, runs along Keelhaul Key's beach, jumps, opens her parasol and floats slowly down to land by the sea" width="49%">
  <img src="docs/images/gif-playas-bowser-quake.gif" alt="Bowser's quake stomp in a Petal Meadows battle: he leaps high, comes down on the stage, NICE, and every Goomba on the ground takes 2" width="49%">
</p>

## Graphics: any screen, sharper, deeper

4:3 to 32:9 with the HUD at the picture's edges (on by default), 120 Hz and up with in-between frames, up to 8x internal resolution, MSAA, HDR and VRR. Texture upscaling and Dolphin texture packs; paper looks (sharp text, smooth paper edges, paper physics), **paper shadows**, **bloom**, **ray-traced lighting** (Windows and Linux computers) and **stereoscopic 3D** for 3D TVs, VR viewers and anaglyph glasses. CRT and paper filters on top. If the frame rate ever collapses, a watchdog turns the costly options off for the session (your settings are kept). On a weaker PC (under 6 GB of memory, or 4 threads or fewer), new graphics are made in the background instead of pausing the game, and take less memory. [docs/PAPER_LOOKS.md](docs/PAPER_LOOKS.md), [docs/STEREOSCOPIC_3D.md](docs/STEREOSCOPIC_3D.md)

<p align="center">
  <img src="docs/images/shot-ultrawide-glitzville.jpg" alt="Glitzville's plaza at 21:9, the hot dog stand and the Glitz Pit's red carpet across the full width, the HUD at the picture's edges" width="98%">
</p>

<p align="center">
  <img src="docs/images/shot-lighting-compare.jpg" alt="Rogueport split down the middle: Off on the left, ray-traced lighting on the right, with deeper shadows around Mario and the barrels" width="49%">
  <img src="docs/images/shot-paper-shadows.jpg" alt="Petalburg split down the middle: Off on the left, paper shadows on the right, each Toad's cut-out shadow laid on the path" width="49%">
  <img src="docs/images/shot-upscale-compare.jpg" alt="The same close crop of the Excess Express dining car at 1x and at 8x internal resolution: Mario, Goombella and the chef soft and jagged on the left, crisp on the right" width="49%">
  <img src="docs/images/shot-filters-crt.jpg" alt="Fahr Outpost in the snow, split: Off on the left, the CRT filter's scanlines on the right" width="49%">
</p>

## Restorations of the Japanese release

The US and European releases changed a few things from the Japanese one. Seven of them come back, each with its own switch and all off by default: Rogueport's chalk outline (read once from your own Japanese disc, or redrawn on your own disc when you have none), the Parlor Boo's bunny ears, the original screen flashes, 1000 X-Nauts in the prologue, the bad ending where Mario stays at the Shadow Queen's side, Hooktail's fear of frogs, and the Japanese recipes (Trial Stew; off online, since it changes play). Fan text packs can change any of the game's messages, from a template Paper Cut writes from your own disc. [docs/DELOCALIZATION.md](docs/DELOCALIZATION.md)

<p align="center">
  <img src="docs/images/shot-restore-chalk-outline.jpg" alt="Rogueport's chalk outline on a ruined house's floor, as in the Japanese release, with Mario beside it" width="49%">
  <img src="docs/images/shot-restore-bunny-boo.jpg" alt="The Parlor Boo in her bunny ears, beside Mario and Goombella" width="49%">
</p>

## A second screen

On a dual-screen Android handheld like the AYN Thor, the lower screen shows the game's own Start menu, live: check the party, use an item, swap partners or badges by touch while the game plays above. Paper Cut finds a built-in second display by itself; nothing to set up. The picture comes from a desktop test build, with its two windows stacked as a Thor's screens are. [docs/SECOND_SCREEN_MENU.md](docs/SECOND_SCREEN_MENU.md)

<p align="center">
  <img src="docs/images/shot-second-screen-rogueport.jpg" alt="The two screens stacked: Mario and Koops in Rogueport's harbour on the main screen, and the Start menu's Party tab on Koops, his HP and moves, on the second screen below" width="60%">
</p>

## Settings worth knowing

Every setting lives in the Start menu's Settings tab, in the game's own menus, or on the full page (F1). A few to know about:
- **Presets** (at the top of Gameplay): thirteen, from Vanilla and Vanilla+ to Story, Challenge, Explorer, Completionist, Couch party, Accessible, Paper Mario 64, Randomizer and Speedrun, in one pick. Your first start asks how you want to play.
- **Skip any scene** (hold Start; on by default, and B stops a skip under way), **Fly through text** (hold B), **Start in Rogueport** for a new file.
- **Achievements:** 181 of them (145, plus 36 more with a Paper Mario 64 ROM linked), in nine groups with the game's own icons and progress bars, earned by online guests too.
- **Fast travel** from the Journal's map, **quick resume**, and **What's new** after each update.
- **Back to the launcher** (Settings > General, or F2) and **Jump to chapter** (Practice: any chapter's start on your file, to try something out; not during a mode).
- **A way back from everything:** Esc, B, a right click, the touch controls' B and Android's back button leave every Paper Cut menu and page, and anything that can't happen right now says why.
- **Battle recap and Records**, **Less flashing**, **Read text aloud**, **one-handed controls**.
- **Controls:** button icons for your own controller, rebinding with the controller alone, the controller's light in your partner's colour.
- **Portable mode** (a `portable.txt` beside the program) and the **save manager** for Dolphin and GameCube saves.

<p align="center">
  <img src="docs/images/shot-presets-picker.jpg" alt="The launcher's first start: How do you want to play?, with a card for each of seven presets, its icon, name and tagline, and More presets... below" width="49%">
  <img src="docs/images/shot-achievements-page.jpg" alt="The settings page's Achievements: File 1, 11 of 181 earned, a tab per group with its count, and the Story group's achievements with their icons and the dates they were earned" width="49%">
  <img src="docs/images/shot-settings-tab.jpg" alt="The Start menu's Settings tab in Rogueport: two columns of categories with the game's item icons, Gameplay chosen" width="49%">
  <img src="docs/images/shot-whats-new.jpg" alt="What's new in Paper Cut 0.5.0 on the launcher: the release's highlights, each with a button to its settings" width="49%">
</p>

## Everything else

Each option is off unless it says otherwise, and the settings page's search finds any of them.

**Party games and versus.** The Multiplayer page's **Party** has Versus, Raid boss, Boss rush, Quiz party, Tattle-card duel, Bingo and Dungeon master; the host picks and everyone goes straight in. **Versus** puts Mario and his partners against Luigi and his on eight of the game's battle stages, each side set up on one page and acting through the game's own command menu, on one screen, online, or against a computer Luigi and a ladder of stronger ones. **Brothers mode** brings Luigi in as a second hero, and **duo moves** give Mario and each partner a team attack. [docs/VERSUS.md](docs/VERSUS.md), [docs/BROTHERS.md](docs/BROTHERS.md), [docs/DUO_MOVES.md](docs/DUO_MOVES.md), [docs/RAID.md](docs/RAID.md)

<p align="center">
  <img src="docs/images/shot-mode-versus.jpg" alt="A versus fight in the Glitz Pit: Mario and Goombella on the left against Luigi and Yoshi on the right, each side's HUD, one at the top and one at the bottom, and Can't flee this fight!" width="49%">
  <img src="docs/images/shot-quiz-party.jpg" alt="The quiz party in Rogueport: question 3 of 10, Which of these enemies has the most HP?, four answers each on a button of the player's own controller, and four players' scores" width="49%">
  <img src="docs/images/shot-mode-raid.jpg" alt="The raid boss: Gloomtail fills the stage over Mario and Goombella, with the raid's health bar and its phase marks at the top" width="49%">
  <img src="docs/images/shot-mode-dungeon-master.jpg" alt="Dungeon master: player 2 runs Lord Crump, picking his Basic attack from their own menu against Mario and Goombella" width="49%">
</p>

**The title menu and Modes.** After Press Start: Play, Multiplayer and Settings. Modes are in the Start menu's Modes tab, from any save, and never change your story: the action-command dojo that tells each press in frames, battle puzzles, challenges and challenge presets, Master Quest (the story's fights remixed), Director's Cut (what the game left out, put back), chapter select, remix modes and hardcore files. [docs/MODES.md](docs/MODES.md), [docs/DOJO.md](docs/DOJO.md), [docs/MASTER_QUEST.md](docs/MASTER_QUEST.md), [docs/DIRECTORS_CUT.md](docs/DIRECTORS_CUT.md)

<p align="center">
  <img src="docs/images/shot-title-menu.jpg" alt="The title screen after Press Start: Play, Multiplayer and Settings in a row along the bottom band, under the logo and the whole cast" width="70%">
</p>

<p align="center">
  <img src="docs/images/shot-mode-dojo.jpg" alt="The action-command dojo: Jump: Perfect! over a bar of frames, with the press on frame 7 of 8 and on time on average" width="49%">
  <img src="docs/images/shot-mode-puzzle.jpg" alt="Battle puzzle 1, First Steps: the goal at the top (win in 1 turn without being hurt) and Mario's jump landing for 10" width="49%">
  <img src="docs/images/shot-mode-master-quest.jpg" alt="Master Quest in Boggly Woods: Goombella attacks a remixed line-up of Clefts and a Pale Piranha" width="49%">
  <img src="docs/images/shot-mode-remix-draft.jpg" alt="A remix mode's badge draft: choose one of three badges for this chapter, Mega Rush P, Happy Heart P or Close Call, with the chosen one's description" width="49%">
</p>

**After the story.** Whacka in the Pit, the Pit Mover to floor 50, exhibition matches and two superbosses, the Gold Medal, a Gallery, the Paper Tower (30 floors of remixed rooms), a trophy room under Rogueport, a Boo quiz, crane prizes and Pianta tokens. [docs/POSTGAME.md](docs/POSTGAME.md)

<p align="center">
  <img src="docs/images/shot-trophy-room-thousand-year-door.jpg" alt="The trophy room under Rogueport: the Crystal Stars over their pedestals before the Thousand-Year Door, paper cut-outs of the bosses beaten around the room, and Mario and Yoshi in front" width="70%">
</p>

**Randomizer and open world.** A built-in item randomizer on every version: a seed and what to shuffle, no server, with entrance shuffle, logic levels and tricks, permalinks, music and sound shuffle, a spoiler log and a check tracker in the Journal. **Paper Cut's open world** opens all eight chapters in any order; **the daily seed** gives everyone the same one each day. [Archipelago](https://archipelago.gg) multiworld seeds on US. [docs/RANDOMIZER.md](docs/RANDOMIZER.md)

<p align="center">
  <img src="docs/images/shot-mode-rando-tracker.jpg" alt="The Journal's randomizer Tracker: checks found and reachable now, and each place's count, Rogueport to Riverside Station" width="70%">
</p>

**Wardrobe, costumes and mirror mode.** The Wardrobe tab colours Mario, every partner and the HUD, with Luigi, Wario, Waluigi and Fire Mario looks, HUD themes, enemy costumes, partner skins, stickers and Cool Mode's sunglasses. Mirror mode flips the world and the controls. [docs/COSTUMES.md](docs/COSTUMES.md)

<p align="center">
  <img src="docs/images/shot-wardrobe-mario.jpg" alt="The Start menu's Wardrobe over Glitzville: Mario and Goombella's looks, the cursor on Looks, and the tip: Mario as his brother or his rivals, in one press" width="49%">
  <img src="docs/images/shot-mirror-mode.jpg" alt="Riverside Station in Normal and in Mirror mode side by side: the door, the route map and Mario and Goombella swap sides, while the HUD stays the same" width="49%">
</p>

**Camera and photo mode.** The right stick turns the camera a little around Mario and eases back. Photo mode stops the game: the camera all the way round, the HUD hidden, a filter, a frame or depth of field; the Journal's Photos book keeps the pictures.

<p align="center">
  <img src="docs/images/gif-free-camera.gif" alt="The right stick swinging the camera round Mario and Goombella in Petalburg, and back" width="49%">
  <img src="docs/images/shot-photo-mode.jpg" alt="Photo mode outside the Great Tree, the HUD hidden: Mario and Goombella sharp, with the tree's red door, roots and the Punies blurred by strong depth of field" width="49%">
</p>

**Accessibility.** A difficulty slider, action-command assist and timing assist, an **Action Command cue** (a ring that closes on the A button), message windows up to 150%, **Read text aloud** (the message windows, the battle menus and the game's lists), game speed down to 50%, one-handed layouts, a message log, colour-blind filters, sound captions and **Less flashing**.

<p align="center">
  <img src="docs/images/shot-battle-cue-keelhaul-key.jpg" alt="A battle on Keelhaul Key: Mario jumps at an Ember while the action-command cue, a ring, closes on the A button over the game's own hint" width="49%">
  <img src="docs/images/shot-accessibility-page.jpg" alt="The Start menu's Accessibility page over the Rogueport Sewers: Message size chosen, with the colour-blind filter, less screen shake, less flashing and captions below, and the tip on its first line" width="49%">
</p>

**Controls.** Xbox, PlayStation, Switch, Steam Deck, GameCube adapters, MFi and about 2,300 more, named as you know them, with their own button icons. One **Bindings** page for every key and button, per player, usable with the controller alone; profiles per kind of controller; haptics, the partner wheel, and the controller's light in your partner's colour. Phones and tablets have on-screen controls in the GameCube's colours, sized by the screen's real size (the A button about 10 mm across on a phone or a 6-inch handheld, 12 mm on a tablet), with a layout editor behind the layout button beside START (drag, snap, pinch, sizes in millimetres, presets). Linux handhelds play through PortMaster. [docs/HANDHELDS.md](docs/HANDHELDS.md), [docs/STEAM_DECK.md](docs/STEAM_DECK.md)

<p align="center">
  <img src="docs/images/shot-crossplay-phone.jpg" alt="Paper Cut's desktop touch build at a phone's size (an iPhone 17 Pro's screen, landscape): the on-screen controls in the GameCube's colours over Mario on the bright approach to Hooktail Castle" width="70%">
</p>

**Saves.** Memory card saves are Dolphin's `.gci` files, backed up automatically and convertible between versions, synced through iCloud or any synced folder. Every save also goes into a library with a picture; each file can keep its own settings; quick resume picks up where you left off; saves move between devices by a QR code; the **save manager** takes GameShark, Action Replay and memory card images in and out; **portable mode** keeps everything beside the program. [docs/SAVE_FILES.md](docs/SAVE_FILES.md), [docs/SAVE_TRANSFER.md](docs/SAVE_TRANSFER.md), [docs/SAVE_MANAGER.md](docs/SAVE_MANAGER.md), [docs/PORTABLE.md](docs/PORTABLE.md)

**Audio.** One mixer, stereo or 5.1 and 7.1 from the game's own surround mix, Headphones (3D), sound from the DualSense's speaker, and your own music files in place of any track. [docs/SOUND.md](docs/SOUND.md)

**Languages.** The European version plays in English, German, French, Spanish or Italian, and any version can show the text of another disc you own. Paper Cut's own menus are in English, Deutsch, Français, Español, Italiano and Japanese; language packs add more. [docs/LANGUAGE_PACKS.md](docs/LANGUAGE_PACKS.md)

**Mods and the map editor.** A mod manager in the launcher; mods replace disc files, textures (Dolphin texture packs as they are) and text, change game data and run code; **Lua mods** run on every platform and version. The **map editor** places the game's own pipes, blocks, switches, signs and NPCs in a room and saves it as a mod. [docs/MODDING.md](docs/MODDING.md), [docs/LUA_MODDING.md](docs/LUA_MODDING.md), [docs/MAP_EDITOR.md](docs/MAP_EDITOR.md), [docs/CHARACTERS.md](docs/CHARACTERS.md)

<p align="center">
  <img src="docs/images/shot-map-editor.jpg" alt="The map editor in Rogueport: its panel at the left with the my-maps mod, a placed pipe and Save Block in the plaza with their markers, and Saved to the mods folder" width="49%">
  <img src="docs/images/shot-mod-manager.jpg" alt="The launcher's Mods tab: four example mods (a script and map mod, two Lua mods and a data mod), each switched on, with Hello, room's details open" width="49%">
</p>

**Practice.** A speedrun timer with load removal and LiveSplit support, frame advance, RNG control, editors for stats, items, badges, partners and story flags, a warp to any room, save-state slots, **replays** checked as they play back and **ghost races**. Achievements don't count while the practice tools are on. [docs/REPLAYS.md](docs/REPLAYS.md), [docs/LEADERBOARDS.md](docs/LEADERBOARDS.md)

**Everyday.** Fast-forward (Tab), screenshots (F12), Discord status, **Add to Steam** on Windows and Linux, a crash report with one click, and **updates** offered, checked against their checksum and installed at the next start (phones check too). [docs/DISCORD_STATUS.md](docs/DISCORD_STATUS.md), [docs/STEAM.md](docs/STEAM.md), [docs/UPDATES.md](docs/UPDATES.md)

**Faithful by construction:** GameCube behaviours the game relies on are reproduced on 64-bit hosts: big-endian disc data, integer division by zero, paired-single quantization, the C library's `rand` sequence and the GameCube's own maths library (MSL's fdlibm). Every platform computes the same results to the last bit.

## Install

- **Windows:** unzip `papercut-<version>-windows-x64.zip` and start `Paper Cut.exe`.
- **macOS:** unzip and open `Paper Cut.app` (Apple silicon). It isn't notarized: the first time, try to open it, then System Settings > Privacy & Security > Open Anyway.
- **Linux:** the zip, the AppImage or the Flatpak, on glibc 2.35 (Ubuntu 22.04) and newer. On a Steam Deck the first start sets itself up for the Deck's screen; in its game mode, the launcher's Add looks in the `discs` folder in Paper Cut's user folder and says where that is.
- **Linux handhelds:** the PortMaster package, for Anbernic and similar ARM64 devices (experimental: [docs/HANDHELDS.md](docs/HANDHELDS.md)).
- **iPhone and iPad:** the unsigned `.ipa` for your disc's release, installed with AltStore, SideStore or Sideloadly. Add your disc in the launcher (the system's file picker) or put it in the app's folder in the Files app; it stays listed through updates and reinstalls.
- **Android:** the `.apk` for your disc's release.

**The launcher.** Start Paper Cut and add your disc image (`.iso`, `.rvz`, `.gcm`...) to the launcher's library: it recognises the release and shows the disc's own banner. The launcher is a little paper theatre in the style of the game's menus, and works with a mouse, a keyboard, a controller or touch. It also holds the settings, so you can set things up before the game starts; the Multiplayer settings are the game's own, on its title menu and Start menu.

<p align="center">
  <img src="docs/images/gif-launcher-opening.gif" alt="The launcher opening: the red curtains part on a paper night sky with its moon and stars, the Paper Cut title and the disc library come up, and the cast springs up along the footlights, Paper Mario 64's partners among them" width="800">
</p>

**Controls:** controllers go by button position, as on a GameCube controller (a GameCube controller through an adapter plays as it is).

| GameCube | Keyboard | Xbox | PlayStation | Switch |
|---|---|---|---|---|
| Control stick | WASD or the arrows | left stick | left stick | left stick |
| C-stick | (none; set one if you like) | right stick | right stick | right stick |
| A | Space | A | Cross | B |
| B | E | X | Square | Y |
| X | R | B | Circle | A |
| Y | F | Y | Triangle | X |
| Z | Q | RB | R1 | R |
| L / R | 1 / 3 | LT / RT | L2 / R2 | ZL / ZR |
| Start | Enter | Menu | Options | + |

Change any of them on the Bindings page (Settings > Controls), in the Start menu or on the full settings page. Paper Cut's own keys:

| Key | Does |
|---|---|
| Esc | back, in every menu and page (as B) |
| F1 | settings |
| F2 | back to the launcher (it asks first) |
| F5 / F8 | save / load state |
| F6 / F7 | frame advance |
| Tab | fast-forward |
| F11 | fullscreen |
| F12 | screenshot |

## Linking your own discs

- **The game:** one disc image per release (JP, US, EU). On a computer, one download plays all three; on a phone, each release has its own app.
- **Paper Mario 64:** your own unmodified ROM, from the US, Japanese, European or iQue release (`.z64`, `.v64`, `.n64`, any byte order; hacks aren't linked; with the Japanese or iQue ROM, the PM64 move names stay the game's own), added in the launcher or the Start menu. It's read on your device only and kept in your user folder; nothing is uploaded. [docs/PM64_LINK.md](docs/PM64_LINK.md)
- **The Japanese disc, for Restorations (optional):** read once for Rogueport's original chalk outline; without one, the outline is redrawn on your own disc.
- **Your saves:** Dolphin's `.gci` files as they are, or the save manager for other formats.

## Status

| Platform | State |
|---|---|
| Windows (x86-64) | Plays. Every map boots and runs on JP, US and EU; the battle and story sweeps pass on JP and US. Built with clang. |
| macOS (arm64, x86-64) | Plays (the Aurora `.app`). Every map boots. |
| iOS | Plays on a device (iPhone) and in the Simulator. Disc image in the app's Documents. |
| Linux (x86-64, aarch64) | x86-64 is built on Ubuntu 22.04 and runs on glibc 2.35 (Ubuntu 22.04) and newer: the zip, the AppImage and the Flatpak. Played on desktops and the Steam Deck. aarch64 (handhelds, through PortMaster) builds and is packaged; not yet played on a device. |
| Android (arm64) | Plays on phones, tablets and handhelds such as the AYN Odin 3. The second screen on dual-screen handhelds is not yet tried on a real device. |
| Online play | Two to four games on any mix of Windows, macOS, Linux, Android and iOS, by room code (through the relay when a direct link fails) or by address. The host's game leads; a 30-minute session over a laggy, lossy connection held without a drop. Not yet checked between phones. |

## FAQ

- **Do I need an emulator?** No: it's the game's own code, rebuilt.
- **Where do I get the game?** From your own disc. Paper Cut includes no game data, and we can't help find it.
- **Can my friend on a phone play with me on my PC?** Yes: the same Paper Cut version, the same disc release (both US, say), and a room code.
- **Does online need a port forward?** No: with a room code, the games go through Paper Cut's relay when a direct link fails, encrypted end to end so the relay can't read anything ([how it works](docs/ONLINE_RELAY.md)). Playing **by address** skips the relay (the host forwards TCP port 47700, or everyone joins one Tailscale or ZeroTier network) and isn't encrypted by Paper Cut.
- **Will it change my saves?** No: saves are Dolphin's `.gci` format and go back to Dolphin or a GameCube. Online guests' own saves are never touched.
- **Is Paper Mario 64 included?** No: you link your own ROM, and it's read on your device.
- **Can I turn everything off and play the original?** Yes: the Vanilla preset, or console-accurate mode.
- **Which controllers work?** Xbox, PlayStation, Switch, Steam Deck, GameCube adapters and about 2,300 more, with their own button icons.
- **It crashed. What do I send?** The crash notice's report (a filled-in GitHub issue or a zip).
- **The game stalls or goes white entering a room on an older PC?** On PCs under 6 GB or with 4 threads or fewer, Paper Cut makes new graphics in the background by itself; on any PC, turning off Graphics > Never skip loading graphics does the same. Updating the graphics driver helps too.
- **How do I get back to the launcher?** F2, or Settings > General > Back to the launcher, in the Start menu or on the settings page. It asks first, and says if anything since your last save would be lost.
## Building and running

For players, Paper Cut is one app. Under the hood each release is its own build of the game code, because the three differ in their code and data: `ttyd-jp`, `ttyd-us` and `ttyd-eu`, configured with `-DTTYD_VERSION=G8MJ01`, `G8ME01` or `G8MP01`. Every build has the launcher. Build the versions you have discs for, side by side, and the launcher starts the right one for each disc. A build with a disc on its command line skips the launcher and plays it, if it's that build's version.

The game code is C, from the decompilation in [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd). The GameCube platform under it (graphics, audio, input, disc, memory card) is replaced, through [Aurora](https://github.com/encounter/aurora) (WebGPU/Dawn and SDL3).

### Windows

Needs Git Bash, LLVM clang, and Visual Studio Build Tools.

```bash
git clone --recurse-submodules https://github.com/LLCOOLT/paper-cut.git && cd paper-cut
. tools/port/winenv.sh
cmake -S . -B build-pc-us-aurora -G Ninja --toolchain cmake/toolchains/windows-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
"build-pc-us-aurora/Paper Cut.exe"
```

- `Paper Cut.exe` opens the launcher.
- `build-pc-us-aurora/ttyd-us.exe "path/to/your/disc.iso"` plays a US disc directly.
- `tools/port/release.py` packages the three builds together, as the release does.

### macOS

Needs Xcode (or its Command Line Tools), CMake 3.25+, Ninja, Python 3 and Homebrew LLVM. Only LLVM's `llvm-objcopy`/`llvm-nm`/`llvm-ar` are used, to link the RELs in; Apple clang compiles everything.

```bash
brew install cmake ninja llvm
git clone --recurse-submodules https://github.com/LLCOOLT/paper-cut.git && cd paper-cut
cmake -S . -B build-pc-us-aurora -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
open build-pc-us-aurora/ttyd-us.app
```

- **Opening a disc:** you can drop a disc image on the app's icon.
- **Where saves live:** saves, settings and caches are in `~/Library/Application Support/ttyd/`.
- **A standalone copy:** `cmake --install build-pc-us-aurora --prefix dist` makes a self-contained, ad hoc signed copy.
  - It isn't notarized, so another Mac blocks it the first time.
  - To open it anyway: try to open it, then go to System Settings > Privacy & Security > Open Anyway. Before macOS 15, right-click it and choose Open instead.
  - The release's `Paper Cut.app` holds all three versions.
- **Intel Macs:** the build is for the Mac's own architecture. To build for Intel on Apple silicon, add `-DCMAKE_OSX_ARCHITECTURES=x86_64 -DAURORA_NOD_PROVIDER=package`.

### iOS and iPadOS

An iPhone app can't start another program, so there is one app per version. Each app's launcher plays its own version's discs.

Needs the same tools as macOS, plus Rust with the iOS targets (Aurora's disc reader is built from source):

```bash
rustup target add aarch64-apple-ios aarch64-apple-ios-sim
cmake -S . -B build-ios-us -G Ninja --toolchain cmake/toolchains/ios.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 \
  -DTTYD_IOS_BUNDLE_ID_PREFIX=<your.prefix> \
  -DTTYD_IOS_CODESIGN_IDENTITY="Apple Development: <you>" \
  -DTTYD_IOS_PROVISIONING_PROFILE="<path to the .mobileprovision>"
ninja -C build-ios-us
xcrun devicectl device install app --device <device id> build-ios-us/ttyd-us.app
```

- **Signing:** the app is signed after every link with your identity and provisioning profile. The profile must be for `<your.prefix>.us` (or `.jp`, `.eu`).
  - With a paid developer team, Xcode makes a profile if you build any app with that bundle id for your device once.
  - Without one, build the unsigned `.ipa` (`ninja -C build-ios-us ttyd_ipa`) and sideload it with AltStore, SideStore or Sideloadly. A free Apple ID signs for 7 days at a time.
- **Simulator:** add `-DTTYD_IOS_PLATFORM=SIMULATOR` (Apple silicon Macs, signed ad hoc), then run `xcrun simctl install booted <build dir>/ttyd-us.app`. The first Simulator build compiles Dawn from source, so it takes a while.
- **The disc:** put your disc image in the app's folder in the Files app, or in Finder's device window. You can also add it from the launcher. RVZ is the smallest format.
- **Saves:** memory cards (`.gci`, interchangeable with Dolphin) and settings are in that folder's `Saves`.
- **Controls:** on-screen touch controls (movable, either way up, iPad included) or any MFi, Xbox, PlayStation or Switch controller.
- **Use the Ninja generator:** the RELs are linked by custom build steps that `-G Xcode` doesn't handle reliably. Xcode can still debug the app (Debug > Attach to Process).

### Linux

The release's Linux builds need glibc 2.35 (Ubuntu 22.04) or newer; they are built on Ubuntu 22.04 with the C++ runtime linked in. ARM64 handhelds have their own PortMaster package ([docs/HANDHELDS.md](docs/HANDHELDS.md)).

The release comes as a zip, an AppImage (`Paper_Cut-<version>-x86_64.AppImage`) and a Flatpak (`flatpak install --user papercut-<version>-linux-x64.flatpak`, which takes its runtime from Flathub). On a Steam Deck, the first start sets itself up for the Deck's screen: full screen, 16:10, the HUD at the edges and larger messages.

Needs clang (the RELs are linked in with clang-only section pragmas), CMake 3.25+, Ninja, Python 3 and LLVM's tools. On Debian/Ubuntu:

```bash
sudo apt install clang lld llvm cmake ninja-build python3 pkg-config
git clone --recurse-submodules https://github.com/LLCOOLT/paper-cut.git && cd paper-cut
cmake -S . -B build-pc-us-aurora -G Ninja --toolchain cmake/toolchains/linux-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
build-pc-us-aurora/ttyd-us
```

- **SDL3:** Aurora builds SDL3 from source unless a system SDL3 (3.2+) is installed. That needs the usual X11, Wayland, audio and input development packages; [docs/PC_PORT.md](docs/PC_PORT.md#linux) lists them.
- **Dawn:** Dawn (Vulkan or OpenGL) comes prebuilt for x86-64. On arm64, Aurora's disc reader is built from source, which needs Rust (`cargo`).
- **gcc:** to build with gcc instead of clang, add `-DTTYD_RELS_STATIC=OFF`, which builds the RELs as loadable modules. This is untested.
- **The release:** the release's `papercut` script starts the launcher.

Android builds, and more detail for every platform, are in [docs/PC_PORT.md](docs/PC_PORT.md).

## Testing

`tools/port/local_ci.py` runs the checks on one machine, for each version:
- every PC build configuration;
- unit tests and audits;
- the Linux cross-check and the Android build;
- every map with golden audio;
- memory card saves, the example mods and an Archipelago session.

CI runs it on self-hosted runners, with Linux in an Ubuntu container on a Mac ([docs/RELEASING.md](docs/RELEASING.md)).

The sweeps are driven by:
- scripted controller input (`TTYD_INPUT`);
- map and save warps (`TTYD_MAP`, `TTYD_LOAD_SAVE`);
- frame captures (`TTYD_SHOT`);
- per-run settings (`TTYD_SET_<setting>`).

[docs/PC_PORT.md](docs/PC_PORT.md#testing) and [docs/TESTING.md](docs/TESTING.md) list them.

## Documentation

| Document | Contents |
|---|---|
| [docs/PC_PORT.md](docs/PC_PORT.md) | Building on each platform, running, options, settings, tests, audits, design notes |
| [docs/FRAME_INTERPOLATION.md](docs/FRAME_INTERPOLATION.md) | How 120 Hz and up works, and how its purity is checked |
| [docs/PLAYABLE_LUIGI.md](docs/PLAYABLE_LUIGI.md), [docs/LUIGI_ANIMS.md](docs/LUIGI_ANIMS.md), [docs/PLAY_AS.md](docs/PLAY_AS.md) | Play as Luigi, and how his animations are made and shipped without any disc data; Peach and Bowser |
| [docs/VERSUS.md](docs/VERSUS.md) | Versus battles: the set-up, the stages, the fight, the computer, the ladder, online |
| [docs/PLAYERS.md](docs/PLAYERS.md), [docs/BROTHERS.md](docs/BROTHERS.md), [docs/DUO_MOVES.md](docs/DUO_MOVES.md), [docs/RAID.md](docs/RAID.md) | Three or four players; Brothers mode; duo moves and Bros. moves; the weekly raid boss |
| [docs/MODES.md](docs/MODES.md) | The Modes page; the boss rush, endless Glitz Pit, chapter select, remix, bingo, quiz party, Tattle cards, daily seed and audience modes |
| [docs/DOJO.md](docs/DOJO.md), [docs/DUNGEON_MASTER.md](docs/DUNGEON_MASTER.md) | The action-command dojo and timing assist by command type; another player as the enemies' dungeon master |
| [docs/MODDING.md](docs/MODDING.md), [docs/LUA_MODDING.md](docs/LUA_MODDING.md) | Making, installing and managing mods; Lua mods |
| [docs/MASTER_QUEST.md](docs/MASTER_QUEST.md), [docs/DIRECTORS_CUT.md](docs/DIRECTORS_CUT.md), [docs/BATTLE_EXTRAS.md](docs/BATTLE_EXTRAS.md) | Master Quest and writing its sets; what Director's Cut puts back; battle grades, the badge set and maker, new dishes, battle puzzles, the Enemy maker |
| [docs/POSTGAME.md](docs/POSTGAME.md) | After the story: area maps, the trophy room, the Paper Tower, daily shop specials, the new achievements |
| [docs/SAVE_FILES.md](docs/SAVE_FILES.md), [docs/SAVE_TRANSFER.md](docs/SAVE_TRANSFER.md) | The save library, settings per save file and the copy for Dolphin; sending saves between devices |
| [docs/REPLAYS.md](docs/REPLAYS.md) | Recording and playing back replays, and ghost races |
| [docs/SOUND.md](docs/SOUND.md), [docs/STEAM.md](docs/STEAM.md), [docs/UPDATES.md](docs/UPDATES.md) | The soundtrack, headphone 3D audio and the DualSense's speaker; Add to Steam; how updates are offered, checked and installed |
| [docs/SECOND_SCREEN_MENU.md](docs/SECOND_SCREEN_MENU.md) | The second screen's live Start menu on dual-screen handhelds |
| [docs/CHARACTERS.md](docs/CHARACTERS.md), [docs/COSTUMES.md](docs/COSTUMES.md) | Exporting a character, editing it (Blender, PNGs, JSON) and making it a mod; costumes, partner skins and stickers |
| [docs/PAPER_LOOKS.md](docs/PAPER_LOOKS.md) | Sharp text, smooth paper edges, paper shadows, paper physics, picture filters, and the launcher's stage, pop-up opening and look |
| [docs/RANDOMIZER.md](docs/RANDOMIZER.md) | The built-in item randomizer and Paper Cut's open world: seeds, what it shuffles, its logic, the spoiler log |
| [docs/STATE_SYNC.md](docs/STATE_SYNC.md) | Online play: what the games share, shared battles, joining late and coming back |
| [docs/ONLINE_RELAY.md](docs/ONLINE_RELAY.md), [docs/PAPER_NOTES.md](docs/PAPER_NOTES.md), [docs/LEADERBOARDS.md](docs/LEADERBOARDS.md) | How online play connects and what the relay sees; paper notes; leaderboards |
| [docs/PM64_LINK.md](docs/PM64_LINK.md), [docs/PM64_EXTRAS.md](docs/PM64_EXTRAS.md) | Linking your Paper Mario 64 ROM, what is read and where it goes; the partners, bosses, badges, enemies and soundtrack it brings |
| [docs/REMAKE_MAPS.md](docs/REMAKE_MAPS.md), [docs/MAP_EDITOR.md](docs/MAP_EDITOR.md) | The remake's map changes; the map editor and map edits in mods |
| [docs/DELOCALIZATION.md](docs/DELOCALIZATION.md) | Restorations of the Japanese release, and fan text packs |
| [docs/STEREOSCOPIC_3D.md](docs/STEREOSCOPIC_3D.md) | Stereoscopic 3D modes and settings |
| [docs/HANDHELDS.md](docs/HANDHELDS.md), [docs/STEAM_DECK.md](docs/STEAM_DECK.md) | Linux handhelds through PortMaster; the Steam Deck and PlayStation controllers |
| [docs/SAVE_MANAGER.md](docs/SAVE_MANAGER.md), [docs/PORTABLE.md](docs/PORTABLE.md) | Importing and exporting saves in other tools' formats; portable mode |
| [docs/MOD_COMPAT.md](docs/MOD_COMPAT.md) | Big GameCube mods and the port's mod tools |
| [docs/TESTING.md](docs/TESTING.md) | Offscreen test runs and the test pass |
| [docs/MOD_DATA.md](docs/MOD_DATA.md) | The game data mods can change |
| [docs/LANGUAGE_PACKS.md](docs/LANGUAGE_PACKS.md) | Translating Paper Cut's menus with a language pack |
| [docs/DISCORD_STATUS.md](docs/DISCORD_STATUS.md) | What the Discord status shows, and how it talks to Discord |
| [docs/RELEASING.md](docs/RELEASING.md) | CI, the self-hosted runners, and cutting a release |
| [docs/DECOMP_SYNC.md](docs/DECOMP_SYNC.md) | Bringing the decompilation's newer work into Paper Cut |
| [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) | Third-party code and assets, and their licences |

The game code's own documentation (how the game is put together, the glossary of its Japanese names, the differences between the versions, the game's known bugs) is in the decompilation, [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd/tree/main/docs).

## Credits

- **The decompilation:** [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd), started by NWPlayer123, with its contributors.
- **Paper Mario 64:** the [Paper Mario decompilation](https://github.com/pmret/papermario) and its contributors, the reference for reading your ROM and for how the Paper Mario partners, enemies and bosses behave. No code from it is included.
- **The platform layer:** [Aurora](https://github.com/encounter/aurora) (the GameCube SDK on WebGPU/Dawn and SDL3), [Dawn](https://dawn.googlesource.com/dawn) and [SDL](https://www.libsdl.org/).
- **The launcher's typeface:** [Fredoka](https://github.com/google/fonts/tree/main/ofl/fredoka), SIL Open Font License.
- **The settings page** follows the one in Dusklight, the Twilight Princess port on Aurora.

The original MIT licence and copyright notice are kept in [LICENSE](LICENSE).

Paper Mario and The Thousand-Year Door are trademarks of Nintendo. Paper Cut is not affiliated with or endorsed by Nintendo.
