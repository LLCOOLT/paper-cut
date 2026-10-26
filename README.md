Paper Mario: The Thousand-Year Door: native port
=============

A native port of Paper Mario: The Thousand-Year Door, Japanese (G8MJ01) and US (G8ME01), to Windows, macOS, Linux, iOS and Android, 64-bit, with no emulator.

The game code is C, from the decompilation in [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd). The GameCube platform under it (graphics, audio, input, disc, memory card) is replaced, through [Aurora](https://github.com/encounter/aurora) (WebGPU/Dawn and SDL3).

**No game data is included.** You need your own copy of the game: the port runs from your disc image.

## Features

**Playing**
- Every map of both versions boots and runs, with every battle setup.
- The whole story is played through scripted tests at every story value, and about 120 real saves load.
- Field play, battles, menus, audio (MusyX in software) and memory card saves are compatible with Dolphin's `.gci` files.

**Display**
- **120 Hz and up:** extra frames are drawn between the game's 60 Hz ones, with interpolated camera, characters and animation. The game's logic is untouched.
- **Widescreen 16:9** (optional; much of the game was built for 4:3).
- Any window size, an internal resolution of 1–8×, and an FPS counter.

**Settings page:** F1, a controller's Back/Select, or a three-finger tap. It covers:
- video, audio and controls (rumble, on-screen controls);
- gameplay (fast-forward, text speed, run speed, skip intro);
- **save warps:** continue from any `.gci` save, e.g. a practice save pack.

**Save states:** four slots, F5 to save and F8 to load.

**Mods:** a mods folder and a Mods tab on the settings page. Mods replace or add disc files, textures and text (Dolphin texture packs work as they are), change game data, run scripts and code, and GameCube code mods can be ported from their source. US builds play [Archipelago](https://archipelago.gg) multiworld seeds. See [docs/MODDING.md](docs/MODDING.md).

**Phones and tablets**
- On-screen controls: movable, sized, with haptics.
- Portrait or landscape, safe areas, and pause and resume with the app.

**Faithful by construction**
- GameCube behaviours the game relies on are reproduced on 64-bit hosts: big-endian disc data, integer division by zero, paired-single quantization, and the C library's `rand` sequence.

## Status

| Platform | State |
|---|---|
| Windows (x86-64) | Plays. Every map, battle and story sweep passes. Built with clang. |
| macOS (arm64, x86-64) | Plays (Aurora `.app`). Every map boots, JP and US. |
| iOS | Plays on a device (iPhone, US) and in the Simulator. Disc image in the app's Documents. |
| Linux (x86-64, aarch64) | Cross-compiled and linked; not yet run on a Linux machine. |
| Android (arm64) | Both APKs build; not yet run on a device. |

## Building and running

Windows (Git Bash, LLVM clang, Visual Studio Build Tools):

```bash
git clone --recurse-submodules https://github.com/LLCOOLT/ttyd-port.git && cd ttyd-port
. tools/port/winenv.sh
cmake -S . -B build-pc-us-aurora -G Ninja --toolchain cmake/toolchains/windows-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
build-pc-us-aurora/ttyd-us.exe "path/to/your/disc.iso"
```

- `-DTTYD_VERSION=G8MJ01` builds the JP version (`ttyd-jp`).
- Without a disc argument, a file dialog asks for one the first time.
- The disc must match the build's version.

### macOS

Needs Xcode (or its Command Line Tools), CMake 3.25+, Ninja, Python 3 and Homebrew LLVM. Only LLVM's `llvm-objcopy`/`llvm-nm`/`llvm-ar` are used, to link the RELs in; Apple clang compiles everything.

```bash
brew install cmake ninja llvm
git clone --recurse-submodules https://github.com/LLCOOLT/ttyd-port.git && cd ttyd-port
cmake -S . -B build-pc-us-aurora -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
open build-pc-us-aurora/ttyd-us.app
```

- Opened without a disc, the app asks for one. You can also drop a disc image on its icon, or run `build-pc-us-aurora/ttyd-us.app/Contents/MacOS/ttyd-us "path/to/your/disc.iso"`.
- Saves, settings and caches are in `~/Library/Application Support/ttyd/`.
- `cmake --install build-pc-us-aurora --prefix dist` makes a self-contained, ad hoc signed copy. It isn't notarized: on another Mac, right-click > Open it the first time.
- The build is for the Mac's own architecture. For Intel from Apple silicon, add `-DCMAKE_OSX_ARCHITECTURES=x86_64 -DAURORA_NOD_PROVIDER=package`.

### iOS and iPadOS

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

- **Signing:** the app is signed after every link with your identity and provisioning profile. The profile must be for `<your.prefix>.us` (or `.jp`). With a paid developer team, Xcode makes one if you build any app with that bundle id for your device once. Without one, build the unsigned `.ipa` (`ninja -C build-ios-us ttyd_ipa`) and sideload it with AltStore, SideStore or Sideloadly. A free Apple ID signs for 7 days at a time.
- **Simulator:** add `-DTTYD_IOS_PLATFORM=SIMULATOR` (Apple silicon Macs, signed ad hoc), then `xcrun simctl install booted <build dir>/ttyd-us.app`. The first Simulator build compiles Dawn from source, so it takes a while.
- **The disc:** put your disc image in the app's folder in the Files app (On My iPhone/iPad > TTYD US) or Finder's device window, or choose it with the picker at first launch. RVZ is the smallest format.
- **Saves:** memory cards (`.gci`, interchangeable with Dolphin) and settings are in that folder's `Saves`.
- **Controls:** on-screen touch controls (you can move them, held either way up, iPad included) or any MFi, Xbox, PlayStation or Switch controller.
- Use the Ninja generator: the RELs are linked by custom build steps that `-G Xcode` doesn't handle reliably. Xcode can still debug the app (Debug > Attach to Process).

### Linux

Not yet built or run on a Linux machine: so far it is cross-compiled and linked for x86-64 and arm64 from Windows. Reports are welcome in this repository's issues.

Needs clang (the RELs are linked in with clang-only section pragmas), CMake 3.25+, Ninja, Python 3 and LLVM's tools. On Debian/Ubuntu:

```bash
sudo apt install clang lld llvm cmake ninja-build python3 pkg-config
git clone --recurse-submodules https://github.com/LLCOOLT/ttyd-port.git && cd ttyd-port
cmake -S . -B build-pc-us-aurora -G Ninja --toolchain cmake/toolchains/linux-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
build-pc-us-aurora/ttyd-us "path/to/your/disc.iso"
```

- Aurora builds SDL3 from source unless a system SDL3 (3.2+) is installed. That needs the usual X11, Wayland, audio and input development packages; [docs/PC_PORT.md](docs/PC_PORT.md#linux) lists them.
- Dawn (Vulkan or OpenGL) comes prebuilt for x86-64. On arm64, Aurora's disc reader is built from source, which needs Rust (`cargo`).
- With gcc instead of clang, add `-DTTYD_RELS_STATIC=OFF` (the RELs as loadable modules; untested).

Android builds, and more detail for every platform, are in [docs/PC_PORT.md](docs/PC_PORT.md).

## Testing

`tools/port/local_ci.py` runs the checks on one machine:
- every PC build configuration;
- unit tests and audits;
- the Linux cross-check and the Android build;
- every map with golden audio.

Scripted controller input (`TTYD_INPUT`), map warps (`TTYD_MAP`) and frame captures (`TTYD_SHOT`) drive the sweeps; [docs/PC_PORT.md](docs/PC_PORT.md#testing) lists them.

## Documentation

| Document | Contents |
|---|---|
| [docs/PC_PORT.md](docs/PC_PORT.md) | Building on each platform, running, options, settings, tests, audits, design notes |
| [docs/FRAME_INTERPOLATION.md](docs/FRAME_INTERPOLATION.md) | How 120 Hz and up works, and how its purity is checked |
| [docs/APPLE_BRIEF.md](docs/APPLE_BRIEF.md) | Bringing up macOS and iOS |
| [docs/MODDING.md](docs/MODDING.md) | Making and installing mods |
| [docs/MODDING_PLAN.md](docs/MODDING_PLAN.md) | The mod survey and the plan for mod support |

## Credits

- **The decompilation:** [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd).
- **The platform layer:** [Aurora](https://github.com/encounter/aurora) (the GameCube SDK on WebGPU/Dawn and SDL3), [Dawn](https://dawn.googlesource.com/dawn) and [SDL](https://www.libsdl.org/).
- **The settings page** follows the one in Dusklight, the Twilight Princess port on Aurora.

The original MIT licence and copyright notice are kept in [LICENSE](LICENSE).

Paper Mario and The Thousand-Year Door are trademarks of Nintendo. This project is not affiliated with or endorsed by Nintendo.
