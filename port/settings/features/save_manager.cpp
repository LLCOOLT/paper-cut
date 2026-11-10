// The Saves tab's save manager: saves in and out of Paper Cut in the formats
// other GameCube tools use, and whole-folder backups.
//   Import  a Dolphin .gci (its GCI folder's file), a .gcs (GameShark) or
//           .sav (Action Replay / MaxDrive) export, or a whole memory card
//           image (.raw, .mcp: Dolphin's "Memory Card" mode, Nintendont,
//           a GameCube card dumped with GCMM). Picked, or dropped on the
//           window. Each save of this game found goes into its release's card
//           (a backup of the save there kept first); the running game's card
//           is written once the game isn't writing it, then the game starts
//           again (from the launcher on a phone or tablet).
//   Export  a card's .gci, or one memory card image (.raw) holding every
//           release's save, for Dolphin's Memory Card mode or Nintendont.
//   Backups every card and the save-file library copied into
//           <user>/Backups/All <date>/, and restored from there.
// Card image layout (the GameCube's, as Dolphin's GCMemcard reads it): blocks of
// 0x2000 bytes; 0 the header, 1-2 the directory and its copy, 3-4 the block
// map and its copy, files from block 5. A .gci is a directory entry (0x40
// bytes) and the file's blocks.
#include "port/restart.h"
#include "port/offscreen.h"
#include "port/resume.h"
#include "port/save_convert.h"
#include "port/save_transfer.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../file_util.h"
#include "../file_export.h"
#include "../settings_ui.h" // gPortLauncherShown

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include "port/android_pc.h" // the file picker's content:// URIs
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "port/ios_pc.h" // portIosPickedDone
#endif
#endif
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" void portSettingsOpenAt(const char* tab); // settings_ui.cpp

namespace {

constexpr size_t kBlock = 0x2000, kEntry = 0x40, kMaxImage = 128u << 20 >> 3; // 128 Mbit
// the game's save file: always 17 blocks (CardMetadata and eight slots: port/src/common/save_convert_pc.c,
// which refuses any other size too)
constexpr size_t kSaveBlocks = 17, kSaveSize = kEntry + kSaveBlocks * kBlock;
// the largest file Import reads: a whole card, or one save behind GameShark's header (the biggest)
constexpr uint64_t kMaxImport = kMaxImage + 0x110;
constexpr char kReleases[] = {'J', 'E', 'P'};

const char* releaseName(char release) {
  return release == 'J' ? "Japan (JP)" : release == 'P' ? "Europe (EU)" : "North America (US)";
}
const char* regionDir(char release) { return release == 'J' ? "JAP" : release == 'P' ? "EUR" : "USA"; }

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
void put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v >> 8);
  p[1] = uint8_t(v);
}

// the card's checksums over size bytes (both 0xFFFF become 0, as the SDK does)
void checksums(const uint8_t* data, size_t size, uint8_t* sum, uint8_t* inv) {
  uint16_t a = 0, b = 0;
  for (size_t i = 0; i + 1 < size; i += 2) {
    const uint16_t w = be16(data + i);
    a = uint16_t(a + w);
    b = uint16_t(b + (w ^ 0xFFFF));
  }
  put16(sum, a == 0xFFFF ? 0 : a);
  put16(inv, b == 0xFFFF ? 0 : b);
}

bool checksumsOk(const uint8_t* data, size_t size, const uint8_t* sum, const uint8_t* inv) {
  uint8_t s[2], i[2];
  checksums(data, size, s, i);
  return std::memcmp(s, sum, 2) == 0 && std::memcmp(i, inv, 2) == 0;
}

// A directory entry is this game's save file: its game code and file name
bool isOurEntry(const uint8_t* e) {
  return std::memcmp(e, "G8M", 3) == 0 && std::strchr("JEP", e[3]) != nullptr && e[3] != 0 &&
         std::strncmp(reinterpret_cast<const char*>(e + 8), "mariost_save_file", sizeof("mariost_save_file")) == 0;
}

