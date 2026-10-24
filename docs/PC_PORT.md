# Native PC port of the TTYD decomp

Paper Mario: The Thousand-Year Door, Japanese (G8MJ01) and US (G8ME01), as a
native 64-bit program, built from this decomp. The game code is the decomp's;
the GameCube platform under it is replaced. No game data is shipped: the port
builds without it and runs from the user's own disc image.

## Status

| Milestone | State |
|---|---|
| Builds on Windows (clang + lld), JP and US, headless and Aurora | done |
| Boots to the title (JP, US) | done |
| Every map of the game's map table boots and runs (279 per version) | done, headless and Aurora |
| Field play: walking, jumping, blocks, messages (scripted input) | done |
| Battles (a battle on every map with enemies) | done, JP and US |
| Audio (MusyX on PC; every map plays, checked by ear) | done; SDL output not yet heard on a real device |
| Memory card: create a file, save at a Save Block, reload | done (Aurora) |
| GameCube matching build unchanged (SHA-1 OK, both versions) | checked after every change |
| CI (`.github/workflows/pc-port.yml`, Windows) | written, not yet run (branch not pushed) |
| Linux, macOS | not built yet |
| iOS, Android | planned after desktop |

## Building

The matching GameCube build is untouched: `python configure.py --version
G8MJ01 && ninja` (or `G8ME01`). The PC build is a separate CMake project.

Windows (Git Bash): LLVM clang, the Visual Studio Build Tools (or Visual
Studio 2022) and the Windows SDK. `tools/port/winenv.sh` puts clang, CMake and
Ninja on `PATH`.

```bash
. tools/port/winenv.sh
cmake -S . -B build-pc-jp -G Ninja --toolchain cmake/toolchains/windows-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01 -DTTYD_BACKEND=aurora
ninja -C build-pc-jp
```

- `TTYD_VERSION`: `G8MJ01` (JP) or `G8ME01` (US); one executable per version
  (`ttyd-jp`, `ttyd-us`).
- `TTYD_BACKEND`: `aurora` (window, GPU, controllers, audio; `extern/aurora`)
  or `headless` (no window or dependencies; tests and sweeps).
- `TTYD_RELS_STATIC` (default on Windows): the area RELs are linked in.
- No disc is needed to build. The save banner/icon and DSP microcode the decomp
  extracts from the disc (`build/<version>/include/assets`) are used when
  present; otherwise stand-ins (`port/include/no_disc_assets`: blank banner).

## Running

Aurora: `ttyd-jp.exe <path to your G8MJ01 disc image>` (or `$TTYD_DISC`).
Without one, the disc chosen last time (`disc.txt` in the user folder), else
a file dialog asks for it (any image Aurora's nod reads: ISO, GCM, CISO, GCZ,
RVZ, WIA, ...); a disc of the other region or game is refused with a message.
Headless: `$TTYD_DATA_DIR` = the extracted disc (or its `files/` directory).
The disc ID is checked against the build's version.

The game pauses while its window is minimized or hidden (and, with
`TTYD_PAUSE_UNFOCUSED`, while another window has the focus): its clock and the
audio device stop, and it resumes where it left off
(`port/src/common/pause_pc.c`, `portPause`/`portResume`; mobile apps will call
the same on going to the background).

Environment switches (all optional):

| Variable | Effect |
|---|---|
| `TTYD_TRACE=1/2/3` | sequence/map changes, missing messages, card steps; 2: running scripts; 3: every script command |
| `TTYD_FAST` | frames back to back on a virtual 60 Hz clock (timed waits keep their frame counts); Aurora draws wait for their pipelines |
| `TTYD_MAP=<map>[,<bero>]` | at the first field map or the title, start a new game on that map (with a partner, `TTYD_PARTY`, default Goombella) |
| `TTYD_EXIT_FRAMES`, `TTYD_WARP_LIMIT` | with `TTYD_MAP`: exit 0 after N frames on the map; exit 3 if not reached in N seconds |
| `TTYD_INPUT=<script>` | scripted controller 1 (below) |
| `TTYD_SHOT=<f>[,<f>...]`, `TTYD_SHOT_DIR` | Aurora: write those frames as PNG |
| `TTYD_WINDOW_POS=x,y` | Aurora window position (off-screen for unattended runs) |
| `TTYD_CACHE_DIR`, `TTYD_USER_DIR` | Aurora's pipeline cache / memory cards and settings elsewhere |
| `TTYD_AUDIO_WAV=<file>` | headless: write the mixed audio |
| `TTYD_WATCHDOG=<s>` | no frame for that long: print the main thread's stack, exit 4 |
| `TTYD_PAUSE_UNFOCUSED` | Aurora: also pause while unfocused |
| `TTYD_GXARRAY_TRACE=1/2` | Aurora: vertex-array uploads without a size / every changed array |

