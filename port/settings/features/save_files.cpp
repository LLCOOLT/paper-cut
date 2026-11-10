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
#include "port/area_maps.h"
#include "port/cardsave_pc.h"
#include "port/restart.h"
#include "port/resume.h"
#include "port/save_convert.h"
#include "port/save_files.h"
#include "port/save_transfer.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/together.h"
#include "port/tr.h"
extern "C" {
#include "port/gameplus.h"
#include "port/replay.h"
int portPlayAs(void); // port/include/port/luigi.h
}
#include "../file_util.h"
#include "../launcher_shot.h"
#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <aurora/picture.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    "coin_cap", "full_bag_use", "levelup_heal_party", // (qol_pc.c)
};

// the per-file settings 0.5.0 added: a file saved before has none of their lines
bool newIn050(const char* key) {
  return !std::strcmp(key, "coin_cap") || !std::strcmp(key, "full_bag_use") || !std::strcmp(key, "levelup_heal_party");
}

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

using portfile::readAll;

using portfile::writeAll;

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
    // who was played (Play as)
    static const char* const kChar[] = {"mario", "luigi", "peach", "bowser"};
    const int pick = portPlayAs(); // (luigi_pc.c: none while Brothers mode is on)
    text += std::string("char=") + kChar[pick > 0 && pick < 4 ? pick : 0] + "\n";
    if (*sPerFile) {
      for (const char* key : kPerFile) {
        int own = 0;
        const int held = portStateSyncRuleOwn(key, &own);
        if (held == 2) {
          continue; // (a session's rule the player has no value of: never the host's into the player's save)
        }
        if (const int* v = portSettingsFind(key)) {
          text += std::string("s.") + key + "=" + std::to_string(held == 1 ? own : *v) + "\n";
        }
      }
    }
    std::printf("[savefiles] file %s saved %s\n", id.c_str(),
                *sPerFile ? "with its own settings" : "without settings (settings per file off)");
  }
  if (static_cast<int>(text.size()) >= cap) {
    text.resize(static_cast<size_t>(cap - 1));
  }
  std::memcpy(out, text.c_str(), text.size() + 1);
}

std::string sLoadedId; // the save file being played

extern "C" int portConsoleAccurateCovers(const char* key); // console_accurate.cpp

