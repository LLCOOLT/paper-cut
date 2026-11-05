#!/bin/bash
# Builds brand_export (brand_export.cpp) with clang++ from a configured build's fetched Dear ImGui and
# SDL3 headers; nothing is linked from the game.
#   tools/brand_export/build.sh [build_dir] [out_exe]
# build_dir: a configured build (default build-pc-us-aurora), for _deps/imgui-src and SDL3's headers.
# Then: <out_exe> <out_dir> [icons|banners|all]   (run from the repo root, for the Fredoka font)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${1:-$ROOT/build-pc-us-aurora}
OUT=${2:-$ROOT/build-brand/brand_export.exe}
IMGUI=$B/_deps/imgui-src
SDL=$(dirname "$(dirname "$(find "$B/_deps" -path '*include/SDL3/SDL.h' | head -1)")")
[ -f "$IMGUI/imgui.cpp" ] && [ -d "$SDL" ] || { echo "no imgui-src or SDL3 headers under $B/_deps" >&2; exit 1; }
export PATH="/c/Program Files/LLVM/bin:$PATH"
SYS=()
if [ -z "$INCLUDE" ]; then
  # no developer prompt: the newest MSVC and Windows SDK on disk, as cmake/toolchains/windows-clang.cmake does
  shopt -s nullglob
  newest() { printf '%s\n' "$@" | sort -V | tail -1; }
  MSVC=$(newest /c/Program\ Files*/Microsoft\ Visual\ Studio/*/*/VC/Tools/MSVC/*)
  SDKI=$(newest /c/Program\ Files\ \(x86\)/Windows\ Kits/10/Include/10.*)
  SDKL="/c/Program Files (x86)/Windows Kits/10/Lib/$(basename "$SDKI")"
  SYS=(-idirafter "$MSVC/include" -idirafter "$SDKI/ucrt" -idirafter "$SDKI/um" -idirafter "$SDKI/shared"
       -L"$MSVC/lib/x64" -L"$SDKL/ucrt/x64" -L"$SDKL/um/x64")
fi
mkdir -p "$(dirname "$OUT")"
clang++ -std=c++20 -O2 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -w -fuse-ld=lld "${SYS[@]}" \
  -I"$ROOT/port/settings" -I"$ROOT/port/include" -I"$ROOT/extern/aurora/include" -I"$IMGUI" -I"$SDL" \
  "$ROOT/tools/brand_export/brand_export.cpp" "$ROOT/port/settings/launcher_brand.cpp" \
  "$ROOT/port/settings/launcher_stage.cpp" \
  "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" \
  -o "$OUT"
echo "built $OUT"