Crashes and `abort()` print a symbolized backtrace.

## Testing

`ctest --test-dir build-pc-jp -LE slow` (build the test targets first, see
the workflow): decoder and loader unit tests on synthetic data, plus, when the
disc is extracted to `data/<version>`:

- `disc_sweep`: every anim/map/camroad/TPL/font file through the loaders.
- `musyx_seq_sweep`: every song swapped and played through the sequencer.

`ctest -L slow` adds `map_sweep`. `tools/port/map_sweep.py EXE data/<version>`
boots every map of the game's map table (`TTYD_MAP` warp), several at once, and
groups failures by cause (crash stack, panic, stall, not reached). Each map is
entered through the first entrance its source declares (as in play; without
one Mario starts at the map's origin, which on some maps is over no floor),
except the maps in `ORIGIN_ONLY` that a warp can't enter as the game does.
Options:

- `--disc ISO --shots DIR` (Aurora): capture each map, then contact sheets
  (`tools/port/contact_sheet.py`).
- `--input SCRIPT`: run an input script on every map (captures `<map>__<name>`).
- `--audio DIR` (headless): dump each map's audio; report silent or clipping maps.
- `--golden FILE` (headless): fast runs are deterministic, so each map's audio
  hash is a fingerprint of how it played; fail on any change from FILE
  (`tests/port/golden/audio_<version>.json`, used by the ctest).
  `--update-golden` records the new hashes after an intended change.
- `--no-bero`: warp to each map's origin instead (the game's debug map select).
- `--collect REGEX`: report matching output lines of every run (e.g.
  `'\[msg\] missing'`).

Each run gets its own Aurora cache and card folder, and a 30 s watchdog.

### Input scripts

`TTYD_INPUT` (`port/src/common/input_pc.c`; examples in `tests/port/input/`)
drives controller 1, applied after the game's one `PADRead` (`DEMOPad.c`). With
`TTYD_MAP` the script starts on the map and ends the run. Commands: `wait N`,
`hold KEYS N`, `press KEYS` (keys `a b x y z l r start`, `left right up down`
for the stick, `dleft`.. d-pad, `cleft`.. C-stick, joined with `+`),
`stick X Y N`, `shot NAME`, `battle` (the nearest enemy NPC, through
`evt_npc_battle_start`), `hp N`, `wait_seq N [MAX]`, `wait_map NAME [MAX]`,
`press_until_seq KEYS N [MAX]`, `press_until_map KEYS NAME [MAX]`, `pause MS`
(`portPause` for MS of real time; `tests/port/input/pause.txt`), `end`.

## Audits

Bugs are fixed as classes: each one found gets a detector, and every instance
in the tree is fixed at once.

- Audit build: `-DTTYD_PTRWIDTH_WARNINGS=ON` enables pointer/int casts,
  `int-conversion`, implicit declarations, unsequenced access, incompatible
  pointer and function-pointer types, `cast-function-type-strict`,
  `shorten-64-to-32` and `format`, instead of `-w`.
- `tools/port/proto_audit.py`: prototypes (often local, MWCC-shaped) against
  their definitions, TARGET_PC-aware; also unprototyped `f()` declarations of
  functions taking floats. Must report 0.
- `tools/port/gen_bedecode.py --check`: generated decoders match specs and headers.
- Helpers: `implicit_fix.py` (declarations for implicit calls),
  `widen_evtword.py` (s32 locals that receive script words), `fncast_audit.py`
  / `fncast_fix.py` (calls through recast function pointers), `sjis_src.py`
  (Shift-JIS literals for clang).

