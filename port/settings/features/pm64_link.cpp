// Link your Paper Mario 64 ROM (Enhancements > Paper Mario 64; port/include/port/pm64.h,
// docs/PM64_LINK.md): the player's own ROM picked with the system's file
// dialog, as the disc is, checked by its SHA-1 and read into a cache in the
// user folder (<user>/pm64/); unlinking deletes the cache. Then "Paper Mario 64
// partners" puts the N64 partners in the game (port/src/common/pm64_partner_pc.c).
// The Start menu has the same on its Paper Mario 64 page (a form): the link's state, the file
// picker, the ROMs found in the user folder and Downloads (A links one: no system dialog needed,
// so a controller alone does it), Unlink, and the Paper Mario 64 music.
#include "port/pause_forms.h"
#include "port/overlay_place.h"
#include "port/pm64.h"
#include "port/practice.h" // portAchievementToast
#include "port/offscreen.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "../settings_ui.h" // gPortLauncherShown
#include "../enhancement_presets.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "port/ios_pc.h" // portIosPickedDone
#endif
#endif

#include <algorithm>
#include <cfloat>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

extern "C" void portSettingsOpenAt(const char* tab); // settings_ui.cpp: the page open on a tab

namespace {

// (left allocated at exit, as sRead below: the reader thread may still be at a slow file as the
// program quits, and takes the lock to say it's done)
std::mutex& sLock = *new std::mutex;
std::string& sPicked = *new std::string; // from the file dialog's thread (sLock; never destroyed either)
std::atomic<bool> sPending{false};
std::string sMessage;
// the last outcome in English, for the Start menu (its text translated there: the game's font)
const char* sOutcome = nullptr;
std::set<std::string> sOutcomes; // (stable addresses for them)

const char* outcome(const std::string& english) {
  return sOutcomes.insert(english).first->c_str();
}

void queue(const char* path);

std::string& sPickError = *new std::string; // the dialog couldn't show (portPickerError's text; sLock)

// One picker at a time: set by pick(), cleared by its answer (a second tap, or the tap that closed the
// picker reaching the page too, is ignored while it's out)
std::atomic<bool> sPicking{false};
std::atomic<bool> sReading{false};
std::atomic<Uint64> sLinkedAt{0}; // (SDL_GetTicks of the last link: a picker error right after it is stale)

void picked(void*, const char* const* files, int) {
  if (files == nullptr) { // (an error, not a cancel: said by work())
    char why[1024];
    portPickerError(why, sizeof(why), nullptr);
    const Uint64 linkedAt = sLinkedAt;
    std::lock_guard<std::mutex> lock(sLock);
    if (sReading || (sPending && !sPicked.empty()) || (linkedAt != 0 && SDL_GetTicks() - linkedAt < 3000)) {
      std::printf("[pm64] a picker error after a pick, dropped: the pick's outcome stays\n");
    } else {
      sPickError = why;
      sPending = true;
    }
  } else if (files[0] != nullptr) {
    queue(files[0]);
  }
  sPicking = false;
}

// the launcher's shorter word on the last try (portPm64LinkPoll)
std::string sShort;

// a refusal by the SHA-1 (portPm64LinkRom's err: "That isn't a Paper Mario ROM ... SHA-1 <hex>")
constexpr char kUnknownPrefix[] = "That isn't a Paper Mario ROM";
constexpr char kUnknown[] = "That ROM isn't one Paper Cut knows. Only an unmodified Paper Mario ROM links (US, Japan, "
                            "Europe or iQue): not a hack, a patched or translated ROM, or another game.";

// (the largest Paper Mario ROM is 40 MB: anything past this, a disc image say, is refused unread)
constexpr Sint64 kRomMax = 64 * 1024 * 1024;
constexpr Sint64 kRomMin = 0x1000;
constexpr char kTooBig[] = "That file is too big to be a Paper Mario ROM.";
constexpr char kNotRom[] = "That file isn't an N64 ROM.";
constexpr char kUnreadable[] = "That file couldn't be read.";
constexpr char kOnline[] = "Not while playing online: link or unlink once the session is over.";

// The read, on a thread of its own (a slow card or a phone's content:// file doesn't stop the page);
// its outcome waits under sLock for work() on the page's thread, where the ROM is checked and cached.
struct Read {
  std::string path;
  void* rom = nullptr;
  size_t size = 0;
  const char* err = nullptr; // (English: kTooBig, kNotRom, kUnreadable)
  bool done = false;
};
Read& sRead = *new Read; // (sLock; never destroyed: see sLock)

void readFile(Read& r) {
  SDL_IOStream* io = SDL_IOFromFile(r.path.c_str(), "rb");
  if (io == nullptr) {
    r.err = kUnreadable;
    return;
  }
  const Sint64 n = SDL_GetIOSize(io); // (-1 when a stream can't say: then read, up to the cap)
  if (n > kRomMax) {
    r.err = kTooBig;
  } else if (n >= 0 && n < kRomMin) {
    r.err = kNotRom;
  } else {
    const size_t cap = n >= 0 ? size_t(n) : size_t(kRomMax) + 1;
    unsigned char* buf = static_cast<unsigned char*>(SDL_malloc(cap));
    size_t got = 0;
    if (buf == nullptr) {
      r.err = kUnreadable;
    } else {
      while (got < cap) {
        const size_t step = SDL_ReadIO(io, buf + got, cap - got);
        if (step == 0) {
          break;
        }
        got += step;
      }
      if (n >= 0 && got != size_t(n)) {
        r.err = kUnreadable;
      } else if (Sint64(got) > kRomMax) {
        r.err = kTooBig;
      } else if (Sint64(got) < kRomMin) {
        r.err = kNotRom;
      }
      if (r.err != nullptr) {
        SDL_free(buf);
      } else {
        r.rom = buf;
        r.size = got;
      }
    }
  }
  SDL_CloseIO(io);
}

int readThread(void*) {
  Read r;
  {
    std::lock_guard<std::mutex> lock(sLock);
    r.path = sRead.path;
  }
  readFile(r);
  std::lock_guard<std::mutex> lock(sLock);
  sRead.rom = r.rom;
  sRead.size = r.size;
  sRead.err = r.err;
  sRead.done = true;
  return 0;
}

void refuse(const char* english) {
  sMessage = sShort = portTr(english);
  sOutcome = english;
  std::printf("[pm64] link refused: %s\n", english);
  std::fflush(stdout);
}

// The dialog's answer, a dropped ROM or one found: read through SDL (Android's content:// files too)
// on a thread of its own, then (finish) checked and cached on the page's (or launcher's) thread; the ROM
// itself isn't kept. One implementation for the settings page, the Start menu's form and the launcher.
void finish(void* rom, size_t size);

void linkPath(const std::string& path) {
  if (portStateSyncLinked()) {
    refuse(kOnline); // (everyone in a session agreed on the partners: one player's link can't change them)
    return;
  }
  if (sReading) {
    queue(path.c_str()); // (one at a time: this one waits for the read under way, then the latest goes)
    return;
  }
  {
    std::lock_guard<std::mutex> lock(sLock);
    sRead = Read();
    sRead.path = path;
  }
  sReading = true;
  sShort = portTr("Reading the ROM...");
  SDL_Thread* t = SDL_CreateThread(readThread, "pm64 link", nullptr);
  if (t != nullptr) {
    SDL_DetachThread(t);
  } else {
    readThread(nullptr); // (no threads here: read now)
  }
}

void finish(void* rom, size_t size) {
  char err[256];
  if (portStateSyncLinked()) {
    SDL_free(rom);
    refuse(kOnline);
    return;
  }
  const int v = portPm64LinkRom(rom, size, err, sizeof(err));
  SDL_free(rom);
  sOutcome = v == PM64_NONE ? outcome(err) : nullptr;
  if (v == PM64_NONE) {
    if (std::strncmp(err, kUnknownPrefix, sizeof(kUnknownPrefix) - 1) == 0) {
      const char* sha = std::strstr(err, "SHA-1 ");
      sShort = portTr(kUnknown);
      sMessage = sShort + (sha != nullptr ? std::string(" (") + sha + ")" : std::string());
    } else {
      sMessage = sShort = portTr(err);
    }
    std::printf("[pm64] link refused: %s\n", err);
  } else {
    char text[300];
    std::snprintf(text, sizeof(text), portTr("Linked: Paper Mario (%s). The ROM itself isn't kept."),
                  portTr(portPm64VersionName(v)));
    sMessage = text;
    sShort = portTr("Paper Mario linked: its partners can join you.");
    sLinkedAt = SDL_GetTicks();
    std::printf("[pm64] linked %s\n", portPm64VersionName(v));
    portPresetPm64Linked(); // "Turn on all Paper Mario 64 content?" (enhancement_presets.cpp)
  }
  std::fflush(stdout);
}

void work() {
  if (sReading) {
    Read r;
    {
      std::lock_guard<std::mutex> lock(sLock);
      if (sRead.done) {
        r = sRead;
        sRead = Read();
      }
    }
    if (r.done) {
      sReading = false;
#if defined(__APPLE__) && TARGET_OS_IPHONE
      portIosPickedDone(r.path.c_str()); // (the picker's copy in tmp/: read, so gone; the cache has it)
#endif
      if (r.err != nullptr) {
        refuse(r.err);
      } else {
        finish(r.rom, r.size);
      }
    }
  }
  if (!sPending || sReading) {
    return;
  }
  std::string path, error;
  {
    std::lock_guard<std::mutex> lock(sLock);
    path.swap(sPicked);
    error.swap(sPickError);
    sPending = false;
  }
  if (!error.empty()) {
    sMessage = sShort = error;
    sOutcome = "Couldn't open the file picker."; // (the Start menu's row: the game's font)
  }
  if (!path.empty()) {
    linkPath(path);
  }
}

// Unlink, but not online (see linkPath)
const char* unlinkRom() {
  if (portStateSyncLinked()) {
    refuse(kOnline);
    return kOnline;
  }
  portPm64Unlink();
  sMessage = sShort = portTr("Unlinked: the cache is deleted.");
  sOutcome = "Unlinked: the cache is deleted.";
  return "Unlinked: the cache is deleted.";
}

void queue(const char* path) {
  std::lock_guard<std::mutex> lock(sLock);
  sPicked = path;
  sPending = true;
}

// the system's file picker (the answer: work())
void pick() {
  static SDL_DialogFileFilter kFilters[] = {{nullptr, "z64;n64;v64;bin;rom"}, {nullptr, "*"}};
  kFilters[0].name = portTr("Nintendo 64 ROM"); // (kept until the dialog answers)
  kFilters[1].name = portTr("All files");
  if (!portOffscreen() && sPicking.exchange(true)) { // (offscreen: not shown, never answered)
    return; // (one is out: its answer comes)
  }
  portShowOpenFileDialog(picked, nullptr, SDL_GetKeyboardFocus(), kFilters, 2, nullptr, false);
}

void section(int inGame) {
  (void)inGame;
  work();
  const int v = portPm64Linked();
  if (v != PM64_NONE) {
    ImGui::TextWrapped(portTr("Linked: Paper Mario (%s)."), portTr(portPm64VersionName(v)));
  } else {
    ImGui::TextWrapped("%s", portTr("Not linked."));
  }
  if (ImGui::Button(portTrLabel(v != PM64_NONE ? "Link another ROM...##pm64_pick" : "Choose your Paper Mario ROM...##pm64_pick"))) {
    pick();
  }
  if (v != PM64_NONE) {
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Unlink##pm64_unlink"))) {
      unlinkRom();
    }
  }
  if (sReading) {
    ImGui::TextWrapped("%s", portTr("Reading the ROM..."));
  } else if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// --- The Start menu's form ---

// N64 ROM files in the user folder, its pm64 folder and Downloads (looked for again every few seconds)
std::vector<std::string> sFound;
Uint64 sFoundAt;

void lookFor() {
  if (sFoundAt != 0 && SDL_GetTicks() - sFoundAt < 3000) {
    return;
  }
  sFoundAt = SDL_GetTicks();
  sFound.clear();
  std::vector<std::string> dirs = {portSettingsUserPath(), std::string(portSettingsUserPath()) + "/pm64"};
  if (const char* d = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)) {
    dirs.push_back(d);
  }
  for (const std::string& dir : dirs) {
    int n = 0;
    char** files = SDL_GlobDirectory(dir.c_str(), "*.*64", SDL_GLOB_CASEINSENSITIVE, &n);
    for (int i = 0; files != nullptr && i < n && sFound.size() < 8; i++) {
      const std::string name = files[i];
      const std::string ext = name.size() > 4 ? name.substr(name.size() - 4) : "";
      if (SDL_strcasecmp(ext.c_str(), ".z64") == 0 || SDL_strcasecmp(ext.c_str(), ".n64") == 0 ||
          SDL_strcasecmp(ext.c_str(), ".v64") == 0) {
        sFound.push_back(dir + "/" + name);
      }
    }
    SDL_free(files);
  }
}