// The saves of this game in a memory card image: each as a .gci
std::vector<std::vector<uint8_t>> fromImage(const std::vector<uint8_t>& img) {
  std::vector<std::vector<uint8_t>> out;
  if (img.size() < 5 * kBlock || img.size() % kBlock != 0 || img.size() > kMaxImage) {
    return out;
  }
  const size_t blocks = img.size() / kBlock;
  // the directory and block map: the copy whose checksums hold, the newer of two
  auto pick = [&](size_t a, size_t sumAt, size_t from, size_t to, size_t counterAt) -> const uint8_t* {
    const uint8_t* best = nullptr;
    for (size_t b : {a, a + 1}) {
      const uint8_t* p = img.data() + b * kBlock;
      if (checksumsOk(p + from, to - from, p + sumAt, p + sumAt + 2) &&
          (best == nullptr || int16_t(be16(p + counterAt) - be16(best + counterAt)) > 0)) {
        best = p;
      }
    }
    return best;
  };
  const uint8_t* dir = pick(1, 0x1FFC, 0, 0x1FFC, 0x1FFA);
  const uint8_t* bat = pick(3, 0x0000, 4, kBlock, 0x0004);
  if (dir == nullptr) {
    return out;
  }
  for (int i = 0; i < 127; i++) {
    const uint8_t* e = dir + i * kEntry;
    if (!isOurEntry(e)) {
      continue;
    }
    const size_t first = be16(e + 0x36), count = be16(e + 0x38);
    // (a damaged card's count past the card's own blocks, or a chain that comes back to a block it has
    // been through, skipped: either would grow the file 8 KB a step up to half a gigabyte; and any count
    // but the game's own, which no card writer or .raw export would take)
    if (blocks <= 5 || count != kSaveBlocks || count > blocks - 5) {
      continue;
    }
    std::vector<uint8_t> gci(e, e + kEntry);
    std::vector<bool> seen(blocks, false);
    size_t b = first;
    for (size_t n = 0; n < count; n++) {
      if (b < 5 || b >= blocks || seen[b]) {
        gci.clear();
        break;
      }
      seen[b] = true;
      gci.insert(gci.end(), img.begin() + long(b * kBlock), img.begin() + long((b + 1) * kBlock));
      // the next block: the map's chain, or the following block without a map
      b = bat != nullptr ? be16(bat + 0x0A + (b - 5) * 2) : b + 1;
    }
    if (!gci.empty() && portSaveRelease(gci.data(), gci.size()) != 0) {
      out.push_back(std::move(gci));
    }
  }
  return out;
}

// A memory card image (Dolphin's .raw) holding these .gci files, the smallest
// size of card they fit on (4 Mbit, 59 blocks, at least)
std::vector<uint8_t> toImage(const std::vector<std::vector<uint8_t>>& given) {
  // only whole files of a size the card's fields hold (the callers pass the game's saves, 17 blocks;
  // anything else is left out rather than copied past the image's end), as many as a directory has
  std::vector<const std::vector<uint8_t>*> files;
  size_t used = 0;
  for (const auto& f : given) {
    if (files.size() < 127 && f.size() > kEntry && (f.size() - kEntry) % kBlock == 0 &&
        (f.size() - kEntry) / kBlock <= kMaxImage / kBlock - 5 - used) {
      files.push_back(&f);
      used += (f.size() - kEntry) / kBlock;
    }
  }
  uint16_t mbits = 4;
  while (size_t(mbits) * 16 - 5 < used && mbits < 128) {
    mbits = uint16_t(mbits * 2);
  }
  const size_t blocks = size_t(mbits) * 16;
  std::vector<uint8_t> img(blocks * kBlock, 0xFF);
  // the header: serial, format time, slot A, size, encoding (Shift JIS when it holds Japanese saves only)
  uint8_t* h = img.data();
  bool onlyJp = !files.empty();
  for (const auto* f : files) {
    onlyJp = onlyJp && (*f)[3] == 'J';
  }
  for (int i = 0; i < 12; i++) {
    h[i] = uint8_t(SDL_rand(256));
  }
  SDL_Time now = 0;
  SDL_GetCurrentTime(&now);
  const uint64_t ticks = uint64_t(now / 1000000000 - 946684800) * 40500000ull; // the console's clock since 2000
  for (int i = 0; i < 8; i++) {
    h[0x0C + i] = uint8_t(ticks >> (56 - 8 * i));
  }
  std::memset(h + 0x14, 0, 0x26 - 0x14);
  put16(h + 0x22, mbits);
  put16(h + 0x24, onlyJp ? 1 : 0);
  put16(h + 0x1FA, 0);
  checksums(h, 0x1FC, h + 0x1FC, h + 0x1FE);
  // the directory and the block map, and the files from block 5
  uint8_t dir[kBlock], bat[kBlock];
  std::memset(dir, 0xFF, sizeof(dir));
  std::memset(bat, 0, sizeof(bat));
  uint16_t next = 5;
  for (size_t i = 0; i < files.size(); i++) {
    const auto& f = *files[i];
    const size_t count = (f.size() - kEntry) / kBlock;
    if (size_t(next) + count > blocks) {
      break; // (can't happen: the card was sized for every file kept above)
    }
    uint8_t* e = dir + i * kEntry;
    std::memcpy(e, f.data(), kEntry);
    put16(e + 0x36, next);
    put16(e + 0x38, uint16_t(count));
    std::memcpy(img.data() + size_t(next) * kBlock, f.data() + kEntry, count * kBlock);
    for (size_t n = 0; n < count; n++) {
      const uint16_t b = uint16_t(next + n);
      put16(bat + 0x0A + (b - 5) * 2, n + 1 == count ? 0xFFFF : uint16_t(b + 1));
    }
    next = uint16_t(next + count);
  }
  put16(dir + 0x1FFA, 0);
  checksums(dir, 0x1FFC, dir + 0x1FFC, dir + 0x1FFE);
  put16(bat + 0x04, 0);
  put16(bat + 0x06, uint16_t(blocks - next));
  put16(bat + 0x08, uint16_t(next - 1));
  checksums(bat + 4, kBlock - 4, bat, bat + 2);
  for (size_t b : {1, 2}) {
    std::memcpy(img.data() + b * kBlock, dir, kBlock);
  }
  for (size_t b : {3, 4}) {
    std::memcpy(img.data() + b * kBlock, bat, kBlock);
  }
  return img;
}

