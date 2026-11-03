// The "Saves" tab: dated backups of each memory card file (.gci) kept as the
// game writes it, restored from here; and a save copied to another release's
// card in that release's layout (port/include/port/save_convert.h). Files:
//   <user>/<JAP|USA|EUR>/Card A/<name>.gci          the cards (Aurora's)
//   <user>/Backups/<JAP|USA|EUR>/<name> <date>.gci  the backups, <date> the
//                                                   saved state's own time
#include "port/cardsave_pc.h"
#include "port/save_convert.h"
#include "port/settings.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kReleases[] = {'J', 'E', 'P'};
constexpr const char* kReleaseNames[] = {"Japan (JP)", "North America (US)", "Europe (EU)"};

int* sKeep; // backups kept per file (0: none)

const char* regionDir(char release) { return release == 'J' ? "JAP" : release == 'P' ? "EUR" : "USA"; }

int releaseIndex(char release) {
  for (int i = 0; i < 3; i++) {
    if (kReleases[i] == release) {
      return i;
    }
  }
  return 1;
}

std::string cardDir(char release) {
  return std::string(portSettingsUserPath()) + "/" + regionDir(release) + "/Card A";
}

std::string backupDir(char release) {
  return std::string(portSettingsUserPath()) + "/Backups/" + regionDir(release);
}

bool isFile(const std::string& path, SDL_Time* modified = nullptr) {
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
    return false;
  }
  if (modified != nullptr) {
    *modified = info.modify_time;
  }
  return true;
}

std::vector<std::string> glob(const std::string& dir, const std::string& pattern) {
  std::vector<std::string> names;
  int count = 0;
  char** found = SDL_GlobDirectory(dir.c_str(), pattern.c_str(), 0, &count);
  for (int i = 0; found != nullptr && i < count; i++) {
    names.push_back(found[i]);
  }
  SDL_free(found);
  std::sort(names.begin(), names.end()); // the dates sort as text: oldest first
  return names;
}

std::string stem(const std::string& name) { return name.substr(0, name.size() - 4); }