Classes found so far, and what catches them:

| Class | Detector |
|---|---|
| Pointer narrowed through s32/u32 (script words, tables, locals, saved LW arrays) | `-Wshorten-64-to-32`, clang-query data flow, crashes at `0xffffffff8…`/low-32-bit addresses |
| Prototype disagrees with the definition (Win64 assigns int/float registers by position; narrow returns are not extended) | `proto_audit.py`, `-Wcast-function-type-strict` |
| Second struct view of the same memory, GC-sized pads/buffers/allocations, GC byte strides and offsets | clang AST cast survey comparing x64 and i686 record layouts |
| Big-endian data read natively (display lists as u32, string pools by GC offset, word layouts) | disc/song sweeps, captures |
| Script tables walked by `EvtWord` but declared `s32` | map sweep crashes, table/type survey |
| Float overlays on 8-byte script words; float-encoded script constants (`0xF24A7A80`) widened unsigned | union survey, grep |
| Small enums (`#pragma enumsalwaysint off`) | `PORT_ENUM_BASE` on the enum headers |
| Hardware timing the game relies on (ARQ callbacks after posting, empty wait loops) | `arq_defer_pc.c`, `PORT_SPIN` |
| Unwritten SDK out-parameters | stub survey (`gx_state_pc.c`) |

## Decisions

- **Guarding rule.** Every change to game or library code is inside
  `#ifdef TARGET_PC` or compiles to the same GameCube code (`s32` to `EvtWord`,
  `PCCALL`, `PORT_SPIN`). Both matching builds are checked after each change.
- **Script words are `EvtWord`** (`include/evt/evt_word.h`: `intptr_t` on PC,
  `s32` on GC); the interpreter and every holder of a script word use it.
- **Sound name words** (`include/sfx_name_word.h`): a word below 0x80000000 is
  a sound id, as on GameCube; a startup check aborts if any host address the
  game uses is below 2 GiB.
- **ARQ callbacks run deferred** (`port/src/common/arq_defer_pc.c`): both
  backends copy at once, the game's callback runs at the next retrace or empty
  wait loop, as the DMA interrupt does.
- **Aurora fixes** live on a local branch of `extern/aurora`
  (`ttyd-port-storage`): 64 MiB per-frame storage buffer, texcoords past
  `numTexGens` read (0, 0), `CARDUnmount` succeeds.
- **Aurora SDK overlay** (`port/include/aurora_overlay`): wrapper headers that
  add what the 2004 SDK headers gave the game, route the game's `OSGetTime`,
  `GXCopyDisp` and texgen calls through the port, and make `GXWGFifo` a compile
  error.
- **Card saves keep the GameCube image**, so `.gci` files move between Dolphin
  and the port (section below).

## Technical notes

### Big-endian disc data (survey)
Most loaders convert offsets to pointers in place (`ptr = base + off`). Chokepoints:
- `fileGarbageDataAdrSet(data, type)` (`src/manager/filemgr.c`): per-type pointer fixup after every
  `fileAlloc`/`fileAsync` load, and again when the compactor moves a buffer — swap once, on fresh
  load only. Types: 0 raw (`.rel`, `b/first`, `*.bin`), 4 `.tpl`, 5 anim models `a/<name>`,
  1/2/3/6–10 have no loader in the game.
- `mapLoadReloc_` (`src/driver/mapdrv.c`): map `d` files, relocation-table driven, ~35 structs.
- `UnpackTexPalette` (`src/sdk/texPalette.c`): every TPL not loaded through filemgr.
- Bespoke: fonts (`fontmgr.c`), messages (`msgdrv.c`), camera roads (`cam_road.c`), setup data
  (`database.c`), `icon.bin`, ARC (`arc.c`), THP, MusyX banks (`sound.c`), card saves.
- Strategy: per-struct swap descriptors (~90 without audio/saves), applied on load or by an offline
  converter at extraction time. GX payloads (texels, vertex arrays, display lists) stay big-endian
  and are swapped by the GX backend at upload. Treat flag words as whole u32s, not C bitfields.

