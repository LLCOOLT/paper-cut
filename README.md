Paper Cut
=========

**Paper Mario: The Thousand-Year Door, as a native program.** Paper Cut plays the Japanese (G8MJ01), US (G8ME01) and European (G8MP01) releases on Windows, macOS, Linux, iOS and Android, 64-bit, with no emulator.

The game code is C, from the decompilation in [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd). The GameCube platform under it (graphics, audio, input, disc, memory card) is replaced, through [Aurora](https://github.com/encounter/aurora) (WebGPU/Dawn and SDL3).

**No game data is included.** You need your own copy of the game: Paper Cut runs from your disc image.

**Download:** packages are on the [Releases](https://github.com/LLCOOLT/paper-cut/releases) page. On a computer, one download plays every version: `papercut-<version>-<platform>.zip`. On phones there is one app per version. To build it yourself, see [Building and running](#building-and-running).

## Getting started

Start **Paper Cut** and add your disc image (.iso, .rvz, .gcm...) to the launcher's library. It recognises the version and plays it with that version's build. The launcher works with a mouse, a keyboard or a controller. It also holds the settings, so you can set things up before the game starts. With "start with the last disc played" turned on, the next start skips straight to the game.

## Features

**Playing**
- Every map of the Japanese, US and European versions boots and runs, with every battle setup. The European version plays in English, German, French, Spanish or Italian, and any version can show the text of another disc you own.
- The whole story is played through scripted tests at every story value, and about 120 real saves load.
- Memory card saves are Dolphin's `.gci` files. Saves are backed up automatically, and can be converted between versions.

**Settings:** F1, a controller's Back/Select, or a three-finger tap. Tabs down the side, a search box, and a mark on every setting that changes how the game plays or what it saves.

**Display**
- **120 Hz and up:** extra frames are drawn between the game's 60 Hz ones, with interpolated camera, characters and animation. The game's logic is untouched.
- **Any shape of screen:** 4:3, 16:9, 16:10, 21:9 and 32:9, with the HUD sized from 50% to 150%.
- Internal resolution of 1–8× with supersampling, anisotropic filtering, sharp text, and CRT, paper-grain and sharpen filters.
- Variable refresh rate, and a performance overlay with frame times, 1% lows and jitter.

**Gameplay+** (each option off by default):
- Partner quick-swap, faster menus and text, skipped repeated scenes, Save Blocks without the question, and item storage at any Save Block.
- A bigger bag, badge loadouts, a walk-speed boost, and coin and Star Point multipliers.
- Retry on Game Over, fast travel, a journal of collectibles, a Star Piece and Shine Sprite tracker, a jukebox, and Tattle without spending a turn.
- Battle pop-ups, Pit of 100 Trials and Glitz Pit tweaks, a remake-style balance, and New Game+.
- **Play as Luigi:** Luigi in Mario's place, with 156 animations of his own made for Paper Cut, and the text, menus and credits to match.

**Accessibility:** a difficulty slider (enemy HP and attack) and action-command assist (wider timing, or automatic).

**Practice:** a speedrun timer with load removal and [LiveSplit](https://livesplit.org) support, frame advance, RNG control, stat, item and story-flag editors, a memory watch, and ten labelled save-state slots (F5 saves, F8 loads). Plus 25 achievements read from your save, which don't count while the practice tools are on.

**Audio and controls:** separate music, effects and ambience volumes. The mouse works in the game's menus, lists and battle commands. Buttons can be remapped, and any MFi, Xbox, PlayStation or Switch controller works.

**Mods:** a mod manager on the settings page. Mods replace or add disc files, textures and text (Dolphin texture packs work as they are), change game data, run scripts and code through hooks generated from the decompilation, and GameCube code mods can be ported from their source. Also in this area:
- [Archipelago](https://archipelago.gg) multiworld seeds (US).
- Audience effects from chat.
- A boss rush and an endless Glitz Pit.

See [docs/MODDING.md](docs/MODDING.md) and [docs/MODES.md](docs/MODES.md).

**Phones and tablets**
- On-screen controls: movable, sized, with haptics.
- Portrait or landscape, safe areas, and pause and resume with the app.

**Faithful by construction:** GameCube behaviours the game relies on are reproduced on 64-bit hosts, namely big-endian disc data, integer division by zero, paired-single quantization, and the C library's `rand` sequence.

## Status

| Platform | State |
|---|---|
| Windows (x86-64) | Plays. Every map, battle and story sweep passes. Built with clang. |
| macOS (arm64, x86-64) | Plays (the Aurora `.app`). Every map boots. |
| iOS | Plays on a device (iPhone) and in the Simulator. Disc image in the app's Documents. |
| Linux (x86-64, aarch64) | x86-64 builds on Ubuntu 24.04 (CI, in a container): every map boots and saves work, headless. Not yet played on a Linux desktop. aarch64 is cross-compiled and linked only. |
| Android (arm64) | The APKs build; not yet run on a device. |

## Building and running

Each version is its own build: `ttyd-jp`, `ttyd-us` and `ttyd-eu`, configured with `-DTTYD_VERSION=G8MJ01`, `G8ME01` or `G8MP01`. Every build has the launcher. Build the versions you have discs for, side by side, and the launcher starts the right one for each disc. A build with a disc on its command line skips the launcher and plays it, if it's that build's version.

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

CI builds and tests x86-64 on Ubuntu 24.04 (headless: every map, saves), but nobody has played it on a Linux desktop yet. arm64 is so far cross-compiled and linked only.

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

[docs/PC_PORT.md](docs/PC_PORT.md#testing) lists them.

## Documentation

| Document | Contents |
|---|---|
| [docs/PC_PORT.md](docs/PC_PORT.md) | Building on each platform, running, options, settings, tests, audits, design notes |
| [docs/VERSIONS.md](docs/VERSIONS.md) | The Japanese, US and European releases, and how one codebase builds all three |
| [docs/FRAME_INTERPOLATION.md](docs/FRAME_INTERPOLATION.md) | How 120 Hz and up works, and how its purity is checked |
| [docs/PLAYABLE_LUIGI.md](docs/PLAYABLE_LUIGI.md), [docs/LUIGI_ANIMS.md](docs/LUIGI_ANIMS.md) | Play as Luigi, and how his animations are made and shipped without any disc data |
| [docs/MODDING.md](docs/MODDING.md), [docs/MODES.md](docs/MODES.md) | Making and installing mods; the boss rush, endless Glitz Pit and audience modes |
| [docs/APPLE_BRIEF.md](docs/APPLE_BRIEF.md) | Bringing up macOS and iOS |
| [docs/RELEASING.md](docs/RELEASING.md) | CI, the self-hosted runners, and cutting a release |
| [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) | Third-party code and assets, and their licences |

## Credits

- **The decompilation:** [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd), started by NWPlayer123, with its contributors.
- **The platform layer:** [Aurora](https://github.com/encounter/aurora) (the GameCube SDK on WebGPU/Dawn and SDL3), [Dawn](https://dawn.googlesource.com/dawn) and [SDL](https://www.libsdl.org/).
- **The launcher's typeface:** [Fredoka](https://github.com/google/fonts/tree/main/ofl/fredoka), SIL Open Font License.
- **The settings page** follows the one in Dusklight, the Twilight Princess port on Aurora.

The original MIT licence and copyright notice are kept in [LICENSE](LICENSE).

Paper Mario and The Thousand-Year Door are trademarks of Nintendo. Paper Cut is not affiliated with or endorsed by Nintendo.
