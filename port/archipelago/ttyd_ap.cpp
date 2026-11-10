// Archipelago for TTYD (port/archipelago/ttyd_ap.h).
//
// After the TTYD apworld (github.com/jamesbrq/ArchipelagoTTYD, MIT License,
// Copyright (c) 2025 jamesbrq): its patcher (Rom.py: the options block, the
// item placements and their rules, the seed's files) and its client
// (TTYDClient.py: the received-item mailbox, the flag ring, location checks,
// DeathLink, the goal), here applied to the PC build directly instead of a
// disc image and Dolphin's memory. Where the GameCube kept a value big-endian
// in memory the TTYD AP mod reads, the PC keeps it native (the mod, ported,
// reads it natively).
#include "ttyd_ap.h"
#include "ttyd_ap_data.h"
#include "ap_client.h"
#include "rando_gen.h"
#include "ttyd_rando.h"
#include "port/open_world.h"
#include "port/save_words.h"

#include "port/json.h"
#include "port/mods.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

extern "C" uint32_t portGcLowMem[]; // port/src/aurora/sdk_aurora.c

using port_json::Json;

namespace {

constexpr const char* kGame = "Paper Mario: The Thousand-Year Door";
constexpr int kRemoteItem = 0x71; // another world's item, or a remote one
constexpr uint8_t kSeedKey[16] = {0xA5, 0x1C, 0x7E, 0x33, 0xC9, 0x58, 0xE2, 0x0F,
                                  0x96, 0x41, 0xDB, 0x6A, 0x24, 0xB7, 0x5D, 0xF0}; // Data.py's SEED_OBFUSCATION_KEY

// GameCube low memory (portGcLowMem), as the mod uses it
constexpr uint32_t kReceivedLength = 0x0FFC;     // u32: a batch waiting
constexpr uint32_t kReceivedItems = 0x1000;      // u16 x 15
constexpr uint32_t kProtocol = 0x31F8;           // u32
constexpr uint32_t kSaveLoaded = 0x3228;         // u8
constexpr uint32_t kKillMario = 0x323F;          // u8: a DeathLink in
constexpr uint32_t kMarioDied = 0x3240;          // u8: a death out
constexpr uint32_t kStarCount = 0x323B;          // u8
constexpr uint32_t kFlagRing = 0x4600;           // u16 head, u16 tail, u16 x 64
constexpr uint32_t kSenderRing = 0x4700;         // 16 x 16 bytes
constexpr int kFlagRingCapacity = 64;
// GSWs the mod keeps (port/include/port/save_words.h: every port feature's words)
constexpr int kReceivedIndexGsw = PORT_GSW_AP_RECEIVED_FIRST; // u32 in GSW bytes 1792..1795
constexpr int kDirtyGsw = PORT_GSW_AP_DIRTY_FIRST;            // 1696
constexpr int kDirtyItemReplay = 0x10;
constexpr int kGoalGsw = PORT_GSW_AP_LOCATIONS_FIRST + 8;     // 1708: the goal's count (goal 1)
constexpr int kRecipeFlagFirst = 6400, kRecipeFlagLast = 6456; // the recipes cooked (goal 4)
constexpr int kEverObtainedFlagBase = PORT_GSWF_AP_EVER_FIRST; // 6600 + the item's id
constexpr int kCounterGsw = PORT_GSW_AP_COUNTERS_FIRST;       // 1801..1805
const std::map<int, int> kEverObtainedCounters = {
    {125, kCounterGsw}, {38, kCounterGsw + 1}, {12, kCounterGsw + 2}, {46, kCounterGsw + 3}, {73, kCounterGsw + 4}};
static_assert(kReceivedIndexGsw + 3 <= PORT_GSW_AP_RECEIVED_LAST && kCounterGsw + 4 <= PORT_GSW_AP_COUNTERS_LAST &&
                  kGoalGsw <= PORT_GSW_AP_LOCATIONS_LAST && kRecipeFlagFirst >= PORT_GSWF_AP_LOCATIONS_FIRST &&
                  kRecipeFlagLast <= PORT_GSWF_AP_LOCATIONS_LAST && kEverObtainedFlagBase + 255 <= PORT_GSWF_AP_EVER_LAST,
              "the AP mod's words: port/include/port/save_words.h");

uint8_t* low(uint32_t offset) { return reinterpret_cast<uint8_t*>(portGcLowMem) + offset; }
template <typename T>
T& lowAs(uint32_t offset) {
  return *reinterpret_cast<T*>(low(offset));
}

const TtydApItem* itemById(int64_t id) {
  for (const TtydApItem& i : kTtydApItems) {
    if (i.id == id) return &i;
  }
  return nullptr;
}

const TtydApLocation* locationByName(const std::string& name) {
  for (const TtydApLocation& l : kTtydApLocations) {
    if (name == l.name) return &l;
  }
  return nullptr;
}

std::string sUserPath;
std::string sSettingsPath;

// --- the seed ---
struct Seed {
  bool loaded = false;
  bool builtIn = false; // the built-in randomizer's (ttyd_rando.h), not an .apttyd
  std::string file, error;
  Json options;
  std::string seedName, playerName;
  int player = 0;
  // location name -> (item id, player, price); fallback: location -> item id
  std::map<std::string, std::tuple<int64_t, int, int>> placements;
  std::map<std::string, int64_t> fallbacks;
} sSeed;

int option(const char* key, int fallback) {
  const Json* v = sSeed.options.get(key);
  return v != nullptr && v->type == Json::Number ? int(v->n) : v != nullptr && v->type == Json::Bool ? int(v->b) : fallback;
}

bool readJson(const std::string& member, Json& out) {
  std::vector<uint8_t> bytes;
  std::string why;
  return portModsReadZip(sSeed.file, member, bytes) && port_json::parseJson(bytes, out, why);
}

// Rom.py's _is_infinitely_repurchasable
bool repurchasable(int rom, int limitMode, int cls) {
  switch (limitMode) {
  case 3: return false;           // limited
  case 0: return cls != 1;        // infinite: all but progression
  case 1: return rom >= 0x80 && rom <= 0xEB;  // consumables and recipes
  case 2: return rom >= 0xF0 && rom <= 0x148; // badges
  }
  return false;
}

// The rom id a placement gives (Rom.py's patch_items)
int romIdOf(const TtydApLocation& loc, int64_t item, int player) {
  if (player != sSeed.player) {
    return kRemoteItem;
  }
  const TtydApItem* it = itemById(item);
  int rom = it != nullptr ? it->romId : 0;
  if (option("remote_items", 0) == 1 && rom != 0) {
    const bool shop = (loc.flags & TTYD_AP_SHOP) != 0;
    if (!(shop && repurchasable(rom, option("shop_purchase_limit", 1), it->cls))) {
      rom = kRemoteItem;
    }
  }
  return rom;
}

// A value into a target: a script word (EvtWord), a table's s32, or a u16
// of the mod's low-memory tables.
void writeTarget(const TtydApTarget& t, int value) {
  if (t.kind == TTYD_AP_LOWMEM) {
    if (t.width == 2) {
      lowAs<uint16_t>(t.offset) = uint16_t(value);
    } else {
      lowAs<uint32_t>(t.offset) = uint32_t(value);
    }
    return;
  }
  if (t.name[0] == '\0') return; // (not placed on this version: kTtydApUnmapped)
  std::string why;
  uint8_t* base = static_cast<uint8_t*>(portModsGameAddress(t.name, why));
  if (base == nullptr) {
    SDL_Log("archipelago: %s: %s", t.name, why.c_str());
    return;
  }
  if (t.kind == TTYD_AP_SCRIPT) {
    *reinterpret_cast<intptr_t*>(base + t.offset) = value;
  } else {
    *reinterpret_cast<int32_t*>(base + t.offset) = value;
  }
}

// The seed's items in an area's data (nullptr: the DOL's, at start)
void place(const char* area) {
  int written = 0;
  for (const auto& [name, p] : sSeed.placements) {
    const TtydApLocation* loc = locationByName(name);
    if (loc == nullptr) continue;
    const auto [item, player, price] = p;
    const int rom = romIdOf(*loc, item, player);
    if (area == nullptr && (loc->flags & TTYD_AP_TATTLE)) {
      // the tattle table, by unit (Rom.py: 0xB00 + 2 * (unit - 1))
      for (const TtydApTattle& t : kTtydApTattles) {
        if (t.location == loc->id) {
          lowAs<uint16_t>(0x3B00 + (t.unit - 1) * 2) = uint16_t(rom);
        }
      }
      continue;
    }
    for (int i = loc->first; i < loc->first + loc->count; i++) {
      const TtydApTarget& t = kTtydApTargets[i];
      if (area == nullptr ? t.area[0] != '\0' : strcmp(t.area, area) != 0) continue;
      writeTarget(t, t.price ? (rom == kRemoteItem ? 20 : price) : rom);
      written++;
    }
  }
  for (const auto& [name, item] : sSeed.fallbacks) {
    const TtydApLocation* loc = locationByName(name);
    const TtydApItem* it = itemById(item);
    if (loc == nullptr || it == nullptr) continue;
    for (int i = loc->first; i < loc->first + loc->count; i++) {
      const TtydApTarget& t = kTtydApTargets[i];
      if (t.price || (area == nullptr ? t.area[0] != '\0' : strcmp(t.area, area) != 0)) continue;
      writeTarget(t, it->romId);
      written++;
    }
  }
  if (written) {
    SDL_Log("archipelago: %s: %d placements", area != nullptr ? area : "the DOL's tables", written);
  }
}

// Rom.py's patch_mod: the options block the mod reads (RAM 0x800031F8...),
// native where it was big-endian
void writeOptions() {
  lowAs<uint32_t>(kProtocol) = 3;
  const std::string name = sSeed.playerName.substr(0, 0x10);
  *low(0x31FF) = uint8_t(name.size());
  memcpy(low(0x3200), name.data(), name.size());
  for (int i = 0; i < 16; i++) {
    *low(0x3210 + i) = uint8_t((i < int(sSeed.seedName.size()) ? sSeed.seedName[i] : 0) ^ kSeedKey[i]);
  }
  struct Byte {
    uint32_t at;
    const char* key;
    int fallback;
  };
  static const Byte kBytes[] = {
      {0x3220, "palace_stars", 7},        {0x3221, "starting_partner", 1},   {0x3222, "yoshi_color", 0},
      {0x3229, "palace_skip", 0},         {0x322A, "westside", 0},           {0x322B, "peekaboo", 0},
      {0x322C, "intermissions", 0},       {0x322D, "starting_hp", 10},       {0x322E, "starting_fp", 5},
      {0x322F, "starting_bp", 3},         {0x3230, "full_run_bar", 0},       {0x3238, "tattlesanity", 0},
      {0x3239, "fast_travel", 0},         {0x323A, "succeed_conditions", 0}, {0x323C, "cutscene_skip", 0},
      {0x323D, "remote_items", 0},        {0x323E, "starting_level", 1},     {0x3241, "music", 0},
      {0x3242, "block_visibility", 1},    {0x3243, "first_attack", 0},       {0x3248, "goal_stars", 7},
      {0x3249, "goal", 1},                {0x324A, "star_shuffle", 1},       {0x324B, "dazzle_rewards", 3},
      {0x324D, "shop_purchase_limit", 1}, {0x324E, "grubba_bribe_direction", 2}, {0x324F, "grubba_bribe_cost", 20},
      {0x3250, "blue_pipe_toggle", 1},    {0x3251, "enemy_randomizer", 0},   {0x3252, "enemy_stat_scaling", 0},
      {0x3258, "badge_bp", 0},            {0x3259, "badge_fp", 0},           {0x325A, "partner_fp", 0},
      {0x325B, "boss_randomizer", 0},     {0x3280, "boss_stat_scaling", 0},  {0x3282, "moon_speed", 0},
      {0x3283, "troubles", 0},            {0x3284, "boss_scaling_nerfs", 0}, {0x3285, "cooksanity", 0},
      {0x3286, "goal_recipes_range", 0},  {0x3287, "epilogue_skip", 0},      {0x328C, "rta_timer", 1},
      {0x328D, "in_game_tracker", 1},     {0x328E, "mirror_mode", 0},        {0x328F, "panel_hints", 0},
  };
  for (const Byte& b : kBytes) {
    *low(b.at) = uint8_t(option(b.key, b.fallback));
  }
  *low(0x3223) = 1;
  *low(0x3281) = 0; // not "console mode": the PC has the memory
  // the Yoshi name's address (a pointer the mod follows), and the name
  lowAs<uint32_t>(0x3224) = 0x80003260; // the mod's code turns it into portGcLowMem's (TTYD_GC_ADDR)
  std::string yoshi = "Yoshi";
  if (const Json* y = sSeed.options.get("yoshi_name"); y != nullptr && y->type == Json::String) yoshi = y->s;
  yoshi = yoshi.substr(0, 8);
  memset(low(0x3260), 0, 9);
  memcpy(low(0x3260), yoshi.data(), yoshi.size());
  if (const Json* chapters = sSeed.options.get("required_chapters"); chapters != nullptr) {
    uint32_t at = 0x3231; // (seven bytes, 0x3231..0x3237: a seed file's longer list stops there)
    for (const Json& c : chapters->items) {
      if (at > 0x3237) break;
      *low(at++) = uint8_t(c.n);
    }
  }
  std::mt19937 rng(uint32_t(option("seed", 0) + option("player", 0)));
  for (int i = 0; i < 4; i++) *low(0x3244 + i) = uint8_t(rng());
  const int64_t generated = option("generation_time", int(std::time(nullptr)));
  lowAs<uint32_t>(0x3288) = uint32_t(std::max<int64_t>(0, generated - 946684800));
  lowAs<uint16_t>(0x3290) = uint16_t(option("experience_multiplier", 1));
}

// sell prices under every AP shop's buy price (Rom.py: never raised)
void lowerSellPrices() {
  // the apworld's item_prices are the shops' buy prices of placed items: the
  // seed's own prices here
  std::map<int, int> buy;
  for (const auto& [name, p] : sSeed.placements) {
    const TtydApLocation* loc = locationByName(name);
    const auto [item, player, price] = p;
    const TtydApItem* it = itemById(item);
    if (loc == nullptr || it == nullptr || !(loc->flags & TTYD_AP_SHOP) || player != sSeed.player) continue;
    if (it->romId >= 0x80 && it->romId <= 0x148 && price > 0) {
      buy[it->romId] = buy.count(it->romId) ? std::min(buy[it->romId], price) : price;
    }
  }
  for (const auto& [rom, price] : buy) {
    const int sell = std::max(1, std::min(portApSellPrice(rom), price / 2));
    if (sell < portApSellPrice(rom)) portApSetSellPrice(rom, sell);
  }
}

// The mod's own messages (msg/US/mod.txt), which the patcher takes from the
// apworld rather than the seed: from the installed ttyd.apworld, if there is
// one ($TTYD_APWORLD_FILE, beside the seed, or Archipelago's custom_worlds).
void loadApworldFiles(const std::string& seed) {
  std::vector<std::string> worlds;
  if (const char* env = getenv("TTYD_APWORLD_FILE")) worlds.push_back(env);
  const size_t slash = seed.find_last_of("/\\");
  worlds.push_back((slash == std::string::npos ? std::string() : seed.substr(0, slash + 1)) + "ttyd.apworld");
#ifdef _WIN32
  if (const char* pd = getenv("ProgramData")) worlds.push_back(std::string(pd) + "\\Archipelago\\custom_worlds\\ttyd.apworld");
#endif
  if (const char* home = getenv("HOME")) worlds.push_back(std::string(home) + "/Archipelago/custom_worlds/ttyd.apworld");
  for (const std::string& world : worlds) {
    std::vector<uint8_t> bytes;
    if (portModsReadZip(world, "ttyd/data/mod.txt", bytes) || portModsReadZip(world, "data/mod.txt", bytes)) {
      bytes.resize((bytes.size() + 31) & ~size_t(31));
      portModsSetHostFile("/msg/US/mod.txt", bytes.data(), bytes.size());
      SDL_Log("archipelago: the mod's messages from %s", world.c_str());
      return;
    }
  }
}

bool loadSeed(const std::string& file) {
  sSeed = Seed{};
  sSeed.file = file;
  if (!readJson("options.json", sSeed.options)) {
    sSeed.error = "can't read " + file + " (options.json)";
    return false;
  }
  sSeed.seedName = sSeed.options.str("seed_name");
  sSeed.playerName = sSeed.options.str("player_name");
  sSeed.player = option("player", 0);
  Json locations, fallbacks;
  if (readJson("locations.json", locations)) {
    for (const auto& [name, v] : locations.fields) {
      if (v.items.size() >= 3) {
        sSeed.placements[name] = {int64_t(v.items[0].n), int(v.items[1].n), int(v.items[2].n)};
      }
    }
  }
  if (readJson("fallback_locations.json", fallbacks)) {
    for (const auto& [name, v] : fallbacks.fields) sSeed.fallbacks[name] = int64_t(v.n);
  }
  // the seed's files, where the patcher puts them on the disc
  static const std::pair<const char*, const char*> kFiles[] = {
      {"enemies.bin", "/mod/enemies.bin"}, {"bosses.bin", "/mod/bosses.bin"},
      {"tracker.bin", "/mod/tracker.bin"}, {"desc.txt", "/msg/US/desc.txt"}};
  for (const auto& [member, disc] : kFiles) {
    std::vector<uint8_t> bytes;
    if (portModsReadZip(file, member, bytes)) {
      bytes.resize((bytes.size() + 31) & ~size_t(31)); // read in 32-byte units, as from the disc
      portModsSetHostFile(disc, bytes.data(), bytes.size());
    }
  }
  loadApworldFiles(file);
  sSeed.loaded = true;
  return true;
}

// The built-in randomizer's seed (ttyd_rando.h), when no .apttyd is chosen:
// its placements applied as a seed's are. In the open world it is a solo
// seed for the TTYD AP mod (every item this player's own, given where it
// lies, so no server is needed): the options block the apworld's defaults,
// with the shuffle's settings.
void startBuiltIn() {
  PortRandoRun run;
  if (!portRandoStartRun(sUserPath.c_str(), run)) return;
  sSeed = Seed{};
  sSeed.loaded = true;
  sSeed.builtIn = true;
  sSeed.seedName = run.seed;
  sSeed.playerName = "Mario";
  sSeed.player = 1;
  for (const auto& [loc, item] : run.placements) {
    // (a shop's shelf, in the shop stock: its new item's own price)
    const TtydApItem* it = itemById(item);
    const int price = (kTtydApLocations[loc].flags & TTYD_AP_SHOP) && it != nullptr ? portApBuyPrice(it->romId) : 0;
    sSeed.placements[kTtydApLocations[loc].name] = {item, sSeed.player, price};
  }
  if (run.openWorld) {
    using port_json::number;
    std::vector<port_json::JsonField> o = {
        {"seed_name", port_json::string(run.seed)},
        {"player_name", port_json::string(sSeed.playerName)},
        {"player", number(sSeed.player)},
        {"seed", number(double(ttyd_rando::seedHash(run.seed) & 0x7FFFFFFF))},
        {"generation_time", number(double(std::time(nullptr)))},
        {"required_chapters", port_json::array({number(1), number(2), number(3), number(4), number(5), number(6), number(7)})},
    };
    static const std::pair<const char*, int> kValues[] = {
        {"palace_stars", 7}, {"goal_stars", 7}, {"goal", 1}, {"starting_partner", 1}, {"star_shuffle", 1},
        {"dazzle_rewards", 1}, {"palace_skip", 0}, {"westside", 0}, {"intermissions", 1}, {"cutscene_skip", 1},
        {"fast_travel", 1}, {"in_game_tracker", 0}, {"tattlesanity", 0}, {"cooksanity", 0}, {"troubles", 0},
        {"remote_items", 0}, {"shop_purchase_limit", 1}, {"blue_pipe_toggle", 1}, {"block_visibility", 1},
        {"experience_multiplier", 100}, {"starting_hp", 10}, {"starting_fp", 5}, {"starting_bp", 3},
        {"starting_level", 1}, {"enemy_randomizer", 0}, {"boss_randomizer", 0}, {"rta_timer", 0},
    };
    for (const auto& [key, value] : kValues) o.push_back({key, number(value)});
    sSeed.options = port_json::object(std::move(o));
    writeOptions();
    loadApworldFiles(sUserPath + "/");
  }
  place(nullptr);
}

// --- the connection ---
struct Session {
  std::unique_ptr<port_ap::Client> client;
  Json slotData;
  std::vector<port_ap::NetworkItem> items; // the slot's, all of them
  std::set<int64_t> sent;                  // locations sent
  std::set<int> pushedFlags;
  std::string lastRoom;
  bool goalSent = false, deathFromServer = false;
  std::string status = "off";
} sSession;

// a file is being played: the mod says so (0x3228); for tests without the
// mod, TTYD_AP_NO_MOD: whenever the game's sequence is a file's
bool saveLoaded() {
  if (*low(kSaveLoaded) == 1) return true;
  static const bool noMod = getenv("TTYD_AP_NO_MOD") != nullptr;
  return noMod && portApInFile();
}

uint32_t& receivedIndex() { return *reinterpret_cast<uint32_t*>(portApGswBytes() + kReceivedIndexGsw); }

int romOf(const port_ap::NetworkItem& item) {
  const TtydApItem* it = itemById(item.item);
  return it != nullptr ? it->romId : 0;
}

// TTYDClient.receive_items: the next batch into the mailbox, once the mod
// took the last
void receiveItems() {
  auto& s = sSession;
  if (lowAs<uint32_t>(kReceivedLength) != 0) return;
  const uint32_t index = receivedIndex();
  if (index > s.items.size()) return;
  const uint32_t n = std::min<uint32_t>(uint32_t(s.items.size()) - index, 15);
  if (n == 0) return;
  portApGswBytes()[kDirtyGsw] |= kDirtyItemReplay;
  std::map<std::string, int> senders;
  for (uint32_t i = 0; i < n; i++) {
    const port_ap::NetworkItem& item = s.items[index + i];
    uint16_t packed = uint16_t(romOf(item) & 0x1FF);
    const int cls = (item.flags & 0b100) ? 3 : (item.flags & 0b001) ? 2 : (item.flags & 0b010) ? 1 : 0;
    packed |= uint16_t(cls << 14);
    if (item.player != s.client->slot()) {
      const std::string name = s.client->playerName(item.player);
      if (!senders.count(name)) {
        const int slot = int(senders.size());
        senders[name] = slot;
        std::string safe;
        for (char c : name) safe += c == '<' ? '(' : c == '>' ? ')' : (c >= ' ' && c < 0x7F) ? c : '?';
        safe = safe.substr(0, 15);
        memset(low(kSenderRing + slot * 16), 0, 16);
        memcpy(low(kSenderRing + slot * 16), safe.data(), safe.size());
      }
      packed |= uint16_t(0x2000 | (senders[name] << 9));
    } else if (item.location >= -1) {
      packed |= uint16_t(0x2000 | (15 << 9)); // own item: announced, "no sender"
    }
    lowAs<uint16_t>(kReceivedItems + i * 2) = packed;
  }
  lowAs<uint32_t>(kReceivedLength) = n;
  SDL_Log("archipelago: %u items to the game (from %u)", n, index);
}

bool pushFlag(int flag) {
  uint16_t& head = lowAs<uint16_t>(kFlagRing);
  const uint16_t tail = lowAs<uint16_t>(kFlagRing + 2);
  if (uint16_t(head - tail) >= kFlagRingCapacity) return false;
  lowAs<uint16_t>(kFlagRing + 4 + (head % kFlagRingCapacity) * 2) = uint16_t(flag);
  head = uint16_t(head + 1);
  return true;
}

// the items the mod has applied (it advances the index, then clears the batch)
std::vector<port_ap::NetworkItem> applied() {
  if (lowAs<uint32_t>(kReceivedLength) != 0) return {};
  const uint32_t index = std::min<uint32_t>(receivedIndex(), uint32_t(sSession.items.size()));
  return std::vector<port_ap::NetworkItem>(sSession.items.begin(), sSession.items.begin() + index);
}

void pushFlags() {
  auto& s = sSession;
  auto push = [&](int flag) {
    if (flag <= 0 || s.pushedFlags.count(flag)) return true;
    if (portApGswfGet(flag)) {
      s.pushedFlags.insert(flag);
      return true;
    }
    if (!pushFlag(flag)) return false;
    s.pushedFlags.insert(flag);
    return true;
  };
  // remote items: the flags of the slot's checked locations (set_received_item_flags)
  const Json* remote = s.slotData.get("remote_items");
  if (remote != nullptr && remote->n != 0) {
    for (int64_t id : s.sent) {
      for (const TtydApLocation& l : kTtydApLocations) {
        if (l.id != id || l.gswType != 1) continue;
        int flag = l.gswOffset;
        if (l.flags & TTYD_AP_TATTLE) {
          for (const TtydApTattle& t : kTtydApTattles) {
            if (t.location == id) {
              flag = 0x117A + t.unit;
              break;
            }
          }
        }
        if (!push(flag)) return;
      }
    }
  }
  // ingredient unlocks, the ever-obtained ledger (set_ever_obtained_flags)
  std::map<int, int> counts;
  for (const port_ap::NetworkItem& item : applied()) {
    const int rom = romOf(item);
    if (rom < 1 || rom > 255) continue;
    counts[rom]++;
    if (!push(kEverObtainedFlagBase + rom)) return;
  }
  for (const auto& [rom, gsw] : kEverObtainedCounters) {
    const int received = std::min(counts[rom], 255);
    if (received > portApGswGet(gsw)) portApGswSet(gsw, received);
  }
}

void checkLocations() {
  auto& s = sSession;
  std::vector<int64_t> found;
  for (const TtydApLocation& l : kTtydApLocations) {
    if (l.gswOffset == 0 || s.sent.count(l.id)) continue;
    bool done = false;
    if (l.flags & TTYD_AP_TATTLE) {
      for (const TtydApTattle& t : kTtydApTattles) {
        done |= t.location == l.id && portApGswfGet(0x117A + t.unit);
      }
    } else if (l.gswType == 0) {
      done = portApGswGet(l.gswOffset) >= l.gswValue;
    } else {
      done = portApGswfGet(l.gswOffset) != 0;
    }
    if (done) found.push_back(l.id);
  }
  if (!found.empty()) {
    s.sent.insert(found.begin(), found.end());
    s.client->checkLocations(found);
  }
}

void checkGoal() {
  auto& s = sSession;
  if (s.goalSent) return;
  const int goal = int(s.slotData.get("goal") ? s.slotData.get("goal")->n : 0);
  bool done = false;
  if (goal == 1) {
    done = portApGswGet(kGoalGsw) >= 18;
  } else if (goal == 2) {
    const int stars = *low(kStarCount);
    done = stars <= 7 && stars >= int(s.slotData.get("goal_stars") ? s.slotData.get("goal_stars")->n : 7);
  } else if (goal == 4) {
    int n = 0;
    for (int f = kRecipeFlagFirst; f <= kRecipeFlagLast; f++) n += portApGswfGet(f);
    done = n >= int(s.slotData.get("goal_recipes_range") ? s.slotData.get("goal_recipes_range")->n : 57);
  } else {
    done = portApGswfGet(5085) != 0;
  }
  if (done) {
    s.goalSent = true;
    s.client->goal();
  }
}

void checkDeath() {
  auto& s = sSession;
  uint8_t& died = *low(kMarioDied);
  if (died == 1) {
    died = 0;
    if (!s.deathFromServer) {
      s.client->sendDeathLink(s.client->playerName(s.client->slot()) + " had no life shrooms.");
    }
    s.deathFromServer = false;
  }
}

std::string sNextSeed; // the seed the settings name, for the next start
std::string sServer, sSlot, sPassword;

void writeSettings(const std::string& server, const std::string& slot, const std::string& password) {
  sServer = server;
  sSlot = slot;
  sPassword = password;
  const Json j = port_json::object({{"seed", port_json::string(sNextSeed)},
                                    {"server", port_json::string(server)},
                                    {"slot", port_json::string(slot)},
                                    {"password", port_json::string(password)}});
  if (SDL_IOStream* io = SDL_IOFromFile(sSettingsPath.c_str(), "wb")) {
    const std::string text = port_json::dump(j);
    SDL_WriteIO(io, text.data(), text.size());
    SDL_CloseIO(io);
  }
}

} // namespace

