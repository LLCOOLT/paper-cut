// Unlimited save files and settings per save file (port/include/port/save_files.h).
//
// The library: every save the game writes into one of the memory card's four
// files is also kept here, as that slot's GameCube image, under the save
// file's own id:
//   <user>/Save files/<JAP|USA|EUR>/<id>/save.bin   the slot (0x4000 bytes)
//                                       /info.txt   name=<the player's name for it>
//                                       /thumb.bmp  where it was saved
// The id lives in the slot itself, in Paper Cut's block (cardsave_pc.h), so it
// travels with the card: a file loaded and saved again updates its own entry;
// a copy (the file select's Copy) is a new file. Any entry goes back into one
// of the card's files (the save there kept in the library first), the card
// staying a .gci Dolphin reads; Dolphin and the GameCube ignore the block.
//
// Settings per save file: the difficulty, the Enhancements (Gameplay+) and
// the challenge rules are written into the same block at each save and set
// again when that file is loaded (cardCopy2Main), so each file plays by its own
// rules; display, sound and controls stay global. The block is part of the
// card's bytes, which online's setup sends to the guest, and both games load
// the file in the same frame: the same settings on both sides.
#include "port/cardsave_pc.h"
#include "port/restart.h"
#include "port/save_convert.h"
#include "port/save_files.h"
#include "port/save_transfer.h"
#include "port/settings.h"
#include "port/tr.h"
extern "C" {
#include "port/gameplus.h"
#include "port/online.h"
#include "port/replay.h"
}
#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <aurora/picture.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr size_t kSlot = PORT_CARD_SLOT_SIZE, kGciHeader = 0x40, kMeta = PORT_CARD_META_SIZE;

// the settings that belong to each save file: the Enhancements (gameplus_pc.c,
// gameplus2_pc.c; Play as Luigi stays the player's), difficulty and the
// challenge rules (challenge_pc.c, the Challenges tab's rules)
const char* const kPerFile[] = {
    "quick_swap", "bigger_bag", "partner_revive", "levelup_dpad", "heal_all_partners", "save_no_prompt",
    "difficulty", "difficulty_enemy_hp", "difficulty_enemy_attack", "ac_attack_assist", "ac_guard_assist",
    "remake_balance", "fast_battle_menus", "gameover_retry", "skip_scenes", "fast_travel", "new_game_plus",
    "pit_plus", "glitz_pit_kind", "battle_popups", "free_tattle", "new_game_plus_from", "badge_loadouts",
    "storage_anywhere", "walk_boost", "walk_boost_speed", "coin_multiplier", "exp_multiplier",
    "collectible_tracker", "fast_field_text", "sell_helper", "challenge_ohko", "challenge_guard",
    "challenge_commands", "challenge_no_battle_items", "remix_enemies", "remix_badge_draft", "remix_seed",
};

int* sLibrary; // keep every save in the library
int* sPerFile; // settings per save file

const char* regionDir() {
  const char r = PORT_GAME_ID[3];
  return r == 'J' ? "JAP" : r == 'P' ? "EUR" : "USA";
}

std::string libraryDir() { return std::string(portSettingsUserPath()) + "/Save files/" + regionDir(); }
std::string entryDir(const std::string& id) { return libraryDir() + "/" + id; }