### Loader design: big-endian data and 64-bit pointers
- **GPU payloads stay big-endian, untouched in the loaded file.** Aurora decodes GameCube texture
  formats and display lists natively, and `GXSetArray(..., le)` takes an endianness flag: arrays
  from disc are registered big-endian (`portGXRegisterArray`), arrays built in code are native.
- **CPU-side structures are decoded into native structs** (little-endian scalars, 64-bit pointers)
  by a table-driven decoder (`port/src/common/bedecode.c`): each struct has a descriptor listing
  every field's GameCube offset/size and native offset, its kind (integer/float of a width,
  pointer-to-struct, pointer-to-payload, embedded struct, fixed or counted array), and for pointer
  fields what they point to. Disc pointers are file offsets (0 = NULL); the decoder follows them,
  memoizes by offset so shared nodes stay shared, and allocates native objects from the game heap.
  Payload pointers (texels, vertices, display lists, strings) point into the original blob.
- **Descriptors are generated**, not hand-written: `tools/port/gen_bedecode.py` reads a spec per
  format (`port/bedecode/*.spec`: struct names, which pointer fields lead where and how element
  counts are found) and the two record layouts from clang (`powerpc-unknown-eabi` for the disc,
  host for native), so a struct edit can't silently desync.
- **Hook points:** `fileGarbageDataAdrSet` per file type (filemgr), `mapLoadReloc_` (maps),
  `UnpackTexPalette` (TPL), and the bespoke loaders (font, msg, cam_road, setup data, icon.bin,
  ARC, THP headers). On PC these call the decoder instead of patching offsets in place; files the
  decoder owns are not moved by the smart-heap compactor.

#### Decoder implementation (status)
- **Runtime** (`port/include/port/bedecode.h`, `port/src/common/bedecode.c`): `PortType`/`PortField`
  descriptors; `portDecodeBegin(ctx, blob, size, name)`, `portDecode(ctx, type, off)` /
  `portDecodeArray`, `portDecodeEnd`. Field kinds: u8..s64, f32/f64, `PTR_STRUCT` (one, fixed `[N]`,
  count from a sibling field, `until` a terminator element, or a callback), `PTR_PAYLOAD`,
  `PTR_STRING` (NUL checked), `PTR_NULL`, `PTR_RAW`, `EMBED`, inline arrays, `CONST`. Every read is
  bounds-checked; errors go to `ctx->onError` (default `OSPanic(name, ...)`, `abort()` in the
  standalone test build). Nodes are memoized by (offset, type) and decoded from a worklist (no deep
  recursion on linked lists). Allocation: `ctx->alloc`, else a malloc-backed `PortDecodeArena` the
  caller frees with `portDecodeArenaFree`.
- **Spec syntax** (`port/bedecode/<fmt>.spec`; full reference in the generator's docstring):
  `format`, `header`, `struct Name`, then per field `f -> T`, `f -> T[N|countField|until g == v|call fn]`,
  `f payload|string|null|raw|skip`, `f = value`, `f as u32`, `f as f32[3]` (a struct field such as
  the anonymous `Vec`/`GXColor` typedefs as N scalars), `f[countField] [-> T|string|...]` (flexible
  array member `T f[]`, allocated with the node; pointer elements point to one `T` each), and
  `f -> T` on an inline pointer array `T* f[8]` (one `T` per element). Scalars and embedded structs are taken
  from the disc layout; an unannotated pointer field is an error. The generator compares x86_64,
  aarch64 and x86_64-windows layouts (they must agree) and emits `_Static_assert`s on offsets/sizes
  for 64-bit hosts and for the i686 headless build.
- **TPL (done)**: `port/bedecode/tpl.spec`; `portTplDecode(blob, size, name)` (`port/src/common/tpl_pc.c`)
  decodes the header graph into a per-blob arena and writes the native `TEXPalette` root over the
  blob's first 16 bytes, so every holder of the blob pointer sees a native palette (as the GC fixup
  did in place). Texel/CLUT pointers point into the blob, still big-endian: Aurora's texture and
  TLUT converters byte-swap from GC order (`lib/gfx/texture_convert.cpp`). `unpacked` is set to 1.
  Idempotent (a native root is detected by its version word); a new TPL at the same address frees
  the old graph; `portTplRelease` frees it explicitly. Hooks: `UnpackTexPalette` (size unknown, loose
  bound), `mapdrv`/`bgdrv` (sized), filemgr type 4 (`portFileDataAdrSet`, sized from the smart block
  or the archive).