enum { kRowState = 1, kRowPick, kRowUnlink, kRowMusic, kRowFound = 100 };
constexpr const char* kMusic[] = {"Off", "Paper Mario 64's", "Paper Mario 64's and the game's, by area"};
constexpr const char kStateNote[] = "Your own Paper Mario ROM (US, Japan, Europe or iQue), read on this device only "
                                    "into a cache in the user folder; the ROM itself isn't kept.";
constexpr const char kPickNote[] = "Opens the system's file picker. Or put the ROM in Paper Cut's user folder or in "
                                   "Downloads: it's listed below, to link with {A}.";
constexpr const char kFoundNote[] = "A ROM file found in the user folder or Downloads: {A} links it.";
constexpr const char kUnlinkNote[] = "Deletes the cache made from the ROM; the Paper Mario 64 settings wait until "
                                     "a ROM is linked again.";
constexpr const char kMusicNote[] = "With the ROM linked, each place and battle plays a fitting Paper Mario song, "
                                    "everywhere or in about half the areas (also Audio > Soundtrack).";

int formRows(PortPauseRow* out, int max) {
  work();
  lookFor();
  const int v = portPm64Linked();
  int n = 0;
  if (max < 6) {
    return 0;
  }
  out[n] = portPauseRow("Your Paper Mario ROM", kStateNote, v != PM64_NONE ? nullptr : "Not linked", kRowState);
  if (v != PM64_NONE) {
    std::snprintf(out[n].value, sizeof(out[n].value), "Paper Mario (%s)", portTrGame(portPm64VersionName(v)));
  }
  n++;
  if (sOutcome != nullptr) { // what the last try did (a refusal's reason)
    out[n++] = portPauseRow(sOutcome, sOutcome, nullptr, kRowState, PORT_PAUSE_ROW_DIM);
  }
  out[n++] = portPauseRow(v != PM64_NONE ? "Link another ROM..." : "Choose your Paper Mario ROM...", kPickNote, nullptr,
                          kRowPick);
  for (size_t i = 0; i < sFound.size() && n < max - 2; i++) {
    const std::string& path = sFound[i];
    const size_t slash = path.find_last_of("/\\");
    PortPauseRow r = portPauseRow(nullptr, kFoundNote, "Link", kRowFound + int(i));
    // the game's font draws the row as it is: a < would start a tag and UTF-8 shows as other
    // letters, so only plain ASCII is kept (anything else a ?)
    std::string name;
    for (const char c : path.substr(slash == std::string::npos ? 0 : slash + 1)) {
      const unsigned char u = static_cast<unsigned char>(c);
      if (u >= 0x80 && u < 0xC0) {
        continue; // (a UTF-8 letter's continuation: one ? a letter)
      }
      name += u < 0x20 || u >= 0x7F || c == '<' || c == '>' ? '?' : c;
    }
    std::snprintf(r.text, sizeof(r.text), "%s", name.c_str());
    out[n++] = r;
  }
  if (v != PM64_NONE) {
    out[n++] = portPauseRow("Unlink", kUnlinkNote, nullptr, kRowUnlink, PORT_PAUSE_ROW_CONFIRM);
  }
  const int* music = portSettingsFind("pm64_music");
  if (music != nullptr && n < max) {
    out[n++] = portPauseRow("Paper Mario 64 music", kMusicNote, kMusic[std::clamp(*music, 0, 2)], kRowMusic);
  }
  return n;
}

