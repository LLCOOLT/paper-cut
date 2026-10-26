Mod support: survey and plan
============================

Status: **M1–M3 done** (assets, data and code: [MODDING.md](MODDING.md)); M4 onward planned. Survey done October 2026 from public sources (GitHub, GameBanana, the Dolphin forums, Super Mario Wiki, the Paper Mario Technical Knowledge Base). Items marked *(unverified)* came from one weak source.

Contents:
1. [What exists](#1-what-exists): GameCube mods, codes, texture packs, Switch mods, how other ports do it
2. [What TTYD mods need from the game](#2-what-ttyd-mods-need-from-the-game)
3. [Design](#3-design): five layers, one mod format
4. [Phases](#4-phases)
5. [Mod ideas](#5-mod-ideas): remake backports, built-ins from codes, new mods
6. [Licences and rules](#6-licences-and-rules)

---

## 1. What exists

### 1.1 GameCube code mods (REL mods)

Nearly every serious TTYD mod is a **REL**: C++ built with PistonMiner's ttyd-tools framework, loaded from the memory card by a Gecko/AR code or a save exploit, which branch-patches game functions. Almost all are **US-only** and **GPL-3.0**.

| Mod | Author | What it is | Source |
|---|---|---|---|
| ttyd-tools | PistonMiner | The REL framework (`patch::hookFunction`), elf2rel, gcipack, `ttydasm` (evt disassembler), Blender map exporter | [github](https://github.com/PistonMiner/ttyd-tools) |
| REL Loader V1–V3, TTYD-REL-Template | Zephiles | The loader codes (hook the `marioStInit` call), mod template; JP/US/EU | [github](https://github.com/Zephiles/TTYD-Practice-Codes/tree/master/relloader) |
| TTYDSaveHacker, ttyd-exploit | magcius, Zephiles | Save-file exploits that start a loader | [github](https://github.com/Zephiles/ttyd-exploit) |
| **Practice Codes** v4 | Zephiles | Trainer: inventory, 34 cheats, stats, memory editor, 23 displays, warps, 25 saved states, frame advance; JP/US/EU | [github](https://github.com/Zephiles/TTYD-Practice-Codes) |
| Levitate Mod | Zephiles | Moon jump | [github](https://github.com/Zephiles/TTYD-Levitate-Mod) |
| TTYD-Randomizers (archived) | Zephiles | Item and loading-zone randomizers | [github](https://github.com/Zephiles/TTYD-Randomizers) |
| **Infinite Pit** | jdaster64 | Roguelike Pit of 100 Trials with custom enemies, items, rewards; US | [github](https://github.com/jdaster64/ttyd-infinite-pit) |
| **Tower of Trials** | jdaster64 | Roguelike with **new maps** (area `gon`), costumes, achievements, new save file; US, ISO patcher | [github](https://github.com/jdaster64/ttyd-tot) |
| Shufflizer | jdaster64 | Item shuffle and rebalance, options menu, seed from the file name; US | [github](https://github.com/jdaster64/shufflizer) |
| ttyd-utils | jdaster64 | Symbol CSVs (US/JP/EU), evt dumps, struct docs | [github](https://github.com/jdaster64/ttyd-utils) |
| **TTYD Randomizer / Archipelago** | jamesbrq | Multiworld randomizer: ISO patch + RELs; Python client reads Dolphin RAM | [mod](https://github.com/jamesbrq/TTYDAP), [AP world](https://github.com/jamesbrq/ArchipelagoTTYD), [site](https://www.ttydrandomizer.com/) |
| Hero Mode | Reed | Difficulty and story overhaul (xdelta, no source) | [github](https://github.com/Reeds-github/Hero-Mode) |
| Thousand-Year Door+ v5 | Griscuit | Script fixes, localization reverts (xdelta) | [romhacking.net](https://www.romhacking.net/hacks/3092/) |
| Paper Daisy HD (+ text sub-mod) | N-Reun, simplytadpole | Model/texture/text swap by file replacement | [itch.io](https://simplytadpole.itch.io/paper-daisy-ttyd-compatible-text-changes) |

Not found: a public LiveSplit autosplitter, a "64-style" mod, a standalone debug-menu restoration (Practice Codes force the debug flag on).

### 1.2 Codes, texture packs, text and music

- **Gecko/AR codes.** Dolphin ships [G8ME01.ini](https://github.com/dolphin-emu/dolphin/blob/master/Data/Sys/GameSettings/G8ME01.ini) / G8MJ01.ini: max HP/FP/BP codes (AR `04`/`02`/`42` data writes) and four 16:9 HUD variants plus "No Letterbox" (code patches: `04 blr`, `C2` inserts). The forum thread adds full Tattle Log, Strange Sack, Hard/Insane mode, culling off, static camera, Yoshi colour. Most *popular* codes are **data writes**; the rest are **code patches** that a native build can't run.
- **HD textures.** Invader's [TTYD HD Texture Pack v2.5](https://forums.dolphin-emu.org/Thread-paper-mario-ttyd-hd-texture-pack-v2-5-may-9th-2024) (PNG/DDS, incomplete), Dolphin naming `tex1_{w}x{h}[_m]_{xxh64}[_{tlut}]_{fmt}`.
  **Aurora already implements this format** (`extern/aurora/lib/gfx/texture_replacement.cpp`: same XXH64 over the GC-encoded base level and used palette range, `$` wildcards, `_mipN`, `_arb`, DDS BC1–7/ASTC, async streaming). The port never calls `aurora::texture::load_replacement_directory`, so it is off today.
- **Text.** `msg/*.txt` are plain tag/text pairs; a PT-BR fan translation exists *(unverified completeness)*; TTYD+ is mostly text.
- **Music.** Only an ISO-rebuild tutorial for replacing streamed music; no remake-music pack found for GameCube.
- **Format references:** noclip.website's `src/PaperMarioTTYD/` (maps, TPL, agb, evt, REL), the Skelux wiki, PistonMiner's Blender exporter.

### 1.3 The Switch remake (2024)

- **Engine:** new, not the GameCube code. `.bfres`/`.bntx`, `.zst`, `.elf` data tables, `.msbt` text, AngelScript bytecode for events ([Technical Knowledge Base](https://papermariotkb.wiki.gg/wiki/Paper_Mario_TTYD_Remake_(Switch)_Technical_Knowledge_Base)). Nothing from it is reusable as data here; only **ideas** transfer.
- **Tools:** Unsimplifier (Darxoon, `.elf` table editor), ElfTool, Ascend (PistonMiner, AngelScript disassembler), plus generic Switch tools.
- **Mods** ([GameBanana](https://gamebanana.com/games/19143), ~158): see [1.3.1](#131-switch-mod-sweep).
- **Differences from GameCube** that make good optional mods here: see [5.1](#51-remake-backports).
- Note: the TCRF page for the remake served text aimed at AI agents (asking for payment and to write a key file) instead of content; it was ignored, and TCRF facts here are second-hand.

#### 1.3.1 Switch mod sweep

All 154 mods in GameBanana's Switch section (through its API), plus its 18 sound mods, 10 WiPs and 5 tools, GitHub and a Cheat Engine table. Nexus Mods has no TTYD page.

| Kind | Mods | Examples |
|---|---|---|
| Character, partner, NPC skins | 70 | Playable Toadette, Paper Luigi, Bow over Flurrie |
| Gameplay and balance | 23 | Easy Superguard, enemy HP/damage multipliers, no 99 damage cap, un-nerfed Power Bounce, badge reworks, Glitz Pit boss rush, loading-zone randomizer |
| Text | 15 | TTYD Omega (the 2004 script), "Shadow Sirens" restorations, translations |
| Other textures, maps, logos | 10 | Retextured Edition |
| GameCube-look restorations | 6 | JP chalk outline, Peeka and Lahla's ears, the GameCube moon |
| Graphics/performance patches | 6 | 60 FPS, resolution to 8K, ultrawide, SSR/AA off |
| Saves | 6 | 100% save, Save File Compendium |
| QoL code patches | 5 | faster text, first-read text skip, first-view cutscene skip, battle-menu speed-up |
| Total overhauls | 5 | Encore, Paper Mario Shadows, Ultra Spicy, New Game+ |
| Prompts, cheats, audio | 8 | Xbox/PS/GameCube glyphs, 999 of everything |

60 FPS (29k downloads), resolution (28k) and faster text (16.6k) lead by ten times; restorations of the 2004 look and script and hard modes are the most liked. Most of the popular ones are things the port has or gets for free (60 Hz logic plus 120 Hz, any resolution, widescreen, fast text, the 2004 script, the original Power Bounce). The rest feed 5.2.

### 1.4 How other native ports do mods

| Port | Assets | Code | iOS/console answer |
|---|---|---|---|
| **Dusklight** (Twilight Princess, **same Aurora + decomp stack**) | `.dusk` zip: `mod.json`, `overlay/` (disc paths, archive members as dirs), `textures/` (Dolphin names) | Native C++ `DEFINE_HOOK(fn)` pre/post/replace by decomp symbol; versioned C "services" (Log, File, Overlay, Texture, Config, Save, Ui, Gfx, Camera…); **Luau** scripts sandboxed | Native mods only when bundled into the app; Luau and assets everywhere ([docs](https://github.com/TwilitRealm/dusklight/blob/main/docs/modding.md)) |
| N64Recomp / Zelda64Recomp | `.nrm` zip, `.rtz` texture packs | Mods are MIPS code built against the decomp, recompiled live; `RECOMP_PATCH`/`RECOMP_HOOK` by jump-patching native code | Needs W^X exceptions: not iOS |
| Ship of Harkinian / 2Ship / Starship | `.o2r` archives in `mods/`, per-file priority, `alt/` toggle | Compile-time only: `GameInteractor` hooks and "should vanilla run" gates | Works everywhere because nothing loads code |
| sm64coopdx | DynOS model packs | Lua mods: `hook_event`, `hook_behavior`, mod-menu widgets | Lua everywhere |
| Perfect Dark port | `--moddir`: files, textures, animations by ID | None | n/a |

**Takeaway:** copy Dusklight's shape (same stack, same constraints), take SoH's compile-time hook points for iOS, and add what is unique to TTYD: the game's own **evt script language** is a sandboxed, data-driven way to add events and maps that runs on every platform.

---

## 2. What TTYD mods need from the game

From reading the seven open-source frameworks' hooks (base ttyd-tools, Practice Codes, Randomizers, Shufflizer, Infinite Pit, Tower of Trials, TTYDAP):

| Need | Hooked by | Port today |
|---|---|---|
| Per-frame update before/after `marioStMain` (or `makeKey`) | 7 / 7 | `vi_aurora.c` frame loop; no mod callback |
| Debug/2D draw layer (`dispEntry` on `kDebug3d`/`k2d`, font, icon, window primitives) | 6 | game functions exist; ImGui overlay exists |
| `msgSearch` override (custom/replaced text) | 6 | `src/driver/msgdrv.c:1489` |
| Post-REL-link callback (`OSLink`) to edit a map's evt scripts and data | 5 | `portRelLink` in `port/src/common/rel_pc.c` |
| `seqSetSeq`, `seq_mapChangeMain`, new file (`stg0_00_init`), load (`cardCopy2Main`) | 3–5 | game code |
| Battle maths: `BattleCalculateDamage`, `BattleCalculateFpDamage`, `BtlUnit_GetWeaponCost`, `BattleActionCommandCheckDefence`, `BtlUnit_Entry` | 2–3 each (ToT: ~40 battle functions) | game code |
| Items/pouch: `itemEntry`, `pouchGetItem`, `pouchReviseMarioParam` | 2–3 | game code |
| `psndBGMOn_f_d`, `statusWinDisp`, `winItemMain`, `winLogMain` | 2–4 | game code |
| Data edits: `itemDataTable`, weapon/unit tables, shop tables, GSW/GSWF, pouch, `g_BattleWork`, `_globalWorkPtr` | 3–5 each | C globals with decomp names |
| File swaps: `msg/*.txt`, `rel/*.rel` data, models through `_fileAlloc`, `icon.bin/tpl`, new areas via `relSetEvtAddr`/`relSetBtlAddr` | ToT, TTYDAP, Paper Daisy | every disc read goes through `DVDMgrOpen` (`src/manager/dvdmgr.c:142`) or `src/sound.c:297` |
| Mid-function splices (`writeBranchPair` at per-region addresses) | Practice Codes, ToT (~50 sites) | need explicit callback points |

So: **four generic services** (frame, draw, text, post-link) plus **named override slots** for ~40 functions and **symbol-addressed data** cover nearly all of it. Arbitrary instruction patching is the only thing that can't come over, and the port doesn't need it because it has the source.

---

## 3. Design

One mod format, five layers. Lower layers run everywhere; only layer 4 (native code) is restricted on iOS.

### 3.0 The mod bundle

`mods/<name>.ttydmod` (a zip) or `mods/<name>/` (a folder, for development), in the user folder next to `settings.txt`:

```
mod.json        id (reverse-DNS), name, version, author, description,
                games: ["G8ME01", "G8MJ01"], dependencies, conflicts,
                options: [{key, type, default, label}]   -> mod.<id>.<key> in settings.txt
overlay/        files replacing or adding disc files, by disc path (msg/US/global.txt, a/..., rel/...)
textures/       Dolphin-format texture replacements
patches/*.toml  data patches by symbol and field (layer 2)
evt/*.evt       evt scripts in text form (layer 3)
lib/<platform>/ native libraries (layer 4): windows-x64, linux-x64, android-arm64, macos
```

- **Mods tab** on the settings page: enable, order (top wins), per-mod options from `mod.json`, errors and logs, "restart needed" when a mod changes code.
- **Precedence:** later mods win per file and per field; a mod's textures beat the user's loose `textures/` folder; both beat the disc.
- **Version-aware:** a mod declares the game IDs it supports; the US-only GameCube mods mark `G8ME01`.
- **Saves:** the active mod set (ids and versions) is recorded beside each save; loading a save made with a different set warns. Mods that change the save layout (Tower of Trials grows it) get their own save file name.

### 3.1 Layer 1: assets (all platforms)

- **File overlay:** Aurora's DVD layer already has one (`aurora_dvd_overlay_files`, used by Dusklight): every disc read, path lookup and directory listing sees the overlaid and added files, so no game code changes.
- **Textures:** call `aurora::texture::load_replacement_directory` for `textures/` and each mod's `textures/`, plus a "dump textures" switch for pack authors. Check hash parity with Dolphin on the HD pack: the port must hand GX the original GC-encoded texture bytes, which the TPL loader keeps. EFB copies and CPU-drawn textures won't match Dolphin's names.
- **Text:** overlaid `msg/*.txt`, plus *additive* message files (`overlay/msg/US/mod_<id>.txt`) merged into the lookup so mods add keys without replacing `global.txt`.
- **Music:** overlaid `.stm`/`.dsp` streams; later, an `.ogg`/`.opus` stream format beside them so music packs don't need GameCube encoders.

This alone covers Paper Daisy, TTYD+, Hero Mode's assets, translations, the HD pack and music swaps, once their xdelta/ISO patches are turned into overlays (a converter that diffs a patched ISO against the clean one and writes `overlay/` could do that automatically).

### 3.2 Layer 2: data patches (all platforms)

Declarative edits of the game's own tables, by **decomp name and field** (built in M2; [MODDING.md](MODDING.md#patches-game-data)):

```
itemDataTable[ITEM_POWER_BOUNCE].bp_cost = 2
itemDataTable[*].sell_price = 1
```

- A **reflection table** generated from the decomp headers by `tools/port/gen_reflect.py` (clang's AST, `offsetof`/`sizeof` in the generated C) names the tables: items, every item/badge/move attack, Mario's and the partners' battle units, and the decomp's enum constants. [MOD_DATA.md](MOD_DATA.md) lists them for authors.
- **Area data** (a map's shop list, a Pit floor table, enemies' units) is mostly `static` in each area's code, so it has no name to patch by. It moves to M3, through hooks where the game uses it (e.g. the shop set-up), which also survive the data being restored on every area load.
- **Gecko/AR data codes: not built.** Translating them by address turned out to reach little: the popular codes write the game's heap (`04B07BD0` and similar: the item bag at its GameCube heap address, which has no counterpart in the port's heap) or file-local statics through pointers (`4241EBE0` reads a pointer inside a `static` struct). Only codes on global data in the DOL would translate. The codes' effects become built-in options instead (5.2), and data patches cover the rest.

### 3.3 Layer 3: scripts (all platforms)

- **evt scripts as data.** Most TTYD logic is evt bytecode the game interprets. A mod ships evt in a text form (compatible with `ttydasm`'s output), assembled at load time with names resolved against a table of evt user functions and game data. This adds or replaces map events, NPCs, shops and battles, and **new areas** (registered through `relSetEvtAddr`/`relSetBtlAddr`, as Tower of Trials does) without native code, so it runs on iOS.
- **Luau** (as Dusklight): sandboxed scripts for logic and UI that evt can't express (menus, trackers, randomizer logic), calling the same services as native mods. Added after evt, only if mods need it.

### 3.4 Layer 4: native code (desktop and Android; iOS bundled)

- **Hook points in the game's C**, under `TARGET_PC`, chosen from the table in section 2: a generated `PORT_HOOK_SLOT(fn)` makes each listed function callable through a slot with pre-, post- and replace-hooks (SoH-style: no code patching, so it works on iOS). About 40 functions to start, grown on request.
- **Services** (versioned C structs of function pointers, Dusklight-style): Log, Frame (pre/post), Draw (2D layer with the game's font/icon/window primitives, and ImGui), Text (`msgSearch` override and additive messages), Rel (post-link callback with module name), File (overlay), Data (layer 2 at runtime), Save (per-mod data in the save), Config (options), Input, Sound.
- **Loading:** `.dll`/`.so`/`.dylib` from `lib/<platform>/` on Windows, Linux, macOS and Android. **iOS:** no runtime code loading; native mods are linked into the app at build time (`-DTTYD_MODS=a;b`), and the Mods tab lists only those plus asset/patch/evt/Luau mods.
- **ttyd-tools compatibility SDK:** headers declaring ttyd-tools' `ttyd::*` and `gc::*` names over the decomp's functions, and a `patch::hookFunction` that registers with the hook slots (and fails loudly for unsupported targets). Practice Codes, Shufflizer, Infinite Pit, Tower of Trials and TTYDAP can then be rebuilt as native mods by their authors or by us in separate repos, with their `writeBranchPair` splices turned into named callback points.

### 3.5 Fitting the port's existing rules

- **120 Hz:** mod frame callbacks run only on real (logic) frames; draw callbacks get the interpolation factor and must not change game state, the same rule as `portInterpInBetween()`. The interpolation audit (`TTYD_INTERP_AUDIT`) runs with mods on.
- **Save states:** native mods put their globals in the snapshot sections (the prefix's pragma, or a `PORT_MOD_STATE` macro); Luau and evt state live in game memory already.
- **Tests:** vanilla stays the default; golden audio and the map sweep run with mods off, plus one sweep with a reference mod set.
- **GameCube matching:** hook slots are `TARGET_PC`-only; the GameCube builds stay byte-identical.

---

## 4. Phases

| # | Phase | Delivers | Size |
|---|---|---|---|
| M1 | Assets (**done**) | `mods/` folder, `mod.json`, folders and zips, file overlay, added text, texture packs on (with a dump switch), Mods tab (enable/order), example mod, local CI stage | small |
| M2 | Data patches (**done**) | reflection table, patches by name, originals restored when a mod is off, data reference for authors; area data moved to M3, Gecko translator dropped (3.2) | medium |
| M3 | Hook slots and services (**done**) | 40 hook slots (generated dispatchers, no code patching), the API (frames, drawing, messages, areas, shops, battles, options, game lookup), libraries on Windows (tested) and Linux/macOS/Android (untested), mods built into the game (iOS), example code mod | medium |
| M4 | ttyd-tools SDK | compatibility headers; port Practice Codes first (it has JP/US/EU and exercises most services), then Infinite Pit | medium |
| M5 | evt scripts | text evt assembler, new-area registration; a sample mod that adds a room | medium |
| M6 | Archipelago | TTYDAP's logic as a native mod plus a built-in AP client (websocket), no Dolphin memory reading | medium |
| M7 | Luau | only if M3–M5 leave real gaps | large |

Each phase ships with an example mod in `examples/mods/` and a test in the local CI.

---

## 5. Mod ideas

### 5.1 Remake backports

Remake *behaviour* re-created on the GameCube engine, as one "Remake QoL" mod with per-feature toggles (no Switch assets).

| Feature | Work |
|---|---|
| Partner Ring (quick-swap radial on L) | small code: overlay calling the party-swap routine |
| Save blocks without the "Save?" prompt | trivial code |
| Bag 15/20 slots, coin cap 9,999, Money Money cap 99 | data + small code (check save field widths) |
| Partners can't drop to 0 HP on the field; revive at 1 HP after battle | small code |
| Level-up: pick with the stick, heal all partners | small code |
| Skip cutscenes (power-ups, cooking, rematch intros, Excess Express) | medium: per-event skip to the end state |
| Game Over: retry the last scene; Pit floor retry | small code (save-state machinery helps) |
| Pit: coins per win, floor-50 Mover after a clear, better Charlieton stock | data + evt |
| Warp hub (menu-driven, unlocked per Crystal Star) | small code + existing entry points; a real pipe room would need a map |
| Glitz Pit: no rank loss on defeat; conditions shown on screen; Exhibition matches | small code/data |
| Tattle check from the Plus menu without a turn | small code |
| GUARD/SUPERGUARD pop-ups, status-resist bounce, target status icons | small code, existing font/sprites |
| Notes tab (Trouble Center, hints), map completion % | new UI, no art |
| Partner hints (ZL) | new system + new text we write |
| **Remake balance** toggle: Star Power multipliers (Peril 6.0→2.38, BINGO 2/3→1.4/1.7, audience values), Power Bounce windows, First Attack 0 BP, Hammer Throw piercing, Power Lift 4 matches, Sleepy Sheep 3 turns, boss coin rewards | data/constants |
| Whacka superboss on Pit floor 100 | medium: GameCube Whacka model exists; new unit, AI, Pit logic |
| Prince Mush superboss | new system + no GameCube model: reuse a fighter model with new AI |
| Sound test (per-chapter unlock) | new UI, no new assets |
| Boo quizzes, extra Tattle entries, Hottest Dog recipe | evt + new text; the recipe needs an icon |
| Art gallery, trophies, card minigames | need new art: out of scope |

The remake's 30 fps showed which timings are frame-counted (guard windows): any frame-rate setting here must keep the game's logic at 60 Hz, as the 120 Hz work already does.

### 5.2 Built-ins from popular codes

Not mods, settings, because the GameCube codes and Switch mods patch code:
- Display: letterbox off, culling off (draw distance), 21:9 and other ratios next to 16:9.
- Speed: text and cutscenes skippable on first viewing, battle-menu input speed-up.
- Assists: Easy Superguard, lower action-command difficulty, party can't die.
- Difficulty: enemy HP/damage multipliers, Hard/Insane scaling, removable 99 damage cap.
- New file: Yoshi colour and name, start inventory, New Game+, full Tattle Log, max stats (practice only).
- Music: battle-theme choice, the "thinking" variant off.
- JP visuals on the US build (chalk outline, Peeka and Lahla's ears), when the player has the JP disc.

### 5.3 New mods (port-native)

- **Practice suite built in:** warps (have), save states (have), frame advance, input display, GSW/GSWF flag editor, memory watch by symbol, RNG display and seeding, action-command timing display (frame-perfect window feedback), trick-practice states. Practice Codes as a native mod covers most of this; the timing display and RNG control are new.
- **Speedrun timer and autosplits:** in-game timer with load removal and splits on GSW changes, plus a LiveSplit server connection; none exists for TTYD today.
- **Accessibility:** action-command assist levels (wider windows, auto-press with a note in the save), colour-blind-friendly stylish/guard cues, larger text, hold-instead-of-mash, one-hand control layouts for the touch overlay.
- **Difficulty slider:** enemy HP/ATK scaling, Danger/Peril SP, item limits, "Superstar" rules (no Superguard, no Life Shroom), in the spirit of Hero Mode and the Switch mod Encore.
- **Randomizer in the box:** Archipelago (M6) plus a solo seed mode in the Mods tab, without patching an ISO.
- **Two-player battles:** a second controller drives the partner in battle (and optionally in the field). Fits the input layer that already exists.
- **Photo mode:** pause, free camera (the 120 Hz camera interpolation already separates the render camera), hide HUD, depth-of-field toggle, filters; export at the internal resolution.
- **Boss rush and endless Glitz Pit:** evt-only mods over existing battle setups.
- **Playable Luigi:** Luigi's NPC model is on the disc, but only with NPC animations; field play as Luigi needs a mapping from Mario's motions to the animations Luigi has, or new animations (as the Switch mod Paper Luigi needed).
- **Restored content:** unused maps, enemies and text in the decomp, as an opt-in "restorations" mod (Super Mario Wiki and the decomp list candidates).
- **Debug menu:** the game's own debug paths (the debug flag Practice Codes force on) behind a developer option.
- **Music packs:** `.ogg` streams per track, including the user's own remake soundtrack rips (users supply them; we never ship them).
- **Map viewer / model viewer:** a tool mode that loads any map or agb with a free camera, useful to mod authors.

---

## 6. Licences and rules

- The port is MIT. The GameCube mods are mostly **GPL-3.0**: they stay in their own repos and ship as separate mods; this repo includes only the SDK and MIT example mods. Copying their code into this repo would make the port GPL.
- Dusklight's mod system is a **design** reference; reuse its code only if its licence allows, with credit.
- No disc-derived data in mods hosted here; mods carry only their own files. Overlays that need original data (an xdelta-style patch) are applied on the user's machine.
- No Switch assets, ever; remake features are re-implemented.
- Mods are off by default, and the vanilla game's tests stay the gate.
