# Paper Cut: what other ports have, and what would make ours easier to use

Compared with Dusklight (Twilight Princess, also on Aurora), Ship of Harkinian (Ocarina of Time), 2 Ship 2 Harkinian (Majora's Mask), Zelda 64: Recompiled, Unleashed Recompiled (Sonic Unleashed) and sm64coopdx (Mario 64 online), 2026-11-04.

## Already on par or ahead

These are covered, most of them further than the others go:

- High resolution, any frame rate, widescreen/ultrawide with the HUD kept at the edges or 16:9.
- HD texture packs (Dolphin's format), texture upscaling, sharp text, supersampling, picture filters, VRR, HDR.
- Mods with a manager, a built-in randomizer with spoiler logs, character export.
- Speedrun timer with splits, load removal and LiveSplit; frame advance, fixed RNG, memory watch, practice warps and flags.
- Achievements, boss rush, refights, endless Glitz Pit, New Game+, challenge presets.
- Autosave, save backups, copying saves between releases, folder/iCloud sync.
- Controllers: remapping per player, dead zones, a set-up wizard, matching button prompts.
- Menus in six languages; local co-op; Twitch chat; quick resume; update check; crash reports.
- Windows, Mac, Linux, Android and iOS; Steam Deck first-start settings.

Nobody else has: the whole settings list inside the game's own Start menu, two-player co-op where the partner walks freely, or a built-in randomizer that needs no server.

## What the others have that we don't (by value)

1. **Online co-op** (sm64coopdx; Ship of Harkinian lists it as planned). The user asked for it: being built now, see the plan below.
2. **Text-to-speech** (Ship of Harkinian, F9): the game's text and our menus read aloud. The biggest accessibility gap. The voice comes from the system (Windows SAPI, macOS/iOS AVSpeechSynthesizer, Android TextToSpeech, Linux speech-dispatcher), so no voice files ship.
3. **Restart now** where a setting applies "from the next start" (texture upscaling, graphics backend, MSAA). Dusklight restarts itself; we tell the player to.
4. **Free camera / camera tilt** (2Ship, Dusklight's Wii U free cam): the right stick nudges the camera in the field (zoom, a little tilt), back to the game's own when let go.
5. **Mirror mode** (Ship of Harkinian, Dusklight).
6. **Cosmetics** (Ship of Harkinian, Dusklight): Mario's and the partners' colours and the HUD's colours, chosen from a palette, no mod needed.
7. **Music shuffle and custom music** (Ship of Harkinian's audio editor): the jukebox's tracks shuffled per room, or replaced by an OGG/MP3/FLAC in a folder.
8. **Input display** (Ship of Harkinian's input viewer) for speedrunners and streamers.
9. **Save import and export** from the settings page while playing (Dusklight's Save Manager: .gci in and out, drag and drop), not only in the launcher before the game starts.
10. **Surround sound** 5.1/7.1 (Dusklight).
11. **Settings presets to share** (Ship of Harkinian's preset manager): export the settings as a file or a code, and load someone else's.
12. **Controller lights and rumble strength** (DualShock/DualSense light colour; rumble on/off and strength).
13. **Mod browser** (Dusklight 2.0): left out of 0.4.0 until there are mods to list.

## Using it: where it's harder than it should be

From going through the launcher, the settings page and the Start menu tab:

- **Settings that need a restart** say "From the next start" and stop there: a Restart button beside them (and one in the Start menu tab's help) would save quitting and reopening.
- **Controller names** come from the driver ("XInput Controller #1"): show "Xbox controller 1", "DualSense", "Switch Pro" instead, as the button prompts already know the kind.
- **Saves' tools only in the launcher**: restoring a backup and copying a save to another release are refused while a game runs. Doing it from the page (it reloads the card) would avoid quitting.
- **First start**: nothing says the settings are in the Start menu now. A one-time notice at the first room ("Settings: Start menu, last tab, or F1") as the co-op hint does.
- **The page's header** says "Paper Cut dev" on a release-built test; check it shows the version on a real release.
- **Long explanations**: some notes are four or five lines; the Start menu's help box cuts them into many pages. A one-line summary first, the detail on the page.
- **The Settings tab's label** sits on a plain shape, not painted like the game's other tabs (known gap).
- **Co-op**: now joinable and labelled (1P/2P, notices, the Players section); still not tried with two real controllers.

## Online co-op: the plan

Both players run their own copy of the game (each needs the disc, as for local play), and only the controllers' input crosses the network, the way Dolphin's netplay works:

1. **The same game on both sides.** The host sends a save state when the guest joins; both start from it on the same frame.
2. **Inputs in lockstep.** Each frame, both games send their player's controller input, a few frames ahead (the input delay, set from the ping), and a frame runs only when both inputs for it are there. Player 1's input is the host's, player 2's the guest's.
3. **Kept in step.** Every second, each game sends a hash of the game's state; when they differ, the host sends a fresh save state and both carry on from it.
4. **Determinism.** The game must play out the same from the same input: disc loads finishing on the same frame, the random numbers, timers. Tried first offline: one input script played twice, the state hashes compared frame by frame.
5. **Connecting.** First version: the host shares its address and port (or a code that holds them) and the guest types it; a relay for players behind strict NATs later.
6. **In the menus.** Online in the Players section: Host, Join, the code, the ping and the delay; notices in the game as for local co-op.

It needs no video streaming and no server of ours for the first version. The hard part is step 4.
