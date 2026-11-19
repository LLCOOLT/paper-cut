# Building Paper Cut

For players, Paper Cut is one app. Under the hood each release is its own build of the game code, because the three differ in their code and data: `ttyd-jp`, `ttyd-us` and `ttyd-eu`, configured with `-DTTYD_VERSION=G8MJ01`, `G8ME01` or `G8MP01`. Every build has the launcher. Build the versions you have discs for, side by side, and the launcher starts the right one for each disc. A build with a disc on its command line skips the launcher and plays it, if it's that build's version.

The game code is C, from the decompilation in [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd). The GameCube platform under it (graphics, audio, input, disc, memory card) is replaced, through [Aurora](https://github.com/encounter/aurora) (WebGPU/Dawn and SDL3).

## Windows

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

## macOS

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

## iOS and iPadOS

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

## Linux

The release's Linux builds need glibc 2.35 (Ubuntu 22.04) or newer; they are built on Ubuntu 22.04 with the C++ runtime linked in. ARM64 handhelds have their own PortMaster package ([docs/HANDHELDS.md](docs/HANDHELDS.md)).

The release comes as a zip, an AppImage (`Paper_Cut-<version>-x86_64.AppImage`, also as `papercut-<version>-linux-x64.AppImage`, the name releases before 0.5.0 had) and a Flatpak (`flatpak install --user papercut-<version>-linux-x64.flatpak`, which takes its runtime from Flathub). On a Steam Deck, the first start sets itself up for the Deck's screen: full screen, 16:10, the HUD at the edges and larger messages.

Needs clang (the RELs are linked in with clang-only section pragmas), CMake 3.25+, Ninja, Python 3 and LLVM's tools. On Debian/Ubuntu:

```bash
sudo apt install clang lld llvm cmake ninja-build python3 pkg-config
git clone --recurse-submodules https://github.com/LLCOOLT/paper-cut.git && cd paper-cut
cmake -S . -B build-pc-us-aurora -G Ninja --toolchain cmake/toolchains/linux-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora
ninja -C build-pc-us-aurora
build-pc-us-aurora/ttyd-us
```

- **SDL3:** Aurora builds SDL3 from source unless a system SDL3 (3.2+) is installed. That needs the usual X11, Wayland, audio and input development packages; [docs/TECHNICAL.md](docs/TECHNICAL.md#linux) lists them.
- **Dawn:** Dawn (Vulkan or OpenGL) comes prebuilt for x86-64. On arm64, Aurora's disc reader is built from source, which needs Rust (`cargo`).
- **gcc:** to build with gcc instead of clang, add `-DTTYD_RELS_STATIC=OFF`, which builds the RELs as loadable modules. This is untested.
- **The release:** the release's `papercut` script starts the launcher.

Android builds, and more detail for every platform, are in [docs/TECHNICAL.md](docs/TECHNICAL.md).