// A file brought in: the saves of this game in it, as .gci files (none: not one)
std::vector<std::vector<uint8_t>> savesIn(const std::vector<uint8_t>& bytes) {
  std::vector<std::vector<uint8_t>> out;
  auto gciAt = [&](size_t at, bool swapped) {
    if (bytes.size() < at || bytes.size() - at != kSaveSize) {
      return false; // (only the game's own size: 17 blocks)
    }
    std::vector<uint8_t> gci(bytes.begin() + long(at), bytes.end());
    if (swapped) {
      // Action Replay's .sav keeps the entry's 16-bit fields byte-swapped: the banner flags with the
      // byte before them, and every field from the image offset to the comments' address (as Dolphin
      // undoes them: the picture's place and format, the permissions and copy count, the first block)
      std::swap(gci[6], gci[7]);
      for (size_t i = 0x2C; i < kEntry; i += 2) {
        std::swap(gci[i], gci[i + 1]);
      }
    }
    put16(gci.data() + 0x38, uint16_t((gci.size() - kEntry) / kBlock));
    if (!isOurEntry(gci.data()) || portSaveRelease(gci.data(), gci.size()) == 0) {
      return false;
    }
    out.push_back(std::move(gci));
    return true;
  };
  if (bytes.size() > 0x110 && std::memcmp(bytes.data(), "GCSAVE", 6) == 0) {
    gciAt(0x110, false); // GameShark's .gcs
  } else if (bytes.size() > 0x80 && std::memcmp(bytes.data(), "DATELGC_SAVE", 12) == 0) {
    gciAt(0x80, true); // Action Replay's .sav
  } else if (!gciAt(0, false)) {
    out = fromImage(bytes); // a whole card
  }
  return out;
}

// --- what is waiting: files picked or dropped, then the saves found in them
std::mutex sLock;
std::vector<std::string> sPicked; // from the dialogs' threads, and drops
std::string sPickError;           // the open dialog couldn't show (portPickerError's text; sLock)
struct Found {
  std::string from;           // the file's name as shown
  std::vector<uint8_t> gci;   // the save
  char release;               // its card
  bool replaces;              // that card has a save already
  bool take = true;           // ticked
};
std::vector<Found> sFound;
std::string sStatus;
bool sConfirm;

std::string cardFile(char release) {
  char name[64];
  portSaveFileName(release, name, sizeof(name));
  return portSavesCardDir(release) + "/" + name;
}

