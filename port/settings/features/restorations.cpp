// Enhancements > Restorations (port/include/port/restorations.h): what the
// Japanese release had and the US and European releases changed, each its own
// setting, off by default. The game side is port/src/common/restorations_pc.c
// and the scripts that call it; the Japanese bad ending, Hooktail's frogs and
// the Trial Stew recipe are postgame_extra.cpp's rows, placed in this group by
// settings_layout.cpp.
//
// The chalk outline is only on the Japanese disc. Once this build's disc is
// open (disc_aurora.c, beside the text language), with the setting on:
// - with the player's own JP disc in the launcher's library, the Japanese
//   release's Rogueport room (m/gor_01/d) is read from it, kept in the user
//   folder (restorations/G8MJ01/m/gor_01/d) so later starts don't need the
//   disc swap, and used in place of this disc's (a host file,
//   port/include/port/mods.h);
// - without one, this disc's own room is used with the outline redrawn from
//   this disc (chalk_outline.cpp: Mario's outline traced from his own
//   picture, an art recipe, and the stains of the room's own texture, where
//   the Japanese release has them).
// The Restorations section says which is in use. Nothing of the game's is
// shipped or committed.
#include "chalk_outline.h"
#include "port/mods.h"
#include "port/restorations.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h"
#include "../file_util.h" // portfile::resolvePath
#include <SDL3/SDL.h>
#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kTab = "Enhancements";
constexpr const char* kRoom = "/m/gor_01/d";
// the Japanese release's file (the demos' differs): its size, and the size the file says it has
constexpr size_t kRoomSize = 1738386;

// in use: the Japanese release's own room; this disc's, redrawn (and why not the Japanese one); or
// neither (it couldn't be redrawn either); or not yet (turned on after the disc was opened)
enum ChalkState { kChalkOff, kChalkJapanese, kChalkRedrawn, kChalkRedrawnUnreadable, kChalkFailed, kChalkNotHere };
ChalkState sChalk = kChalkOff;

bool isJp() {
  return std::strcmp(PORT_GAME_ID, "G8MJ01") == 0;
}

// the launcher's disc images (library.txt in the user folder; as text_language.cpp reads it)
std::vector<std::string> library() {
  std::vector<std::string> paths;
  size_t size = 0;
  char* text = static_cast<char*>(SDL_LoadFile((std::string(portSettingsUserPath()) + "/library.txt").c_str(), &size));
  if (text == nullptr) {
    return paths;
  }
  std::string line;
  for (size_t i = 0; i <= size; i++) {
    if (i == size || text[i] == '\n') {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
      }
      if (!line.empty()) {
        paths.push_back(portfile::resolvePath(line)); // (iOS: kept relative to the app's container)
      }
      line.clear();
    } else {
      line += text[i];
    }
  }
  SDL_free(text);
  return paths;
}

bool roomOk(const std::vector<uint8_t>& b) {
  if (b.size() != kRoomSize) {
    return false;
  }
  const uint32_t said = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
  return said == b.size();
}

std::vector<uint8_t> readOpenDisc(const char* path) {
  std::vector<uint8_t> bytes;
  DVDFileInfo info;
  if (!DVDOpen(path, &info)) {
    return bytes;
  }
  const s32 length = static_cast<s32>(info.length);
  bytes.resize((length + 31) & ~31);
  if (DVDReadPrio(&info, bytes.data(), static_cast<s32>(bytes.size()), 0, 2) >= length) {
    bytes.resize(length);
  } else {
    bytes.clear();
  }
  DVDClose(&info);
  return bytes;
}

std::string cachePath() {
  return std::string(portSettingsUserPath()) + "/restorations/G8MJ01" + kRoom;
}

void statusSection(int) {
  if (isJp()) {
    portui::hint("The Japanese release is already this way: these are for the US and European ones.");
    return;
  }
  portui::hint("What the Japanese release had and the US and European releases changed, put back from your "
               "own discs. Text (Vivian's lines and the like) comes from fan text packs: see "
               "docs/DELOCALIZATION.md.");
  const char* chalk = nullptr;
  switch (sChalk) {
  case kChalkJapanese:
    chalk = "Chalk outline: in use, from your Japanese disc.";
    break;
  case kChalkRedrawn:
    chalk = "Chalk outline: in use, redrawn from this disc (Mario's outline, traced from his own picture). With "
            "the Japanese disc (G8MJ01) in the launcher's library, the Japanese release's own is used from the next "
            "start.";
    break;
  case kChalkRedrawnUnreadable:
    chalk = "Chalk outline: in use, redrawn from this disc: the Japanese disc in the library couldn't be read, or "
            "isn't the retail release.";
    break;
  case kChalkFailed:
    chalk = "Chalk outline: couldn't be redrawn from this disc (see the log); it needs the Japanese disc (G8MJ01) "
            "in the launcher's library.";
    break;
  case kChalkNotHere:
    chalk = "Chalk outline: from the next start.";
    break;
  default:
    break;
  }
  int* v = portSettingsDeclare(portRestoreKey(PORT_RESTORE_CHALK_OUTLINE), 0, 0, 1);
  if (v != nullptr && *v != 0 && sChalk == kChalkOff) {
    chalk = "Chalk outline: from the next start.";
  }
  if (chalk != nullptr) {
    portui::hint(chalk);
  }
}

