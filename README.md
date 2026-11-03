<p align="center"><img src="https://raw.githubusercontent.com/LLCOOLT/paper-cut/main/docs/images/banner@2x.png" alt="Paper Cut" width="640"></p>

# Paper Cut

[![Discord](https://img.shields.io/badge/Discord-join-5865F2?logo=discord&logoColor=white)](https://discord.gg/qkSQuRPzcQ)

Paper Cut is a reverse-engineered reimplementation of the GameCube platform that the decompilation of Paper Mario: The Thousand-Year Door runs on. It builds that decompiled game code ([LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd)) for Windows, macOS, Linux, iOS and Android, with the GameCube's graphics, audio, input and storage provided by [Aurora](https://github.com/encounter/aurora). It is not an emulator.

> [!IMPORTANT]
> This repository does **not** contain or provide any copyrighted game assets. You are required to provide a supported copy of the game, dumped from a disc you own. Paper Cut reads it on your device and never downloads game content.

## Supported discs

| Release | Game ID |
|---|---|
| Japan | G8MJ01 |
| North America | G8ME01 |
| Europe | G8MP01 |

Disc images in .iso, .gcm or .rvz form work. To make one from your own disc, see Dolphin's guide to [ripping games](https://dolphin-emu.org/docs/guides/ripping-games/).

## Getting started

1. Download Paper Cut for your platform from [Releases](https://github.com/LLCOOLT/paper-cut/releases).
2. Start it and add your disc image to the launcher's library.
3. Pick it and play.

Settings are in the launcher and in the game's Start menu. Controllers, keyboard and mouse, and touch all work. Saves and settings are in `%APPDATA%\ttyd` on Windows, `~/Library/Application Support/ttyd` on macOS and `~/.local/share/ttyd` on Linux ([portable mode](docs/PORTABLE.md) keeps them beside the program).

What changed in this version is in its [release notes](https://github.com/LLCOOLT/paper-cut/releases).

## Building

Build steps are in [docs/PC_PORT.md](docs/PC_PORT.md).

## Reporting problems

Open an [issue](https://github.com/LLCOOLT/paper-cut/issues/new/choose) with your platform, the Paper Cut version and your disc's game ID. For help and discussion, join the [Discord](https://discord.gg/qkSQuRPzcQ).

## Contributing

Contributions must be your own work. Do not submit code or data taken from leaked or proprietary sources, or any game assets.

## Credits

- **The decompilation:** [LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd), started by NWPlayer123, with its contributors.
- **The platform layer:** [Aurora](https://github.com/encounter/aurora), [Dawn](https://dawn.googlesource.com/dawn) and [SDL](https://www.libsdl.org/).
- **The launcher's typeface:** [Fredoka](https://github.com/google/fonts/tree/main/ofl/fredoka), SIL Open Font License.
- **The settings page** follows the one in Dusklight, the Twilight Princess project on Aurora.

Paper Cut is licensed under MIT; see [LICENSE](LICENSE). Third-party licences are listed in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

Paper Mario and The Thousand-Year Door are trademarks of Nintendo. Paper Cut is an unofficial fan project and is not affiliated with, endorsed or sponsored by Nintendo. Please support the official releases.
