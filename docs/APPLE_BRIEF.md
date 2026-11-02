# Brief: macOS and iOS bring-up (for a Claude Code session on a Mac)

You are continuing the native port of Paper Mario: The Thousand-Year Door
(JP G8MJ01, US G8ME01) on a Mac. Everything below was written and checked on
Windows, where the port is complete: all 279 maps per version boot, battles,
audio, memory-card saves, pause/resume, disc picker. The macOS and iOS code
(`.app` bundle, Mach-O static RELs, iOS app, touch controls) was written by
cross-compiling from Windows and has **never been built with Xcode or run on
Apple hardware**. Your job: make both build, then run, and prove it the way
the Windows build is proven.

## Read first
- `docs/PC_PORT.md`: building, running, environment switches, testing
  (`map_sweep.py`, golden audio hashes, input scripts), audits, the hazard
  classes table, the guarding rule, and the macOS and iOS sections.
- `tools/port/local_ci.py`: the CI, run locally (GitHub Actions is disabled
  on purpose: no budget; never re-enable or trigger it).

## Rules (the user's standing instructions)
- **Never commit or push disc data**: `orig/`, `data/`, `build*/`, `*.iso`,
  `*.dol`, `*.rel`, extracted files, captures (PNG) of the game. They are
  git-ignored; keep it that way.
- **Guarding rule**: any change to game or library code (`src/`, `rels/`,
  `include/`, `libs/`) is inside `#ifdef TARGET_PC` or compiles to identical
  GameCube code. If the GameCube matching build runs on this Mac
  (`python3 configure.py --version G8MJ01 && ninja`, then G8ME01: both must
  print SHA-1 OK), run it after such changes; if it does not, list every
  game/library file you changed in your report so it is checked on the PC.
- **Sweep, never fix serially**: when you find a bug, find its whole class
  (both versions, all platforms) and fix every instance at once; add a
  detector if one is cheap. Build with `ninja -k 0` so one build reports every
  failing file. `tools/port/diag_census.py BUILD_DIR...` lists every
  hazard-class warning across builds.
- **Few commits**: one or two milestone commits; squash follow-ups. End
  commit messages with `Co-Authored-By: Claude <noreply@anthropic.com>`.
- Work on a branch `port-apple` from `port-v2`. Ask the user before pushing it
  (then `git push -u origin port-apple`); never push other branches, never
  force-push.
- Aurora (`extern/aurora`) is upstream Aurora; the port's fixes to it are
  patch files in `port/patches/aurora/` that CMake applies at configure (the
  submodule's working tree shows them as local edits: expected). A new Aurora
  fix becomes a new patch file there (`git -C extern/aurora diff > ...` or
  `format-patch`), never a commit in the submodule pushed anywhere.
- Keep going without checking in; stop only for decisions that are the
  user's (signing identity, installs that need their password, pushing).

## Setup
1. Xcode 16 or newer (Aurora needs its libc++ `std::stop_token`), opened once
   to accept the license, with the iOS platform installed; then
   `sudo xcode-select -s /Applications/Xcode.app`.
2. `brew install cmake ninja llvm python protobuf gh` (Homebrew LLVM provides
   `llvm-objcopy`/`llvm-nm`/`llvm-ar` for the static RELs; Xcode has no
   objcopy). Rust: `curl https://sh.rustup.rs -sSf | sh`, then
   `rustup target add aarch64-apple-ios aarch64-apple-ios-sim` (nod has no
   prebuilt iOS package).
3. `gh auth login`, then
   `git clone --recurse-submodules -b port-v2 https://github.com/LLCOOLT/paper-cut`
   and `git switch -c port-apple`.
4. Game data (from the user's Windows PC, copied by them, e.g. USB or a
   network share; never through git): the folders `orig/G8MJ01`,
   `orig/G8ME01` (each: the disc image `*.iso`, plus `files/` and `sys/`) and
   `data/G8MJ01`, `data/G8ME01` (the extracted discs), into the same paths in
   the clone. Ask the user for them if they are missing.

## Scenario 1: macOS (arm64)
1. Configure and build all four: `build-pc-jp`, `build-pc-us` (headless) and
   `build-pc-jp-aurora`, `build-pc-us-aurora` (Aurora), e.g.
   `cmake -S . -B build-pc-jp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8MJ01 -DTTYD_BACKEND=headless`
   then `ninja -C build-pc-jp -k 0`. Expect errors: collect them all, group
   by class, fix each class everywhere. Known risk areas: names the game
   shares with Apple's libc (the renames at the end of
   `port/include/port_prefix.h` are the pattern), Mach-O static RELs
   (`rel_rename.py`, `section$start`/`section$end` in `CMakeLists.txt`), game
   threads (`port/src/common/os_thread_pc.c`, the host-thread baton branch:
   never run anywhere yet), the POSIX crash handler and watchdog
   (`port/src/common/crash_pc.c`), Apple's linker, unsigned enums.