- **Anim models (done, filemgr type 5)**: typed layout in `include/driver/animdrv.h` under
  `#if defined(TARGET_PC) || defined(PORT_BEDECODE_DISC)` (the generator defines
  `PORT_BEDECODE_DISC` for the disc layout; MWCC keeps the padded structs). `port/bedecode/anim.spec`;
  `portAnimDecode` (`port/src/common/anim_pc.c`) decodes into a per-blob arena: shapes, draws, groups,
  textures, texture-matrix and vertex/normal/node base values become native; each `AnimData` is
  decoded with itself as the blob base (its sub-pointers are relative to it). The native root does
  not fit the 0x1B0-byte disc header, so the blob's first 16 bytes become a marker + root pointer and
  animdrv reads `portAnimRoot(*handle->data)` where GC casts the blob. Colors/texcoords stay
  big-endian (disc blob registered for GX); index tables stay big-endian and renderProc reads them
  with `ANIM_IDX16/32`; byte-only delta tables are used in place. `animPoseMain` has a typed PC
  version (the raw-offset one assumes GC `AnimationPose`/`AnimationWork` offsets). External raw
  `AnimData + 0x24` readers (win_*, eff_nice, shuryolight, black_peach) use `baseInfo` on PC.
  Types 1-3 and 6-10 have no loader in the game: on PC they panic. Test: `tests/port/anim_test.c`.