const char* formAct(int id, int dir) {
  if (id == kRowPick && dir == 0) {
    pick();
    return nullptr;
  }
  if (id == kRowUnlink && dir == 0) {
    return unlinkRom();
  }
  if (id >= kRowFound && id < kRowFound + int(sFound.size()) && dir == 0) {
    queue(sFound[size_t(id - kRowFound)].c_str()); // (as if picked: work() links it)
    return nullptr;
  }
  if (id == kRowMusic) { // (the Soundtrack row's three other keys as it sets them: soundtrackSet, music.cpp)
    int* music = portSettingsFind("pm64_music");
    if (music == nullptr) {
      return nullptr;
    }
    *music = (*music + (dir < 0 ? 2 : 1)) % 3;
    if (*music != 0) {
      if (int* k = portSettingsFind("music_shuffle")) {
        *k = 0;
      }
      if (int* k = portSettingsFind("custom_music")) {
        *k = 1;
      }
      if (int* k = portSettingsFind("music_shuffle_custom")) {
        *k = 0;
      }
    }
    portSettingsApply();
    portSettingsSave();
    return nullptr;
  }
  return nullptr;
}

// "Kooper joined too!": each PM64 partner as it joins with its TTYD counterpart (portPm64JoinNotice), one at a
// time, a few seconds each, at the top of the screen
void joinNotice(int pageOpen) {
  static int sShowing = 0;
  static double sStart = 0.0;
  const double now = ImGui::GetTime();
  (void)pageOpen;
  if (sShowing == 0 || now - sStart > 3.5) {
    sShowing = portPm64JoinNotice();
    sStart = now;
    if (sShowing == 0) {
      return;
    }
  }
  const double age = now - sStart;
  // the name put in by hand (a language pack's text is no format)
  std::string text = portTr("%s joined too!");
  const size_t at = text.find("%s");
  if (at != std::string::npos) {
    text.replace(at, 2, portPm64PartnerName(sShowing));
  }
  const float in = std::min(1.f, static_cast<float>(age) * 4.f);
  const float out = std::min(1.f, static_cast<float>(3.5 - age) * 2.f);
  const int al = static_cast<int>(std::max(0.f, std::min(in, out)) * 255);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize() * 1.6f;
  const ImVec2 t = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text.c_str());
  const ImVec2 size(t.x + fs * 2.f, t.y + fs * 0.8f);
  const ImVec2 display = ImGui::GetIO().DisplaySize; // (under the HUD's rows: port/overlay_place.h)
  // under an achievement's toast when one is up (practice.cpp's drawToast, the same spot: a boss's win brings its
  // achievement and its partner at once, and the toast hid "Lakilester joined too!"; tests/port/full_run/pm64/91p_*)
  double toastAge = 0.0;
  const float base = ImGui::GetFontSize() * 1.3f; // (the toast's: a title line, a name line 1.3 times it, padding)
  const float below = portAchievementToast(&toastAge) >= 0 ? base * 3.3f + fs * 0.4f : 0.f;
  const ImVec2 o(display.x * 0.5f - size.x * 0.5f,
                 portOverlayTopClear(fs * 0.5f, display.x, display.y, fs * 0.4f) + fs * in + below);
  dl->AddRectFilled(o, ImVec2(o.x + size.x, o.y + size.y), IM_COL32(20, 30, 60, al * 220 / 255), 8.f);
  dl->AddRect(o, ImVec2(o.x + size.x, o.y + size.y), IM_COL32(255, 255, 255, al), 8.f, 0, 2.f);
  dl->AddText(font, fs, ImVec2(o.x + fs, o.y + fs * 0.4f), IM_COL32(255, 255, 255, al), text.c_str());
}