bool isFile(const std::string& path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string shownName(const std::string& path) {
#ifdef __ANDROID__
  char name[256];
  if (PORT_ANDROID_IS_CONTENT_URI(path.c_str()) && portAndroidContentInfo(path.c_str(), name, sizeof(name), nullptr) &&
      name[0] != '\0') {
    return name; // (the file manager's name, not the URI's last part)
  }
#endif
  const size_t at = path.find_last_of("/\\");
  return at == std::string::npos ? path : path.substr(at + 1);
}

void readPicked() {
  std::vector<std::string> picked;
  std::string error;
  {
    std::lock_guard lock(sLock);
    picked.swap(sPicked);
    error.swap(sPickError);
  }
  if (!error.empty()) {
    sStatus = error;
  }
  for (const std::string& path : picked) {
    // (read here, on the frame's thread: only up to the biggest file Import takes, a whole card; a larger
    // one dropped by mistake is refused by its size instead of read whole first; Android's picks are
    // content:// URIs, which portfile::readPicked reads too)
    std::vector<uint8_t> bytes;
    const bool read = portfile::readPicked(path, kMaxImport, bytes);
#if defined(__APPLE__) && TARGET_OS_IPHONE
    portIosPickedDone(path.c_str()); // (the picker's copy in tmp/: read, so gone)
#endif
    if (!read) {
      sStatus = shownName(path) + ": " + portTr("no save of this game in that file.");
      continue;
    }
    std::vector<std::vector<uint8_t>> saves = savesIn(bytes);
    if (saves.empty()) {
      sStatus = shownName(path) + ": " + portTr("no save of this game in that file.");
      continue;
    }
    for (auto& gci : saves) {
      Found f{shownName(path), std::move(gci), 0, false};
      f.release = portSaveRelease(f.gci.data(), f.gci.size());
      // the same release found twice (two files, a card with two): the last one counts
      sFound.erase(std::remove_if(sFound.begin(), sFound.end(), [&](const Found& o) { return o.release == f.release; }),
                   sFound.end());
      f.replaces = isFile(cardFile(f.release));
      sFound.push_back(std::move(f));
    }
    sStatus.clear();
    sConfirm = false;
  }
}

void picked(void*, const char* const* files, int) {
  if (files == nullptr) { // (the dialog couldn't show: a file can still be dropped on the window)
    char why[1024];
    portPickerError(why, sizeof(why), nullptr);
    std::lock_guard lock(sLock);
    sPickError = why;
    return;
  }
  std::lock_guard lock(sLock);
  for (int i = 0; files[i] != nullptr; i++) {
    sPicked.push_back(files[i]);
  }
}

// The chosen saves into their cards (each backed up first)
bool writeFound() {
  int done = 0;
  std::string why;
  for (const Found& f : sFound) {
    if (!f.take) {
      continue;
    }
    char name[64];
    portSaveFileName(f.release, name, sizeof(name));
    if (!portSavesWriteFile(f.release, name, f.gci, why)) {
      sStatus = std::string(portTr("Could not write it: ")) + why;
      return done > 0;
    }
    portResumeForget((std::string("G8M") + f.release + "01").c_str(), -1); // quick resume's save is of the card before
    done++;
  }
  char line[96];
  std::snprintf(line, sizeof(line), portTr("Imported %d save files (backups of the saves they replaced are kept)."),
                done);
  sStatus = line;
  sFound.clear();
  return done > 0;
}

void importSection(int inGame) {
  readPicked();
  ImGui::TextWrapped("%s", portTr("Dolphin's .gci, GameShark's .gcs, Action Replay's .sav, or a whole memory card "
                                  "(.raw, .mcp): pick the file, or drop it on the window."));
  if (ImGui::Button(portTrLabel("Import a save..."))) {
    static const SDL_DialogFileFilter kFilters[] = {
        {"GameCube saves and memory cards (.gci, .gcs, .sav, .raw, .mcp)", "gci;gcs;sav;raw;mcp"},
        {"All files", "*"},
    };
    portShowOpenFileDialog(picked, nullptr, SDL_GetKeyboardFocus(), kFilters, 2, nullptr, true);
  }
  if (!sFound.empty()) {
    bool running = false, any = false;
    for (Found& f : sFound) {
      ImGui::PushID(f.release);
      ImGui::Checkbox("##take", &f.take);
      ImGui::SameLine();
      ImGui::Text(portTr("%s, from %s%s"), portTr(releaseName(f.release)), f.from.c_str(),
                  f.replaces ? portTr(" (replaces the save there)") : "");
      ImGui::PopID();
      running = running || (f.take && inGame && f.release == PORT_GAME_ID[3]);
      any = any || f.take;
    }
    const bool locked = running && !portRestartSupported();
    const bool replaces = std::any_of(sFound.begin(), sFound.end(), [](const Found& f) { return f.take && f.replaces; });
    ImGui::BeginDisabled(!any || locked || portRestartWaiting());
    const char* label = sConfirm ? (running ? "Replace and restart (backups are kept)##imp"
                                            : "Replace (backups are kept)##imp")
                                 : (running ? "Import and restart##imp" : "Import##imp");
    if (ImGui::Button(portTrLabel(label))) {
      if (replaces && !sConfirm) {
        sConfirm = true;
      } else if (running) {
        sConfirm = false;
        portRestartRequestWith(0, [] { return writeFound() ? 1 : 0; });
      } else {
        sConfirm = false;
        writeFound();
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Cancel##imp"))) {
      sFound.clear();
      sConfirm = false;
    }
    if (locked) {
      ImGui::TextDisabled("%s", portTr("Import into the card of the game that is running from the launcher, before it starts."));
    } else if (running) {
      ImGui::TextDisabled("%s", portTr(portRestartWaiting() ? "Restarting when the game has finished saving..."
                                                            : "This is the running game's card: the game starts "
                                                              "again to load it."));
    }
  }
  if (!sStatus.empty()) {
    ImGui::TextWrapped("%s", sStatus.c_str());
  }
}

// --- export: written, then where it goes asked (portfile::exportFile: a computer's save dialog, iOS's export
// picker); on Android, into <user>/Exports
std::string sExportStatus;
std::mutex sExportLock;

// (portfile::exportFile's answer: after the write)
void exported(void*, const char* const* files, int) {
  std::lock_guard lock(sExportLock);
  if (files == nullptr) {
    sExportStatus = std::string(portTr("Could not write it: ")) + SDL_GetError();
  } else if (files[0] != nullptr) {
    sExportStatus = std::string(portTr("Exported: ")) + files[0];
  } // (cancelled: nothing written)
}

void exportBytes(std::vector<uint8_t> bytes, const char* name, const SDL_DialogFileFilter* filter) {
#if defined(__ANDROID__)
  (void)filter;
  const std::string dir = std::string(portSettingsUserPath()) + "/Exports", to = dir + "/" + name;
  SDL_CreateDirectory(dir.c_str());
  const bool ok = portfile::writeAll(to, bytes.data(), bytes.size());
  exported(nullptr, ok ? std::vector<const char*>{to.c_str(), nullptr}.data() : nullptr, 0);
#else
  portfile::exportFile(exported, nullptr, filter, 1, name, std::move(bytes));
#endif
}

void exportSection(int) {
  static const SDL_DialogFileFilter kGci = {"Dolphin save (.gci)", "gci"};
  static const SDL_DialogFileFilter kRaw = {"Memory card image (.raw)", "raw"};
  bool any = false;
  for (char release : kReleases) {
    const std::string path = cardFile(release);
    if (!isFile(path)) {
      continue;
    }
    any = true;
    ImGui::PushID(release);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(portTr(releaseName(release)));
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Export .gci##exp"))) {
      char name[64];
      portSaveFileName(release, name, sizeof(name));
      exportBytes(portfile::readAll(path), name, &kGci);
    }
    ImGui::PopID();
  }
  if (!any) {
    ImGui::TextDisabled("%s", portTr("No card here has a save of this game yet."));
  } else if (ImGui::Button(portTrLabel("Export a memory card image (.raw)..."))) {
    std::vector<std::vector<uint8_t>> all;
    for (char release : kReleases) {
      std::vector<uint8_t> bytes = portfile::readAll(cardFile(release));
      if (bytes.size() > kEntry && portSaveRelease(bytes.data(), bytes.size()) != 0 &&
          (bytes.size() - kEntry) % kBlock == 0) {
        all.push_back(std::move(bytes));
      }
    }
    exportBytes(toImage(all), "Paper Cut.raw", &kRaw);
  }
  ImGui::TextDisabled("%s", portTr("The .gci goes in Dolphin's GCI folder; the .raw is a whole card, for Dolphin's "
                                   "Memory Card mode or Nintendont. Paper Cut's own badges and dishes: make A copy "
                                   "for Dolphin or a GameCube first."));
  std::lock_guard lock(sExportLock);
  if (!sExportStatus.empty()) {
    ImGui::TextWrapped("%s", sExportStatus.c_str());
  }
}

// --- whole backups: <user>/Backups/All <date>/<JAP|USA|EUR>/Card A and Save files
std::string allDir() { return std::string(portSettingsUserPath()) + "/Backups"; }

bool copyTree(const std::string& from, const std::string& to) {
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(from.c_str(), &info)) {
    return true; // nothing there
  }
  if (info.type == SDL_PATHTYPE_FILE) {
    return SDL_CopyFile(from.c_str(), to.c_str());
  }
  SDL_CreateDirectory(to.c_str());
  int count = 0;
  char** names = SDL_GlobDirectory(from.c_str(), "*", 0, &count);
  bool ok = true;
  for (int i = 0; names != nullptr && i < count; i++) {
    // (a folder link or junction inside is never followed: one to a parent would copy without end)
    SDL_PathInfo sub;
    const std::string path = from + "/" + names[i];
    if (std::strchr(names[i], '/') == nullptr && std::strchr(names[i], '\\') == nullptr &&
        !(SDL_GetPathInfo(path.c_str(), &sub) && sub.type == SDL_PATHTYPE_DIRECTORY && portfile::isLink(path))) {
      ok = copyTree(path, to + "/" + names[i]) && ok;
    }
  }
  SDL_free(names);
  return ok;
}