- **Maps (done, `m/<map>/d`)**: `port/bedecode/map.spec` over `include/driver/mapfile.h` (typed
  PC/`PORT_BEDECODE_DISC` copies of mapdrv.c's local structs; mapdrv.c keeps its own under
  `#ifndef TARGET_PC`) plus `MapFileJoint`/`MapFileInfo`/`MapJointDrawMode` from `mapdrv.h`.
  `portMapDecode` (`port/src/common/map_pc.c`, from mapdrv's PC `mapLoadHeader_`) relocates the file
  in place once (relocation-table words become file-relative, +0x20, so a pointer to data+0 is not
  NULL; marker `PCR1` at header +0x14 makes it idempotent for archive-resident maps), decodes the
  chunks by name (`information` joint tree with draw modes, materials, samplers, texture infos;
  `texture_table`, `material_name_table`, `light_table`, `fog_table`, `vcd_table`,
  `animation_table` with its joint/material/alpha/light/light-param tracks), decodes meshes by hand
  (packed GX display lists stay in the file as payload + byte size; unpacked vertex records and the
  shared vcd table become native) and checks that every relocated word was read by some pointer
  field (`portMapUncoveredRelocs`; nonzero = a chunk/field the spec misses, reported with
  `OSReport`). Native roots go to `MapEntry` (`pcMap`, chunk fields); GC raw-offset reads in mapdrv
  (`MapEntryOv`, `MapObjectDL`, material `+0xA/+0xC/+0x10C`, joint `+0x3C..+0x64`, anim `+0xC..+0x1C`,
  light table `[i + 1]`, fog words, `entry + 0x178`) have typed PC branches; `MapObject` has real
  `dl[8]/dlSize[8]` and `MapAnim` stays 0x20 bytes on PC. ARAM round trip (`bmapLoad`/`bmapUnLoad`):
  the native graph and the TPL graph stay alive (`portTplDetach`/`portTplAttach`), and after the copy
  back `portMapRebase` repoints payload/string pointers (header pointers and `MapTexInfo` too).
  hitdrv reads collision geometry the GPU also uses through `portMapLoadU16/S16/Vec` (display-list
  bytes always big-endian; vertex positions big-endian when `portMapIsDiscData`, native for meshes
  built in code). Test: `tests/port/map_test.c` (synthetic map, coverage, errors, rebase, TPL round trip).
- **Compactor opt-out**: on PC `fileGarbageIsPinned` (filemgr) is TRUE for every typed file
  (type != 0); `smartGarbage` treats pinned blocks like in-flight reads and leaves them in place, so
  `fileGarbageDataAdrClear/Set` never relocate them (`fileGarbageMoveMem` panics if asked). Raw
  files stay movable. Cost: more smart-heap fragmentation; enlarge the PC heap if it shows up.
- **Vertex-array endianness** (`port/include/port/gx_arrays.h`): arrays in disc data are big-endian
  (`GXSetArray(..., le=false)`), arrays built in code are native (`le=true`). Loaders register disc
  data with `portGXRegisterDiscBlob(base, size)` (map `d` files in `_mapLoad`, filemgr type 5 anim
  models) and unregister before freeing; `portGXRegisterArray`/`portGXRegisterNativeArray` record
  single arrays. `portGXSetArraySized` (Aurora, `port/include/port/aurora_compat.h`) picks
  endianness and size from that; a static array passed directly gets its exact `sizeof`;
  unregistered pointers are native.
- **Adding a format**: write `port/bedecode/<fmt>.spec`, run the generator, call `portDecode` from the
  hook and give the game a native root at the pointer it already holds (in place when the native
  root fits, as for TPL; otherwise change the holder, e.g. a PC field in `FileEntry`), then add the
  file kind to `tools/port/disc_sweep.py` so every disc file of that kind is checked.

#### Bespoke loaders (status)
Tests: `loaders_test` (ctest; synthetic big-endian font, camera road, card slot/metadata and U8
archive). Decoder additions: `portDecodeInto` (decode into caller memory), `portEncode` (native ->
disc image for flat types; pointers written 0), `portSwapInPlace` (pointer-free types whose native
layout equals the disc layout), `portSwap16/32/64Run`.
- **Fonts** (`f/*.bfn`, `JUTFontSetup`): `port/bedecode/font.spec`; `portFontSwap`
  (`port/src/common/font_pc.c`) swaps the header, every block header (INF1/GLY1/MAP1/WID1) and MAP1
  u16 tables in place, once, after the read. WID1 entries are bytes; the GLY1 sheet stays big-endian.
  Shift-JIS codes read as `*(u16*)msg` (fontmgr, msgdrv) are `SJIS_CODE(p)` = `portBE16(p)` on PC,
  and `JUTFont_CodeToGlyph` splits the lead/trail bytes arithmetically.
- **Messages** (`mess/<lang>/*.txt`): plain NUL-separated tag/text pairs, no binary header; the
  lookup table is built natively by `msgLoad`. Nothing to decode.
- **Setup data** (`m/<map>/s`): text parsed by `parse.c`. Nothing to decode.
- **Camera roads** (`camRoadSetup`): `port/bedecode/camroad.spec` decodes the 0x10C header to a
  native `CamRoadMap` (types shared via `include/cam_road.h` on PC); `portCamRoadDecode`
  (`camroad_pc.c`) swaps the tables in place (each table runs to the next offset): `f_e8`, strips,
  Vec and index tables as 32-bit words; curves (0xB8, `curveNum`) from byte 0x20 (name before);
  groups `f_f0`/`f_104` (0x68) from byte 0x40; `f_108` (unread) untouched. The blob is marked so a
  second setup from the same archive-resident data returns the same header without re-swapping.
  Uncertain: curve/group pads are not fully known; only fields the C reads are guaranteed right.
- **icon.bin**: all u16 (count, offsets, per-icon u16 records): swapped whole in `_callback_bin`.
- **ARC / U8** (`src/sdk/arc.c`): header and FST read through `portBE32` accessors; the archive
  stays big-endian (its member files are decoded by their own loaders). `b/first`'s LZ77 size is
  read with `portBE32` (`seq_logo.c`).
- **THP** (`seq_demo.c` THPSimple): Aurora's `THPVideoDecode`/`THPAudioDecode` parse frame
  payloads from big-endian data; the game's own reads are swapped: `THPHeader` (after the 4-char
  magic), `THPFrameCompInfo.numComponents`, video/audio info words, the per-frame next-size word
  and component sizes (`portBE32`, frame buffers are not modified). `dig_00.c` only calls THPSimple.
