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
- macOS, iOS, Linux and Android builds are covered in [docs/PC_PORT.md](docs/PC_PORT.md).

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

## Credits

- **The decompilation:** [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd). It started as a fork of [doldecomp/ttyd](https://github.com/doldecomp/ttyd), which grew out of [NWPlayer123](https://github.com/NWPlayer123)'s original G8MJ01 decomp-toolkit setup, and its US version draws on [Silver136](https://github.com/Silver-136/ttyd_dtk_Silver136)'s work.
- **The platform layer:** [Aurora](https://github.com/encounter/aurora) (the GameCube SDK on WebGPU/Dawn and SDL3), [Dawn](https://dawn.googlesource.com/dawn) and [SDL](https://www.libsdl.org/).
- **The settings page** follows the one in Dusklight, the Twilight Princess port on Aurora.

The original MIT licence and copyright notice are kept in [LICENSE](LICENSE).

This is an **independent project, not affiliated with doldecomp**. The port, like the decompilation it is built from, was written with the help of AI tools (Anthropic's Claude). Please use this repository's issues for questions about it, not the doldecomp community.

Paper Mario and The Thousand-Year Door are trademarks of Nintendo. This project is not affiliated with or endorsed by Nintendo.