// a file made with the ROM, played without it (portPm64UnlinkedNotice): one note per launch, a few seconds, at the
// top of the screen as the joining notice is
void unlinkedNote(int pageOpen) {
  static double sStart = -1.0;
  const double now = ImGui::GetTime();
  (void)pageOpen;
  if (sStart < 0.0) {
    if (!portPm64UnlinkedNotice()) {
      return;
    }
    sStart = now;
  }
  const double age = now - sStart;
  if (age > 6.0) {
    return;
  }
  const char* text = portTr("Paper Mario 64 isn't linked: its partners and items show as TTYD's until you link it again.");
  const float in = std::min(1.f, static_cast<float>(age) * 4.f);
  const float out = std::min(1.f, static_cast<float>(6.0 - age) * 2.f);
  const int al = static_cast<int>(std::max(0.f, std::min(in, out)) * 255);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize() * 1.2f;
  const float wrap = ImGui::GetIO().DisplaySize.x * 0.8f;
  const ImVec2 t = font->CalcTextSizeA(fs, FLT_MAX, wrap, text);
  const ImVec2 size(t.x + fs * 2.f, t.y + fs * 0.8f);
  const ImVec2 display = ImGui::GetIO().DisplaySize; // (under the HUD's rows: port/overlay_place.h)
  const ImVec2 o(display.x * 0.5f - size.x * 0.5f, portOverlayTopClear(fs * 0.5f, display.x, display.y, fs * 0.4f) + fs * in);
  dl->AddRectFilled(o, ImVec2(o.x + size.x, o.y + size.y), IM_COL32(60, 40, 20, al * 220 / 255), 8.f);
  dl->AddRect(o, ImVec2(o.x + size.x, o.y + size.y), IM_COL32(255, 255, 255, al), 8.f, 0, 2.f);
  dl->AddText(font, fs, ImVec2(o.x + fs, o.y + fs * 0.4f), IM_COL32(255, 255, 255, al), text, nullptr, wrap);
}

