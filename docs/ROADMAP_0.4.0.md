# Paper Cut 0.4.0: where it stands

All the work is on the branch `feature/0.4.0` (from `main` at 0.2.0). Super Luigi (0.3.0) is on its own branch and wasn't touched.

**Everything agreed for 0.4.0 is done. JP, US and EU build** on Windows (2026-11-03), with no errors, and so does the headless build. Each feature below was tried once in the US game on Windows, through the test driver (`$TTYD_INPUT`), the settings page's test switches and window captures. Nothing has been tried on a phone, a Mac or Linux yet, and `tools/port/local_ci.py` hasn't been run on the branch.

## Done

| Feature | Where | Tried on Windows (US) |
|---|---|---|
| The settings in the Start menu: a Settings tab after the Journal, with every plain setting and All settings | `port/src/aurora/pause_settings_aurora.c`, `src/window/win_root.c` | Yes: the tab, scrolling, changing values (saved), All settings opening the page and coming back. JP and EU: the page draws. |
| Volume and sound sliders: one mixer, mute buttons, an audio taper, F9/F10/F3 keys | `port/settings/features/volume.cpp`, `port/src/common/audio_mix_pc.c` | Yes: the page; F9 three times took 100% to 85%, F3 muted, both saved |
| Better windowed support: opens where it was left, never off-screen, 1x-4x sizes, keep on top, Alt+Enter | `port/settings/features/window.cpp`, `main_aurora.c` | The place and size are saved; reopening there not checked by eye |
| Controllers: the community mapping list, players 1-4, battery, a live test, dead zones, remapping for every player, set-up for unknown pads | `port/settings/features/controllers.cpp`, `port/assets/controllers/` | The 869 mappings load and an 8BitDo pad is found; the page draws. The live test and dead zones weren't used |
| Autosave at room changes (off by default) | `port/src/aurora/autosave_aurora.c`, `features/autosave.cpp` | Yes, after a fix: a file loaded, Petalburg by fast travel, and the card loaded again starts there |
| Challenge presets and four new rules | `port/src/common/challenge_pc.c`, `features/challenges.cpp`, battle hooks | One-hit KO: a Goomba's hit knocks Goombella, then Mario, out (a Game Over); off, the same hits do 1 |
| Button prompts that match the controller | `features/button_prompts.cpp`, `port/settings/button_glyphs.cpp`, `src/driver/icondrv.c` | Yes: GameCube, Xbox, PlayStation and Switch pictures in a battle's messages and action-command help, following the mapping |
| Decomp sync in one command | `tools/port/decomp_sync.py`, `port/decomp_base.txt` | `--check`: 0 new decomp commits since 2ee4796 |
| Boss refights | `port/src/aurora/modes_aurora.c`, `features/modes.cpp` | Hooktail's starts, with its timer (`$TTYD_MODE_TEST=refight:3`). Winning and the way back weren't played |
| Second player plays the partner in battle, and guards the attacks on it | `port/src/common/second_player_pc.c`, `src/system.c`, `src/battle/battle_ac.c` | No: it needs two controllers. With one, nothing changes |
| HDR output (Windows D3D, Apple Metal; Linux and Android stay SDR) | `port/patches/aurora/0014-display-hdr-output.patch`, `features/display.cpp` | On an SDR display: the patch applies, the page says "Not available here" and the picture stays SDR. Not seen on an HDR display |
| Character export and import | `features/character_export.cpp`, `tools/port/character_tool.py`, `ttyd_tpl.py` | Yes: Export on the Mods tab, `character_tool.py export` completes it, `build` gives back the same files |
| The port's menus in German, French, Spanish, Italian and Japanese (System > Menu language) | `port/settings/tr.cpp`, `port/assets/lang/`, `tools/port/gen_lang.py` | The page in German and Japanese (US), the Start menu's tab in German (EU) and Japanese (JP); every text checked against the game's fonts |
| Built-in randomizer (US only) | `port/archipelago/rando_gen.cpp`, `ttyd_rando.cpp`, `features/randomizer.cpp` | The seed is made at start (173 items moved), its spoiler log written and Rogueport's 26 placements applied; no item picked up by hand. `rando_test`: 40 seeds |
| Bigger text: Accessibility > Message size (100-150%), the message and choice windows larger as far as each fits | `port/src/aurora/hud_aurora.c`, `features/display.cpp` | Yes: larger windows in 4:3 and widescreen |
| "Where next?" hints: Y on the Journal tab names the area of the story's next step, hidden until asked | `port/src/aurora/journal_hint_aurora.c`, `story_hints.inc` (`tools/port/gen_story_hints.py`) | Yes: story 30, 60 and the end; JP too |
| Save sync through any folder (Windows and Linux; Apple keeps iCloud): newer copy wins, a replaced save kept in Backups first | `port/src/aurora/folder_sync_aurora.c`, the Saves tab | Yes: a save brought from the folder and loaded, a new file copied to it byte for byte |
| Co-op in the field: the second player's stick walks the partner anywhere, A hops, B talks or hits, X is its ability; Mario's Y calls it back | `port/src/common/second_player_pc.c`, `src/party/party_move.c`, `features/second_player.cpp` | With the test driver's controller 2 (`$TTYD_P2_PRESS`): walking, the hop, a Toad's talk, the leash and the recall. Not with two real controllers |
| Texture upscaling (Display: 2x or 4x, from the next start): our own edge-following filter, not xBRZ | `port/patches/aurora/0015-gx-texture-upscale.patch`, `port/src/aurora/upscale_aurora.c` | Yes: Rogueport at 4x, its graffiti and rope smoother; the patches apply on a clean Aurora |
| Linux AppImage and Flatpak, made from the release's Linux zip | `tools/port/linux_packages.py`, `port/linux/`, `linux-release.Dockerfile` | No: the Mac (where the Linux container runs) wasn't reachable. Only the script's file layout was checked, on Windows |
| A Steam Deck's first-start settings: full screen, 16:10, the HUD at the edges, messages at 115% | `port/src/aurora/settings_aurora.c` (`portSteamDeck`), `features/window.cpp` | On Windows with `$TTYD_STEAM_DECK=1`: the 16:10 picture and the settings saved. Not on a Deck |

