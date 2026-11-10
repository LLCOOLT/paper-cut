#!/usr/bin/env python3
"""THIRD_PARTY.txt for a release package: the notices of what a PC build
embeds and links (docs/THIRD_PARTY.md), with their licence texts.

  third_party.py BUILD_DIR [--out FILE]

The texts come from the build's own copies (BUILD_DIR/_deps/*-src, Aurora,
libs/musyx, the font's OFL.txt); a component whose licence file is not in
the build (Dawn's and nod's prebuilt packages ship none) gets the standard
text of its licence with its authors' notice. release.py writes the result
into every desktop package; the phone apps name the same components on the
launcher's About tab (port/settings/launcher_ui.cpp).
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MIT = """Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

ZLIB = """This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
"""

BSD3 = """Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""

NCSA = """Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal with
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimers.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimers in the
      documentation and/or other materials provided with the distribution.

    * Neither the names of the LLVM Team, University of Illinois at
      Urbana-Champaign, nor the names of its contributors may be used to
      endorse or promote products derived from this Software without specific
      prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.
"""


def sgi_notice():
    """The SGI Free Software License B notice, as docs/THIRD_PARTY.md reproduces it."""
    text = open(os.path.join(ROOT, "docs", "THIRD_PARTY.md"), encoding="utf-8").read().replace("\r\n", "\n")
    m = re.search(r"```\n(SGI FREE SOFTWARE LICENSE B.*?)```", text, re.S)
    if not m:
        sys.exit("third_party: the SGI notice is missing from docs/THIRD_PARTY.md")
    return m.group(1)


# (name, what it is in Paper Cut, licence, files to take the text from (in
# order; "@" = the repository's root, else the build folder), fallback text,
# the build file that must exist for the component to be listed)
COMPONENTS = [
    ("Aurora", "the GameCube platform layer (graphics, input, audio, disc)", "MIT",
     ["@/extern/aurora/LICENSE"], None, None),
    ("SDL 3", "windows, input, audio", "zlib",
     ["_deps/sdl3_prebuilt-src/licenses/SDL3/LICENSE.txt", "_deps/sdl3-src/LICENSE.txt", "_deps/sdl-src/LICENSE.txt"],
     "Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>\n\n" + ZLIB, None),
    ("Dawn", "WebGPU, the graphics API Aurora draws with (its library includes Dawn's own third-party "
     "code, such as Tint, SPIRV-Tools and Abseil, whose notices are in Dawn's source: "
     "https://dawn.googlesource.com/dawn/+/refs/heads/main/third_party/)", "BSD 3-Clause",
     ["_deps/dawn-src/LICENSE", "_deps/dawn_prebuilt-src/LICENSE"],
     "Copyright 2017-2023 The Dawn & Tint Authors\n\n" + BSD3, None),
    ("DirectX Shader Compiler", "dxcompiler.dll and dxil.dll, from Dawn's Windows package "
     "(https://github.com/microsoft/DirectXShaderCompiler; dxil.dll under Microsoft's terms in that "
     "repository's LICENSE-MS.txt)", "University of Illinois/NCSA Open Source License",
     [], "Copyright (c) 2003-2015 University of Illinois at Urbana-Champaign.\n"
     "Copyright (c) Microsoft Corporation.\nAll rights reserved.\n\n" + NCSA, "dxcompiler.dll"),
    ("nod", "the disc image reader (https://github.com/encounter/nod)", "MIT (or Apache 2.0, at your option)",
     ["_deps/nod-src/LICENSE-MIT", "_deps/aurora_nod-src/LICENSE-MIT"],
     "Copyright (c) Luke Street\n\n" + MIT, None),
    ("Dear ImGui", "the settings page and launcher", "MIT", ["_deps/imgui-src/LICENSE.txt"], None, None),
    ("FreeType", "font rendering", "FreeType License (FTL)",
     ["_deps/freetype-src/docs/FTL.TXT"], None, None),
    ("libpng", "PNG images (screenshots, textures)", "libpng License", ["_deps/png-src/LICENSE"], None, None),
    ("zlib", "compression", "zlib", ["_deps/zlib-src/LICENSE.md", "_deps/zlib-src/LICENSE"], None, None),
    ("Zstandard", "compression", "BSD 3-Clause", ["_deps/zstd-src/LICENSE"], None, None),
    ("xxHash", "hashing", "BSD 2-Clause", ["_deps/xxhash-src/LICENSE"], None, None),
    ("{fmt}", "text formatting", "MIT", ["_deps/fmt-src/LICENSE"], None, None),
    ("Abseil", "C++ support library", "Apache License 2.0", ["_deps/abseil-cpp-src/LICENSE"], None, None),
    ("Tracy", "profiler client, linked by Aurora", "BSD 3-Clause", ["_deps/tracy-src/LICENSE"],
     None, None),
    ("SQLite", "Aurora's pipeline cache", "public domain", [],
     "The author disclaims copyright to this source code. In place of a legal notice, here is a blessing:\n\n"
     "    May you do good and not evil.\n    May you find forgiveness for yourself and forgive others.\n"
     "    May you share freely, never taking more than you give.\n", None),
    ("MusyX", "the game's sound library, from the MusyX decompilation by Axiomatic Data Laboratories",
     "MIT", ["@/libs/musyx/LICENSE"], None, None),
    ("SGI GLU tessellator", "in the European release's tst.rel (rels/tst/src/glu)",
     "SGI Free Software License B 2.0", [], None, None),
    ("Fredoka", "the launcher's typeface, by The Fredoka Project Authors (also in OFL.txt)",
     "SIL Open Font License 1.1", ["@/port/assets/fonts/OFL.txt"], None, None),
    ("Mbed TLS", "online play's secure connection to its relay and its rooms' sealing", "Apache License 2.0",
     ["_deps/mbedtls-src/LICENSE"], None, None),
    ("QR Code generator", "the QR code for sending saves to another device, by Project Nayuki; its source: "
     "https://github.com/nayuki/QR-Code-generator (v1.8.0, unchanged)", "MIT", ["@/libs/qrcodegen/LICENSE"], None, None),
    ("Lua", "the language of Lua mods, by Lua.org, PUC-Rio; its source: https://www.lua.org (5.4.8, unchanged)",
     "MIT", ["@/libs/lua/LICENSE"], None, None),
    ("stb_vorbis, dr_mp3, dr_flac", "the players' own music files (OGG, MP3, FLAC), by Sean Barrett and David Reid",
     "public domain, or MIT No Attribution", [],
     "Public domain where that's recognised; elsewhere MIT No Attribution (each file's end, "
     "libs/audio_decoders/): permission is granted to use, copy, modify and distribute them for any purpose, "
     "with or without fee, without conditions.\n", None),
]


def text(build):
    out = ["Paper Cut: third-party notices",
           "",
           "Paper Cut is under the MIT licence (LICENSE.txt). It embeds or links the",
           "components below, each under its own licence, whose text follows. The",
           "game's other libraries (the Dolphin SDK, the Metrowerks runtime, MSL and",
           "TRK) are reconstructions in Paper Cut's source; docs/THIRD_PARTY.md in",
           "the source repository says where each comes from. On Windows the package",
           "also carries Microsoft's Visual C++ runtime, redistributed under the",
           "Visual Studio licence terms.",
           ""]
    bodies = []
    for name, what, licence, files, fallback, needs in COMPONENTS:
        if needs and not glob.glob(os.path.join(build, needs)):
            continue
        body = None
        for f in files:
            path = os.path.join(ROOT, f[2:]) if f.startswith("@/") else os.path.join(build, f)
            if os.path.isfile(path):
                body = open(path, encoding="utf-8", errors="replace").read()
                break
        if body is None and name == "SGI GLU tessellator":
            body = sgi_notice()
        if body is None:
            body = fallback
        # a library the build didn't fetch is the system's own (Apple builds link the system's zlib)
        deps = {f.split("/")[1] for f in files if f.startswith("_deps/")}
        if body is None and deps and all(f.startswith("_deps/") for f in files) and \
                not any(os.path.isdir(os.path.join(build, "_deps", d)) for d in deps):
            print(f"third_party: {name} isn't in this build (the system's own); not listed", file=sys.stderr)
            continue
        if body is None:
            sys.exit(f"third_party: no licence text for {name} (looked for {', '.join(files)} in {build})")
        out.append(f"- {name}: {what}. {licence}.")
        bodies += ["", "=" * 78, f"{name} ({licence})", "=" * 78, "", body.replace("\r\n", "\n").strip("\n")]
    return "\n".join(out + bodies) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build")
    ap.add_argument("--out")
    a = ap.parse_args()
    t = text(a.build)
    if a.out:
        open(a.out, "w", encoding="utf-8", newline="\n").write(t)
    else:
        sys.stdout.write(t)
    return 0


if __name__ == "__main__":
    sys.exit(main())