2. `python3 tools/port/local_ci.py`: GameCube matching, the four builds, unit
   tests, audits, map sweeps with golden audio for both versions, card saves.
   - Golden audio hashes were recorded on Windows x64. The build disables
     fused multiply-add (`-ffp-contract=off`) so arm64 rounds the same way;
     if hashes still differ, find out why (Apple's libm, uninitialized
     memory, an actual bug) before accepting anything. Do not re-record the
     golden files to make a mismatch go away.
3. Aurora on screen: run `build-pc-jp-aurora/ttyd-jp.app` with no arguments
   (the disc dialog), choose the JP disc image, play to Rogueport with a
   controller or keyboard; open a disc image from Finder; minimize and restore
   (the game and its audio must pause and resume); check that sound plays.
   Then the capture sweep: `python3 tools/port/map_sweep.py
   build-pc-jp-aurora/ttyd-jp.app/Contents/MacOS/ttyd-jp data/G8MJ01 --disc
   orig/G8MJ01/*.iso --shots /tmp/shots_jp` and look through the contact
   sheets it writes for anything wrong (missing geometry, wrong colours,
   black frames); same for US.
4. `-DTTYD_RELS_STATIC=OFF` (REL modules) builds and boots too.

## Scenario 2: iOS
1. Simulator first: `cmake -S . -B build-ios-sim -G Ninja --toolchain
   cmake/toolchains/ios.cmake -DTTYD_VERSION=G8MJ01 -DTTYD_IOS_PLATFORM=SIMULATOR
   -DTTYD_LLVM_NM=$(brew --prefix llvm)/bin/llvm-nm ... -DPROTOC_EXECUTABLE=$(command -v protoc)`
   (see the iOS section of `docs/PC_PORT.md` for the exact options; the
   Simulator builds Dawn from source, which is slow and needs the host
   protoc), then `ninja -C build-ios-sim -k 0`.
2. Install and launch in a Simulator (`xcrun simctl install booted ...app`,
   `xcrun simctl launch --console booted <bundle id>`; the bundle ids are
   `io.github.ttyd-port.jp` and `.us`). Put the disc image in
   the app's Documents (`xcrun simctl get_app_container booted <bundle id>
   data`, then `Documents/`), relaunch: it must find it, boot to the title,
   reach a map; the touch controls must drive Mario; sending the app to the
   background must pause it and coming back resume it.
3. Device build (`-DTTYD_IOS_PLATFORM=OS`): build it and check the bundle
   (`plutil -lint`, `otool -l` shows one `__rd_`/`__rb_` section pair per
   REL). Running on a device needs the user's Apple ID signing: ask them.
4. US version the same way.

## Done means
- macOS: all four builds and `local_ci.py` pass, golden audio matches (or
  every difference is explained), the app plays and pauses correctly.
- iOS: Simulator build boots and plays with touch; device build links.
- `docs/PC_PORT.md` updated (status rows, anything learned), one or two
  commits on `port-apple`.
- A report for the Windows session: commit hashes, every bug class found and
  how it was fixed, the game/library files changed (for the GameCube matching
  check if it could not run here), what is verified and how, what is not.