// online and in replays: this device's own files (a dimmed row there)
const PortPauseForm kForm = {"Enhancements", "Link Paper Mario 64", formRows, formAct, PORT_PAUSE_FORM_OWN};

// an N64 ROM's first word in each byte order (.z64, .v64, .n64)
bool romMagic(const unsigned char* b) {
  static const unsigned char kMagic[3][4] = {{0x80, 0x37, 0x12, 0x40}, {0x37, 0x80, 0x40, 0x12}, {0x40, 0x12, 0x37, 0x80}};
  for (const auto& m : kMagic) {
    if (std::memcmp(b, m, 4) == 0) {
      return true;
    }
  }
  return false;
}

bool romName(const char* path) {
  const size_t n = std::strlen(path);
  return n > 4 && (SDL_strcasecmp(path + n - 4, ".z64") == 0 || SDL_strcasecmp(path + n - 4, ".v64") == 0 ||
                   SDL_strcasecmp(path + n - 4, ".n64") == 0);
}

// SDL's drop events, in the launcher and over the game: a ROM dropped is linked (the mod manager
// leaves ROMs to this, mods_install.cpp); over the game the settings page opens on Paper Mario 64
int dropEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type != SDL_EVENT_DROP_FILE || e->drop.data == nullptr || !portPm64LinkIsRom(e->drop.data)) {
    return 0;
  }
  queue(e->drop.data);
  if (!gPortLauncherShown) {
    portSettingsOpenAt("Paper Mario 64");
  }
  return 1;
}