std::string sBackupStatus;

// every card and the whole library backed up as "All <date>" (its folder name in *name): false when a part
// of it couldn't be written
bool backUpAll(std::string* name = nullptr) {
  SDL_Time now = 0;
  SDL_GetCurrentTime(&now);
  SDL_DateTime dt;
  SDL_TimeToDateTime(now, &dt, true);
  char date[48];
  std::snprintf(date, sizeof(date), "All %04d-%02d-%02d %02d.%02d.%02d", dt.year, dt.month, dt.day, dt.hour,
                dt.minute, dt.second);
  std::string folder = date;
  SDL_PathInfo there;
  for (int n = 2; SDL_GetPathInfo((allDir() + "/" + folder).c_str(), &there) && n < 100; n++) {
    folder = std::string(date) + " (" + std::to_string(n) + ")"; // (two in one second: never over the first)
  }
  const std::string user = portSettingsUserPath(), to = allDir() + "/" + folder;
  SDL_CreateDirectory(allDir().c_str());
  SDL_CreateDirectory(to.c_str());
  bool ok = true;
  for (char release : kReleases) {
    SDL_CreateDirectory((to + "/" + regionDir(release)).c_str());
    ok = copyTree(portSavesCardDir(release), to + "/" + regionDir(release) + "/Card A") && ok;
  }
  ok = copyTree(user + "/Save files", to + "/Save files") && ok;
  sBackupStatus = ok ? std::string(portTr("Backed up to ")) + to : std::string(portTr("Could not write it: ")) + SDL_GetError();
  if (name != nullptr) {
    *name = folder;
  }
  return ok;
}

std::string sRestoreFrom;

// The backups a restore makes first (of the saves as they were) carry this file; the newest kAutoKept of
// them stay, the older ones go, so swapping cards back and forth by restoring doesn't pile up copies of every
// card and the whole library. The player's own ("Back up all saves now") are never removed.
constexpr const char* kBeforeRestore = "made before a restore.txt";
constexpr size_t kAutoKept = 10;

