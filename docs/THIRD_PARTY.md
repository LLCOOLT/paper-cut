# Third-party code and tools

The game links libraries that Nintendo, Metrowerks and others wrote, and one REL carries a copy of SGI's GLU tessellator. This repository contains C reconstructions of that code (no original binaries) so that the whole game builds from source. This page lists where each part comes from and under which terms it is here. The rest of the repository is under the MIT licence in [LICENSE](../LICENSE).

## Libraries in `libs/`

| Folder | What it is | Where it comes from | Licence in this tree |
|---|---|---|---|
| `libs/dolsdk2004/` | Nintendo's Dolphin SDK libraries (OS, GX, DVD, VI, PAD, CARD, AR/ARQ, THP, ...), as built into the game | A decompilation of the archive libraries of the 2004-04-20 Dolphin SDK. It came into this tree with the upstream project's setup (2025) and was completed and extended here. | No licence file of its own |
| `libs/musyx/` | MusyX, the sound library (Factor 5) | The MusyX decompilation by Axiomatic Data Laboratories, extended here | MIT, `libs/musyx/LICENSE` |
| `libs/msl/` | Metrowerks Standard Library (C library and math) | Decompiled in this project (2026) | This repository's MIT licence |
| `libs/runtime/` | Metrowerks PowerPC EABI runtime (`__mem`, `__va_arg`, `runtime.c`, ...) | Decompiled in this project (2026) | This repository's MIT licence |
| `libs/trk/` | Metrowerks Target Resident Kernel, the debugger stub linked into the game | Decompiled in this project (2026) | This repository's MIT licence |
| `libs/audio_decoders/` | The players' own music files' decoders (`port/src/common/music_pc.c`): `stb_vorbis.c` (Ogg Vorbis, v1.22), `dr_mp3.h` (v0.7.4), `dr_flac.h` (v0.13.4) | Sean Barrett's [stb](https://github.com/nothings/stb) (commit 1ee679c) and David Reid's [dr_libs](https://github.com/mackron/dr_libs) (commits 51e61d3 and dfe8377), unchanged | Public domain, or MIT No Attribution (each file's end) |
| `libs/qrcodegen/` | The QR code the Saves tab shows to send saves to another device (`port/settings/features/save_transfer.cpp`): `qrcodegen.c`, `qrcodegen.h` (v1.8.0) | Project Nayuki's [QR Code generator library](https://github.com/nayuki/QR-Code-generator) (tag v1.8.0), unchanged | MIT, `libs/qrcodegen/LICENSE` |

The assembly in these folders is code that the original libraries shipped as assembly (for example paired-single math, cache and context routines, the TRK exception vectors). It is reproduced as assembly on purpose.

`src/sdk/` holds SDK demo-library code (`DEMOInit.c`, `DEMOPad.c`) and helpers that the game compiled as part of its own source; they are covered by the repository licence like the rest of `src/`.

## SGI GLU tessellator in `rels/tst/`

The developer test REL `tst.rel` (European release only) contains a copy of the GLU polygon tessellator (libtess, by Eric Veach) from the OpenGL Sample Implementation by Silicon Graphics, Inc. Its reconstruction is in `rels/tst/src/glu/` with headers in `rels/tst/include/glu/`.

It is under the SGI Free Software License B, version 2.0. Five of the eleven source files (`priorityq.c`, `priorityq-heap.c`, `render.c`, `sweep.c`, `tess.c`) carry the full notice at the top; `geom.c`, `mesh.c` and `tessmono.c` name the licence in their header; `dict.c`, `memalloc.c`, `normal.c` and the headers do not repeat it. These files cannot gain lines at the top without changing the build (the tessellator's assertions record source line numbers), so the notice for all of them is reproduced here:

```
SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice including the dates of first publication and
either this permission notice or a reference to
http://oss.sgi.com/projects/FreeB/
shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Except as contained in this notice, the name of Silicon Graphics, Inc.
shall not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization from
Silicon Graphics, Inc.
```

## Fredoka font in `port/assets/fonts/`

The launcher's typeface is [Fredoka](https://github.com/hafontia/Fredoka-One) by The Fredoka Project Authors, under the SIL Open Font License 1.1 (`port/assets/fonts/OFL.txt`). The files are static Bold (700) and Medium (500) instances made with fontTools' `varLib.instancer` from the variable `Fredoka[wdth,wght].ttf` in [google/fonts](https://github.com/google/fonts/tree/main/ofl/fredoka) (November 2026), embedded in the PC builds.

## Controller mappings in `port/assets/controllers/`

`gamecontrollerdb.txt` is the community [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) (commit 07af1d5, 2026-11-02). It holds SDL mappings for about 2,300 controller models, so that controllers SDL doesn't know itself work as gamepads. It's under the zlib licence (`port/assets/controllers/LICENSE`) and embedded in the PC builds.

## Libraries in the PC builds

Paper Cut's builds embed or link these (Aurora's dependencies, fetched when the build is configured). Every desktop package carries their notices and licence texts in `THIRD_PARTY.txt` (written by `tools/port/third_party.py` from the build's own copies) and the font's in `OFL.txt`; the phone apps name them on the launcher's About tab.

| Component | Used for | Licence |
|---|---|---|
| [Aurora](https://github.com/encounter/aurora) (`extern/aurora`, with `port/patches/aurora`) | the GameCube platform layer: graphics, input, audio, disc | MIT |
| [SDL 3](https://libsdl.org) | windows, input, audio | zlib |
| [Dawn](https://dawn.googlesource.com/dawn) | WebGPU (its library includes Dawn's own third-party code: Tint, SPIRV-Tools, Abseil, ...) | BSD 3-Clause |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) (Windows: `dxcompiler.dll`, `dxil.dll`, from Dawn's package) | Direct3D 12 shaders | University of Illinois/NCSA; `dxil.dll` under Microsoft's terms |
| [nod](https://github.com/encounter/nod) | reading disc images | MIT or Apache 2.0 |
| [Dear ImGui](https://github.com/ocornut/imgui), [FreeType](https://freetype.org) | the settings page and launcher; font rendering | MIT; FreeType License |
| libpng, zlib, Zstandard, xxHash | images, compression, hashing | libpng License; zlib; BSD 3-Clause; BSD 2-Clause |
| {fmt}, Abseil, Tracy, SQLite | Aurora's support code (formatting, containers, the profiler client, the pipeline cache) | MIT; Apache 2.0; BSD 3-Clause; public domain |
| MusyX (`libs/musyx/`), the SGI tessellator (`rels/tst/`), Fredoka (`port/assets/fonts/`), SDL_GameControllerDB (`port/assets/controllers/`) | above | MIT; SGI Free Software License B; SIL OFL 1.1; zlib |
| stb_vorbis, dr_mp3, dr_flac (`libs/audio_decoders/`) | the players' own music (OGG, MP3, FLAC) | public domain, or MIT No Attribution |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) v1.8.0 (`libs/qrcodegen/`, unchanged) | the QR code for sending saves to another device | MIT |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6.7 (fetched by CMake, pinned by hash) | online co-op's TLS to its relay (`port/src/common/online_ws_pc.c`) | Apache 2.0 |
| [libjuice](https://github.com/paullouisageneau/libjuice) 1.7.4 (fetched by CMake, pinned by hash, unchanged) | online co-op's direct UDP path, ICE (`port/src/common/online_ice_pc.c`) | MPL 2.0 |
| The relay's root certificates (`port/src/common/online_roots.inc`, from curl's copy of Mozilla's list, `tools/port/gen_online_roots.py`) | checking the relay is Cloudflare's | MPL 2.0 (Mozilla's list) |

On Windows the desktop package also carries Microsoft's Visual C++ runtime DLLs, redistributed under the Visual Studio licence terms.

## Build scripts in `tools/`

`tools/project.py`, `tools/download_tool.py`, `tools/decompctx.py`, `tools/transform_dep.py` and `tools/changes_fmt.py` come from [dtk-template](https://github.com/encounter/dtk-template), the decomp-toolkit project template. `tools/ninja_syntax.py` is from the [ninja](https://github.com/ninja-build/ninja) project (Apache License 2.0, notice in the file). The other scripts were written for this project; the port's, in `tools/port/`, are described in [PC_PORT.md](PC_PORT.md).

## Build-time tools

These are not in the repository. The first build downloads them into `build/`, at the versions pinned in `configure.py` (see the decompilation's [BUILDING.md](https://github.com/LLCOOLT/ttyd/blob/main/docs/BUILDING.md)); each comes under its own licence from its authors.

| Tool | Used for | Source |
|---|---|---|
| decomp-toolkit (`dtk`) | splitting the original files, linking RELs, the hash check | [encounter/decomp-toolkit](https://github.com/encounter/decomp-toolkit) |
| objdiff-cli | progress reports (`report.json`) | [encounter/objdiff](https://github.com/encounter/objdiff) |
| sjiswrap | converting UTF-8 sources to Shift-JIS for the compiler | [encounter/sjiswrap](https://github.com/encounter/sjiswrap) |
| Metrowerks CodeWarrior compilers and linker (GC/2.6 for the game and the link, GC/1.2.5n for the SDK, GC/1.3.2 for MusyX) | compiling and linking | the decomp.dev compiler archive (`files.decomp.dev`) |
| GNU binutils for PowerPC | assembling the original assembly routines | [encounter/gc-wii-binutils](https://github.com/encounter/gc-wii-binutils) (a build of [GNU binutils](https://www.gnu.org/software/binutils/)) |
| wibo (Linux) | running the Windows compilers | [decompals/wibo](https://github.com/decompals/wibo) |

`tools/ttydasm` (a Rust program) pulls its Rust crates from crates.io when you build it with Cargo.