struct Register {
  Register() {
    for (int i = 0; i < PORT_RESTORE_COUNT; i++) {
      portSettingsDeclare(portRestoreKey(i), 0, 0, 1);
    }
    portSettingsAddSection(kTab, "Restorations", nullptr, 0, statusSection,
                           "restorations japanese release censored changed localization delocalization");
    portSettingsAddToggle(kTab, "Rogueport's chalk outline",
                          "The chalk outline and the marks on a Rogueport house's floor, under the flies, as in the "
                          "Japanese release. With your own Japanese disc in the launcher's library, the Japanese "
                          "release's own, read from it once; without it, redrawn from this disc: Mario's outline, "
                          "traced from his own picture.",
                          PORT_SETTING_NEXT_START, portRestoreKey(PORT_RESTORE_CHALK_OUTLINE),
                          "chalk outline rogueport body flies japanese restoration");
    portSettingsAddToggle(kTab, "The Parlor Boo's bunny ears",
                          "The Pianta Parlor's Boo, and her turn in the credits, in her Japanese bunny ears instead of "
                          "cat ears. From your disc's own Japanese model.",
                          0, portRestoreKey(PORT_RESTORE_BUNNY_BOO),
                          "bunny boo pianta parlor ears japanese restoration");
    portSettingsAddToggle(kTab, "Japanese screen flashes",
                          "The strobing flashes the US release cut down: the Great Tree's time bomb, the darkness "
                          "falling, the Shadow Queen. Warning: rapid flashing light.",
                          0, portRestoreKey(PORT_RESTORE_JP_FLASHES),
                          "flash flashes strobe photosensitive japanese restoration");
    portSettingsAddToggle(kTab, "1000 X-Nauts in the prologue",
                          "The prologue's crowd of X-Nauts at its Japanese size: 1000, not 400, and more smoke.", 0,
                          portRestoreKey(PORT_RESTORE_XNAUT_CROWD), "x-naut crowd 1000 prologue japanese restoration");
    for (int i = 0; i < PORT_RESTORE_COUNT; i++) {
      portSettingsSetShown(portRestoreKey(i), [] { return isJp() ? 0 : 1; }); // the JP release has them already
    }
  }
} sRegister;

} // namespace

extern "C" int portRestorationsDiscOpened(const char* path) {
  if (isJp() || !portRestoreOn(PORT_RESTORE_CHALK_OUTLINE)) {
    sChalk = kChalkOff;
    return 1;
  }
  std::vector<uint8_t> room;
  size_t size = 0;
  if (void* cached = SDL_LoadFile(cachePath().c_str(), &size)) {
    room.assign(static_cast<uint8_t*>(cached), static_cast<uint8_t*>(cached) + size);
    SDL_free(cached);
  }
  if (!roomOk(room)) {
    room.clear();
    bool sawJp = false;
    aurora_dvd_close();
    for (const std::string& disc : library()) {
      if (!aurora_dvd_open(disc.c_str())) {
        continue;
      }
      if (std::memcmp(DVDGetCurrentDiskID()->gameName, "G8MJ", 4) == 0) {
        sawJp = true;
        room = readOpenDisc(kRoom);
      }
      aurora_dvd_close();
      if (roomOk(room)) {
        break;
      }
      room.clear();
    }
    if (!aurora_dvd_open(path)) { // the disc that was open a moment ago
      std::fprintf(stderr, "restorations: could not reopen %s\n", path);
      return 0;
    }
    if (room.empty()) {
      // no Japanese disc to read it from: this disc's own room, the outline redrawn from this disc
      std::string why;
      std::vector<uint8_t> redrawn;
      if (portChalkOutlineRoom(readOpenDisc(kRoom), redrawn, why)) {
        portModsSetHostFile(kRoom, redrawn.data(), redrawn.size());
        sChalk = sawJp ? kChalkRedrawnUnreadable : kChalkRedrawn;
        std::fprintf(stderr, "restorations: the chalk outline, redrawn from this disc (%s)\n",
                     sawJp ? "the Japanese disc in the library couldn't be read" : "no Japanese disc in the library");
      } else {
        sChalk = kChalkFailed;
        std::fprintf(stderr, "restorations: the chalk outline couldn't be redrawn from this disc (%s), and %s\n",
                     why.c_str(),
                     sawJp ? "the Japanese disc in the library couldn't be read" : "there is no Japanese disc in the library");
      }
      return 1;
    }
    const std::string cache = cachePath();
    SDL_CreateDirectory(cache.substr(0, cache.rfind('/')).c_str());
    if (!SDL_SaveFile(cache.c_str(), room.data(), room.size())) {
      std::fprintf(stderr, "restorations: could not keep %s\n", cache.c_str());
    }
  }
  portModsSetHostFile(kRoom, room.data(), room.size());
  sChalk = kChalkJapanese;
  std::fprintf(stderr, "restorations: the chalk outline, from the Japanese release\n");
  return 1;
}