// (a link or junction inside is removed itself, never emptied through: what it points to stays)
void removeBackupTree(const std::string& path) {
  SDL_PathInfo info;
  if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY && !portfile::isLink(path)) {
    int count = 0;
    char** names = SDL_GlobDirectory(path.c_str(), "*", 0, &count);
    for (int i = 0; names != nullptr && i < count; i++) {
      if (std::strchr(names[i], '/') == nullptr && std::strchr(names[i], '\\') == nullptr) {
        removeBackupTree(path + "/" + names[i]);
      }
    }
    SDL_free(names);
  }
  SDL_RemovePath(path.c_str());
}

// made (the backup just written) marked as a restore's own, then the older restore backups past the newest
// kAutoKept removed; never made itself or the one being restored from
// When a restore backup was made, in real time (UTC, unlike its local-time name, which a clock change or
// a time zone change reorders): the stamp in its marker, else the marker file's own time.
static const char kMadeAt[] = "made at (ns since 1970, UTC): ";

SDL_Time restoreBackupMadeAt(const std::string& marker) {
  size_t size = 0;
  if (char* text = static_cast<char*>(SDL_LoadFile(marker.c_str(), &size)); text != nullptr) {
    const char* at = std::strstr(text, kMadeAt);
    const SDL_Time made = at != nullptr ? SDL_Time(std::strtoll(at + sizeof(kMadeAt) - 1, nullptr, 10)) : 0;
    SDL_free(text);
    if (made > 0) {
      return made;
    }
  }
  SDL_PathInfo info;
  return SDL_GetPathInfo(marker.c_str(), &info) ? info.modify_time : 0;
}

void pruneRestoreBackups(const std::string& made) {
  SDL_Time now = 0;
  SDL_GetCurrentTime(&now);
  const std::string note = std::string("Paper Cut backed these saves up before restoring another backup over them.\n") +
                           kMadeAt + std::to_string(static_cast<long long>(now)) + "\n";
  SDL_SaveFile((allDir() + "/" + made + "/" + kBeforeRestore).c_str(), note.data(), note.size());
  int count = 0;
  char** names = SDL_GlobDirectory(allDir().c_str(), "All *", 0, &count);
  std::vector<std::pair<SDL_Time, std::string>> marked;
  for (int i = 0; names != nullptr && i < count; i++) {
    const std::string b = names[i];
    const std::string marker = allDir() + "/" + b + "/" + kBeforeRestore;
    if (b.find_first_of("/\\") == std::string::npos && isFile(marker)) {
      marked.emplace_back(restoreBackupMadeAt(marker), b);
    }
  }
  SDL_free(names);
  std::sort(marked.rbegin(), marked.rend()); // newest first, by when each was made
  for (size_t i = kAutoKept; i < marked.size(); i++) {
    const std::string& b = marked[i].second;
    if (b != made && b != sRestoreFrom && !portfile::isLink(allDir() + "/" + b)) {
      removeBackupTree(allDir() + "/" + b);
    }
  }
}