**Fixed while trying them:**
- Autosave never saved: it forgot the room whenever the game left the field, and every room change goes through the map-change sequence.
- The Saves tab drew its two sections' headings twice (also in 0.2.0).
- A settings-page crash the Settings tab's first version had: a section's draw function read as a setting.
- The iCloud Drive section was missing from the Mac's and iPhone's settings page (also in 0.2.0): the settings library wasn't built with `PORT_CLOUD_SYNC`.

**Known gaps:**
- The randomizer's key-item mode needs the TTYD AP mod (built separately, GPL).
- HDR10/PQ isn't done.
- The remap list shows SDL's names for a controller's buttons (`rightshoulder`, `dpup`) and `?` for the triggers (as in 0.2.0).
- The settings page's sections draw some text of their own (buttons, lists, status lines) that is still English; the labels, notes, choices and tabs are translated. The Settings tab's label sits on a plain shape, not painted like the game's.
- The translations are Claude's, with the games' own names for their terms (read from the discs' text); a native speaker hasn't checked them.
- Some settings' explanations mention "this page" or "below", written for the settings page.
- The Linux packages haven't been built: the AppImage needs appimagetool (in the container now), and the Flatpak, written without `flatpak-builder`, takes its runtime (org.freedesktop.Platform 24.08) from Flathub on install.
- Texture upscaling skips textures larger than 512 texels a side and those uploaded as they are (block-compressed ones); at 4x a big room's textures take 16 times the video memory.

## Left out of 0.4.0

- **The mod browser** (the user, 2026-11-03: left out until there are mods to list). It downloaded a catalogue of mods (`mods/catalogue.json` on `main`) and installed, updated and uninstalled them from the Mods tab, with each zip checked against the catalogue's sha256 and refused if it could write outside the mods folder. Only our three example mods were in it. It is in this branch's history: commit `646ab562b` (and its `docs/MODDING.md` section); `port/settings/http.cpp`, which it brought, stays for the update check.
- **An open-world randomizer of our own** (the user, 2026-11-03: the next version's, not 0.4.0's). Key items shuffled without the TTYD AP mod: the built-in randomizer's logic already has the apworld's 403 locations and rules (MIT); what's missing is the game opened up, an "open world" setting that sets the story flags and opens Rogueport's pipes and the doors the rules assume open, and each area's map scripts changed where the story assumes an item or a finished chapter (27 areas; the AP mod's changes are about 18,500 lines). It is to be written from the decomp and the apworld alone: the TTYD AP mod is GPL, so its code is not a reference. Suggested in stages (the core, Rogueport and chapters 1-2; then 3-5; then 6-8 and the Palace), each checked with scripted runs that hand out the key items in an unusual order, and whole seeds played through at the end.

## Also still to do
- `tools/port/local_ci.py` on the branch, and a try on a phone, a Mac, Linux (the AppImage and the Flatpak) and a Steam Deck.
- Release notes: `docs/release_notes/0.4.0.md`.
- A pull request from `feature/0.4.0` to `main`.