// each frame over the game: a ROM dropped while the page is closed is linked all the same
void frame(int pageOpen) {
  (void)pageOpen;
  if (!gPortLauncherShown) {
    work();
  }
}

struct Pm64LinkSettings {
  Pm64LinkSettings() {
    portSettingsAddSection("Enhancements", "Link Paper Mario 64",
                           "Your own Paper Mario ROM (US, Japan, Europe or iQue) for the N64 partners. It's read on "
                           "this device only, into a cache in the user folder (its sprites, text, moves and music); "
                           "nothing from it is uploaded, and the ROM itself isn't kept. Unlink deletes the cache.",
                           0, section, "paper mario 64 pm64 n64 rom link goombario kooper bombette parakarry watt sushie "
                           "lakilester partners");
    portSettingsDeclare("pm64_partners", 0, 0, 1);
    portSettingsAddToggle("Enhancements", "Paper Mario 64 partners",
                          "With your Paper Mario ROM linked: Goombario, Kooper, Bombette, Parakarry, Watt, Sushie, "
                          "Lakilester and Bow. Each joins you when a TTYD partner does (Goombario with Goombella, "
                          "Kooper with Koops, Bombette with Bobbery, Parakarry with Yoshi, Lakilester with Flurrie, "
                          "Sushie and Bow with Vivian, and Watt, in Ms. Mowz's place, after chapter 4 even without "
                          "her) and takes that partner's place and field ability, with "
                          "their own moves in battle. Chosen on the Partner wheel. Online, only when everyone has the "
                          "ROM linked.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_partners",
                          "paper mario 64 pm64 n64 partners goombario kooper bombette parakarry watt sushie lakilester bow");
    portSettingsDeclare("pm64_met_all", 0, 0, 1);
    portSettingsAddToggle("Enhancements", "Paper Mario 64 partners from the start",
                          "They're on the Partner wheel as soon as their TTYD partners have joined, without the "
                          "notices.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_met_all", "paper mario 64 pm64 partners start");
    // how they join (port/include/port/pm64.h portPm64JoinByBoss): on, once their TTYD partner has joined and their
    // chapter's Paper Mario boss is beaten (the boss list on this page, pm64_media.cpp); off, with the TTYD partner
    portSettingsDeclare("pm64_join", 1, 0, 1);
    portSettingsAddToggle("Enhancements", "Win Paper Mario partners from their bosses",
                          "Each Paper Mario partner waits with its chapter's boss, which comes to Rogueport as the "
                          "story reaches it (mostly once its TTYD partner has joined): beat the boss there or from the "
                          "Paper Mario 64 boss list, and the partner joins you.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_join",
                          "paper mario 64 pm64 partners join boss beat win jr troopa goombario koopa bros kooper");
    // their own field abilities (pm64_field_pc.c): 0 full (as in Paper Mario), 1 only what the TTYD partner reaches
    static const char* const kField[] = {"Full", "Like their partner"};
    portSettingsDeclare("pm64_field", 0, 0, 1);
    portSettingsAddChoice("Enhancements", "Paper Mario field abilities",
                          "Full: Parakarry's flutter flies three times as far, Lakilester floats over spikes and lava "
                          "unhurt, and Sushie carries Mario across deep water, so some parts of the game can be "
                          "skipped (never to a place with no way back). Like their partner: each reaches only what the "
                          "TTYD partner in their place does (Watt's light and sparks stay).",
                          PORT_SETTING_CHANGES_PLAY, "pm64_field", kField, 2,
                          "paper mario 64 pm64 partners field abilities parakarry flutter lakilester spikes lava "
                          "sushie swim water skip");
    portSettingsAddOverlay(joinNotice, nullptr);
    portSettingsAddOverlay(unlinkedNote, nullptr);
    portPauseAddForm(&kForm);
    portSettingsAddOverlay(frame, dropEvent);
  }
} sPm64Link;

} // namespace

extern "C" int portPm64LinkIsRom(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return 0;
  }
  if (romName(path)) {
    return 1; // (named as one: linking it says what's wrong with it, if anything)
  }
  unsigned char head[4];
  SDL_IOStream* io = SDL_IOFromFile(path, "rb"); // (Android's content:// URIs, often without a name)
  if (io == nullptr) {
    return 0;
  }
  const size_t got = SDL_ReadIO(io, head, sizeof(head));
  SDL_CloseIO(io);
  return got == sizeof(head) && romMagic(head) ? 1 : 0;
}

extern "C" void portPm64LinkQueue(const char* path) {
  if (path != nullptr && path[0] != '\0') {
    queue(path);
  }
}

extern "C" void portPm64LinkPick(void) {
  pick();
}

extern "C" const char* portPm64LinkPoll(void) {
  work();
  return sShort.c_str();
}

extern "C" const char* portPm64LinkFound(int i) {
  lookFor();
  return i >= 0 && i < static_cast<int>(sFound.size()) ? sFound[size_t(i)].c_str() : nullptr;
}
