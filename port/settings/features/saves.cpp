// The "Saves" tab: dated backups of each memory card file (.gci) kept as the
// game writes it, restored from here; and a save copied to another release's
// card in that release's layout (port/include/port/save_convert.h). Files:
//   <user>/<JAP|USA|EUR>/Card A/<name>.gci          the cards (Aurora's)
//   <user>/Backups/<JAP|USA|EUR>/<name> <date>.gci  the backups, <date> the
//                                                   saved state's own time
// Both work while a game runs, on its own card too: the file is written once
// the game isn't writing the card, the save there backed up first, and the
// game starts again to read it (port/include/port/restart.h); a phone or
// tablet app, which can't restart itself, does that from the launcher.
#include "port/cardsave_pc.h"
#include "port/offscreen.h"
#include "port/restart.h"
#include "port/resume.h"
#include "port/save_convert.h"
#include "port/save_transfer.h"
#include "port/settings.h"
#include "port/pause_forms.h"
#include "port/tr.h"
#include "../file_util.h"
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

// (why a replace stopped: the card there kept as it was)
const char* const kNoBackup = "the save there couldn't be backed up first, so it was left as it was. ";

// Copies the card file name (in release's Card A) to the backups, named by
// its own time (one copy per state), then keeps the newest *sKeep (never
// spare, the backup a restore is putting back; with "Backups kept" at 0 none
// are taken away: 0 only means none are made on saving). False when the card
// file is there and no copy of it could be made (a replace that promises its
// backups then stops: portSavesWriteFile).
bool backUp(char release, const std::string& name, const std::string& spare = {}) {
  SDL_Time modified;
  if (!isFile(cardDir(release) + "/" + name, &modified)) {
    return true;
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
    return false;
  }
  if (*sKeep <= 0) {
    return true;
  }
  std::vector<std::string> kept = glob(dir, stem(name) + " *.gci");
  const size_t keep = static_cast<size_t>(*sKeep);
  size_t count = kept.size();
  for (size_t i = 0; count > keep && i < kept.size(); i++) { // oldest first
    if (kept[i] != spare && kept[i] != to.substr(dir.size() + 1)) {
      SDL_RemovePath((dir + "/" + kept[i]).c_str());
      count--;
    }
  }
  return true;
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

using portfile::readAll;

// the backup's date from its name ("<stem> <date>.gci")
std::string dateOf(const std::string& backup) {
  const size_t at = backup.size() >= 23 ? backup.size() - 23 : 0;
  std::string date = backup.substr(at, 19);
  std::replace(date.begin(), date.end(), '.', ':');
  return date;
}

std::string sStatus;        // the copy's outcome (copySection)
std::string sRestoreStatus; // a restore's (backupsSection)

// Restores backup b (in release's backups) over its save file, the save as it
// is now backed up first, so this can be undone
bool restore(char release, const std::string& b) {
  if (b.size() <= 24) {
    sRestoreStatus = portTr("Could not restore it: that isn't a backup's name.");
    return false;
  }
  const std::string name = b.substr(0, b.size() - 24) + ".gci"; // "<stem>.gci"
  // the backup read first, and spared when the save as it is now is backed up (the
  // newest kept would otherwise take the oldest away: the one being restored)
  const std::vector<uint8_t> bytes = readAll(backupDir(release) + "/" + b);
  if (bytes.empty()) {
    sRestoreStatus = portTrf("Could not restore it: %s", SDL_GetError());
    return false;
  }
  SDL_CreateDirectory(cardDir(release).c_str());
  if (!backUp(release, name, b)) {
    sRestoreStatus = std::string(portTr("Could not restore it: ")) + portTr(kNoBackup) + SDL_GetError();
    return false;
  }
  const bool ok = portfile::writeAll(cardDir(release) + "/" + name, bytes.data(), bytes.size());
  if (ok) {
    // Continue's quick resume save is of the card before: it would bring that game back
    portResumeForget((std::string("G8M") + release + "01").c_str(), -1);
  }
  sRestoreStatus =
      ok ? portTrf("Restored the save of %s.", dateOf(b).c_str()) : portTrf("Could not restore it: %s", SDL_GetError());
  return ok;
}

// Writes a converted save over dst (to's), the save there backed up first
bool writeSave(char to, const std::string& toName, const std::string& dst, const std::vector<uint8_t>& out, int slots,
               const char* toLabel) {
  SDL_CreateDirectory(cardDir(to).c_str());
  if (!backUp(to, toName)) {
    sStatus = std::string(portTr("Could not write it: ")) + portTr(kNoBackup) + SDL_GetError();
    return false;
  }
  if (portfile::writeAll(dst, out.data(), out.size())) { // (the close checked too: file_util.h)
    sStatus = portTrf("Copied to %s (%d of its files and their backups).", portTr(toLabel), slots);
    return true;
  }
  sStatus = portTrf("Could not write it: %s", SDL_GetError());
  return false;
}

// The running game's own card, changed from here: written once the game isn't
// writing the card (it has the card's files in memory and would write them
// back), then the game started again so it reads them (Restart now, with no
// quick resume: the save is the one to load). What waits for the card:
struct CardChange {
  char release;
  std::string backup;                 // a backup to restore, or
  std::string toName, dst, label;     // a converted save to write
  std::vector<uint8_t> bytes;
  int slots;
} sChange;

void backupsSection(int inGame) {
  static int sRelease = -1;
  static std::string sConfirm; // the backup whose Restore was pressed once
  if (sRelease < 0) {
    sRelease = releaseIndex(PORT_GAME_ID[3]);
  }
  ImGui::Combo(portTrLabel("Card"), &sRelease, portTrList(kReleaseNames, 3), 3);
  const char release = kReleases[sRelease];
  const std::vector<std::string> backups = glob(backupDir(release), "*.gci");
  if (backups.empty()) {
    ImGui::TextDisabled("%s", portTr("No backups yet: one is kept each time the game saves."));
  }
  // the running game's card: restored, then the game restarted to load it
  // (a phone or tablet app can't restart itself: from the launcher there)
  const bool running = inGame && release == PORT_GAME_ID[3];
  const bool locked = running && !portRestartSupported();
  for (size_t i = backups.size(); i-- > 0;) { // newest first
    const std::string& b = backups[i];
    ImGui::PushID(b.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(dateOf(b).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(locked || portRestartWaiting());
    const char* label = sConfirm != b    ? (running ? "Restore and restart##restore" : "Restore##restore")
                        : running        ? "Replace the save with this one and restart##restore"
                                         : "Replace the save with this one##restore";
    if (ImGui::SmallButton(portTrLabel(label))) {
      if (sConfirm != b) {
        sConfirm = b;
      } else if (running) {
        sChange = CardChange{release, b, {}, {}, {}, {}, 0};
        portRestartRequestWith(0, [] { return restore(sChange.release, sChange.backup) ? 1 : 0; });
        sConfirm.clear();
      } else {
        restore(release, b);
        sConfirm.clear();
      }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (locked && !backups.empty()) {
    ImGui::TextDisabled("%s", portTr("Restore saves from the launcher, before starting."));
  } else if (running && !backups.empty()) {
    ImGui::TextDisabled("%s", portTr(portRestartWaiting() ? "Restarting when the game has finished saving..."
                                                          : "The game starts again to load it; the save as it is "
                                                            "now is kept as a backup."));
  }
  if (!sRestoreStatus.empty()) { // (a restore's outcome: done, or why not)
    ImGui::TextWrapped("%s", sRestoreStatus.c_str());
  }
}

void copySection(int inGame) {
  static int sFrom = -1, sTo = -1;
  static bool sConfirm;
  if (sFrom < 0) {
    sFrom = releaseIndex(PORT_GAME_ID[3]);
    sTo = sFrom == 0 ? 1 : 0;
  }
  ImGui::TextWrapped("%s", portTr("The Japanese release keeps its Pianta Parlor records differently; the copy is converted. "
                     "Names typed in one release's letters may look different in another."));
  const bool fromChanged = ImGui::Combo(portTrLabel("From"), &sFrom, portTrList(kReleaseNames, 3), 3);
  if (ImGui::Combo(portTrLabel("To"), &sTo, portTrList(kReleaseNames, 3), 3) || fromChanged) { // (both drawn)
    sConfirm = false;
  }
  const char from = kReleases[sFrom], to = kReleases[sTo];
  char fromName[64], toName[64];
  portSaveFileName(from, fromName, sizeof(fromName));
  portSaveFileName(to, toName, sizeof(toName));
  const std::string src = cardDir(from) + "/" + fromName, dst = cardDir(to) + "/" + toName;
  const bool have = isFile(src), replaces = isFile(dst);
  // to the running game's card: written, then the game restarted to load it
  // (from the launcher on a phone or tablet, which can't restart the app)
  const bool running = inGame && to == PORT_GAME_ID[3];
  const bool locked = running && !portRestartSupported();
  if (!have) {
    ImGui::TextDisabled("%s", portTr("That card has no save of this game."));
  }
  ImGui::BeginDisabled(!have || from == to || locked || portRestartWaiting());
  const char* label = sConfirm ? (running ? "Replace its save and restart (a backup is kept)"
                                          : "Replace its save (a backup is kept)")
                               : (running ? "Copy and restart" : "Copy");
  if (ImGui::Button(portTrLabel(label))) {
    if (replaces && !sConfirm) {
      sConfirm = true;
    } else {
      sConfirm = false;
      std::vector<uint8_t> in = readAll(src), out(in.size());
      const int slots = in.empty() ? -1 : portSaveConvert(in.data(), in.size(), to, out.data());
      if (slots < 0) {
        sStatus = portTr("That file isn't a save of this game.");
      } else if (running) {
        sChange = CardChange{to, {}, toName, dst, kReleaseNames[sTo], std::move(out), slots};
        portRestartRequestWith(0, [] {
          return writeSave(sChange.release, sChange.toName, sChange.dst, sChange.bytes, sChange.slots,
                           sChange.label.c_str())
                     ? 1
                     : 0;
        });
      } else {
        writeSave(to, toName, dst, out, slots, kReleaseNames[sTo]);
      }
    }
  }
  ImGui::EndDisabled();
  if (locked) {
    ImGui::TextDisabled("%s", portTr("Copy to the card of the game that is running from the launcher, before it starts."));
  } else if (running && portRestartWaiting()) {
    ImGui::TextDisabled("%s", portTr("Restarting when the game has finished saving..."));
  } else if (running && !sConfirm) {
    ImGui::TextDisabled("%s", portTr("This is the running game's card: the game starts again to load it."));
  } else if (sConfirm) {
    ImGui::TextDisabled(portTr("%s already has a save."), portTr(kReleaseNames[sTo]));
  }
  if (!sStatus.empty()) {
    ImGui::TextUnformatted(sStatus.c_str());
  }
}

// A copy of each card for Dolphin or a GameCube (<user>/For Dolphin/<JAP|USA|EUR>/<name>.gci):
// Paper Cut's own badges and dishes, which the GameCube game reads past its item table,
// sold for coins (port/include/port/save_convert.h portSaveDolphinSafe); the card here
// is left as it is.
std::string dolphinDir(char release) {
  return std::string(portSettingsUserPath()) + "/For Dolphin/" + regionDir(release);
}

void dolphinSection(int) {
  static std::string sDone;
  ImGui::TextWrapped("%s", portTr("The GameCube game can't read Paper Cut's own badges and dishes (the badge set's, "
                                  "the badge maker's, Zess T.'s new dishes). This copy has them sold for the coins "
                                  "a shop pays (up to 999), a worn one taken off; your card here keeps them."));
  bool any = false;
  for (int i = 0; i < 3; i++) {
    char name[64];
    portSaveFileName(kReleases[i], name, sizeof(name));
    const std::string src = cardDir(kReleases[i]) + "/" + name;
    if (!isFile(src)) {
      continue;
    }
    any = true;
    ImGui::PushID(i);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(portTr(kReleaseNames[i]));
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Make the copy##dolphin"))) {
      std::vector<uint8_t> bytes = readAll(src);
      int coins = 0;
      const int sold = bytes.empty() ? -1 : portSaveDolphinSafe(bytes.data(), bytes.size(), &coins);
      const std::string dir = dolphinDir(kReleases[i]), dst = dir + "/" + name;
      char line[256];
      if (sold < 0) {
        std::snprintf(line, sizeof(line), "%s", portTr("That file isn't a save of this game."));
      } else {
        SDL_CreateDirectory(dir.c_str());
        if (!portfile::writeAll(dst, bytes.data(), bytes.size())) {
          std::snprintf(line, sizeof(line), "%s", portTr("The copy couldn't be written."));
        } else if (sold == 0) {
          std::snprintf(line, sizeof(line), portTr("Nothing to sell: the copy is the card as it is. %s"), dst.c_str());
        } else {
          std::snprintf(line, sizeof(line), portTr("%d sold for %d coins: %s"), sold, coins, dst.c_str());
        }
      }
      sDone = line;
    }
    ImGui::PopID();
  }
  if (!any) {
    ImGui::TextDisabled("%s", portTr("No card here has a save of this game yet."));
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (any && ImGui::SmallButton(portTrLabel("Open the folder##dolphin"))) {
    const std::string dir = std::string(portSettingsUserPath()) + "/For Dolphin";
    SDL_CreateDirectory(dir.c_str());
    if (!portOpenFolder(dir.c_str())) {
      char why[1200];
      sDone = portOpenError(why, sizeof(why), dir.c_str(), true);
    }
  }
#endif
  if (!sDone.empty()) {
    ImGui::TextWrapped("%s", sDone.c_str());
  }
}

// The Start menu's forms (port/include/port/pause_forms.h): the running game's card's backups,
// and its save copied to the other releases' cards
std::vector<std::string>& formBackups() {
  static std::vector<std::string> list;
  return list;
}

int backupRows(PortPauseRow* out, int max) {
  std::vector<std::string>& list = formBackups();
  const char release = PORT_GAME_ID[3];
  list = glob(backupDir(release), "*.gci");
  std::reverse(list.begin(), list.end()); // newest first
  int n = 0;
  out[n++] = PortPauseRow{"Restore a backup", nullptr, {}, {}, nullptr, -1, PORT_PAUSE_ROW_HEADING};
  if (list.empty()) {
    out[n++] = PortPauseRow{"No backups yet", "One is kept each time the game saves.", {}, {}, nullptr, -1,
                            PORT_PAUSE_ROW_DIM};
    return n;
  }
  for (size_t i = 0; i < list.size() && n < max; i++) {
    PortPauseRow r{nullptr, "The game starts again to load it; the save as it is now is kept as a backup.", {}, {},
                   "Restore", static_cast<int>(i), PORT_PAUSE_ROW_CONFIRM};
    std::snprintf(r.text, sizeof(r.text), "%s", dateOf(list[i]).c_str());
    if (!portRestartSupported()) {
      r.flags = PORT_PAUSE_ROW_DIM;
    }
    out[n++] = r;
  }
  return n;
}

const char* backupAct(int id, int dir) {
  std::vector<std::string>& list = formBackups();
  if (dir != 0 || id < 0 || id >= static_cast<int>(list.size())) {
    return id < 0 ? "One is kept each time the game saves." : nullptr;
  }
  if (!portRestartSupported()) {
    return "Restore saves from the launcher, before starting.";
  }
  if (portRestartWaiting()) {
    return "Restarting when the game has finished saving...";
  }
  sChange = CardChange{PORT_GAME_ID[3], list[id], {}, {}, {}, {}, 0};
  portRestartRequestWith(0, [] { return restore(sChange.release, sChange.backup) ? 1 : 0; });
  return "Restarting when the game has finished saving...";
}

int copyRows(PortPauseRow* out, int max) {
  int n = 0;
  out[n++] = PortPauseRow{"Copy a save to another release", nullptr, {}, {}, nullptr, -1, PORT_PAUSE_ROW_HEADING};
  for (int i = 0; i < 3 && n < max; i++) {
    if (kReleases[i] == PORT_GAME_ID[3]) {
      continue;
    }
    char toName[64];
    portSaveFileName(kReleases[i], toName, sizeof(toName));
    const bool replaces = isFile(cardDir(kReleases[i]) + "/" + toName);
    out[n++] = PortPauseRow{kReleaseNames[i],
                            replaces ? "This game's save copied there, in place of the save it has; a backup of that "
                                       "one is kept."
                                     : "This game's save copied there.",
                            {}, {}, "Copy", i, PORT_PAUSE_ROW_CONFIRM};
  }
  return n;
}

const char* copyAct(int id, int dir) {
  if (dir != 0 || id < 0 || id >= 3) {
    return nullptr;
  }
  const char from = PORT_GAME_ID[3], to = kReleases[id];
  char fromName[64], toName[64];
  portSaveFileName(from, fromName, sizeof(fromName));
  portSaveFileName(to, toName, sizeof(toName));
  const std::string src = cardDir(from) + "/" + fromName, dst = cardDir(to) + "/" + toName;
  if (!isFile(src)) {
    return "That card has no save of this game.";
  }
  std::vector<uint8_t> in = readAll(src), out(in.size());
  const int slots = in.empty() ? -1 : portSaveConvert(in.data(), in.size(), to, out.data());
  if (slots < 0) {
    return "That file isn't a save of this game.";
  }
  return writeSave(to, toName, dst, out, slots, kReleaseNames[id]) ? "Copied." : "The save couldn't be written there.";
}

const PortPauseForm kBackupForm = {"Saves", "Restore a backup", backupRows, backupAct, PORT_PAUSE_FORM_OWN};
const PortPauseForm kCopyForm = {"Saves", "Copy a save to another release", copyRows, copyAct, PORT_PAUSE_FORM_OWN};

struct SavesSettings {
  SavesSettings() {
    sKeep = portSettingsDeclare("save_backups", 5, 0, 20);
    gPortCardWillWrite = willWrite;
    portSettingsAddSlider("Saves", "Backups kept", "Dated copies of each save file, made on saving. 0: none.",
                          0, "save_backups", 0, 20, "%d", "backup restore gci memory card");
    portSettingsAddSection("Saves", "Restore a backup", nullptr, 0, backupsSection,
                           "backup restore undo gci memory card");
    portSettingsAddSection("Saves", "Copy a save to another release", nullptr, 0, copySection,
                           "convert transfer japan jp us eu pal region gci memory card");
    portSettingsAddSection("Saves", "A copy for Dolphin or a GameCube", nullptr, 0, dolphinSection,
                           "dolphin gamecube export gci badges dishes sell memory card");
    portPauseAddForm(&kBackupForm);
    portPauseAddForm(&kCopyForm);
  }
} sSaves;

} // namespace

std::string portSavesCardDir(char release) { return cardDir(release); }

bool portSavesWriteFile(char release, const std::string& name, const std::vector<uint8_t>& bytes, std::string& why) {
  // only this release's own save file, at the game's size (a damaged or made-up file would become the
  // card's: its .raw export copies the card's blocks, and the game reads it)
  char expected[64];
  portSaveFileName(release, expected, sizeof(expected));
  if (name != expected || portSaveRelease(bytes.data(), bytes.size()) != release) {
    why = portTr("that isn't a save file of this game.");
    return false;
  }
  const std::string dst = cardDir(release) + "/" + name;
  SDL_CreateDirectory(cardDir(release).c_str());
  if (!backUp(release, name)) {
    why = std::string(portTr(kNoBackup)) + SDL_GetError();
    return false;
  }
  if (portfile::writeAll(dst, bytes.data(), bytes.size())) { // (the close checked too: file_util.h)
    return true;
  }
  why = SDL_GetError();
  return false;
}
