// Saves > Portable mode (port/src/aurora/portable_aurora.c, docs/PORTABLE.md):
// where the settings and saves are, and a copy made portable from here: the
// user folder copied to User beside the program, portable.txt written there,
// then the game started again to use it. Leaving it takes portable.txt away;
// the folder beside the program is kept.
#include "port/restart.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <imgui.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "../file_util.h"

extern "C" const char* portPortableDir(void); // portable_aurora.c
extern "C" int portPortableActive(void);

namespace {

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE) && !defined(__EMSCRIPTEN__)

// (links and junctions are left out, never followed; no deeper than portfile::kMaxTreeDepth)
bool copyTree(const std::string& from, const std::string& to, int depth = 0) {
  SDL_PathInfo info;
  if (portfile::isLink(from) || !SDL_GetPathInfo(from.c_str(), &info)) {
    return true;
  }
  if (info.type == SDL_PATHTYPE_FILE) {
    return SDL_CopyFile(from.c_str(), to.c_str());
  }
  if (depth > portfile::kMaxTreeDepth) {
    return false;
  }
  SDL_CreateDirectory(to.c_str());
  int count = 0;
  char** names = SDL_GlobDirectory(from.c_str(), "*", 0, &count);
  bool ok = true;
  for (int i = 0; names != nullptr && i < count; i++) {
    const size_t len = std::strlen(names[i]);
    // (a .tmp is a file being written whole as the game goes on meanwhile, renamed over its own once
    // done: never one to copy, the finished file is)
    if (len >= 4 && SDL_strcasecmp(names[i] + len - 4, ".tmp") == 0) {
      continue;
    }
    if (std::strchr(names[i], '/') == nullptr && std::strchr(names[i], '\\') == nullptr) {
      ok = copyTree(from + "/" + names[i], to + "/" + names[i], depth + 1) && ok;
    }
  }
  SDL_free(names);
  return ok;
}

// The copy runs on a thread of its own (a big user folder takes minutes), which may still be at it as
// the program quits: what it shares is left allocated at exit, never destroyed under it
std::mutex& sLock = *new std::mutex;
std::string& sStatus = *new std::string; // (sLock)
std::atomic<bool> sCopying{false}; // (trivially destroyed)

std::string status() {
  std::lock_guard lock(sLock);
  return sStatus;
}

void setStatus(const std::string& text) {
  std::lock_guard lock(sLock);
  sStatus = text;
}

// (saves aren't held while it copies: nothing can hold the game's card writes, which would stall or drop
// a save for minutes, and portSettingsHoldSaves is a plain on/off that online play and replays set, which a
// copy's "off" would undo. Files written whole through a .tmp renamed over them (file_util.h writeAll) are
// skipped while being written, so each is copied as one whole version, from before a save made meanwhile
// or after it)
void makePortable(std::string user, std::string dir, std::string marker) {
  const bool ok = copyTree(user, dir + "User");
  SDL_IOStream* f = ok ? SDL_IOFromFile(marker.c_str(), "wb") : nullptr;
  if (f != nullptr) {
    static const char kText[] = "Paper Cut keeps its settings and saves in User beside this file.\n";
    const bool written = SDL_WriteIO(f, kText, sizeof(kText) - 1) == sizeof(kText) - 1;
    if (SDL_CloseIO(f) && written) {
      setStatus(portTr("Done: it applies from the next start."));
    } else {
      SDL_RemovePath(marker.c_str());
      setStatus(std::string(portTr("Could not write it: ")) + SDL_GetError());
    }
  } else {
    setStatus(std::string(portTr("Could not write it: ")) + SDL_GetError());
  }
  sCopying = false;
}

void section(int) {
  const std::string dir = portPortableDir(), marker = dir + "portable.txt";
  ImGui::TextWrapped(portTr("Settings and saves are in %s"), portSettingsUserPath());
  if (dir.empty()) {
    return;
  }
  static bool sConfirm;
  if (portPortableActive()) {
    ImGui::TextWrapped("%s", portTr("This copy is portable: portable.txt beside the program keeps its settings, "
                                    "saves and mods in its own User folder."));
    if (ImGui::Button(portTrLabel(sConfirm ? "Leave portable mode (User stays beside the program)##portable"
                                           : "Leave portable mode##portable"))) {
      if (!sConfirm) {
        sConfirm = true;
      } else {
        sConfirm = false;
        setStatus(SDL_RemovePath(marker.c_str()) ? portTr("From the next start, the system's folder is used again.")
                                                 : std::string(portTr("Could not remove portable.txt: ")) + SDL_GetError());
      }
    }
  } else {
    ImGui::TextWrapped("%s", portTr("Portable mode keeps everything beside the program (a USB stick, a folder of "
                                    "its own): the settings, saves and mods are copied to User there."));
    ImGui::BeginDisabled(sCopying);
    if (ImGui::Button(portTrLabel(sConfirm ? "Copy everything and make this copy portable##portable"
                                           : "Make this copy portable##portable"))) {
      if (!sConfirm) {
        sConfirm = true;
      } else {
        sConfirm = false;
        portSettingsSave(); // the settings as they are now, then copied
        sCopying = true;
        setStatus(portTr("Copying..."));
        std::thread(makePortable, std::string(portSettingsUserPath()), dir, marker).detach();
      }
    }
    ImGui::EndDisabled();
  }
  const std::string now = status();
  if (!now.empty()) {
    ImGui::TextWrapped("%s", now.c_str());
    if (!sCopying) {
      portRestartButton();
    }
  }
}

const bool sAdded = [] {
  portSettingsAddSection("Saves", "Portable mode", nullptr, 0, section,
                         "portable usb stick folder beside the program user data where are my saves settings");
  return true;
}();

#endif

} // namespace