// a backup's library merged into the library (<region>/<id>/: save.bin, info.txt, thumb.bmp, slot/): an
// entry the library hasn't got is added; one it has is taken from the backup only when the backup's save
// is the newer, so a save file played on since the backup keeps its progress. Returns the entries kept as
// they were.
int mergeLibrary(const std::string& from, const std::string& to) {
  int kept = 0;
  SDL_CreateDirectory(to.c_str());
  int regions = 0;
  char** regionNames = SDL_GlobDirectory(from.c_str(), "*", 0, &regions);
  for (int r = 0; regionNames != nullptr && r < regions; r++) {
    const std::string region = regionNames[r];
    if (region.find_first_of("/\\") != std::string::npos) {
      continue;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo((from + "/" + region).c_str(), &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
      copyTree(from + "/" + region, to + "/" + region);
      continue;
    }
    SDL_CreateDirectory((to + "/" + region).c_str());
    int ids = 0;
    char** idNames = SDL_GlobDirectory((from + "/" + region).c_str(), "*", 0, &ids);
    for (int i = 0; idNames != nullptr && i < ids; i++) {
      const std::string id = idNames[i];
      if (id.find_first_of("/\\") != std::string::npos) {
        continue;
      }
      const std::string src = from + "/" + region + "/" + id, dst = to + "/" + region + "/" + id;
      SDL_PathInfo mine, theirs;
      if (SDL_GetPathInfo((dst + "/save.bin").c_str(), &mine)) {
        if (!SDL_GetPathInfo((src + "/save.bin").c_str(), &theirs)) {
          continue; // (nothing to restore there)
        }
        if (theirs.modify_time <= mine.modify_time) {
          kept += theirs.modify_time < mine.modify_time; // (the library's own is the newer: it stays)
          continue;
        }
      }
      copyTree(src, dst);
    }
    SDL_free(idNames);
  }
  SDL_free(regionNames);
  return kept;
}

// a whole backup's cards over the cards, its library merged into the library (mergeLibrary); everything as
// it was is backed up first, as a backup of its own in the same list, so a restore can be undone
bool restoreAll() {
  const std::string from = allDir() + "/" + sRestoreFrom;
  int done = 0;
  std::string why, before;
  if (!backUpAll(&before)) {
    sBackupStatus = std::string(portTr("Not restored: the saves as they are now couldn't be backed up first. ")) +
                    sBackupStatus;
    return false;
  }
  pruneRestoreBackups(before);
  for (char release : kReleases) {
    char name[64];
    portSaveFileName(release, name, sizeof(name));
    const std::string path = from + "/" + regionDir(release) + "/Card A/" + name;
    if (!isFile(path)) {
      continue;
    }
    if (!portSavesWriteFile(release, name, portfile::readAll(path), why)) {
      sBackupStatus = std::string(portTr("Could not write it: ")) + why;
      return done > 0;
    }
    portResumeForget((std::string("G8M") + release + "01").c_str(), -1);
    done++;
  }
  const int kept = mergeLibrary(from + "/Save files", std::string(portSettingsUserPath()) + "/Save files");
  sBackupStatus = std::string(portTr("Restored the backup of ")) + sRestoreFrom.substr(4) + ". " +
                  portTr("The saves from before it are kept as the backup of ") + before.substr(4) + ".";
  if (kept > 0) {
    char line[160];
    std::snprintf(line, sizeof(line), portTr("%d save files played since the backup kept their newer progress."),
                  kept);
    sBackupStatus += std::string(" ") + line;
  }
  return done > 0;
}

void backupAllSection(int inGame) {
  static std::string sConfirmed;
  if (ImGui::Button(portTrLabel("Back up all saves now"))) {
    backUpAll();
  }
  int count = 0;
  char** names = SDL_GlobDirectory(allDir().c_str(), "All *", 0, &count);
  std::vector<std::string> list;
  for (int i = 0; names != nullptr && i < count; i++) {
    list.push_back(names[i]);
  }
  SDL_free(names);
  std::sort(list.rbegin(), list.rend()); // newest first
  const bool locked = inGame && !portRestartSupported();
  for (const std::string& b : list) {
    ImGui::PushID(b.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(b.substr(4).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(locked || portRestartWaiting());
    const char* label = sConfirmed != b ? (inGame ? "Restore and restart##all" : "Restore##all")
                                        : "Replace every card's save with these (the saves as they are now are "
                                          "backed up first)##all";
    if (ImGui::SmallButton(portTrLabel(label))) {
      if (sConfirmed != b) {
        sConfirmed = b;
      } else {
        sConfirmed.clear();
        sRestoreFrom = b;
        if (inGame) {
          portRestartRequestWith(0, [] { return restoreAll() ? 1 : 0; });
        } else {
          restoreAll();
        }
      }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (locked && !list.empty()) {
    ImGui::TextDisabled("%s", portTr("Restore saves from the launcher, before starting."));
  }
  if (!sBackupStatus.empty()) {
    ImGui::TextWrapped("%s", sBackupStatus.c_str());
  }
}

// --- drag and drop: a save or card file dropped on the window (the launcher or over the game)
bool isSaveName(const char* path) {
  const char* dot = std::strrchr(path, '.');
  if (dot == nullptr) {
    return false;
  }
  for (const char* ext : {".gci", ".gcs", ".sav", ".raw", ".mcp"}) {
    if (SDL_strcasecmp(dot, ext) == 0) {
      return true;
    }
  }
  return false;
}

int dropEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type != SDL_EVENT_DROP_FILE || e->drop.data == nullptr || !isSaveName(e->drop.data)) {
    return 0;
  }
  {
    std::lock_guard lock(sLock);
    sPicked.push_back(e->drop.data);
  }
  if (!gPortLauncherShown) {
    portSettingsOpenAt("Saves"); // over the game: the import shows what came in
  }
  return 1;
}

// Tests: $TTYD_SAVES_ROUNDTRIP=<dir>: once, at the first frame, each card's save written out as Export
// writes it (its .gci; every card in one .raw) and read back as Import reads it (savesIn): "[saves]" lines,
// and the files left in <dir> (for Dolphin, or tools/port/gcmemcard.py). An image's directory entry gets
// the block the file starts at on that card (0x36), so that field is not compared.
bool sameSave(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.size() != b.size() || a.size() < kEntry) {
    return false;
  }
  return std::memcmp(a.data(), b.data(), 0x36) == 0 &&
         std::memcmp(a.data() + 0x38, b.data() + 0x38, a.size() - 0x38) == 0;
}

void roundTripOnce(int) {
  static bool done;
  const char* dir = SDL_getenv("TTYD_SAVES_ROUNDTRIP");
  if (done || dir == nullptr || dir[0] == '\0') {
    return;
  }
  done = true;
  SDL_CreateDirectory(dir);
  std::vector<std::vector<uint8_t>> all;
  int bad = 0;
  for (char release : kReleases) {
    std::vector<uint8_t> bytes = portfile::readAll(cardFile(release));
    if (bytes.size() <= kEntry || (bytes.size() - kEntry) % kBlock != 0 ||
        portSaveRelease(bytes.data(), bytes.size()) == 0) {
      continue;
    }
    char name[64];
    portSaveFileName(release, name, sizeof(name));
    const bool wrote = portfile::writeAll(std::string(dir) + "/" + name, bytes.data(), bytes.size());
    const std::vector<std::vector<uint8_t>> back = savesIn(bytes);
    const bool ok = wrote && back.size() == 1 && back[0] == bytes;
    std::printf("[saves] roundtrip %s .gci (%zu bytes): %s\n", releaseName(release), bytes.size(),
                ok ? "read back the same" : "DIFFERENT");
    bad += ok ? 0 : 1;
    all.push_back(std::move(bytes));
  }
  if (!all.empty()) {
    const std::vector<uint8_t> img = toImage(all);
    const bool wrote = portfile::writeAll(std::string(dir) + "/Paper Cut.raw", img.data(), img.size());
    const std::vector<std::vector<uint8_t>> back = savesIn(img);
    bool ok = wrote && back.size() == all.size();
    for (size_t i = 0; ok && i < all.size(); i++) {
      ok = sameSave(back[i], all[i]);
    }
    std::printf("[saves] roundtrip .raw (%zu KB, %zu saves): %s\n", img.size() / 1024, back.size(),
                ok ? "read back the same" : "DIFFERENT");
    bad += ok ? 0 : 1;
    // a save of any other size (damaged or made up: its entry, then 2100 blocks) is refused by Import, and
    // a .raw made with it leaves it out instead of copying it past the image's end
    std::vector<uint8_t> big = all[0];
    big.resize(big.size() + 2100 * kBlock, 0);
    std::vector<uint8_t> raw = toImage({big});
    const bool refused = savesIn(big).empty() && raw.size() == 64 * kBlock && fromImage(raw).empty();
    // and in a card image, a directory entry that claims more blocks than the game's file has
    std::vector<uint8_t> card = toImage({all[0]});
    put16(card.data() + 1 * kBlock + 0x38, uint16_t(kSaveBlocks + 1));
    put16(card.data() + 2 * kBlock + 0x38, uint16_t(kSaveBlocks + 1));
    for (size_t b : {1, 2}) {
      checksums(card.data() + b * kBlock, 0x1FFC, card.data() + b * kBlock + 0x1FFC, card.data() + b * kBlock + 0x1FFE);
    }
    const bool cardRefused = fromImage(card).empty();
    std::printf("[saves] roundtrip oversized save: %s\n", refused && cardRefused ? "refused" : "TAKEN");
    bad += refused && cardRefused ? 0 : 1;
  }
  // and $TTYD_SAVES_IMPORT=<file>[;<file>...]: other tools' files (a card Dolphin wrote), read as Import reads them
  if (const char* in = SDL_getenv("TTYD_SAVES_IMPORT")) {
    std::string list = in;
    for (size_t at = 0; at <= list.size();) {
      const size_t end = std::min(list.find(';', at), list.size());
      const std::string path = list.substr(at, end - at);
      at = end + 1;
      if (path.empty()) {
        continue;
      }
      const std::vector<std::vector<uint8_t>> saves = savesIn(portfile::readAll(path));
      std::string releases;
      for (const auto& gci : saves) {
        releases += releases.empty() ? "" : ", ";
        releases += releaseName(portSaveRelease(gci.data(), gci.size()));
      }
      std::printf("[saves] import %s: %zu saves%s%s\n", shownName(path).c_str(), saves.size(),
                  saves.empty() ? "" : ": ", releases.c_str());
      bad += saves.empty() ? 1 : 0;
    }
  }
  std::printf("[saves] roundtrip %s: %zu cards\n", all.empty() ? "FAIL (no card)" : bad ? "FAIL" : "ok",
              all.size());
  std::fflush(stdout);
}

const bool sAdded = [] {
  portSettingsAddOverlay(roundTripOnce, nullptr); // (tests only: $TTYD_SAVES_ROUNDTRIP)
  portSettingsAddSection("Saves", "Import saves", nullptr, 0, importSection,
                         "import dolphin gci gcs sav raw mcp memory card image nintendont action replay gameshark "
                         "drag drop");
  portSettingsAddSection("Saves", "Export saves", nullptr, 0, exportSection,
                         "export dolphin gci raw memory card image nintendont");
  portSettingsAddSection("Saves", "Back up everything", nullptr, 0, backupAllSection,
                         "backup restore all saves library folder");
  portSettingsAddOverlay(nullptr, dropEvent);
  return true;
}();

} // namespace

// for the mods' drop (mods_install.cpp): these files are the save manager's
bool portSaveManagerTakesDrop(const char* path) { return path != nullptr && isSaveName(path); }