- **Raw files (type 0)**: `w/jp/mono.dat` (`win_log.c`) read through `portBE32`/`portBEF32`
  (raw files are shared/movable, so no in-place swap); its size comes from the smart block slot
  after the data pointer. `a/ag2tg.bin` is 0x20-byte strings (nothing to swap). `a/vivian.bin`
  (animdrv.c, owned by the anim work) is 0xC00 bytes of f32 pairs + `s32[4]` copied into
  `_vivihimoData`/`vivihimoTailGroupNo`: needs `portSwap32Run` after the copies, and the static
  `_vivihimoData` initializer (big-endian bytes) needs the same treatment.

#### Card saves
- **Policy: the card keeps the GameCube image** (big-endian, GameCube `GlobalWork` layout, GameCube
  checksums), so `.gci` saves move between Dolphin/GameCube and the PC port. Aurora's CARD writes
  the file; the game keeps native structs.
- `CardSaveSlot` on PC is a union of the native `data` with `u8[0x3FF0]`, so the slot stays 0x4000
  bytes with the trailer at 0x3FF0 and every card offset/size in cardmgr is the GameCube one.
- `memcard_write`/`memcard_read` go through `portCardWriteImage` / `portCardReadBegin` +
  `portCardReadEnd` (from `readCallback`) (`port/src/common/cardsave_pc.c`): regions are known by
  offset (metadata at 0, 0x4000-byte slots after it). Slots: `port/bedecode/cardsave.spec`
  (`GlobalWork`, `PouchData`, `NpcDeadInfo`, `BadgeShopWork`) with `portDecodeInto`/`portEncode`;
  by hand: `savePlayerPos` (anonymous `Vec`), `LotteryWork` fields inside `lottery_info`'s pad,
  `PiantaParlorWork` (u32 words), `InformantUnknown` (bytes). Runtime pointers
  (`relocationBase`, `mapAlloc`, `fbatData`) are written as 0 (cardCopy2Main restores/clears them).
- Checksums: the image gets GameCube checksums (byte sum over the 0x2140-byte GameCube data) when
  the native slot verifies; a valid image gets native checksums so `dataCheck` passes unchanged;
  invalid either way stays invalid. Metadata has the same layout (u16 options and checksums swap).
- Unknown pads in `GlobalWork`/`PouchData` are copied as bytes: a multi-byte value hidden in a pad
  would keep GameCube byte order on PC (none is read through a cast today except LotteryWork).

### Audio: MusyX PC hardware layer
- `libs/musyx` is built with `MUSY_TARGET_PC` (object library `musyx_pc`, linked into `ttyd`).
  `hw_pc.c`: output in 160-frame (5 ms) quanta — SDL3 audio stream (S16 stereo 32 kHz) on Aurora,
  a null sink pumped from `VIWaitForRetrace` on headless (`$TTYD_AUDIO_WAV` dumps it); then
  `portAudioMixExtra` (DVD stream) and, on Aurora, the game's AI DMA callback. IRQ masking is
  one recursive mutex. `hw_pcmixer.c` executes `salBuildCommandList`'s DSP command list in
  software (ADPCM/PCM16/PCM8, SRC, LPF, envelope, ITD, ramps, depop, aux buses, compressor);
  list/PB addresses are 32-bit handles (`salPcPtrHandle`). `hw_aramdma.c` uses the port's
  ARAM buffer via `portAramPtr` (headless `ar_pc.c`, Aurora `ar_aurora.c` + `mem2Size`).
- PCM16 sample data (.samp) is big-endian in ARAM; stream buffers (top of ARAM) are native.
- `hw_pcstripped.c`: functions TTYD's linker stripped but other MusyX objects reference.
- Every edit to existing MusyX files is under `MUSY_TARGET_PC`; GC preprocessed output is unchanged.
- Test: `musyx_mixer_test` (ctest, headless; `$MUSYX_TEST_WAV` writes the end-to-end render).
