// The mod manager's logic (port/settings/features/mods_install.cpp), apart
// from its drawing (port/settings/features/mods_manager.cpp: the launcher's
// Mods tab, the settings page's, the Start menu's): installs from a file or
// folder dropped on the window or picked, their progress and results, and
// the drop being dragged over the window. Only the player's own files: nothing
// is ever downloaded. The list itself is port/include/port/mods.h's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace modmgr {

// A file or folder to install, as given (a dropped path, a picked one, an
// Android content:// URI): queued, installed on a worker thread in order.
// What it can be: a mod folder or .ttydmod/.zip (mod.json), a language pack
// (a folder or zip with lang/pack.txt), a folder or zip of mods, a Dolphin
// texture pack's folder or zip (made a mod of its own), a .lua file (made a
// Lua mod), or a texture file (to the textures folder). A mod with the id
// of one installed replaces it; the one replaced goes to <user>/mods_old.
void install(const std::string& path);

// The system's file or folder dialog, its choices installed. The desktop has
// both; Android files only (content URIs); iOS neither (the Files app's mods
// folder instead).
bool canPickFiles();
bool canPickFolder();
void pickFiles();
void pickFolder();

struct Progress {
  bool busy = false;
  std::string item;     // the file or folder being installed (its name)
  uint64_t done = 0;    // bytes copied of it so far
  uint64_t total = 0;   // and in all (0: not known yet)
  int waiting = 0;      // more queued after it
};
Progress progress();

// One install's outcome, newest last: what was installed, updated, or why not.
struct Result {
  bool ok = false;
  std::string text; // English, with the mod's name in it: shown as it is
  uint64_t ticks = 0;
};
std::vector<Result> results();
void clearResults();

// Each UI frame: an install that finished reads the mods folder again (and the
// language packs, for a pack); true when the list changed.
bool poll();

// Files being dragged over the window now (SDL's drop events), and when a drop
// came in (SDL ticks, 0 never): the launcher shows its Mods tab then.
bool dragging();
uint64_t droppedAt();

// After a change from the list (on, off, the order, removed): the language
// packs read again when a pack is involved, so the menus follow at once.
void listChanged(unsigned kinds);

} // namespace modmgr
