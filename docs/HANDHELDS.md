# Paper Cut on Linux handhelds (PortMaster)

Paper Cut runs on ARM64 Linux handhelds through [PortMaster](https://portmaster.games/): the
Anbernic RG35XX, RG40XX, RG CubeXX and RG34XX families (Allwinner H700), the RG353 family and
other RK3566 devices, and similar machines, on Knulli, muOS, ROCKNIX and the makers' own
firmwares. It plays from your own disc image; nothing from the game comes with it.

> **Status: experimental.** The handheld build is new and has not yet been played on a real
> device. What has and has not been checked is listed at the end of this page.

## Installing

1. Install PortMaster on the device if your firmware doesn't have it.
2. Unzip `papercut-<version>-portmaster.zip` into the firmware's ports folder (for example
   `/roms/ports/` on Knulli and ROCKNIX, `/mnt/mmc/ports/` or `/mnt/sdcard/ports/` on muOS), so
   that `Paper Cut.sh` and the `papercut/` folder sit side by side.
3. Copy your own disc image (`.iso`, `.gcm`, `.ciso`, `.gcz`, `.rvz`, `.wia`, `.nfs` or `.tgc`)
   into `ports/papercut/games/`.
4. Refresh the game list and start **Paper Cut** from Ports. The launcher lists the discs in
   `games/`: pick one and press Play.

The US build is always in the package. A Japanese or European disc needs its own build
(`ttyd-jp` or `ttyd-eu`) beside it in `papercut/`.

## Controls

Paper Cut reads the device's controller directly (SDL3, with PortMaster's mapping for the
device), so gptokeyb isn't used.

| Button | In Paper Cut |
| --- | --- |
| A, B, X, Y | the GameCube controller's |
| L1, R1 / L2, R2 | Z, L and R (change them under Settings > Controls) |
| Left stick, D-pad | the stick and the D-pad |
| Right stick | the C-stick |
| **Select** | Paper Cut's settings (open, close) |
| **Select + Start** | quit; where you are is kept, and Continue in the launcher comes back to it |
| Power key | saves where you are at once, before the device sleeps or turns off |

Quick resume also writes where you are every 10 seconds on a handheld, so a firmware that
ends the game without warning loses very little.

## What a handheld gets

Paper Cut checks for a handheld (`TTYD_HANDHELD=1` from the launch script, or a handheld chip in
the device tree). On one it:

- draws with **OpenGL ES** (Dawn's compatibility mode). Without X11 or Wayland, as on most
  firmwares, SDL3 draws to the screen itself through KMSDRM, and each frame is read back from
  the GPU and shown through SDL's own OpenGL ES renderer.
- uses **smaller graphics buffers**: about 210 MiB rather than 714 MiB of staging and frame
  buffers. Any computer with less than 6 GB gets these too.
- sets the **Handheld preset** at the first start: 640x480 inside (the screen's own lines),
  the screen's own shape (4:3 on 640x480; 3:2 at 720x480 and wider screens at their own aspect),
  Battery saver always on (drawn at 30 frames a second while the game still runs at 60),
  and anti-aliasing, supersampling, bloom, post filters, anisotropic filtering, texture upscaling,
  the paper effects, blurred side bars and rewind off. All of it can be changed in the settings
  afterwards. For 60 frames a second, set Battery saver to Off.

The saves, settings and caches are kept in `ports/papercut/conf` and `ports/papercut/cache`, and
the last run's log in `ports/papercut/log.txt`.

### Switches

| Variable | What it does |
| --- | --- |
| `TTYD_HANDHELD=0` / `1` | not a handheld / a handheld, whatever the board |
| `TTYD_LOW_MEMORY=0` / `1` | the normal / the smaller graphics buffers |
| `TTYD_GPU=vulkan` | try Vulkan first (Panfrost's PanVK, a vendor driver) |
| `AURORA_PRESENT=copy` / `surface` | always / never present through the readback and SDL's renderer |

## Building it

The handheld build is cross-compiled with clang and lld against a Debian sysroot, on Windows,
macOS or Linux:

```sh
python tools/port/arm64_sysroot.py
cmake -S . -B build-arm64-us -G Ninja --toolchain cmake/toolchains/linux-arm64.cmake \
      -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora -DCMAKE_BUILD_TYPE=Release
ninja -C build-arm64-us ttyd
python tools/port/portmaster_package.py 0.5.0 --build us=build-arm64-us
```

The sysroot holds Debian 11's glibc 2.31, so the executable starts on any firmware with glibc
2.31 or newer. GCC 14's C++ library and runtime come from Debian 13 and are linked in, as are
SDL3 (built from source with its KMSDRM, Wayland, X11, ALSA and PulseAudio drivers, which load
their libraries at run time), Dawn and nod. Aurora's zlib is the one shared library beside the
executable (`papercut/libs/libz.so.1`). On a machine without pkg-config or wayland-scanner
(Windows), `tools/port/pkg_config.py` and `tools/port/wayland_scanner.py` stand in for them.
`release.py VERSION --targets portmaster` builds all three versions and writes the package.

## Checked so far, and not yet

Checked, in an ARM64 Debian 11 container (glibc 2.31) without a GPU:

- the build starts. It needs nothing newer than glibc 2.30's symbols.
- Dawn's OpenGL ES backend runs on Mesa's software llvmpipe (OpenGL ES 3.2) through SDL's own
  EGL display. Frames go through the readback to SDL's OpenGL ES renderer (SDL's offscreen
  driver standing in for KMSDRM), and the launcher is drawn.
- the Handheld preset is set at the first start, and the game exits cleanly.
- memory: about 420 MB at most at the launcher, llvmpipe's own buffers included.

Not yet checked, as no handheld has run it yet:

- a game in play: the container could not read a disc image, so there is no in-game memory or
  frame time yet.

- the frame rate on a Mali-G31 (H700) or Mali-G52 (RK3566). Expect 30 frames a second to be
  the realistic goal on the H700.
- the GPU drivers: Panfrost (Mesa) on ROCKNIX and Knulli, and the Mali blobs on muOS and the
  stock firmwares. Dawn needs OpenGL ES 3.1 with `EGL_EXT_create_context_robustness` and
  storage buffers in vertex shaders.
- KMSDRM presentation through SDL's OpenGL ES renderer, the power key's event, and PortMaster's
  launch script on each firmware.
- the smaller buffers: a very crowded scene could need more than they hold.