// Copies the card file name (in release's Card A) to the backups, named by
// its own time (one copy per state), then keeps the newest *sKeep.
void backUp(char release, const std::string& name) {
  SDL_Time modified;
  if (!isFile(cardDir(release) + "/" + name, &modified)) {
    return;
  }
  SDL_DateTime dt;
  SDL_TimeToDateTime(modified, &dt, true);
  char date[40];
  std::snprintf(date, sizeof(date), "%04d-%02d-%02d %02d.%02d.%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                dt.second);
  const std::string dir = backupDir(release);
  const std::string to = dir + "/" + stem(name) + " " + date + ".gci";
  SDL_CreateDirectory(dir.c_str());
  if (!isFile(to) && !SDL_CopyFile((cardDir(release) + "/" + name).c_str(), to.c_str())) {
    std::fprintf(stderr, "saves: no backup of %s: %s\n", name.c_str(), SDL_GetError());
    return;
  }
  std::vector<std::string> kept = glob(dir, stem(name) + " *.gci");
  const size_t keep = static_cast<size_t>(std::max(*sKeep, 1));
  for (size_t i = 0; i + keep < kept.size(); i++) {
    SDL_RemovePath((dir + "/" + kept[i]).c_str());
  }
}

// gPortCardWillWrite: before the game's first write of a save (one save is a
// few writes; the card's state before them is what is kept)
void willWrite() {
  static Uint64 sLast;
  const Uint64 now = SDL_GetTicks();
  if (*sKeep == 0 || (sLast != 0 && now - sLast < 20000)) {
    return;
  }
  sLast = now;
  const char release = PORT_GAME_ID[3];
  for (const std::string& name : glob(cardDir(release), "*.gci")) {
    backUp(release, name);
  }
}

std::vector<uint8_t> readAll(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::vector<uint8_t> bytes;
  if (data != nullptr) {
    bytes.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    SDL_free(data);
  }
  return bytes;
}

// the backup's date from its name ("<stem> <date>.gci")
std::string dateOf(const std::string& backup) {
  const size_t at = backup.size() >= 23 ? backup.size() - 23 : 0;
  std::string date = backup.substr(at, 19);
  std::replace(date.begin(), date.end(), '.', ':');
  return date;
}

std::string sStatus;

void backupsSection(int inGame) {
  static int sRelease = -1;
  static std::string sConfirm; // the backup whose Restore was pressed once
  if (sRelease < 0) {
    sRelease = releaseIndex(PORT_GAME_ID[3]);
  }
  ImGui::SeparatorText("Backups");
  ImGui::Combo("Card", &sRelease, kReleaseNames, 3);
  const char release = kReleases[sRelease];
  const std::vector<std::string> backups = glob(backupDir(release), "*.gci");
  if (backups.empty()) {
    ImGui::TextDisabled("No backups yet: one is kept each time the game saves.");
  }
  const bool running = inGame && release == PORT_GAME_ID[3];
  for (size_t i = backups.size(); i-- > 0;) { // newest first
    const std::string& b = backups[i];
    ImGui::PushID(b.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(dateOf(b).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    if (ImGui::SmallButton(sConfirm == b ? "Replace the save with this one" : "Restore")) {
      if (sConfirm != b) {
        sConfirm = b;
      } else {
        const std::string name = b.substr(0, b.size() - 24) + ".gci"; // "<stem>.gci"
        SDL_CreateDirectory(cardDir(release).c_str());
        backUp(release, name); // the save as it is now, so this can be undone
        sStatus = SDL_CopyFile((backupDir(release) + "/" + b).c_str(), (cardDir(release) + "/" + name).c_str())
                      ? "Restored the save of " + dateOf(b) + "."
                      : std::string("Could not restore it: ") + SDL_GetError();
        sConfirm.clear();
      }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (running && !backups.empty()) {
    ImGui::TextDisabled("Restore this card's saves from the launcher, before the game starts.");
  }
}

void copySection(int inGame) {
  static int sFrom = -1, sTo = -1;
  static bool sConfirm;
  if (sFrom < 0) {
    sFrom = releaseIndex(PORT_GAME_ID[3]);
    sTo = sFrom == 0 ? 1 : 0;
  }
  ImGui::SeparatorText("Copy a save to another release");
  ImGui::TextWrapped("The Japanese release keeps its Pianta Parlor records differently; the copy is converted. "
                     "Names typed in one release's letters may look different in another.");
  if (ImGui::Combo("From", &sFrom, kReleaseNames, 3) | ImGui::Combo("To", &sTo, kReleaseNames, 3)) {
    sConfirm = false;
  }
  const char from = kReleases[sFrom], to = kReleases[sTo];
  char fromName[64], toName[64];
  portSaveFileName(from, fromName, sizeof(fromName));
  portSaveFileName(to, toName, sizeof(toName));
  const std::string src = cardDir(from) + "/" + fromName, dst = cardDir(to) + "/" + toName;
  const bool have = isFile(src), replaces = isFile(dst);
  const bool running = inGame && to == PORT_GAME_ID[3];
  if (!have) {
    ImGui::TextDisabled("That card has no save of this game.");
  }
  ImGui::BeginDisabled(!have || from == to || running);
  const char* label = sConfirm ? "Replace its save (a backup is kept)" : "Copy";
  if (ImGui::Button(label)) {
    if (replaces && !sConfirm) {
      sConfirm = true;
    } else {
      sConfirm = false;
      std::vector<uint8_t> in = readAll(src), out(in.size());
      const int slots = in.empty() ? -1 : portSaveConvert(in.data(), in.size(), to, out.data());
      if (slots < 0) {
        sStatus = "That file isn't a save of this game.";
      } else {
        SDL_CreateDirectory(cardDir(to).c_str());
        backUp(to, toName);
        SDL_IOStream* f = SDL_IOFromFile((dst + ".tmp").c_str(), "wb");
        const bool ok = f != nullptr && SDL_WriteIO(f, out.data(), out.size()) == out.size();
        if (f != nullptr) {
          SDL_CloseIO(f);
        }
        if (ok && SDL_RenamePath((dst + ".tmp").c_str(), dst.c_str())) {
          char text[160];
          std::snprintf(text, sizeof(text), "Copied to %s (%d of its files and their backups).",
                        kReleaseNames[sTo], slots);
          sStatus = text;
        } else {
          SDL_RemovePath((dst + ".tmp").c_str());
          sStatus = std::string("Could not write it: ") + SDL_GetError();
        }
      }
    }
  }
  ImGui::EndDisabled();
  if (running) {
    ImGui::TextDisabled("Copy to the card of the game that is running from the launcher, before it starts.");
  } else if (sConfirm) {
    ImGui::TextDisabled("%s already has a save.", kReleaseNames[sTo]);
  }
  if (!sStatus.empty()) {
    ImGui::TextUnformatted(sStatus.c_str());
  }
}

struct SavesSettings {
  SavesSettings() {
    sKeep = portSettingsDeclare("save_backups", 5, 0, 20);
    gPortCardWillWrite = willWrite;
    portSettingsAddSlider("Saves", "Backups kept", "Dated copies of each save file, made when the game saves. 0: none.",
                          0, "save_backups", 0, 20, "%d", "backup restore gci memory card");
    portSettingsAddSection("Saves", "Restore a backup", nullptr, 0, backupsSection,
                           "backup restore undo gci memory card");
    portSettingsAddSection("Saves", "Copy a save to another release", nullptr, 0, copySection,
                           "convert transfer japan jp us eu pal region gci memory card");
  }
} sSaves;

} // namespace
