# Paper Cut: technical reference

Paper Cut is Paper Mario: The Thousand-Year Door, Japanese (G8MJ01), US
(G8ME01) and European (G8MP01), as a native 64-bit program, built from the
matching decompilation ([LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd)). The
game code is the decomp's; the GameCube platform under it is replaced. No game
data is shipped: Paper Cut builds without it and runs from the user's own disc
image.

## Status

| Milestone | State |
|---|---|
| Builds on Windows (clang + lld), JP, US and EU, headless and Aurora | done |
| Boots to the title (JP, US, EU) | done |
| Every map of the game's map table boots and runs (279 per version) | done: headless, all three versions (EU in all five languages); Aurora, JP and US |
| Field play: walking, jumping, blocks, messages (scripted input) | done |
| Battles (`map_sweep --battles`: every entry of every area's battle table) | done, JP and US; EU not yet swept |
| Audio (MusyX on PC; every map plays, checked by ear) | done; SDL output not yet heard on a real device |
| Memory card: create a file, save at a Save Block, reload | done, headless and Aurora (the same `.gci` files) |
| GameCube matching build unchanged (SHA-1 OK, all three versions) | checked after every change |
| CI | `tools/port/local_ci.py` (below), on self-hosted runners (Windows; Linux in a container on a Mac: docs/RELEASING.md); the GitHub-hosted workflows are manual-only |
| Linux | x86_64: built and tested on Ubuntu 24.04 in CI (headless: unit and disc tests, every map with golden audio, memory card scripts; the Aurora build is packaged but not yet played on a Linux desktop); aarch64: cross-compiled and linked only |
| macOS (arm64, x86_64; headless and Aurora; one `Paper Cut.app` holds the three builds) | builds (Apple clang, Xcode 26); every map boots headless, JP and US (arm64; JP also x86_64 under Rosetta); every JP map captured on Aurora, 4:3 and widescreen; unit tests and audits pass; GameCube matching build runs here (wibo); golden audio differs from Windows (libm, Testing); EU not yet checked on the Mac |
| iOS | Simulator: boots from the disc in Documents, plays, touch controls drive Mario, background pause/resume; 4:3 by default, 16:9 widescreen opt-in (Settings app). Device: the US build runs on an iPhone 17 Pro (development-signed; iOS section) |
| Android | builds (three APKs: jp, us, eu; on Windows); not yet run on a device |

## Building

The matching GameCube build is untouched: `python configure.py --version
G8MJ01 && ninja` (or `G8ME01`, `G8MP01`). The PC build is a separate CMake project.

Windows (Git Bash): LLVM clang, the Visual Studio Build Tools (or Visual
Studio 2022) and the Windows SDK. `tools/port/winenv.sh` puts clang, CMake and
Ninja on `PATH`.

```bash
. tools/port/winenv.sh
cmake -S . -B build-pc-jp -G Ninja --toolchain cmake/toolchains/windows-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01 -DTTYD_BACKEND=aurora
ninja -C build-pc-jp
```

- `TTYD_VERSION`: `G8MJ01` (JP), `G8ME01` (US) or `G8MP01` (PAL, the European
  release); one executable per version (`ttyd-jp`, `ttyd-us`, `ttyd-eu`).
- `TTYD_BACKEND`: `aurora` (window, GPU, controllers, audio; `extern/aurora`)
  or `headless` (no window or dependencies; tests and sweeps).
- `TTYD_RELS_STATIC` (default with clang: Windows, Apple, Linux, Android): the
  area RELs are linked in.
- No disc is needed to build. The save banner/icon and DSP microcode the decomp
  extracts from the disc (`build/<version>/include/assets`) are used when
  present; otherwise stand-ins (`port/include/no_disc_assets`: blank banner).

### Linux

x86_64 or arm64, clang (the RELs are linked in with `#pragma clang section`;
with gcc 13+ use `-DTTYD_RELS_STATIC=OFF`, loadable modules, untested), lld
optional. Debian/Ubuntu packages: `clang lld llvm cmake ninja-build python3`
(`llvm` for `llvm-nm`/`llvm-objcopy`/`llvm-ar`; binutils' `nm`/`objcopy`/`ar`
also work). Aurora additionally builds SDL3 from source unless a system SDL3
(3.2+) is found: `pkg-config libasound2-dev libpulse-dev libaudio-dev
libfribidi-dev libjack-dev libsndio-dev libx11-dev libxext-dev libx11-xcb-dev
libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev
libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev
libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libthai-dev
libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev` (Aurora's own
CI list). Dawn (Vulkan/GL) and nod come as prebuilt packages for x86_64; on
arm64 nod builds from source (Rust, `cargo`). CMake 3.25+ (3.29+ to pick lld).
The unit tests' layout check also compiles for 32-bit x86: `gcc-multilib` on
x86_64. CI builds and tests Linux in an Ubuntu 24.04 container
(`tools/port/linux-release.Dockerfile`, run by `tools/port/linux_container.py`).

```bash
cmake -S . -B build-pc-jp -G Ninja --toolchain cmake/toolchains/linux-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01 -DTTYD_BACKEND=headless
ninja -C build-pc-jp
TTYD_DATA_DIR=/path/to/extracted/G8MJ01 ./build-pc-jp/ttyd-jp
```

Linux specifics: the executable is position-independent (every host address
must be 2 GiB or higher, see Decisions); each REL's data and bss go to their
own ELF sections `rel_<area>_data`/`rel_<area>_bss`, bounded by the linker's
`__start_`/`__stop_` symbols (CMakeLists.txt); game threads are `ucontext`
coroutines on glibc and baton-passing pthreads elsewhere (Apple, Android,
musl; `port/src/common/os_thread_pc.c`); crashes are reported from signal
handlers on an alternate stack with names from the executable's own ELF symbol
table (frame lines end in `(ttyd-jp+0xOFFSET)`: `addr2line -e ttyd-jp
0xOFFSET`); `$TTYD_WATCHDOG` signals the main thread (`SIGUSR2`) to print its
own stack. The headless DVD layer resolves disc paths case-insensitively.

Status: x86_64 is built and tested on Ubuntu 24.04 in CI (an x86_64 container
on the Mac runner, `tools/port/linux_container.py`): the JP and US headless
builds pass the unit and disc tests, every map with the golden audio hashes
(recorded on Windows, and equal on Linux), and the memory card scripts. The
Aurora build is built and packaged there (the release's Linux zips), but no
one has played it on a Linux desktop yet. aarch64 is only compiled and linked,
against stand-in glibc headers (`tools/port/linux_xcheck.py`, local CI's linux
stage without WSL); arm64 floating point may change the audio hashes without
anything being wrong, so a mismatch there needs a listen before an update.
### macOS

Status: built and run on an Apple-silicon Mac (macOS 26, Xcode 26.3): JP
and US, headless and Aurora (EU not yet checked there); unit tests pass; every map of the JP map
table boots headless on arm64 and on x86_64 (Rosetta), and every US map on
arm64. Golden audio: see
Testing (Apple's libm rounds differently from Windows').

Needs Xcode or its Command Line Tools (Apple clang), CMake 3.25+, Ninja,
Python 3 and Homebrew LLVM (`brew install llvm ninja`): only its
`llvm-objcopy`/`llvm-nm`/`llvm-ar`, which Xcode lacks, for linking the RELs in;
CMake finds them in `/opt/homebrew/opt/llvm/bin` (or `/usr/local/...`) without
`PATH` changes. Apple clang compiles everything. Aurora's first configure
fetches its dependencies (Dawn and nod prebuilt, SDL3 built from source).

```bash
cmake -S . -B build-pc-jp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTTYD_VERSION=G8MJ01 -DTTYD_BACKEND=aurora
ninja -C build-pc-jp
open build-pc-jp/ttyd-jp.app            # or: .../Contents/MacOS/ttyd-jp <disc image>
cmake --install build-pc-jp --prefix dist  # self-contained, ad hoc signed copy
```

- Aurora builds an app bundle, `ttyd-jp.app`/`ttyd-us.app`/`ttyd-eu.app`, shown as
  "Paper Cut" (`port/macos/Info.plist.in`: bundle id
  `org.ttyd-port.ttyd-jp` (`-DTTYD_MACOS_BUNDLE_ID_PREFIX`), Retina, minimum
  macOS, the disc image types). Opened without a disc, it shows the disc picker;
  Finder can open a disc image with it (Open With, or dropped on its icon:
  `disc_aurora.c` takes the open-document event at startup). Headless builds a
  plain `ttyd-jp`.
- User data (memory cards, `disc.txt`, Aurora's pipeline cache):
  `~/Library/Application Support/ttyd/` (Aurora's `SDL_GetPrefPath`).
- The app is self-contained: Aurora's dependencies (fmt, abseil, zstd,
  libpng, ...) are fetched and linked statically, never Homebrew's or
  MacPorts' (CMake ignores those prefixes while Aurora configures); `otool -L`
  lists only system libraries.
- Window restoration is off (`ApplePersistenceIgnoreState`, set before SDL
  starts): otherwise, after a crash or a killed run, macOS asks "reopen
  windows?" in a modal alert before the game starts.
- Minimum macOS 12.0 (`CMAKE_OSX_DEPLOYMENT_TARGET`, set before `project()`; Aurora's prebuilt Dawn is built for 12).
- One architecture per build, the host's by default; for Intel from Apple
  silicon, `-DCMAKE_OSX_ARCHITECTURES=x86_64` (Aurora: also
  `-DAURORA_NOD_PROVIDER=package`, else nod is built from source with Rust).
  Aurora has no prebuilt Dawn for a universal build: build each architecture and
  `lipo -create` the executables if one app is wanted.
- RELs: linked in (Mach-O sections `__DATA,__rd_<area>`/`__rb_<area>`, bounded
  by `section$start`/`section$end`; `tools/port/rel_rename.py` keeps the
  leading underscore). iOS cannot load code, so macOS takes the same path and
  tests it. `-DTTYD_RELS_STATIC=OFF`: one loadable bundle per REL
  (`-bundle_loader`), in `Contents/MacOS/rel/`.
- Game threads (`os_thread_pc.c`): macOS's ucontext is deprecated and iOS has
  none, so each coroutine is a host thread with a baton (one runs at a time),
  as on Android and musl.
- Not notarized: a downloaded copy is blocked the first time; System Settings >
  Privacy & Security > Open Anyway allows it (before macOS 15, right-click >
  Open), or `xattr -dr com.apple.quarantine ttyd-jp.app`.
- Golden audio hashes (`map_sweep --golden`) are recorded on Windows x64;
  arm64 (fused multiply-add) and Apple's libm may round differently.

## Running

Aurora: `ttyd-jp.exe <path to your G8MJ01 disc image>` (or `$TTYD_DISC`).
Without one, the disc chosen last time (`disc.txt` in the user folder), else
a file dialog asks for it (any image Aurora's nod reads: ISO, GCM, CISO, GCZ,
RVZ, WIA, ...); a disc of the other region or game is refused with a message.
Headless: `$TTYD_DATA_DIR` = the extracted disc (or its `files/` directory).
The disc ID is checked against the build's version.

The settings page (Aurora; `port/settings/settings_ui.cpp`): F1, a
controller's Back/Select or a three-finger tap opens it; Escape or B closes
it; F11 or Alt+Enter toggles fullscreen. The desktop window
(`port/settings/features/window.cpp`) opens where it was left, at its size,
maximized or full screen as it was, and never larger than the screen or off
every screen; the first time, it fills about 80% of the screen at the
picture's shape. Display > Window sizes it to 1x-4x the 640x480 picture or
to the picture's shape, centres it, and can keep it above other windows.
Video: frame rate (60, 120, 144, 240, the
display's; docs/FRAME_INTERPOLATION.md), VSync, fullscreen, widescreen 16:9,
internal resolution, a frame rate counter. Audio
(`port/settings/features/volume.cpp`): a mixer of master, music, sound
effects and ambience, each with a mute button and an audio taper (50% sounds
about half as loud), mute in the background, and the volume keys (F9
quieter, F10 louder, Shift for steps of 1, F3 mute) with a bar on the
screen. Gameplay (`port/include/port/gameplay.h`): fast-forward speed
(2-8x) and hold or toggle, text speed, Mario's run speed, skip the intro (the
logos at 16x, then the title without the story demo). Controls: rumble and
its strength, and the on-screen controls' haptics, opacity, size and
auto-run, and controllers in the background. General: pause in the background.
Saves: the save folder.

Controllers (Controls tab; `port/settings/features/controllers.cpp`):
- **Mappings:** the community SDL_GameControllerDB is loaded at start (`port/assets/controllers`, about 2,300 models), then the player's own `<user>/gamecontrollerdb.txt`. Most controllers SDL doesn't know work as gamepads.
- **The list:** each connected controller with its kind and battery. Each can be made player 1-4 (Aurora's ports), tested live (buttons, sticks, triggers), and given dead zones and trigger click points per player, saved with Aurora's mappings.
- **Remapping:** Bindings (Controls tab; `port/settings/features/bindings.cpp`) remaps any player, not only the first.
- **Set up a controller:** a controller SDL can't map (an old USB pad, an arcade stick) is set up by pressing each button when asked. The mapping takes effect at once and is added to `<user>/gamecontrollerdb.txt`.

Save states (`port/src/aurora/savestate_aurora.c`; every Aurora build with
static RELs: Windows, macOS, iOS, Linux, Android): F5 saves slot 1, F8 loads it, the settings page's
States tab has four slots; an input script's `state save|load N`;
`TTYD_STATE_TEST=<save frame>:<load frame>`. A state is MEM1, ARAM, the
game's data and bss (their own sections from `port_prefix.h`'s pragma,
bounded per object format in `port/src/common/snapshot_bounds_pc.c`: Mach-O
`__ttyd_snapd`/`__ttyd_snapb` with section$start/end, COFF grouped sections
`.data$ttyd_snap_{a,m,z}` with marker variables, ELF `ttyd_snapd`/`ttyd_snapb`
with __start_/__stop_; the game is built `-fno-common` for it), every
REL's sections and the linked REL, the save-state heap
(`port/src/common/snapheap_pc.c`: what the decoders made, which the game's
data points into; its bookkeeping is inside it), the game clock and its
`rand()` state. It is taken between frames on the main thread, in the field
or a battle, with the audio callback held and disc reads and ARAM transfers
finished; a load reopens the disc files open then (Aurora patch 0008) and
drops Aurora's texture caches. Kept compressed in memory (Apple's LZ4; elsewhere
the port's own LZ77 codec in the same file), about 18 MiB: saving or loading
takes about 50 ms on an M-series Mac, 120-220 ms on Windows. They hold host
addresses, so they last for the run. Checked: the frames after a load equal
those after the save (`TTYD_FAST`) in the field, in a battle, and after a map
and an area (REL) change (macOS); in the field and a battle on Windows (40
frames on, still equal). Android: built, the sections and their bounds
linked; not yet run on a device.

Fast-forward (`port/src/aurora/speed_aurora.c`): Tab, a controller's right
stick click or the on-screen `>>`. The game clock runs at the chosen multiple
of real time (`portClockSetSpeedPct`, `os_time_aurora.c`) and frame pacing,
which waits on that clock, follows; vsync is off meanwhile and no in-between
frames are drawn. `TTYD_FF_TEST` holds it from the start (tests).
Changes apply at once and are written to `settings.txt` in the user folder
(`port/src/aurora/settings_aurora.c`); the game gets no input while the page
is open. `TTYD_SETTINGS_OPEN` opens it at start (tests).
Warp: continue from any save file (`.gci`, this version's) in the user
folder's `warps` folder (folders in it group them; on iOS, Files > TTYD >
Saves > warps), e.g. a speedrun practice save pack, without going back to
the title (`port/src/common/warp_pc.c`; on a field map or the title, not in
a battle). The save is loaded as file 4: a save made after a warp is written
there, never to files 1-3. `TTYD_LOAD_SAVE=<file.gci>` warps at start.

The game pauses while its window is minimized or hidden (and, with
`TTYD_PAUSE_UNFOCUSED`, while another window has the focus): its clock and the
audio device stop, and it resumes where it left off
(`port/src/common/pause_pc.c`, `portPause`/`portResume`; mobile apps will call
the same on going to the background).

Environment switches (all optional):

| Variable | Effect |
|---|---|
| `TTYD_TRACE=1/2/3` | sequence/map changes, missing messages, card steps; 2: running scripts; 3: every script command |
| `TTYD_TRACE_CMD=<text>[,<text>...]`, `TTYD_TRACE_BUDGET` | with `TTYD_TRACE=3`: only the scripts whose name contains one (frame-stamped); how many commands (default 4000) |
| `TTYD_FAST` | frames back to back on a virtual 60 Hz clock (timed waits keep their frame counts); Aurora draws wait for their pipelines |
| `TTYD_MAP=<map>[,<bero>]` | at the first field map or the title, start a new game on that map (with a partner, `TTYD_PARTY`, default Goombella) |
| `TTYD_EXIT_FRAMES`, `TTYD_WARP_LIMIT` | with `TTYD_MAP`: exit 0 after N frames on the map; exit 3 if not reached in N seconds |
| `TTYD_INPUT=<script>` | scripted controller 1 (below) |
| `TTYD_SHOT=<f>[,<f>...]`, `TTYD_SHOT_DIR` | Aurora: write those frames as PNG |
| `TTYD_FRAMERATE=120/144/240/display` | Aurora: frames drawn between the game's (docs/FRAME_INTERPOLATION.md); else `framerate=` in the user folder's settings.txt |
| `TTYD_SHOT_INBETWEEN=<k>` | Aurora: capture the k-th in-between frame after each `TTYD_SHOT` frame instead |
| `TTYD_INTERP_AUDIT` | Aurora on Windows: report the game state in-between frames change (by function and line, and globals) |
| `TTYD_WINDOW_POS=x,y` | Aurora window position (off-screen for unattended runs) |
| `TTYD_USER_DIR` | the user folder elsewhere: memory cards (both backends), Aurora's settings |
| `TTYD_CACHE_DIR` | Aurora's pipeline cache elsewhere |
| `TTYD_AUDIO_WAV=<file>` | headless: write the mixed audio |
| `TTYD_WATCHDOG=<s>` | no frame for that long: print the main thread's stack, exit 4 |
| `TTYD_PAUSE_UNFOCUSED` | Aurora: also pause while unfocused |
| `TTYD_WIDESCREEN=1/0` | Aurora: 16:9 picture (off by default; overrides the `widescreen` preference); see below |
| `TTYD_GXARRAY_TRACE=1/2` | Aurora: vertex-array uploads without a size / every changed array |
| `TTYD_NO_MODS` | Aurora: no mods and no loose textures ([MODDING.md](MODDING.md)) |
| `TTYD_DUMP_TEXTURES` | Aurora: save every texture without a replacement, under its texture-pack name |
| `TTYD_FINGERPRINT=<file>`, `TTYD_FINGERPRINT_PARTS` | Aurora: each frame's game-state fingerprint in the file (same input, same file); with PARTS, each part's hash too ([ONLINE_RELAY.md](ONLINE_RELAY.md)) |
| `TTYD_TR_DUMP=<file>` | Aurora: every English text the menus ask to translate, once each, added to the file ([LANGUAGE_PACKS.md](LANGUAGE_PACKS.md)) |
| `TTYD_PAUSE_ROW=<label>` | Aurora: the Start menu's Settings tab opens with that row chosen |
| `TTYD_SETTINGS_HINT=1/0` | Aurora: show or skip the first room's settings hint (skipped by default with `TTYD_MAP`) |
| `TTYD_STEAM_DECK=1/0` | Aurora: take this computer for a Steam Deck or not (its first-start settings) |
| `TTYD_ART=off/lazy` | Aurora: the art recipes not run (the drawn art stands in), or each image made when first asked for |
| `TTYD_P2_PRESS=<keys>@<frame>[+N][,...]` | scripted controller 2: those keys held from that frame for N frames (`port/src/common/input_pc.c`) |
| `TTYD_HAPTICS_PRESS=<k>/last`, `TTYD_HAPTICS_LOG` | Aurora: A pressed k frames into each window a haptic cue announces; log each cue, pulse and judgement |
| `TTYD_BATTLE_SPEED_LOG` | log Faster battles' speed changes and guard windows |
| `TTYD_CAPTION_LOG` | Aurora: log each caption for a sound as it comes |
| `TTYD_SPEECH_LOG=<file>` | Aurora: what Read text aloud would say, a line each in the file, no voice |
| `TTYD_DALTON_LOG` | Aurora: log the colour-blind filter's matrix when it changes |
| `TTYD_REWIND_LOG`, `TTYD_REWIND_NO_WATCH` | Aurora: log Rewind's states and steps; compare every memory page, not only the written ones |
| `TTYD_BINDINGS_TEST=1` | Aurora: a virtual controller pressed through the Bindings checks, each step's input printed (`bindings.cpp`) |
| `TTYD_ONE_HANDED_TEST=left/right` | Aurora: a virtual controller with that one-handed preset, each button's result printed |
| `TTYD_MESSAGE_LOG_TEST=<pages>`, `TTYD_MESSAGE_LOG_PRINT` | Aurora: the message log opened once it holds that many pages; each page printed as it is logged |
| `TTYD_PHOTO_TEST=<frame>:<yaw>:<pitch>:<zoom>`, `TTYD_PHOTO_MENU_SHOT` | Aurora: photo mode at that frame and view, two pictures; a picture once it is opened from the Start menu |
| `TTYD_PRESET_TEST=<file>`, `TTYD_PRESET_APPLY=<code>` | Aurora: this setup's preset code written to the file; a code put in place at start |
| `TTYD_COMPANION_WINDOW=1`, `TTYD_COMPANION_MENU=0`, `TTYD_COMPANION_TAB=<n>` | Aurora: the second screen in a desktop window; its Start menu picture off; the tab it opens at ([SECOND_SCREEN_MENU.md](SECOND_SCREEN_MENU.md)) |
| `TTYD_COMPANION_DUMP`, `TTYD_COMPANION_AUDIT` | Aurora: the second screen's snapshot and input in the log; its menu frames checked as `TTYD_INTERP_AUDIT` checks in-between frames |
| `TTYD_DISCORD_IPC=<pipe>` | Aurora: Discord status talks to that pipe or socket instead of Discord's ([DISCORD_STATUS.md](DISCORD_STATUS.md)) |
| `TTYD_ONLINE_HOST=<port>`, `TTYD_ONLINE_JOIN=<host>:<port>`, `TTYD_ONLINE_LAN` | Aurora: host or join an online game by address at start; LAN: the host listens on the network, not only this computer ([ONLINE_RELAY.md](ONLINE_RELAY.md)) |
| `TTYD_ONLINE_HOST_ROOM=<code>`, `TTYD_ONLINE_JOIN_ROOM=<code>`, `TTYD_ONLINE_RELAY=<url>` | Aurora: host or join a room on the relay at start; another relay than the built-in one |
| `TTYD_ONLINE_WATCH=<host>:<port>`, `TTYD_ONLINE_WATCH_ROOM=<code>` | Aurora: watch an online game at start |
| `TTYD_ONLINE_CONTINUE`, `TTYD_ONLINE_RECOVERIES=<n>` | Aurora: host from the last save ("Continue where you left off"); the out-of-step recoveries so far (set by the game as it starts again) |
| `TTYD_ONLINE_NO_ICE`, `TTYD_ONLINE_NO_RECONNECT` | Aurora: online games only through the relay (no direct path); no reconnecting after a drop |
| `TTYD_ONLINE_DROP_AT=<frame>`, `TTYD_ONLINE_DESYNC_AT=<frame>` | Aurora: the connection dropped at that frame, once; the guest's random numbers nudged at that frame, once |
| `TTYD_ONLINE_INVITE=<link>` | Aurora: an invite link handed from the launcher to another release's build (set by the game) |
| `TTYD_REPLAY_RECORD`, `TTYD_REPLAY_NAME=<name>`, `TTYD_REPLAY_PLAY=<file>` | Aurora: record a replay from the start, its name; play one (set by the game as it restarts; [REPLAYS.md](REPLAYS.md)) |
| `TTYD_REPLAY_OUT=<dir>`, `TTYD_REPLAY_FRAMES=<n>`, `TTYD_REPLAY_EXIT` | Aurora: recordings saved there; a recording saved after n frames; exit once it is saved or the playback is over |
| `TTYD_SAVE_SEND`, `TTYD_SAVE_SEND_QR=<file>`, `TTYD_SAVE_SEND_BIND=<address>` | Aurora: send saves from the first frame; the QR code saved as a picture; listen on that address ([SAVE_TRANSFER.md](SAVE_TRANSFER.md)) |
| `TTYD_SAVE_RECEIVE=<url>`, `TTYD_SAVE_RECEIVE_FILES=<1234>` | Aurora: receive saves from that address at start, taking those files |
| `TTYD_UPDATE_API=<url>`, `TTYD_UPDATE_INSTALL` | Aurora: a test release on this computer instead of GitHub's; press Install without the page ([UPDATES.md](UPDATES.md)) |
| `TTYD_UPDATE_APPLIED=<version>` | Aurora: the version just installed, shown on the General tab (set by the update as it starts the new version) |
| `TTYD_URL_SCHEME_TEST` | Aurora on Windows: register `papercut-test://` links instead of `papercut://` (the registration's own test) |
| `TTYD_LOAD_SAVE_SLOT=0-3` | with `TTYD_LOAD_SAVE`: that save as this file slot (the launcher's Continue) |
| `TTYD_LAUNCHER_TAB=<tab>` | Aurora: the launcher opens on that tab (Online, for an invite link) |
| `TTYD_UPDATE_REPO=<owner>/<repo>` | Aurora: check another repository's releases for updates |

Crashes and `abort()` print a symbolized backtrace.

The picture keeps its shape in any window or screen: Aurora's FIT viewport
policy (`AuroraSetViewportPolicy`, set at startup) bars the 608x480 frame
instead of stretching it (it stretched 1.6x on a 19.5:9 phone before).

**Widescreen** (`port/src/aurora/widescreen_aurora.c`), opt-in: off by
default on every platform, on with the `widescreen` preference (iOS: the
switch in the Settings app, `port/ios/Settings.bundle`; macOS: `defaults write
<bundle id> widescreen -bool true`) or `$TTYD_WIDESCREEN=1`. Much of the game
was built for the 4:3 frame only: a capture sweep of the title, the opening
story, menus, field maps and battles showed the title diorama's backdrop
(green), the storybook's table, and black past the walls of indoor rooms at
the sides; outdoor maps and battle stages widen well. Switching aspect per
scene was rejected (inconsistent), so 4:3 is the default and 16:9 is the
player's choice, with those artifacts. When on, the frame is locked to
16:9 (`VILockAspectRatio`, an Aurora patch) and every draw to it has clip-space
x scaled by (608/480)/(16/9) (`GXSetProjectionScaleX`, applied by Aurora when
it builds a draw's uniforms, so `GXGetProjection` still returns the game's
matrix and save/restore pairs stay exact). 3D shows more to the sides, 2D
keeps its shape, centred. Per camera (`camdrv`'s loop calls
`portWidescreenCamera`): the background and fade cameras are stretched to the
full width instead (full-screen layers). Full-screen quads drawn at the
608-wide frame's edges (letterbox bars, screen-copy effects, the name-entry
backdrop) use `WIDE_X(x)` (`include/wide_x.h`) to reach the widened edges.
Offscreen render targets are not scaled. Interior rooms show black past their
walls at the sides (the room ends there), as in other widescreen hacks.

### Display options

The Display tab (`port/settings/features/display.cpp`, applied once a frame
by `port/src/aurora/display_aurora.c` through Aurora's display patches) holds
the aspect ratio and widest picture, HUD size, anisotropic filtering, texture
detail, supersampling, the picture filter, variable refresh rate, the
performance overlay and HDR output. All are off, or the game's own, by
default.

HDR output (settings `hdr`, `hdr_paper_white`, `hdr_boost`; Aurora patch
0014, `include/aurora/hdr.h`) presents the picture on an HDR display. The
game draws in SDR as before, and so do its screen copies, the settings page
and the other overlays. Only the window's swapchain changes: it becomes a 16-bit
float surface in extended linear sRGB, and one last pass converts the
finished SDR frame into it. That pass undoes the sRGB curve, scales white to
the chosen paper white (80 to 400 nits, 200 by default), optionally raises
whatever is close to white by up to 50% more (Highlight boost, 0 by default,
judged by luminance so saturated colours are left alone), and never goes past
the peak the display reports. On Windows the float surface is scRGB, where
1.0 is 80 nits, so 200 nits is 2.5. On Macs and iPads the window's Metal
layer is switched to extended dynamic range (EDR) in extended linear sRGB.
EDR brightness is relative to the system's own white, which Aurora takes to
be 100 nits, so there the paper white is a multiple of the system's white
rather than an exact brightness. HDR10 (PQ, Rec. 2020) is not used: the
only way to ask the bundled Dawn for a colour space (`SurfaceColorManagement`,
given when the surface is made) is unsupported in some of its backends, and a
surface that fails to be made would break SDR too.

The option is greyed out, with a note saying why, unless SDL reports HDR on
for the window's display (`SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN`) and the
graphics backend can present it. That means Direct3D 12 or 11 on Windows, or
Metal on macOS 10.12+ and iOS 16+, and the surface must offer an
RGBA16Float format. Dawn's Vulkan and OpenGL swapchains only accept
sRGB-nonlinear surfaces, so Linux, Android, and Windows when forced onto
Vulkan stay SDR. If the window moves to a display without HDR, or HDR is
turned off in the system, SDL sends `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`
and Aurora reconfigures the swapchain back to SDR. A switch left on from an
HDR session can still be turned off while greyed out. With the option off,
nothing changes: the swapchain, its format and the present passes are
exactly as before, and no HDR pipeline or texture is created.

Untested: the HDR path has not been run on any machine yet. It was written
without a full build, so it has only been syntax-checked against the bundled
Dawn and SDL3 headers, and its WGSL shader has not been compiled. Still to be
checked on hardware: that Dawn's Direct3D 12 and 11 swapchains list
RGBA16Float and that Windows shows them as scRGB; that Dawn leaves the
CAMetalLayer's EDR settings alone after configuring it (Aurora sets them
again after every configure to be safe); the brightness on a real HDR10
monitor, a MacBook Pro XDR screen and an HDR iPad; and how the reported
peak (`SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT` times the SDR white level) is
used on each system. Android HDR (an scRGB dataspace on the native window)
would need Dawn's Vulkan swapchain to accept that colour space, so it is
not done.

## iOS

Built and run in the Simulator (iPhone 17 Pro, iOS 26.3, Xcode 26.3): the
disc image in Documents is found, the game boots, plays (field, HUD,
partner), the touch controls drive Mario, and Home / back pauses and resumes
the same process. Aurora backend, arm64, devices and the Simulator (Apple silicon Macs); one
app per version (`ttyd-jp.app`, `ttyd-us.app`, `ttyd-eu.app`, bundle ids
`io.github.ttyd-port.jp`/`.us`/`.eu`, `-DTTYD_IOS_BUNDLE_ID_PREFIX` to change).

### Building

On a Mac with Xcode 15 or later, CMake 3.25+, Ninja, Python 3, Homebrew
LLVM (`llvm-nm`, `llvm-objcopy`, `llvm-ar` for the RELs; Xcode has no
objcopy) and Rust with the iOS targets (Aurora's disc reader, nod):

```bash
brew install cmake ninja llvm
rustup target add aarch64-apple-ios aarch64-apple-ios-sim
cmake -S . -B build-ios-jp -G Ninja --toolchain cmake/toolchains/ios.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01
ninja -C build-ios-jp ttyd ttyd_ipa   # ttyd-jp.app and an unsigned ttyd-jp.ipa
```

- `-DTTYD_IOS_PLATFORM=SIMULATOR` builds for the Simulator, signed ad hoc:
  `xcrun simctl install booted build-ios-sim/ttyd-jp.app`, then
  `xcrun simctl launch booted io.github.ttyd-port.jp`. Aurora's prebuilt Dawn
  is for devices only, so the Simulator build compiles Dawn from source (a
  long first build).
- The toolchain file (`cmake/toolchains/ios.cmake`) builds SDL3 and nod from
  source, statically, uses Aurora's prebuilt Dawn (Metal) for devices, and
  finds libraries only in the iOS SDK, so the Mac's Homebrew packages are not
  linked by mistake.
- Use the Ninja generator: the static RELs post-process object files in
  custom commands, which `-G Xcode` does not handle reliably. Xcode can still
  debug the app (Debug > Attach to Process, or open the `.app`).

### Installing on a device

A development profile for the app's bundle id is needed once per bundle id
(and device). With a paid team, Xcode makes it: build any app target with
that bundle id and your team for the device (Run once, or `xcodebuild
-allowProvisioningUpdates`); the profile lands in
`~/Library/Developer/Xcode/UserData/Provisioning Profiles/`. Then configure
with `-DTTYD_IOS_BUNDLE_ID_PREFIX=<prefix>` (bundle id `<prefix>.jp`/`.us`/`.eu`),
`-DTTYD_IOS_CODESIGN_IDENTITY` and `-DTTYD_IOS_PROVISIONING_PROFILE`.

No App Store. Sideload the `.ipa` with AltStore, SideStore or Sideloadly,
which sign it with your Apple ID (a free account signs for 7 days at a time).
Or sign it yourself: `-DTTYD_IOS_CODESIGN_IDENTITY="Apple Development: ..."
-DTTYD_IOS_PROVISIONING_PROFILE=<.mobileprovision>` signs the app after every
link (`port/ios/codesign.sh`, the profile's entitlements), then
`xcrun devicectl device install app --device <id> build-ios-jp/ttyd-jp.app`.

### The disc image

The app's Documents folder is shared: it shows up in the Files app (On My
iPhone/iPad > TTYD JP) and in Finder's device window (Files tab). Put your
own disc image there (any format Aurora's nod reads; RVZ is the smallest),
or at the first launch tap Choose File: the system picker's copy is moved
into that folder. At each launch the app opens the first image there that is
this build's game. No path is remembered: an app's container path changes
with every update. Importing rather than opening the file in place avoids a
security-scoped bookmark held across launches and file providers (iCloud
Drive) that evict files under the reader (`port/ios/ios_app.m`).

Memory cards (`.gci`) and settings are in `Saves` inside that folder, so saves
can be copied to and from Dolphin; Aurora's pipeline caches are in
`Library/Caches` (not backed up).

### How it differs from desktop

- **Layout** (`port/src/aurora/layout_aurora.c`, Aurora patch 0006
  `AuroraSetPresentArea`): held upright, the picture (its actual shape) is
  the full safe width, centered above the on-screen controls; sideways, it is
  centered with the controls beside it. Settings > Video > Orientation turns
  with the device, or keeps landscape or portrait (`SDL_HINT_ORIENTATIONS`,
  set at run time). The controls can be moved per orientation (Settings >
  Controls > Move the on-screen controls; `touch_layout.txt` in Saves).
- **Crash reports** (`crash_pc.c`, every platform): a crash or stall also
  writes `crash_report.txt` in the user folder: the build (version, image
  UUID and load address, for symbolizing the offsets), the sequence and map,
  and the stack. At the next start it becomes `last_crash.txt` and the game
  offers to copy it (also Settings > General).
- **RELs are linked in** (`TTYD_RELS_STATIC` is forced: an app may only run
  code signed into its bundle). Mach-O has no grouped sections, so each REL's
  data and bss go to `__DATA,__rd_<area>`/`__rb_<area>`, bounded by the
  linker's `section$start$`/`section$end$` symbols; the archives are
  `-force_load`ed and `rel_rename.py` keeps Mach-O's leading underscore
  (`_rel_<area>__name`). Checked on Windows with LLVM's Mach-O tools
  (`clang --target=arm64-apple-ios14`, `llvm-objcopy`, `ld64.lld`): the
  table's bounds land on each REL's sections.
- **Coroutines** (`os_thread_pc.c`): the ucontext routines are not
  implemented for arm64 on Apple systems, so the game's threads switch with a
  small routine saving the callee-saved registers (Apple arm64, also Apple
  silicon Macs).
- **Bundle** (`port/ios/ios.cmake`, `Info.plist.in`): full screen, landscape,
  a launch screen entry (else iOS runs the app letterboxed), file sharing and
  opening documents in place, game controller keys
  (`GCSupportsControllerUserInteraction`, `GCSupportedGameControllers`,
  `UIApplicationSupportsIndirectInputEvents`), Metal required. The icon is the
  title screen's key art (Mario and his hammer), cropped from the user's own
  extracted disc (`data/<version>/files/mariost.tpl`, decoded with
  `tools/port/tpl2png.py`) at configure time, so it exists only in the build;
  without an extraction, a generated placeholder (`make_icon.py`).
- **Lifecycle:** SDL sends `SDL_EVENT_WINDOW_MINIMIZED` when the app resigns
  active (home, app switcher, Control Center, a call) and `RESTORED` when it is
  active again; Aurora turns those into its Background pause, the port pauses
  (`portPause`: game clock and audio device) and stops drawing until it is
  back, so no Metal work is submitted in the background. The audio device is
  paused, not closed; SDL handles audio session interruptions. The audio
  session is SDL's default, ambient (follows the silent switch).
- **Display:** Aurora fits the 640x480 picture to the screen (pillarboxed on
  phones); in landscape the notch and corners fall in the side bars, and the
  home indicator is deferred (SDL's default for full screen windows).
- **Input:** controllers through SDL3 and Aurora's PAD as on desktop. The touch
  overlay (`port/touch/touch_overlay.cpp`, `TTYD_TOUCH_OVERLAY`, on by default
  on iOS): touches become port 1's virtual status (Aurora's
  `PADSetVirtualStatus`, merged into `PADRead`), so the game reads them like a
  controller; drawn with Aurora's Dear ImGui. Floating stick on the left,
  A/B/X/Y on the right (GameCube arrangement), L, R, Z and START at the top,
  laid out in the safe area (on phones in the bars beside the picture).
  Buttons act on touch-down, before that frame's `PADRead`, for action
  commands. Hidden (and the virtual status cleared) once a controller is
  used, shown again on a touch (a connected controller is not enough: the iOS
  Simulator reports one that never sends input). Built and run on Windows with `-DTTYD_TOUCH_OVERLAY=ON`: drawn
  over the picture, and a scripted touch on START
  (`TTYD_TOUCH_TEST=1900:0.5,0.07:10`, fast mode) skips the opening exactly
  as a scripted START press does (same frame hash). `TTYD_TOUCH_MOUSE=1` lets
  the mouse touch on desktop builds with the option on. Not yet: D-pad,
  C-stick, a layout editor, opacity and size settings, haptics on press.

- **Widescreen** (16:9, opt-in: Settings app > Paper Cut > Widescreen,
  read at launch; Running has the details and the scenes it does not suit):
  sized for the iPhone 17 Pro, whose landscape safe area
  (about 750 x 402 points beside the Dynamic Island) holds a 715-point-wide
  16:9 picture; the controls sit in the ~80-point bars and overlap its edges
  translucently.

- **Pipeline seed:** `data/initial_pipeline_cache.db`, when present, is
  bundled (CMakeLists.txt; iOS bundle root, macOS `Contents/Resources`, else
  next to the executable). Aurora builds its pipelines at launch, before the
  game draws with them: on an iPhone a model drawn for the first time was
  otherwise skipped until its pipeline was built (invisible cutscene
  characters, their shadows drawn). Pipeline configurations are the same on
  every platform (checked: every pipeline seen on both an iPhone and a Mac
  had the same hash and config), so the seed is recorded on a desktop: run
  `map_sweep.py` (Aurora, `--shots`) over every map and `--battles` of each
  version, then `tools/port/pipeline_cache.py merge
  data/initial_pipeline_cache.db <shots dirs>`. It comes from the user's own
  disc, so it is never committed. Aurora merges it into the user's cache at
  every launch, so an update reaches existing installs.
- **Debugging on a device:** `Documents/env.txt` takes the switches of
  Running (`NAME=value` lines, `{docs}` = the Documents folder), as Android's
  env.txt: `xcrun devicectl device copy to --domain-type appDataContainer
  --domain-identifier <bundle id> --source env.txt --destination
  Documents/env.txt`, then `xcrun devicectl device process launch --console
  <bundle id>` streams the game's output (`TTYD_TRACE=2` lists every running
  script and what it waits on).

Not done yet: the security-scoped alternative to
importing; a suspend snapshot (iOS may end the app between saves).

## Testing

`python tools/port/local_ci.py` is the CI, run on this machine: the three
GameCube matching builds, the six PC builds (headless and Aurora, JP, US and
EU), unit tests, audits (`proto_audit`, `gen_bedecode --check`, `diag_census`),
map sweeps with golden audio and the card scripts (headless and Aurora, then
the headless card loaded on Aurora), plus a quick Linux check (a build in WSL
when a distribution is installed, else a cross-compile and link); independent stages run at once, each logs to
`build-ci/<stage>.log`. `--quick` leaves out the sweeps and card scripts. On
Linux (CI's container) `TTYD_BUILD_SUFFIX=-linux TTYD_CI_HEADLESS_ONLY=1` keeps
its builds apart and leaves out Aurora. Steps that download (configuring,
building, Gradle) are retried after 30 s and 2 minutes.

`ctest --test-dir build-pc-jp -LE slow` (build the test targets first, see
the workflow): decoder and loader unit tests on synthetic data, plus, when the
disc is extracted to `data/<version>` (`python3 tools/port/extract_disc.py
DISC.iso data/<version>`: `files/` and `sys/`, as Dolphin extracts it):

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
  The hashes depend on the host's C math library: the game calls the host's
  `sin`/`cos`/`atan2`/... (the GameCube used MSL's fdlibm, in
  `libs/msl/src/MSL_C.PPCEABI.bare.H.a/`), whose last-bit results differ
  between Windows (UCRT) and Apple's libm. On macOS, 158 of 279 JP maps
  differ from the Windows hashes, identically on arm64 and x86_64 (Rosetta),
  and arm64 and x86_64 agree with each other on all 279: the architecture
  (with `-ffp-contract=off`) is not the cause, the OS's libm is. Making
  every platform agree (and match the GameCube) means building MSL's fdlibm
  for the port and routing the game's math calls to it; not done yet, and
  the Windows golden files would then be re-recorded.
- `--no-bero`: warp to each map's origin instead (the game's debug map select).
- `--every-bero`: one run per entrance of every map (`map@bero`, ~600).
- `--battles`: one run per entry of every area's battle table (`area#N`, 407 on
  JP), fought from a map of that area: `battle N` gives the entry to the
  nearest NPC, as a script's `evt_npc_set_battle_info` does, then
  `battle_sweep.txt` attacks a few turns. Every enemy formation, bosses
  included.
- `--party N`: every run with partner N (1 Goombella .. 7 Ms. Mowz; default 1).
- `--story`: every map at every story value its scripts compare `GSW(0)` with,
  and the one below (`map%NNN`, ~1700 runs; `$TTYD_STORY` sets it at the warp),
  pressing A through the cutscenes they start (`story_sweep.txt`). Failures a
  warp causes by skipping earlier maps' setup are listed in `WARP_ARTIFACTS`
  and reported apart.
- `--collect REGEX`: report matching output lines of every run (e.g.
  `'\[msg\] missing'`).

Each run gets its own card folder (and, on Aurora, its own cache), and a 30 s
watchdog.

### Input scripts

`TTYD_INPUT` (`port/src/common/input_pc.c`; examples in `tests/port/input/`)
drives controller 1, applied after the game's one `PADRead` (`DEMOPad.c`). With
`TTYD_MAP` the script starts on the map and ends the run. Commands: `wait N`,
`hold KEYS N`, `press KEYS` (keys `a b x y z l r start`, `left right up down`
for the stick, `dleft`.. d-pad, `cleft`.. C-stick, joined with `+`),
`stick X Y N`, `shot NAME`, `battle [N]` (the nearest enemy NPC, through
`evt_npc_battle_start`; with N, or `$TTYD_BATTLE_SETUP`, entry N of the
area's battle table on the nearest NPC), `hp N`, `wait_seq N [MAX]`, `wait_map NAME [MAX]`,
`press_until_seq KEYS N [MAX]`, `press_until_map KEYS NAME [MAX]`, `pause MS`
(`portPause` for MS of real time; `tests/port/input/pause.txt`), `end`.

Real saves: `tools/port/gcmemcard.py` takes files out of raw GameCube memory
card images (`.raw`, as Dolphin keeps them) as `.gci`, the port's card
format (`tree RAW_DIR GCI_DIR --game G8MJ` for a whole folder);
`tools/port/save_sweep.py EXE GCI_DIR --disc ISO` loads each save through
the file menu on a fresh card and plays ~20 s (`save_sweep.txt`), reporting
the map and story value it loaded into and any failure. A collection of
speedrun practice saves (a point in every chapter, JP and US) lives,
git-ignored, in `data/saves/` (`raw_*` the card images, `gci_*` extracted).

For playing the story: `goto HIT` or `goto X Y Z` (Mario onto a hit object,
e.g. an exit's zone or a trigger floor, or a position, as `evt_mario_set_pos`
does), `press_until_control KEYS [MAX]` (press every 30 frames until Mario can
be moved again for a second: cutscenes, messages and battles over),
`press_until_story KEYS N [MAX]` (until the story counter reaches N), `save`
(the open file, there and then, as a Save Block does: a checkpoint a later
run resumes from with `load_file.txt`). `$TTYD_TRACE` logs each change of the
story counter (`[trace] frame N: story S`). `tools/port/story_steps.py
--from N --to M` lists, for each story value the scripts set, the script that
sets it, what starts it (a map's init, an NPC, a hit object's case event, a
battle's end) and the position checks it waits for: the scene's trigger.

## Audits

Bugs are fixed as classes: each one found gets a detector, and every instance
in the tree is fixed at once.

- Audit build: `-DTTYD_PTRWIDTH_WARNINGS=ON` enables pointer/int casts,
  `int-conversion`, implicit declarations, unsequenced access, incompatible
  pointer and function-pointer types, `cast-function-type-strict`,
  `shorten-64-to-32` and `format`, instead of `-w`.
- `tools/port/noproto_calls.py`: calls through a no-prototype `f()`
  declaration passing fewer arguments than the definition takes (the callee
  reads a stale register: `L_partyVivianTailStart()` crashed moo_00 on
  x86-64). Must report 0.
- `-DTTYD_FORTIFY=ON`: keeps the C library's `_FORTIFY_SOURCE` in game code
  (off by default: the game copies strings past char fields into unread
  padding, which Apple's and bionic's default fortify abort on); a sweep
  then lists every overflowing copy.
- `tools/port/proto_audit.py`: prototypes (often local, MWCC-shaped) against
  their definitions, TARGET_PC-aware; also unprototyped `f()` declarations of
  functions taking floats, and variadic/non-variadic mismatches. Must report 0.
- `tools/port/gen_bedecode.py --check`: generated decoders match specs and headers.
- `tools/port/linux_xcheck.py --out DIR [--arch x86_64|aarch64] [--version ...]`
  (Windows, no Linux sysroot needed): compiles the headless build and the unit
  tests for Linux against stand-in glibc headers, links with ld.lld (C library
  unresolved) and checks the RELs' ELF sections.
- `tools/port/fncast_audit.py`: classifies the calls through recast function
  pointers in an audit build's warnings (safe, or wrong on a PC ABI).
- `tools/port/ptrwidth_warnings.py`: compiles every game file with only the
  int/pointer width warnings on and lists them by file and kind.
- `tools/port/dolphin_compare.py`: runs the disc in Dolphin with the port's map
  warp and compares each map's frame with the Aurora build's capture.
- `tools/port/sjis_src.py`: Shift-JIS literals for clang.

Classes found so far, and what catches them:

| Class | Detector |
|---|---|
| Pointer narrowed through s32/u32 (script words, tables, locals, saved LW arrays) | `-Wshorten-64-to-32`, clang-query data flow, crashes at `0xffffffff8…`/low-32-bit addresses |
| Prototype disagrees with the definition (Win64 assigns int/float registers by position; narrow returns are not extended) | `proto_audit.py`, `-Wcast-function-type-strict` |
| Second struct view of the same memory, GC-sized pads/buffers/allocations, GC byte strides and offsets | clang AST cast survey comparing x64 and i686 record layouts |
| Big-endian data read natively (display lists as u32, string pools by GC offset, word layouts) | disc/song sweeps, captures |
| Script tables walked by `EvtWord` but declared `s32` | map sweep crashes, table/type survey |
| Float overlays on 8-byte script words; float-encoded script constants (`0xF24A7A80`) widened unsigned | union survey, grep |
| Script words written as unsigned decimals past 2^31 (`4264967296` is `LW(0)`, `4064967296+` floats, `4224967296`, `4294966296` = -1000): they wrap to the variable/float encodings in GameCube's 32-bit words but stay positive in 64-bit `EvtWord`s, so the script got a number instead of its variable (Goombella's field tattle passed `0xFE363C80` to msgSearch and crashed; the Toad houses/inns, Punio's chapter, the Pianta Parlor). 413 literals in 11 files now written as their signed values (identical GameCube output) | `grep -rnE "[0-9]{10}"` over src/rels for values in 2^31..2^32 not preceded by `-`; hex: `(s32)` casts |
| Small enums (`#pragma enumsalwaysint off`) | `PORT_ENUM_BASE` on the enum headers |
| Hardware timing the game relies on (ARQ callbacks after posting, empty wait loops) | `arq_defer_pc.c`, `PORT_SPIN` |
| Unwritten SDK out-parameters | stub survey (`gx_state_pc.c`) |
| Integer division by zero, which PowerPC computes (quotient 0, or -1 for a negative dividend; remainder the dividend) and x86-64 traps (`rand() % 0` in eff_stardust_n64, a pose step / 0 in the X-Naut platoon) | `map_sweep --battles` crashes; fixed for every site at once: the x86-64 exception handlers emulate the PowerPC result (`divzero_pc.c`), `$TTYD_TRACE` logs each site (`[divzero]`) |
| Enums unsigned on the SysV ABIs (Linux, Apple, Android): without a negative enumerator an enum is `unsigned int` (MWCC, MSVC: `int`), which changes `<`, division, float conversion and widening to `EvtWord` | `PORT_ENUM_BASE(int)` on every game enum (a flag enum with `0x80000000` stays unsigned); Linux-vs-Windows warning diff (`-Wsign-compare`, `-Wsign-conversion`) |
| MSVC-only leniencies hidden by the Windows build: a `static` definition after an `extern` declaration (a header's, or `<math.h>`'s `sinf`), one plain `inline` function name defined in several files (COMDAT on Windows: one body serves every file, even with another signature) | compiling and linking for Linux (`tools/port/linux_xcheck.py` on Windows) |
| `extern inline` under `-fgnu89-inline`: no out-of-line copy, so a call the compiler does not inline (`-O0`, the Android debug build) is unresolved, or binds to a C library function of the same name (snd3d.c's approximate `sqrtf`) | `-O0` link (Android debug build); `grep -rn 'extern inline'` over PC-compiled code |
| A call through a no-prototype `f()` declaration with fewer arguments than the definition takes: the callee reads whatever the argument register holds (r3 held the right value on GameCube; x0 happened to on arm64, rdi did not on x86-64: moo_00) | `noproto_calls.py` |
| C library hardening on by default (Apple clang, bionic, some Linux distros: `_FORTIFY_SOURCE`) aborting on the game's intended overflows into padding | off in game code; `-DTTYD_FORTIFY=ON` sweep |
| Per-address records outliving their memory: GX array ranges/disc blobs, decoded anim models and TPL graphs kept for freed blocks or cleared heaps, so new data at the same address was taken for the old (a pose's native vertices read as big-endian through a stale range: the title's red curtain wedge after the opening demo) | `portMemForget` from `smartFree`/`L_smartReInit`/`memClear` (`port/mem_pc.h`); a new range drops overlapping older ones; a range's endianness only from disc-data records (`portGXIsDiscData`); `TTYD_GXARRAY_TRACE=2` shows upload sizes |
| Empty files on the disc read into `_mapAlloc(0)` (NULL): a 0-byte read must complete | headless DVD; map sweep with a full extraction (`dtk vfs cp`) |
| Variadic function called as non-variadic or the reverse (Apple arm64 passes variadic arguments on the stack); an undeclared call passing an f32 (sent as a double) | `proto_audit.py`; `-Werror=implicit-function-declaration`; `diag_census.py` over every version |

## Android

Status: **builds, not yet run**. On Windows, `libmain.so` (arm64-v8a, JP, US
and EU) links with no undefined symbols beyond the system libraries, and the
Gradle build makes the three debug APKs (`assembleJpDebug`, `assembleUsDebug`,
`assembleEuDebug`). No device
or emulator was available, so the game on Android is unverified (x86_64
emulators would need Dawn built from source, below). CI job: `android` in
`.github/workflows/pc-port.yml` (manual, like the rest).

### Building

Needs: JDK 17; the Android SDK with platform 35, build-tools 35, NDK
29.0.14206865 (the one Aurora's prebuilt Dawn was built with) and the SDK's
CMake 3.31.6; Gradle 8.12 (the version built with; the Android Gradle plugin
is 8.7.3); Python 3 on `PATH` (the CMake project runs
`tools/port/*.py`); Rust (`rustup target add aarch64-linux-android`: Aurora
builds its disc reader, nod, from source on Android); the `extern/aurora`
submodule. The first build downloads SDL's source (Gradle task `prepareSdl`,
the release Aurora pins), Dawn's arm64 package and nod's crates.

```bash
cd port/android
gradle wrapper --gradle-version 8.12   # once; or run an installed gradle directly
./gradlew assembleJpDebug              # G8MJ01; assembleUsDebug G8ME01, assembleEuDebug G8MP01
# -> app/build/outputs/apk/jp/debug/app-jp-debug.apk
```

- Gradle needs `ANDROID_HOME` (or `sdk.dir` in `port/android/local.properties`)
  and `JAVA_HOME` set to JDK 17. A from-scratch build takes about 12 minutes
  per flavor; debug builds compile the game at `-O0`.
- The native library alone, for faster turnaround (`ninja -k 0` lists every
  failing file at once):

  ```bash
  cmake -S . -B build-android-jp -G Ninja \
    --toolchain "$ANDROID_HOME/ndk/29.0.14206865/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01 -DTTYD_BACKEND=aurora
  ninja -C build-android-jp     # -> build-android-jp/libmain.so
  ```

  (Ninja: the SDK's `cmake/3.31.6/bin`.) Without `FETCHCONTENT_SOURCE_DIR_SDL`
  Aurora downloads SDL itself.
- Windows with rustup's `windows-gnu` toolchain: Cargo builds nod's build
  scripts for that host, and rustc wants a `dlltool` on `PATH` for their import
  libraries, which rustup does not ship. The CMake project then puts a copy of
  the NDK's `llvm-dlltool` named `dlltool.exe` on `PATH` for Cargo
  (`build*/rust-host-tools`). The `windows-msvc` toolchain needs nothing.

- Flavors `jp`/`us`/`eu` set `TTYD_VERSION` and the app id (`dev.ttyd.port.jp`,
  `.us`, `.eu`), so all three install side by side. `-PTTYD_PORT_VERSION=X.Y.Z`
  sets the app's version (versionCode X*10000 + Y*100 + Z; without it 0.2.0,
  code 2); `release.py` passes the release's. Release builds
  (`assembleJpRelease`, optimized) are signed with the debug key, for
  sideloading.
- ABIs: `ttyd.abis` in `port/android/gradle.properties` (or `-Pttyd.abis=`),
  default `arm64-v8a`. `x86_64` (Intel/AMD emulators) has no prebuilt Dawn:
  Aurora builds Dawn from source for it (slow, untried). On an Apple-silicon
  Mac the arm64 emulator images run the arm64 build.
- `minSdk` 28: Dawn's prebuilt package links against API 28, and Dawn needs
  Vulkan 1.1 (the manifest requires `android.hardware.vulkan.version` 1.1).

### Installing and playing

- `adb install -r app-jp-debug.apk` (or open the APK on the device and allow
  installing from that source).
- First start: the system file picker asks for the disc image (ISO, GCM, CISO,
  GCZ, RVZ, WIA, ...). Put it in the device's local storage (e.g. Download):
  the game reads the picked document through a file descriptor with random
  access; documents from cloud providers can arrive as a stream that can't
  seek and won't open. The choice is remembered (`disc.txt`, with a
  persistable read grant). To pick another disc, clear the app's storage
  (this deletes the saves too) or, on a debug build,
  `adb shell run-as dev.ttyd.port.jp rm files/disc.txt`.
- Saves (Aurora's memory card) and settings: the app's internal storage
  (`SDL_GetPrefPath`). Moving `.gci` files in and out is not done yet.
- Controls: any controller SDL supports (Bluetooth or USB); otherwise the
  on-screen controls (below), which fade while a controller is in use and come
  back when the screen is touched. Back does nothing (it is not "quit").
- Debugging: everything the game prints goes to logcat with the tag `ttyd`
  (`adb logcat -s ttyd SDL`). The switches of "Running" are read from
  `env.txt` (`NAME=value` lines) in the app's external files folder:
  `adb push env.txt /sdcard/Android/data/dev.ttyd.port.jp/files/`. A crash logs
  the signal and a backtrace under `ttyd`, then the system writes its
  tombstone (`ndk-stack -sym <folder of the unstripped libmain.so, under
  app/build/intermediates/merged_native_libs/>` symbolizes it).

### How it fits together

- **Native library**: the same CMake project; with `ANDROID` the game is a
  shared library `libmain.so` (SDL's activity loads it and calls `SDL_main`,
  Aurora's `lib/main.cpp`, kept with `--undefined=SDL_main`). SDL3 is built
  from source (Aurora requires it on Android) and linked statically
  (`AURORA_SDL3_LINKAGE=static`); Dawn is Aurora's prebuilt
  `dawn-android-aarch64`; nod is built with Corrosion/Cargo; libc++ static.
- **RELs are static** (`TTYD_RELS_STATIC`, default on Android): one
  `libmain.so`, no RELs to `dlopen` that would have to link against it, and no
  reliance on bionic's `dlclose` really unloading a module to get fresh data.
  On ELF each REL's data and bss go to sections `rel_<area>_data` /
  `rel_<area>_bss` (`#pragma clang section`), bounded by the linker's
  `__start_`/`__stop_` symbols, linked with `--whole-archive` and
  `-z nostart-stop-gc`; a one-byte pad keeps the bounds defined for a REL
  without data. The same scheme serves Linux with `-DTTYD_RELS_STATIC=ON`.
- **Java** (`port/android/app/src/main/java/dev/ttyd/port/TtydActivity.java`):
  `SDLActivity` with only `main` to load, Aurora's `AuroraSurface` (tells
  Aurora when the surface exists), and a persistable grant for the picked
  disc's `content://` URI (SDL's dialog doesn't take one). SDL's Java sources
  are not in the repo: Gradle unpacks the SDL release Aurora pins, applies
  Aurora's two SDL patches to it, compiles `org.libsdl.app` from it and hands
  the same tree to CMake (`FETCHCONTENT_SOURCE_DIR_SDL`).
- **Untagged heap**: arm64 Android 11+ tags the top byte of heap pointers,
  which makes them negative as `intptr_t`. The port assumes plain user-space
  addresses, as on the desktop (script words share one number line with the
  negative variable ranges, sound-name words, pointer range checks), so the
  manifest opts out (`allowNativeHeapPointerTagging="false"`).
- **Game threads**: bionic has no `makecontext`/`swapcontext`, so on Android
  `os_thread_pc.c` runs each game thread on a host thread with a baton (one
  runs at a time), as on Apple and musl.
- **Lifecycle**: when the activity pauses, SDL sends
  `SDL_EVENT_WILL_ENTER_BACKGROUND` from its event pump on the game's thread
  and then blocks there until resumed (`SDL_HINT_ANDROID_BLOCK_ON_PAUSE`); an
  event watch (`port/android/native/android_pc.c`) calls `portPause` first and
  `portResume` on `DID_ENTER_FOREGROUND`. SDL pauses its audio devices too.
  Aurora releases the WebGPU surface while the window's surface is gone
  (`AuroraSurface` → `set_surface_ready`, `is_presentable`) and creates a new
  one (swapchain) on the first frame after it is back (`aurora_begin_frame` →
  `webgpu::refresh_surface`), reporting `AURORA_PAUSED`/`UNPAUSED` meanwhile.
- **Disc**: SDL's `SDL_IOFromFile("content://...")` opens the URI with
  `ContentResolver.openFileDescriptor` and `fdopen`s it, so Aurora's nod
  reader (`read_at` = seek + read on the `SDL_IOStream`) works as with a file,
  as long as the provider gives a real, seekable file descriptor.
- **Touch controls**: the shared overlay (`port/touch/touch_overlay.cpp`, iOS
  section), on by default on Android.

### Not done yet

- A device run: everything above the CMake/LLVM checks is unverified.
- x86_64 emulators (Dawn from source), and safe-area insets on notched screens.
- Saving state when the system kills the app in the background between
  saves; a suspend snapshot.
- `.gci` import/export through the system file picker.
- Quitting (`AURORA_EXIT`) calls `exit(0)` from the game's thread instead of
  returning from `SDL_main`.
- Touch layout editing, haptics on touch, per-game button hints.

## The game code and the decomp

The game code (src/, rels/, include/, libs/, config/, configure.py and the
decomp's tools) is [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd) as it is,
plus the port's changes, nearly all inside `#ifdef TARGET_PC`.

- **Updating to a newer decomp** is two commits: the decomp's files exactly as
  they are, then the port's changes put back on top. The decomp's code wins: a
  port change is re-applied on top of it, or dropped when the decomp made it
  unnecessary.
- **The checks that keep the port honest:** the GameCube builds still match
  (local CI's `gamecube` stage: the port's changes do not touch GameCube code),
  `proto_audit` (prototypes against definitions, per version), the pointer-width
  audit build and `fncast_audit`, `tools/check_versions.py` (no per-version
  macros in game code), and the map sweep with golden audio.
- **Versions:** JP (G8MJ01), US (G8ME01) and EU (G8MP01), one executable each;
  `port/sources_<version>.cmake` lists each version's files (`gen_sources.py`,
  from configure.py). The decomp's demo discs are not built here; their `#if`
  branches stay in the game code.
- **PAL:** the game's text is the language of the settings page's choice, else
  `$TTYD_LANGUAGE` (`en de fr es it`), else the computer's (it picks
  `msg/<language>` from the console language, `OSGetLanguage`). Its boot
  question, 50 or 60 Hz, is in the console's ROM font, which the PC hasn't, so
  the port answers it: 60 Hz (EURGB60, the same pacing as JP and US) unless
  the settings page's "Run at 50 Hz" or `$TTYD_50HZ=1` asks for 50. At 50 Hz
  the frame pacing, the audio per frame and the fast-run clock follow
  (`portFieldRate`); in-between frames are whole multiples of 50. Saves are
  the US layout (the European save structs are the US ones), under PAL's own
  game ID.

## Web

A spike: the game built as WebAssembly with Emscripten, headless, run in
Node. The browser build (Aurora on the browser's WebGPU, in a worker) comes
later.

```bash
emcmake cmake -S . -B build-web-us -G Ninja -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=headless -DCMAKE_BUILD_TYPE=Release
ninja -C build-web-us ttyd
TTYD_DATA_DIR=<extracted disc, absolute path> node build-web-us/ttyd-us.js
```

How it fits:
- **wasm32** has 32-bit pointers and script words, as on GameCube.
- **Memory** starts at 2 GiB (`-sGLOBAL_BASE`), so sound names (pointers)
  are told from sound ids by value as on GameCube (`include/sfx_name_word.h`);
  the image, stack and heap land at 0x80000000 and up.
- **Game threads** are host threads (the pthread path of
  `port/src/common/os_thread_pc.c`), from a pool made at startup.
- **RELs** are linked in (`TTYD_RELS_STATIC`). llvm-objcopy cannot rename
  wasm symbols and clang ignores `#pragma clang section` for wasm, so
  `tools/port/wasm_redefine_syms.py` does both for `rel_rename.py`.
- **The disc and saves** go through Node's file system (`NODERAWFS`).

Status: JP and US build and link; US runs about 4,000 frames (boot,
the opening sequences, the `dmo` area) and then traps at the first call
whose declaration differs from its definition. WebAssembly is strict about
those where native PCs are not; `tools/port/web_abi_audit.py` lists them, to
be fixed in the decomp.

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
- **Headless CARD** (`port/src/headless/sdk/card_pc.c`): the CARD calls cardmgr makes, over the
  same GCI folder as Aurora's (`extern/aurora/lib/dolphin/card.cpp`, `lib/card/CardGciFolder.cpp`):
  `<user>/<JAP|USA>/Card A/01-<game>-mariost_save_file.gci`, the 0x40-byte big-endian directory
  entry then the blocks, results and synchronous callbacks as Aurora's, so a headless save loads on
  Aurora and the reverse. `<user>` is `$TTYD_USER_DIR`, else Aurora's default
  (`SDL_GetPrefPath(NULL, "ttyd")`: `%APPDATA%\ttyd`, `~/Library/Application Support/ttyd`,
  `$XDG_DATA_HOME/ttyd` or `~/.local/share/ttyd`). With no card there, slot A is an empty
  formatted card whose folder is made on the first save (Aurora makes it at start), so runs that
  never save write nothing; `map_sweep` gives every run a temporary folder anyway. Checked against
  Aurora with the card scripts: the same steps and results, and `.gci` files that differ only in
  the entry's modification time and the time stamps/counters the game saves (and their checksums).
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
- GSW bytes are copied as bytes too, so the 8-byte play-time stamps scripts keep in them
  (`evt_sub_save_playtime`/`evt_sub_load_progresstime`, the Trouble Center mail check, the Yoshi
  egg in `stg3_12_init__4`/tou_10) are written and read big-endian on PC, as on the GameCube.

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
