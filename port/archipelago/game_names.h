// A game name's address, on every backend: a function or variable the
// executable exports (a REL's with its prefix, rel_<rel>__name), or a file's
// static ("file::name" where the name alone is ambiguous), through the
// pointer the build exports for it (CMakeLists.txt's ttyd_static_exports,
// listed in the generated statics_index.c). Mods' scripts link by it
// (port/mods/mods.cpp); the randomizers write their placements by it
// (ttyd_ap.cpp, open_world.cpp), headless too.
#pragma once

#include <string>

bool portGameName(const std::string& text, void*& out, std::string& why);