extern "C" {

void portApInit(const char* userPath) {
  portRandoBoot(); // (online's setup and replays: the classic randomizer applied, or not, now)
  sUserPath = userPath;
  portRandoApplySound(userPath); // (the randomizer's music and sound shuffle, the sound editor: cosmetic)
  sSettingsPath = sUserPath + "/archipelago.json";
  // a seed by environment (tests) or the settings
  Json settings;
  std::vector<uint8_t> bytes;
  size_t size = 0;
  if (void* data = SDL_LoadFile(sSettingsPath.c_str(), &size)) {
    bytes.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    SDL_free(data);
    std::string why;
    port_json::parseJson(bytes, settings, why);
  }
  if (getenv("TTYD_AP_VERIFY") != nullptr) {
    portApVerifyTargets(); // (ap_verify.cpp: headless too)
  }
  std::string seed = settings.str("seed");
  sNextSeed = seed;
  sServer = settings.str("server");
  sSlot = settings.str("slot");
  sPassword = settings.str("password");
  if (const char* env = getenv("TTYD_AP_SEED")) seed = env;
  if (seed.empty()) {
    startBuiltIn();
    return;
  }
  if (!loadSeed(seed)) {
    SDL_Log("archipelago: %s", sSeed.error.c_str());
    return;
  }
  writeOptions();
  place(nullptr);
  lowerSellPrices();
  SDL_Log("archipelago: seed %s, %s (%d placements)", sSeed.seedName.c_str(), sSeed.playerName.c_str(),
          int(sSeed.placements.size()));
  std::string server = settings.str("server");
  if (const char* env = getenv("TTYD_AP_SERVER")) server = env;
  if (!server.empty()) {
    portApConnect(server.c_str(), settings.str("slot").empty() ? sSeed.playerName.c_str() : settings.str("slot").c_str(),
                  settings.str("password").c_str());
  }
}

void portApAreaLinked(const char* area) {
  if (sSeed.loaded) place(area);
  portOpenAreaLinked(area); // an open-world file's own items, over a run's (port/open_world.h)
}

void portApConnect(const char* server, const char* slot, const char* password) {
  auto& s = sSession;
  port_ap::Client::Callbacks cb;
  cb.connected = [](const Json& data, const std::set<int64_t>& checked) {
    sSession.slotData = data;
    sSession.sent = checked;
    sSession.pushedFlags.clear();
    sSession.goalSent = false;
    // remote items, then: the server sends this slot's own finds too
    if (const Json* r = data.get("remote_items"); r != nullptr && r->n != 0) {
      // (items_handling is fixed at connect: 0b111 covers both)
    }
  };
  cb.items = [](int index, const std::vector<port_ap::NetworkItem>& items) {
    auto& v = sSession.items;
    if (index == 0) v.clear();
    if (size_t(index) <= v.size()) {
      v.resize(index);
      v.insert(v.end(), items.begin(), items.end());
    }
  };
  cb.print = [](const std::string& text) { SDL_Log("archipelago: %s", text.c_str()); };
  cb.deathLink = [](const std::string& source, const std::string& cause) {
    SDL_Log("archipelago: DeathLink from %s: %s", source.c_str(), cause.c_str());
    sSession.deathFromServer = true;
    *low(kKillMario) = 1;
  };
  s.client = std::make_unique<port_ap::Client>(cb);
  port_ap::Settings settings;
  settings.server = server;
  settings.slot = slot;
  settings.password = password;
  settings.game = kGame;
  settings.itemsHandling = 0b111;
  settings.tags = {"AP"};
  s.client->start(settings);
  writeSettings(server, slot, password);
}

void portApSetSeed(const char* path) {
  sNextSeed = path;
  writeSettings(sServer, sSlot, sPassword);
}

const char* portApSeedPath(void) { return sNextSeed.c_str(); }

const char* portApSlotName(void) { return sSeed.loaded && !sSeed.builtIn ? sSeed.playerName.c_str() : ""; }

void portApDisconnect(void) {
  if (sSession.client) sSession.client->stop();
}

const char* portApStatus(void) {
  static std::string text;
  if (sSeed.builtIn) {
    text = std::string("the built-in randomizer's ") + portRandoActive() + " (no server)";
  } else if (!sSeed.loaded) {
    text = sSeed.error.empty() ? "no seed" : sSeed.error;
  } else if (!sSession.client) {
    text = "seed " + sSeed.seedName + " (" + sSeed.playerName + "), not connected";
  } else {
    text = sSession.client->message();
  }
  return text.c_str();
}

void portApFrame(void) {
  auto& s = sSession;
  if (!s.client) return;
  s.client->poll();
  if (s.client->status() != port_ap::Status::Connected || !saveLoaded()) return;
  receiveItems();
  pushFlags();
  checkLocations();
  checkGoal();
  checkDeath();
  const std::string room = portApMapName();
  if (room != s.lastRoom) {
    s.lastRoom = room;
    s.client->setValue("ttyd_room_" + std::to_string(0) + "_" + std::to_string(s.client->slot()), port_json::string(room));
  }
}

} // extern "C"