// gPortCardExtraApply: a file loaded; its own settings set again
void extraApply(int slot, const char* textC) {
  (void)slot;
  const std::string text = textC != nullptr ? textC : "";
  sLoadedId = lineOf(text, "id");
  if (!*sPerFile || text.find("\ns.") == std::string::npos) {
    return; // a file saved before this (or elsewhere): the settings as they are
  }
  bool changed = false;
  for (const char* key : kPerFile) {
    std::string v = lineOf(text, std::string("s.") + key);
    int* now = portSettingsFind(key);
    if (v.empty() && newIn050(key)) {
      // a file with per-file settings saved before 0.5.0 had these, played on what they were then (their
      // defaults): not the last file's or a session's left in memory
      int def = 0;
      if (portSettingsExtraDefault(key, &def)) {
        v = std::to_string(def);
      }
    }
    if (v.empty() || now == nullptr || portConsoleAccurateCovers(key)) {
      continue; // (console-accurate mode's locked ones stay its originals: console_accurate.cpp)
    }
    if (portStateSyncRuleSetOwn(key, std::atoi(v.c_str()))) {
      changed = true; // (a session's rule: the file's value is the player's own after it, the host's now)
      continue;
    }
    if (std::to_string(*now) == v) {
      continue;
    }
    portSettingsSetKey(key, v.c_str());
    changed = true;
  }
  if (changed) {
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
  if (!*sLibrary || portReplayPlaying()) {
    if (!*sLibrary) {
      std::printf("[savefiles] slot %d: not kept (Keep every save file is off)\n", slot);
    }
    return; // a playback writes nothing of its own
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
  std::printf("[savefiles] slot %d: file %s kept in the library\n", slot, id.c_str());
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
// (written into the image's block), or newer than the library's copy of
// its id, kept over it; returns the id ("" for none)
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
    // there already; but the card's may be the newer (saved while Keep every save file was off, or its
    // keep failed): then the library takes it, so nothing put over the card brings back the older one
    const std::vector<uint8_t> kept = readAll(entryDir(id) + "/save.bin");
    PortSaveInfo old;
    const bool unreadable = kept.size() != kSlot || !portSaveImageInfo(kept.data(), &old);
    const bool newer = unreadable || (info.saveCount != old.saveCount ? info.saveCount > old.saveCount
                                                                      : info.saveTime > old.saveTime);
    if (newer && (kept.size() != kSlot || std::memcmp(kept.data(), gci.data() + at, kSlot) != 0) &&
        keepImage(id, gci.data() + at)) {
      std::printf("[savefiles] file %d: the card's newer save kept over the library's (id %s)\n", file + 1,
                  id.c_str());
      std::fflush(stdout);
    }
    return id;
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

// The records kept beside the card by its file's number (achievements, stats, area maps,
// stickers, the Enhancements' loadouts and tracks heard, a chapter replay's kept state): they
// leave the card with the save that goes to the library, in its entry's slot/ folder, and come
// back with it, so a file put in place never takes over (or resets) the records of the one it
// replaces. In the user folder, then their names in the entry.
struct SlotRecord {
  std::string path, kept;
};
std::vector<SlotRecord> slotRecords(int file) {
  const std::string user = portSettingsUserPath(), game = PORT_GAME_ID, n = std::to_string(file + 1);
  const char r = PORT_GAME_ID[3];
  const std::string v = r == 'J' ? "jp" : r == 'P' ? "eu" : "us";
  return {
      {user + "/Achievements/" + game + "/file" + n + ".txt", "achievements.txt"},       // achievements_pc.c
      {user + "/Stats/" + game + "/file" + n + ".txt", "stats.txt"},                     // stats_pc.c
      {user + "/AreaMaps/" + game + "/file" + n + ".txt", "area_maps.txt"},              // area_maps_pc.c
      {user + "/Stickers/" + game + "/file" + n + ".txt", "stickers.txt"},               // sticker_pc.c
      {user + "/gameplus_" + v + "_file" + n + ".txt", "gameplus.txt"},                  // gameplus2_pc.c
      {user + "/chapter_replay_" + std::to_string(file) + ".bin", "chapter_replay.bin"}, // chapters_aurora.c
  };
}

bool isFile(const std::string& path) {
  SDL_PathInfo pi;
  return SDL_GetPathInfo(path.c_str(), &pi) && pi.type == SDL_PATHTYPE_FILE;
}

// file's records: out's (the save leaving; "" when none could be kept) into its entry, in's
// (the save put there) from its entry
void swapSlotRecords(const std::string& out, const std::string& in, int file) {
  if (out == in) {
    return;
  }
  for (const SlotRecord& r : slotRecords(file)) {
    const bool here = isFile(r.path);
    if (!out.empty()) {
      const std::string dir = entryDir(out) + "/slot", kept = dir + "/" + r.kept;
      if (here) {
        const std::vector<uint8_t> bytes = readAll(r.path);
        SDL_CreateDirectory(dir.c_str());
        if (!writeAll(kept, bytes.data(), bytes.size())) {
          std::fprintf(stderr, "save files: can't keep %s: %s\n", r.path.c_str(), SDL_GetError());
          continue; // left where it is
        }
      } else {
        SDL_RemovePath(kept.c_str()); // none now: none kept from before either
      }
    } else if (here) {
      // no entry to keep them in (the slot's save couldn't be read): kept beside, not lost
      SDL_RemovePath((r.path + ".bak").c_str());
      SDL_RenamePath(r.path.c_str(), (r.path + ".bak").c_str());
    }
    const std::string from = entryDir(in) + "/slot/" + r.kept;
    if (isFile(from)) {
      const std::vector<uint8_t> bytes = readAll(from);
      SDL_CreateDirectory(r.path.substr(0, r.path.find_last_of('/')).c_str());
      writeAll(r.path, bytes.data(), bytes.size());
    } else {
      SDL_RemovePath(r.path.c_str()); // a file with no records yet starts its own
    }
  }
}

bool removeEntry(const std::string& id);

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
  // An id is one save file's: one already in another of the card's files goes in as a copy with an id of
  // its own (as the file select's Copy makes one), so the two files never save over one library entry.
  // The copy starts with the records that file has now (achievements, stats, maps, stickers).
  int otherFile = -1;
  {
    const std::vector<std::string> ids = cardIds(gci);
    for (int k = 0; k < 4; k++) {
      if (k != file && ids[k] == id) {
        otherFile = k;
      }
    }
  }
  std::string placed = id;
  std::vector<uint8_t> copy = image;
  if (otherFile >= 0) {
    char block[PORT_CARD_EXTRA_SIZE];
    const char* old = portCardImageExtra(image.data());
    block[0] = '\0';
    extraForSave(file, old != nullptr ? old : "", 1, block, static_cast<int>(sizeof(block)));
    std::memset(copy.data() + PORT_CARD_EXTRA_AT, 0, PORT_CARD_EXTRA_SIZE);
    std::memcpy(copy.data() + PORT_CARD_EXTRA_AT, block,
                std::min(std::strlen(block), static_cast<size_t>(PORT_CARD_EXTRA_SIZE - 1)));
    placed = lineOf(block, "id");
    if (!validId(placed) || placed == id || !keepImage(placed, copy.data())) {
      sStatus = "Could not keep it in Save files.";
      return false;
    }
    for (const char* f : {"/info.txt", "/thumb.bmp"}) {
      const std::vector<uint8_t> bytes = readAll(entryDir(id) + f);
      if (!bytes.empty()) {
        writeAll(entryDir(placed) + f, bytes.data(), bytes.size());
      }
    }
    const std::vector<SlotRecord> from = slotRecords(otherFile), to = slotRecords(file);
    SDL_CreateDirectory((entryDir(placed) + "/slot").c_str());
    for (size_t r = 0; r < from.size() && r < to.size(); r++) {
      if (isFile(from[r].path)) {
        const std::vector<uint8_t> bytes = readAll(from[r].path);
        writeAll(entryDir(placed) + "/slot/" + to[r].kept, bytes.data(), bytes.size());
      }
    }
  }
  const std::string out = adopt(gci, file); // the save there now stays in the library
  if (out == id && otherFile < 0) {
    // the same save file back into its own file: the library's copy as adopt left it (the card's, when
    // that was the newer), never an older one over newer progress
    const std::vector<uint8_t> now = readAll(entryDir(id) + "/save.bin");
    if (now.size() == kSlot) {
      copy = now;
    }
  }
  std::memcpy(gci.data() + at, copy.data(), kSlot);
  std::memcpy(gci.data() + at + 4 * kSlot, copy.data(), kSlot);
  std::string why;
  char name[64];
  portSaveFileName(PORT_GAME_ID[3], name, sizeof(name));
  if (!portSavesWriteFile(PORT_GAME_ID[3], name, gci, why)) {
    sStatus = portTrf("Could not write the memory card: %s", why.c_str());
    if (placed != id) {
      removeEntry(placed);
    }
    return false;
  }
  swapSlotRecords(out, placed, file); // and the records by the file's number with it
  portResumeForget(PORT_GAME_ID, file); // (Continue would bring the file there before back)
  char text[160];
  if (placed != id) {
    std::snprintf(text, sizeof(text),
                  portTr("Put a copy in File %d (it is in File %d too: each file now saves on its own)."), file + 1,
                  otherFile + 1);
    std::printf("[save files] id %s is in File %d: a copy (id %s) put in File %d\n", id.c_str(), otherFile + 1,
                placed.c_str(), file + 1);
    std::fflush(stdout);
  } else {
    std::snprintf(text, sizeof(text), portTr("Put in File %d."), file + 1);
  }
  sStatus = text;
  sDirty = true;
  return true;
}

// true once its folder is gone (the page says when it isn't)
bool removeEntry(const std::string& id) {
  const std::string dir = entryDir(id);
  for (const char* f : {"/save.bin", "/info.txt", "/thumb.bmp"}) {
    SDL_RemovePath((dir + f).c_str());
  }
  for (const SlotRecord& r : slotRecords(0)) {
    SDL_RemovePath((dir + "/slot/" + r.kept).c_str());
  }
  SDL_RemovePath((dir + "/slot").c_str());
  SDL_RemovePath(dir.c_str());
  sDirty = true;
  return !SDL_GetPathInfo(dir.c_str(), nullptr);
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
      launcherShotTexture(t.tex, rgba->w, rgba->h, rgba->pixels); // (so the UI captures draw it too)
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
                        portTr(e.character == "luigi"    ? "Luigi"
                               : e.character == "peach"  ? "Peach"
                               : e.character == "bowser" ? "Bowser"
                                                         : "Mario"));
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
        sStatus = removeEntry(e.id) ? "Deleted." : "It couldn't be deleted: is the folder open elsewhere?";
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
        sStatus = writeAll(entryDir(e.id) + "/info.txt", text.data(), text.size()) ? "Renamed."
                                                                                     : "The name couldn't be saved.";
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

// Co-op copies (port/include/port/statesync.h portCoopCopyList): the copies of a host's adventure kept while
// playing together online, <user>/coop. Each: the host, the room, when it was last kept, where its story is;
// Keep as a file (into an empty file of the card, as the launcher's offer does: a file of its own, also kept
// in the library) and Delete (asked first). The card is written itself, so not while the game runs it.
void coopSection(int inGame) {
  static Uint64 sRead;
  static std::vector<PortCoopCopyInfo> sCopies;
  static std::vector<PortSaveInfo> sInfo;
  static std::string sConfirm, sSaid;
  static bool sTested;
  if (!sTested) {
    // tests: $TTYD_COOP_COPY (a slot image or a .gci) kept as a copy from $TTYD_COOP_COPY_HOST, once
    sTested = true;
    if (const char* path = SDL_getenv("TTYD_COOP_COPY")) {
      std::vector<uint8_t> data = readAll(path);
      const size_t at = data.size() > kSlot ? kGciHeader + kMeta : 0;
      const char* host = SDL_getenv("TTYD_COOP_COPY_HOST");
      if (data.size() >= at + kSlot) {
        std::printf("[together] test co-op copy for Save files: %s\n",
                    portCoopCopyWrite(host != nullptr ? host : "Host", "TEST", data.data() + at, kSlot) ? "kept"
                                                                                                         : "not a save");
        std::fflush(stdout);
      }
    }
  }
  if (sRead == 0 || SDL_GetTicks() - sRead > 3000) {
    sRead = SDL_GetTicks();
    sCopies.assign(32, PortCoopCopyInfo{});
    sCopies.resize(static_cast<size_t>(std::max(0, portCoopCopyList(sCopies.data(), 32))));
    sInfo.assign(sCopies.size(), PortSaveInfo{});
    std::vector<uint8_t> image(kSlot);
    for (size_t i = 0; i < sCopies.size(); i++) {
      if (portCoopCopyRead(sCopies[i].file, image.data())) {
        portSaveImageInfo(image.data(), &sInfo[i]);
      }
    }
    static bool sLogged;
    if (!sLogged && !sCopies.empty()) {
      sLogged = true;
      std::printf("[together] Save files: %d co-op copies listed (the newest from %s)\n",
                  static_cast<int>(sCopies.size()), sCopies[0].host);
      std::fflush(stdout);
    }
  }
  if (sCopies.empty()) {
    ImGui::TextDisabled("%s", portTr("None yet: playing together online as a guest keeps a copy of the host's "
                                     "adventure here."));
  }
  // the card's empty files (a copy goes into one, never over a save)
  int freeFile = -1;
  bool card = false;
  {
    const std::vector<uint8_t> gci = readAll(cardPath());
    card = gci.size() >= kGciHeader + kMeta + 4 * kSlot;
    for (int f = 0; f < 4 && freeFile < 0; f++) {
      const size_t at = kGciHeader + kMeta + static_cast<size_t>(f) * kSlot;
      PortSaveInfo info;
      if (gci.size() >= at + kSlot && !portSaveImageInfo(gci.data() + at, &info)) {
        freeFile = f;
      }
    }
  }
  for (size_t i = 0; i < sCopies.size(); i++) {
    const PortCoopCopyInfo& c = sCopies[i];
    const PortSaveInfo& info = sInfo[i];
    ImGui::PushID(c.file);
    ImGui::Separator();
    char text[200];
    std::snprintf(text, sizeof(text), portTr("With %s, room %s"), c.host, c.room);
    ImGui::TextUnformatted(text);
    const SDL_Time t = static_cast<SDL_Time>(c.mtime) * 1000000000ll;
    SDL_DateTime dt;
    char when[40] = "";
    if (c.mtime > 0 && SDL_TimeToDateTime(t, &dt, true)) {
      std::snprintf(when, sizeof(when), "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
    }
    if (info.valid) {
      ImGui::TextDisabled("%s  ·  %s %d  ·  %s  ·  %s", chapterName(info.chapter), portTr("Level"), info.level,
                          portAreaPlaceName(info.map), when);
    } else {
      ImGui::TextDisabled("%s", when);
    }
    if (c.taken) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", portTr("(kept in a file already)"));
    }
    ImGui::BeginDisabled(inGame || freeFile < 0 || !info.valid);
    if (ImGui::SmallButton(portTrLabel("Keep as a file"))) {
      std::vector<uint8_t> image(kSlot);
      char why[192];
      if (portCoopCopyRead(c.file, image.data()) && portSaveFilesPutImage(image.data(), freeFile, why, sizeof(why))) {
        portCoopCopyMarkTaken(c.file);
        std::snprintf(text, sizeof(text), portTr("Kept in File %d."), freeFile + 1);
        sSaid = text;
        sRead = 0;
      } else {
        sSaid = portTr("That save file can't be read.");
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool confirm = sConfirm == c.file;
    if (ImGui::SmallButton(portTrLabel(confirm ? "Delete it for good" : "Delete"))) {
      if (!confirm) {
        sConfirm = c.file;
      } else {
        sConfirm.clear();
        const bool gone = portCoopCopyDelete(c.file) != 0;
        sSaid = portTr(gone ? "Deleted." : "It couldn't be deleted: is the folder open elsewhere?");
        std::printf("[together] Save files: the co-op copy %s %s\n", c.file, gone ? "deleted" : "not deleted");
        std::fflush(stdout);
        sRead = 0;
      }
    }
    ImGui::PopID();
  }
  if (!sCopies.empty() && inGame) {
    ImGui::TextDisabled("%s", portTr("Keep one as a file from the launcher, before the game starts."));
  } else if (!sCopies.empty() && !card) {
    ImGui::TextDisabled("%s", portTr("The memory card has no save file yet: start a game and save once first."));
  } else if (!sCopies.empty() && freeFile < 0) {
    ImGui::TextDisabled("%s", portTr("No empty file on the memory card: delete one first."));
  }
  if (!sSaid.empty()) {
    ImGui::TextUnformatted(sSaid.c_str());
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
    portSettingsAddSection("Saves", "Co-op copies", nullptr, 0, coopSection,
                           "co-op copies online together host guest adventure copy keep delete");
  }
} sSaveFiles;

} // namespace

// The guest's co-op save copy (port/include/port/together.h), from the launcher: the image into the card's
// file as a file of its own (a new id, the copy's own settings, as the file select's Copy makes one), kept in
// the library too; the save there before kept in the library first, and the records by the file's number go
// with it (place's). Writes the card file itself: the game isn't running it yet.
extern "C" int portSaveFilesPutImage(const unsigned char* src, int file, char* why, int cap) {
  auto fail = [&](const char* text) {
    std::snprintf(why, static_cast<size_t>(cap), "%s", text);
    return 0;
  };
  PortSaveInfo info;
  if (src == nullptr || file < 0 || file > 3 || !portSaveImageInfo(src, &info)) {
    return fail("That save file can't be read.");
  }
  std::vector<uint8_t> gci = readAll(cardPath());
  const size_t at = kGciHeader + kMeta + static_cast<size_t>(file) * kSlot;
  if (gci.size() < at + 4 * kSlot + kSlot) {
    return fail("The memory card has no save file yet: start a game and save once first.");
  }
  std::vector<uint8_t> image(src, src + kSlot);
  char block[PORT_CARD_EXTRA_SIZE];
  const char* old = portCardImageExtra(src);
  block[0] = '\0';
  extraForSave(file, old != nullptr ? old : "", 1, block, static_cast<int>(sizeof(block)));
  std::memset(image.data() + PORT_CARD_EXTRA_AT, 0, PORT_CARD_EXTRA_SIZE);
  std::memcpy(image.data() + PORT_CARD_EXTRA_AT, block,
              std::min(std::strlen(block), static_cast<size_t>(PORT_CARD_EXTRA_SIZE - 1)));
  const std::string id = lineOf(block, "id");
  if (!validId(id) || !keepImage(id, image.data())) {
    return fail("Could not keep it in Save files.");
  }
  const std::string out = adopt(gci, file); // the save there now stays in the library
  std::memcpy(gci.data() + at, image.data(), kSlot);
  std::memcpy(gci.data() + at + 4 * kSlot, image.data(), kSlot);
  std::string problem;
  char name[64];
  portSaveFileName(PORT_GAME_ID[3], name, sizeof(name));
  if (!portSavesWriteFile(PORT_GAME_ID[3], name, gci, problem)) {
    return fail("Could not write the memory card.");
  }
  swapSlotRecords(out, id, file);
  portResumeForget(PORT_GAME_ID, file);
  std::printf("[together] co-op copy put into file %d (save files id %s)\n", file + 1, id.c_str());
  std::fflush(stdout);
  return 1;
}