std::string cardPath() {
  char name[64];
  portSaveFileName(PORT_GAME_ID[3], name, sizeof(name));
  return portSavesCardDir(PORT_GAME_ID[3]) + "/" + name;
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

bool writeAll(const std::string& path, const void* data, size_t size) {
  SDL_IOStream* f = SDL_IOFromFile((path + ".tmp").c_str(), "wb");
  const bool ok = f != nullptr && SDL_WriteIO(f, data, size) == size;
  if (f != nullptr) {
    SDL_CloseIO(f);
  }
  if (ok && SDL_RenamePath((path + ".tmp").c_str(), path.c_str())) {
    return true;
  }
  SDL_RemovePath((path + ".tmp").c_str());
  return false;
}

// --- the block's lines ---

std::string lineOf(const std::string& text, const std::string& key) {
  const std::string at = "\n" + key + "=";
  const size_t p = text.find(at);
  if (p == std::string::npos) {
    return {};
  }
  const size_t from = p + at.size(), end = text.find('\n', from);
  return text.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

std::string newId() {
  SDL_Time now = 0;
  SDL_GetCurrentTime(&now);
  char id[32];
  std::snprintf(id, sizeof(id), "%012llx%04x", static_cast<unsigned long long>(now / 1000000) & 0xFFFFFFFFFFFFull,
                static_cast<unsigned>(SDL_rand_bits() & 0xFFFF));
  return id;
}

bool validId(const std::string& id) {
  return !id.empty() && id.size() <= 24 &&
         std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// gPortCardExtraForSave: the block for a save (or a copy) into slot
void extraForSave(int slot, const char* oldText, int copy, char* out, int cap) {
  (void)slot;
  const std::string old = oldText != nullptr ? oldText : "";
  std::string id = lineOf(old, "id");
  if (copy || !validId(id)) {
    id = newId();
  }
  std::string text = PORT_CARD_EXTRA_MAGIC;
  text += "id=" + id + "\n";
  if (copy) {
    // the copy plays as the file it came from
    size_t at = old.find('\n');
    while (at != std::string::npos && at + 1 < old.size()) {
      const size_t end = old.find('\n', at + 1);
      const std::string line = old.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
      if (line.rfind("s.", 0) == 0 || line.rfind("char=", 0) == 0) {
        text += line + "\n";
      }
      at = end;
    }
  } else {
    text += std::string("char=") + (portGameplus("playable_luigi") ? "luigi" : "mario") + "\n";
    if (*sPerFile) {
      for (const char* key : kPerFile) {
        if (const int* v = portSettingsFind(key)) {
          text += std::string("s.") + key + "=" + std::to_string(*v) + "\n";
        }
      }
    }
  }
  if (static_cast<int>(text.size()) >= cap) {
    text.resize(static_cast<size_t>(cap - 1));
  }
  std::memcpy(out, text.c_str(), text.size() + 1);
}

std::string sLoadedId; // the save file being played

// gPortCardExtraApply: a file loaded; its own settings set again
void extraApply(int slot, const char* textC) {
  (void)slot;
  const std::string text = textC != nullptr ? textC : "";
  sLoadedId = lineOf(text, "id");
  if (!*sPerFile || text.find("\ns.") == std::string::npos) {
    return; // a file saved before this (or elsewhere): the settings as they are
  }
  const bool guest = portOnlineActive() && !portOnlineIsHost();
  bool changed = false;
  for (const char* key : kPerFile) {
    const std::string v = lineOf(text, std::string("s.") + key);
    int* now = portSettingsFind(key);
    if (v.empty() || now == nullptr || std::to_string(*now) == v) {
      continue;
    }
    if (guest) {
      portSettingsSetForRun(key, v.c_str());
    } else {
      portSettingsSetKey(key, v.c_str());
    }
    changed = true;
  }
  if (changed && !guest) {
    portSettingsSave();
  }
}

// --- the library ---

bool sDirty = true; // the list to read again

// the thumbnail: the picture as shown when the game saved, made small on the
// capture's thread and written there
struct ThumbJob {
  std::string path;
};

void SDLCALL thumbRead(void* user, const unsigned char* rgba, unsigned int w, unsigned int h) {
  ThumbJob* job = static_cast<ThumbJob*>(user);
  if (rgba != nullptr && w > 0 && h > 0) {
    const int tw = 192, th = std::max(1, std::min(144, static_cast<int>(192ull * h / w)));
    SDL_Surface* s = SDL_CreateSurface(tw, th, SDL_PIXELFORMAT_RGB24);
    if (s != nullptr) {
      for (int y = 0; y < th; y++) {
        uint8_t* row = static_cast<uint8_t*>(s->pixels) + static_cast<size_t>(y) * s->pitch;
        for (int x = 0; x < tw; x++) {
          // a box filter over the source pixels under this one
          const unsigned x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw);
          const unsigned y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
          unsigned sum[3] = {0, 0, 0}, n = 0;
          for (unsigned sy = y0; sy < y1; sy += 2) {
            for (unsigned sx = x0; sx < x1; sx += 2) {
              const unsigned char* p = rgba + (static_cast<size_t>(sy) * w + sx) * 4;
              sum[0] += p[0];
              sum[1] += p[1];
              sum[2] += p[2];
              n++;
            }
          }
          for (int c = 0; c < 3; c++) {
            row[x * 3 + c] = static_cast<uint8_t>(sum[c] / std::max(n, 1u));
          }
        }
      }
      SDL_SaveBMP(s, job->path.c_str());
      SDL_DestroySurface(s);
      sDirty = true;
    }
  }
  delete job;
}

bool keepImage(const std::string& id, const uint8_t* image) {
  const std::string dir = entryDir(id);
  SDL_CreateDirectory(dir.c_str());
  if (!writeAll(dir + "/save.bin", image, kSlot)) {
    std::fprintf(stderr, "save files: can't write %s: %s\n", dir.c_str(), SDL_GetError());
    return false;
  }
  sDirty = true;
  return true;
}

// gPortCardSlotWritten: a save into the card's file slot, kept in the library
void slotWritten(int slot, const uint8_t* image) {
  (void)slot;
  if (!*sLibrary || (portOnlineActive() && !portOnlineIsHost()) || portReplayPlaying()) {
    return; // the guest plays on the host's card; a playback writes nothing of its own
  }
  PortSaveInfo info;
  const char* extra = portCardImageExtra(image);
  const std::string id = extra != nullptr ? lineOf(extra, "id") : std::string();
  if (!validId(id) || !portSaveImageInfo(image, &info)) {
    return; // an erase, or a copy's empty slot
  }
  if (!keepImage(id, image)) {
    return;
  }
  ThumbJob* job = new ThumbJob{entryDir(id) + "/thumb.bmp"};
  if (!aurora_capture_picture(thumbRead, job)) {
    delete job; // a screenshot is being taken: the thumbnail from before stays
  }
}

struct Entry {
  std::string id, name;
  PortSaveInfo info;
  std::string character;
  SDL_Time modified = 0;
  int cardFile = -1; // in the card's File 1-4 (0-3) now
};

std::vector<Entry> sEntries;
std::string sStatus;

std::string infoName(const std::string& id) {
  const std::vector<uint8_t> bytes = readAll(entryDir(id) + "/info.txt");
  const std::string text = "\n" + std::string(bytes.begin(), bytes.end());
  std::string name = lineOf(text, "name");
  if (!name.empty() && name.back() == '\r') {
    name.pop_back();
  }
  return name;
}

// the card's four files' ids (empty where none or not a save)
std::vector<std::string> cardIds(const std::vector<uint8_t>& gci) {
  std::vector<std::string> ids(4);
  for (int i = 0; i < 4; i++) {
    const size_t at = kGciHeader + kMeta + i * kSlot;
    if (gci.size() >= at + kSlot) {
      if (const char* extra = portCardImageExtra(gci.data() + at)) {
        ids[i] = lineOf(extra, "id");
      }
    }
  }
  return ids;
}

void readList() {
  sDirty = false;
  sEntries.clear();
  const std::string dir = libraryDir();
  const std::vector<std::string> ids = cardIds(readAll(cardPath()));
  int count = 0;
  char** found = SDL_GlobDirectory(dir.c_str(), "*", 0, &count);
  for (int i = 0; found != nullptr && i < count; i++) {
    const std::string id = found[i];
    SDL_PathInfo pi;
    if (!validId(id) || !SDL_GetPathInfo((dir + "/" + id + "/save.bin").c_str(), &pi)) {
      continue;
    }
    const std::vector<uint8_t> image = readAll(dir + "/" + id + "/save.bin");
    Entry e;
    e.id = id;
    e.modified = pi.modify_time;
    if (image.size() != kSlot || !portSaveImageInfo(image.data(), &e.info)) {
      continue;
    }
    const char* extra = portCardImageExtra(image.data());
    e.character = extra != nullptr ? lineOf(extra, "char") : std::string();
    e.name = infoName(id);
    for (int k = 0; k < 4; k++) {
      if (ids[k] == id) {
        e.cardFile = k;
      }
    }
    sEntries.push_back(e);
  }
  SDL_free(found);
  std::sort(sEntries.begin(), sEntries.end(), [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
}

// a card slot's save not in the library yet, kept there with a new id
// (written into the image's block); returns the id ("" for none)
std::string adopt(std::vector<uint8_t>& gci, int file) {
  const size_t at = kGciHeader + kMeta + file * kSlot;
  PortSaveInfo info;
  if (gci.size() < at + kSlot || !portSaveImageInfo(gci.data() + at, &info)) {
    return {};
  }
  const char* extra = portCardImageExtra(gci.data() + at);
  std::string id = extra != nullptr ? lineOf(extra, "id") : std::string();
  SDL_PathInfo pi;
  if (validId(id) && SDL_GetPathInfo((entryDir(id) + "/save.bin").c_str(), &pi)) {
    return id; // there already
  }
  std::string text = extra != nullptr ? std::string(extra) : std::string(PORT_CARD_EXTRA_MAGIC);
  if (!validId(id)) {
    id = newId();
    text = PORT_CARD_EXTRA_MAGIC + ("id=" + id + "\n") + text.substr(sizeof(PORT_CARD_EXTRA_MAGIC) - 1);
  }
  std::vector<uint8_t> image(gci.begin() + long(at), gci.begin() + long(at + kSlot));
  std::memset(image.data() + PORT_CARD_EXTRA_AT, 0, PORT_CARD_EXTRA_SIZE);
  std::memcpy(image.data() + PORT_CARD_EXTRA_AT, text.c_str(), std::min(text.size(), size_t(PORT_CARD_EXTRA_SIZE - 1)));
  // the card's copy carries the id too (main and backup), so its next save updates this entry
  std::memcpy(gci.data() + at, image.data(), kSlot);
  if (gci.size() >= at + 4 * kSlot + kSlot) {
    std::memcpy(gci.data() + at + 4 * kSlot, image.data(), kSlot);
  }
  return keepImage(id, image.data()) ? id : std::string();
}

// the card's saves the library hasn't got, added
int adoptAll() {
  std::vector<uint8_t> gci = readAll(cardPath());
  const std::vector<std::string> before = cardIds(gci);
  int added = 0;
  for (int k = 0; k < 4; k++) {
    SDL_PathInfo pi;
    const bool had = validId(before[k]) && SDL_GetPathInfo((entryDir(before[k]) + "/save.bin").c_str(), &pi);
    if (!had && !adopt(gci, k).empty()) {
      added++;
    }
  }
  return added;
}

// The library's file id put into the card's file (0-3): what waits for the
// card when the game runs (written once it isn't writing, then a restart)
struct Placing {
  std::string id;
  int file = 0;
} sPlacing;

bool place(const std::string& id, int file) {
  std::vector<uint8_t> gci = readAll(cardPath());
  const std::vector<uint8_t> image = readAll(entryDir(id) + "/save.bin");
  const size_t at = kGciHeader + kMeta + file * kSlot;
  if (gci.size() < at + 4 * kSlot + kSlot) {
    sStatus = "The memory card has no save file yet: start a game and save once first.";
    return false;
  }
  if (image.size() != kSlot) {
    sStatus = "That save file can't be read.";
    return false;
  }
  adopt(gci, file); // the save there now stays in the library
  std::memcpy(gci.data() + at, image.data(), kSlot);
  std::memcpy(gci.data() + at + 4 * kSlot, image.data(), kSlot);
  std::string why;
  char name[64];
  portSaveFileName(PORT_GAME_ID[3], name, sizeof(name));
  if (!portSavesWriteFile(PORT_GAME_ID[3], name, gci, why)) {
    sStatus = "Could not write the memory card: " + why;
    return false;
  }
  char text[96];
  std::snprintf(text, sizeof(text), portTr("Put in File %d."), file + 1);
  sStatus = text;
  sDirty = true;
  return true;
}

void removeEntry(const std::string& id) {
  const std::string dir = entryDir(id);
  for (const char* f : {"/save.bin", "/info.txt", "/thumb.bmp"}) {
    SDL_RemovePath((dir + f).c_str());
  }
  SDL_RemovePath(dir.c_str());
  sDirty = true;
}

// --- thumbnails for the page ---

struct Thumb {
  SDL_Time modified = -1;
  ImTextureID tex{};
  int w = 0, h = 0;
};
std::map<std::string, Thumb> sThumbs;

const Thumb* thumbOf(const std::string& id) {
  const std::string path = entryDir(id) + "/thumb.bmp";
  SDL_PathInfo pi;
  if (!SDL_GetPathInfo(path.c_str(), &pi)) {
    return nullptr;
  }
  Thumb& t = sThumbs[id];
  if (t.modified != pi.modify_time) {
    t.modified = pi.modify_time; // (Aurora keeps a texture for the run: one per change)
    SDL_Surface* s = SDL_LoadBMP(path.c_str());
    SDL_Surface* rgba = s != nullptr ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (rgba != nullptr && rgba->pitch == rgba->w * 4) {
      t.tex = aurora_imgui_add_texture(static_cast<uint32_t>(rgba->w), static_cast<uint32_t>(rgba->h), rgba->pixels);
      t.w = rgba->w;
      t.h = rgba->h;
    }
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(s);
  }
  return t.w > 0 ? &t : nullptr;
}

std::string playTime(long long seconds) {
  char text[32];
  std::snprintf(text, sizeof(text), "%lld:%02lld", seconds / 3600, (seconds / 60) % 60);
  return text;
}

std::string savedDate(long long gcSeconds) {
  // OSTime counts from 2000-01-01; SDL_Time from 1970-01-01
  const SDL_Time t = static_cast<SDL_Time>(gcSeconds + 946684800ll) * 1000000000ll;
  SDL_DateTime dt;
  if (gcSeconds <= 0 || !SDL_TimeToDateTime(t, &dt, false)) {
    return {};
  }
  char text[40];
  std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
  return text;
}

const char* chapterName(int chapter) {
  static const char* const kNames[] = {"Prologue",  "Chapter 1", "Chapter 2", "Chapter 3", "Chapter 4",
                                       "Chapter 5", "Chapter 6", "Chapter 7", "Chapter 8", "The end"};
  return portTr(kNames[std::clamp(chapter, 0, 9)]);
}

void librarySection(int inGame) {
  static Uint64 sRead;
  static std::string sConfirm, sRenaming;
  static char sName[64];
  static int sFile = 0;
  if (sDirty || SDL_GetTicks() - sRead > 5000) {
    readList();
    sRead = SDL_GetTicks();
  }
  if (ImGui::Button(portTrLabel("Add the card's saves to the library"))) {
    const int added = adoptAll();
    char text[96];
    std::snprintf(text, sizeof(text), portTr("%d added."), added);
    sStatus = text;
    // (the card's own copy gets its id at its next save)
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
  const char* files[] = {"File 1", "File 2", "File 3", "File 4"};
  const char* filesTr[4];
  for (int i = 0; i < 4; i++) {
    filesTr[i] = portTr(files[i]);
  }
  ImGui::SameLine();
  ImGui::Combo(portTrLabel("Play in"), &sFile, filesTr, 4);
  const bool locked = inGame && !portRestartSupported();
  if (sEntries.empty()) {
    ImGui::TextDisabled("%s", portTr("No save files yet: each save the game writes is kept here."));
  }
  const float thumbH = ImGui::GetFontSize() * 4.5f;
  for (const Entry& e : sEntries) {
    ImGui::PushID(e.id.c_str());
    ImGui::Separator();
    if (const Thumb* t = thumbOf(e.id)) {
      ImGui::Image(t->tex, ImVec2(thumbH * t->w / t->h, thumbH));
    } else {
      ImGui::Dummy(ImVec2(thumbH * 4 / 3, thumbH));
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    const std::string title = !e.name.empty() ? e.name : e.info.name;
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextDisabled("%s  ·  %s %d  ·  %s  ·  %s", chapterName(e.info.chapter), portTr("Level"), e.info.level,
                        playTime(e.info.seconds).c_str(),
                        portTr(e.character == "luigi" ? "Luigi" : "Mario"));
    const std::string date = savedDate(e.info.saveTime);
    if (e.cardFile >= 0) {
      ImGui::TextDisabled(portTr("Saved %s on %s · in File %d"), date.c_str(), e.info.map, e.cardFile + 1);
    } else {
      ImGui::TextDisabled(portTr("Saved %s on %s"), date.c_str(), e.info.map);
    }
    ImGui::BeginDisabled(locked || portRestartWaiting());
    const bool confirmPlace = sConfirm == "p" + e.id;
    char label[96];
    std::snprintf(label, sizeof(label), "%s##place",
                  portTr(confirmPlace ? (inGame ? "Replace that file and restart" : "Replace that file")
                                      : (inGame ? "Play in that file and restart" : "Play in that file")));
    if (ImGui::SmallButton(label)) {
      if (!confirmPlace) {
        sConfirm = "p" + e.id;
      } else {
        sConfirm.clear();
        if (inGame) {
          sPlacing = Placing{e.id, sFile};
          portRestartRequestWith(0, [] { return place(sPlacing.id, sPlacing.file) ? 1 : 0; });
        } else {
          place(e.id, sFile);
        }
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Rename"))) {
      sRenaming = e.id;
      std::snprintf(sName, sizeof(sName), "%s", title.c_str());
    }
    ImGui::SameLine();
    const bool confirmDelete = sConfirm == "d" + e.id;
    if (ImGui::SmallButton(portTrLabel(confirmDelete ? "Delete it for good" : "Delete"))) {
      if (!confirmDelete) {
        sConfirm = "d" + e.id;
      } else {
        sConfirm.clear();
        removeEntry(e.id);
      }
    }
    if (confirmDelete && e.cardFile >= 0) {
      ImGui::TextDisabled(portTr("It stays in File %d of the memory card."), e.cardFile + 1);
    }
    if (sRenaming == e.id) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
      const bool enter = ImGui::InputText("##name", sName, sizeof(sName), ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      if (ImGui::SmallButton(portTrLabel("Save##name")) || enter) {
        const std::string text = std::string("name=") + sName + "\n";
        writeAll(entryDir(e.id) + "/info.txt", text.data(), text.size());
        sRenaming.clear();
        sDirty = true;
      }
    }
    ImGui::EndGroup();
    ImGui::PopID();
  }
  if (locked) {
    ImGui::TextDisabled("%s", portTr("Put a file in the memory card from the launcher, before the game starts."));
  } else if (portRestartWaiting()) {
    ImGui::TextDisabled("%s", portTr("Restarting when the game has finished saving..."));
  }
  if (!sStatus.empty()) {
    ImGui::TextUnformatted(portTr(sStatus.c_str()));
  }
}

struct SaveFiles {
  SaveFiles() {
    sLibrary = portSettingsDeclare("save_library", 1, 0, 1);
    sPerFile = portSettingsDeclare("settings_per_file", 1, 0, 1);
    gPortCardExtraForSave = extraForSave;
    gPortCardExtraApply = extraApply;
    gPortCardSlotWritten = slotWritten;
    portSettingsAddToggle("Saves", "Keep every save file",
                          "Each save the game writes is also kept in the library below, with a picture of where it "
                          "was made, so there is no limit of four files.",
                          0, "save_library", "unlimited save files library slots named thumbnail");
    portSettingsAddToggle("Saves", "Settings per save file",
                          "The difficulty, the Enhancements and the challenge rules are saved with each file and come "
                          "back when it is loaded. Display, sound and controls stay the same for every file.",
                          PORT_SETTING_CHANGES_PLAY, "settings_per_file",
                          "per file save difficulty challenge enhancements gameplay+");
    portSettingsAddSection("Saves", "Save files", nullptr, 0, librarySection,
                           "unlimited save files library slots named thumbnail chapter play time");
  }
} sSaveFiles;

} // namespace
