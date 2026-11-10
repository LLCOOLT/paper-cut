// The built-in randomizer (port/archipelago/ttyd_rando.h): its settings, the
// seed generated at start, the spoiler log.
#include "ttyd_rando.h"
#include "rando_gen.h"

#include "port/json.h"
#include "port/mods.h"
#include "port/online.h"
#include "port/open_world.h"
#include "port/replay.h"
#include "port/sound_shuffle.h"

#include <SDL3/SDL.h>
#include "../settings/file_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>
#include <string>

using port_json::Json;

namespace {

const PortRandoSettings kDefaults = {0, "", 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// What makes the game, as bits (a permalink's and a preset's): badges 0, items
// 1, Star Pieces 2, Shine Sprites 3, key items 4, open world 5, goal 6-8, logic
// level 9-10, tricks 11-18, shops 19, troubles 20, mystery 21, music 22,
// sounds 23, gates 24, room doors 25, ways between areas 26, rides 27.
uint32_t codeOf(const PortRandoSettings& s) {
  return uint32_t(s.badges != 0) | uint32_t(s.items != 0) << 1 | uint32_t(s.starPieces != 0) << 2 |
         uint32_t(s.shineSprites != 0) << 3 | uint32_t(s.keyItems != 0) << 4 | uint32_t(s.openWorld != 0) << 5 |
         uint32_t(s.openGoal & 7) << 6 | uint32_t(s.logicLevel & 3) << 9 | uint32_t(s.tricks & 0xFF) << 11 |
         uint32_t(s.shops != 0) << 19 | uint32_t(s.troubles != 0) << 20 | uint32_t(s.mystery != 0) << 21 |
         uint32_t(s.music != 0) << 22 | uint32_t(s.sounds != 0) << 23 | uint32_t(s.gates != 0) << 24 |
         uint32_t(s.rooms != 0) << 25 | uint32_t(s.mixWays != 0) << 26 | uint32_t(s.transports != 0) << 27;
}

void applyCode(PortRandoSettings& s, uint32_t c) {
  s.badges = c & 1;
  s.items = c >> 1 & 1;
  s.starPieces = c >> 2 & 1;
  s.shineSprites = c >> 3 & 1;
  s.keyItems = c >> 4 & 1;
  s.openWorld = c >> 5 & 1;
  s.openGoal = c >> 6 & 7;
  s.logicLevel = (c >> 9 & 3) > 2 ? 0 : c >> 9 & 3;
  s.tricks = c >> 11 & 0xFF;
  s.shops = c >> 19 & 1;
  s.troubles = c >> 20 & 1;
  s.mystery = c >> 21 & 1;
  s.music = c >> 22 & 1;
  s.sounds = c >> 23 & 1;
  s.gates = c >> 24 & 1;
  s.rooms = c >> 25 & 1;
  s.mixWays = c >> 26 & 1;
  s.transports = c >> 27 & 1;
}
constexpr uint32_t kCodeBits = (1u << 28) - 1;

// The built-in presets: a name, a line, and their settings as above
struct Preset {
  std::string name, note;
  uint32_t code;
  bool builtIn;
};
const Preset kBuiltIn[] = {
    {"Casual", "The game's own story: badges, items, Star Pieces and Shine Sprites move.", 0x0F, true},
    {"Open world", "Paper Cut's open world, every chapter in any order, all seven stars, glitchless.", 0x0F | 1u << 5,
     true},
    {"Short open world", "Paper Cut's open world with a goal of three Crystal Stars.", 0x0F | 1u << 5 | 3u << 6, true},
    {"Speedrunner", "The open world with every trick counted on and shops shuffled.",
     0x0F | 1u << 5 | 1u << 9 | 0xFFu << 11 | 1u << 19, true},
    {"Chaos", "The open world with no logic, its pipes shuffled, shops and troubles too, mystery items, the music "
              "and sounds shuffled.",
     0x0F | 1u << 5 | 2u << 9 | 1u << 19 | 1u << 20 | 1u << 21 | 1u << 22 | 1u << 23 | 1u << 24, true},
};
std::vector<Preset> sPresets; // the player's (randomizer.json's "presets")
std::string sPermalink;

// The sound editor's replacements (<user>/sound_editor.json: {"music": {from: to}, "sounds": {...}})
std::vector<std::pair<std::string, std::string>> sEdits[2];
bool sEditsRead = false;

void applySound(); // (below: the shuffle from the settings, the editor's choices)
PortRandoSettings sSettings = kDefaults;
std::string sUserPath;
bool sRead = false;
std::string sActive; // this run's
std::string sCheck;
bool sForRun = false; // the session's settings (online, a replay): never saved
bool sBooted = false; // portApInit has run: the classic randomizer applied (or not)
std::string sRunText; // the settings it applied at start-up, as text ("" none)
bool sRunMystery = false; // this run's places show their items as a Mystery

std::string settingsFile() { return sUserPath + "/randomizer.json"; }

void parseSettings(const std::vector<uint8_t>& bytes, const std::string& from) {
  Json j;
  std::string why;
  if (!port_json::parseJson(bytes, j, why)) {
    SDL_Log("randomizer: %s: %s", from.c_str(), why.c_str());
    return;
  }
  auto flag = [&](const char* key, int& out) {
    if (const Json* v = j.get(key)) out = v->type == Json::Bool ? int(v->b) : v->type == Json::Number ? int(v->n != 0) : out;
  };
  flag("enabled", sSettings.enabled);
  flag("badges", sSettings.badges);
  flag("items", sSettings.items);
  flag("star_pieces", sSettings.starPieces);
  flag("shine_sprites", sSettings.shineSprites);
  flag("key_items", sSettings.keyItems);
  flag("spoiler", sSettings.spoiler);
  flag("open_world", sSettings.openWorld);
  if (const Json* v = j.get("open_goal")) {
    if (v->type == Json::Number && v->n >= 0 && v->n <= 7) sSettings.openGoal = int(v->n);
  }
  if (const Json* v = j.get("logic_level")) {
    if (v->type == Json::Number && v->n >= 0 && v->n <= 2) sSettings.logicLevel = int(v->n);
  }
  if (const Json* v = j.get("tricks")) {
    if (v->type == Json::Number && v->n >= 0 && v->n <= 255) sSettings.tricks = int(v->n);
  }
  flag("shops", sSettings.shops);
  flag("troubles", sSettings.troubles);
  flag("mystery", sSettings.mystery);
  flag("music", sSettings.music);
  flag("sounds", sSettings.sounds);
  flag("gates", sSettings.gates);
  flag("rooms", sSettings.rooms);
  flag("mix_ways", sSettings.mixWays);
  flag("transports", sSettings.transports);
  std::snprintf(sSettings.seed, sizeof(sSettings.seed), "%s", j.str("seed").c_str());
}

// the player's presets in randomizer.json ({"presets": {name: settings bits}})
void parsePresets(const std::vector<uint8_t>& bytes) {
  Json j;
  std::string why;
  if (!port_json::parseJson(bytes, j, why)) return;
  const Json* p = j.get("presets");
  if (p == nullptr || p->type != Json::Object) return;
  sPresets.clear();
  for (const auto& [name, v] : p->fields) {
    if (v.type == Json::Number && !name.empty()) sPresets.push_back({name, "", uint32_t(v.n) & kCodeBits, false});
  }
}

void readSettings() {
  if (sRead || sUserPath.empty()) return;
  sRead = true;
  size_t size = 0;
  void* data = SDL_LoadFile(settingsFile().c_str(), &size);
  if (data == nullptr) return;
  std::vector<uint8_t> bytes(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  SDL_free(data);
  parseSettings(bytes, settingsFile());
  parsePresets(bytes);
}

Json jsonOf(const PortRandoSettings& s) {
  return port_json::object({{"enabled", port_json::boolean(s.enabled != 0)},
                                    {"seed", port_json::string(s.seed)},
                                    {"badges", port_json::boolean(s.badges != 0)},
                                    {"items", port_json::boolean(s.items != 0)},
                                    {"star_pieces", port_json::boolean(s.starPieces != 0)},
                                    {"shine_sprites", port_json::boolean(s.shineSprites != 0)},
                                    {"key_items", port_json::boolean(s.keyItems != 0)},
                                    {"spoiler", port_json::boolean(s.spoiler != 0)},
                                    {"open_world", port_json::boolean(s.openWorld != 0)},
                                    {"open_goal", port_json::number(s.openGoal)},
                                    {"logic_level", port_json::number(s.logicLevel)},
                                    {"tricks", port_json::number(s.tricks)},
                                    {"shops", port_json::boolean(s.shops != 0)},
                                    {"troubles", port_json::boolean(s.troubles != 0)},
                                    {"mystery", port_json::boolean(s.mystery != 0)},
                                    {"music", port_json::boolean(s.music != 0)},
                                    {"sounds", port_json::boolean(s.sounds != 0)},
                                    {"gates", port_json::boolean(s.gates != 0)},
                                    {"rooms", port_json::boolean(s.rooms != 0)},
                                    {"mix_ways", port_json::boolean(s.mixWays != 0)},
                                    {"transports", port_json::boolean(s.transports != 0)}});
}

// (online's setup and a replay's: the settings that make the game, no presets)
std::string textOf(const PortRandoSettings& s) { return port_json::dump(jsonOf(s)); }

// randomizer.json: the settings and the player's presets
std::string fileTextOf(const PortRandoSettings& s) {
  Json j = jsonOf(s);
  std::vector<port_json::JsonField> presets;
  for (const Preset& p : sPresets) presets.push_back({p.name, port_json::number(p.code)});
  j.fields.push_back({"presets", port_json::object(presets)});
  return port_json::dump(j);
}

ttyd_rando::Options optionsOf(const PortRandoSettings& s) {
  ttyd_rando::Options o;
  o.seed = s.seed;
  o.pools = (s.badges ? ttyd_rando::kBadges : 0u) | (s.items ? ttyd_rando::kItems : 0u) |
            (s.starPieces ? ttyd_rando::kStarPieces : 0u) | (s.shineSprites ? ttyd_rando::kShineSprites : 0u) |
            (s.keyItems && portRandoApWorldSupported() ? ttyd_rando::kKeyItems : 0u);
  o.logic = s.openWorld                                  ? ttyd_rando::kPaperCut
            : s.keyItems && portRandoApWorldSupported() ? ttyd_rando::kOpenWorld
                                                         : ttyd_rando::kVanillaStory;
  o.goalStars = s.openGoal;
  o.logicLevel = s.logicLevel >= 0 && s.logicLevel <= 2 ? s.logicLevel : 0;
  o.tricks = unsigned(s.tricks) & 0xFFu;
  o.extra = (s.shops ? ttyd_rando::kExtraShops : 0u) | (s.troubles ? ttyd_rando::kExtraTroubles : 0u) |
            (s.mystery ? ttyd_rando::kExtraMystery : 0u);
  o.entrances = !s.openWorld ? 0u
                 : (s.gates ? ttyd_rando::kEntranceGates : 0u) | (s.rooms ? ttyd_rando::kEntranceRooms : 0u) |
                       (s.mixWays ? ttyd_rando::kEntranceMix : 0u) |
                       (s.transports ? ttyd_rando::kEntranceTransports : 0u);
  return o;
}

std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// <user>/randomizer/spoiler_<seed>.txt
std::string writeSpoiler(const std::string& seed, const std::string& text) {
  const std::string dir = sUserPath + "/randomizer";
  SDL_CreateDirectory(dir.c_str());
  std::string safe;
  for (char c : seed) safe += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
  const std::string path = dir + "/spoiler_" + (safe.empty() ? "seed" : safe) + ".txt";
  std::string out = "Permalink: " + std::string(portRandoPermalink()) + "\n" + text;
  out += "\nGame: " + std::string(PORT_GAME_ID) + "\n";
  if (!SDL_SaveFile(path.c_str(), out.data(), out.size())) {
    SDL_Log("randomizer: can't write %s: %s", path.c_str(), SDL_GetError());
    return "";
  }
  return path;
}

std::string editsFile() { return sUserPath + "/sound_editor.json"; }

void readEdits() {
  if (sEditsRead || sUserPath.empty()) return;
  sEditsRead = true;
  size_t size = 0;
  void* data = SDL_LoadFile(editsFile().c_str(), &size);
  if (data == nullptr) return;
  std::vector<uint8_t> bytes(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  SDL_free(data);
  Json j;
  std::string why;
  if (!port_json::parseJson(bytes, j, why)) {
    SDL_Log("randomizer: %s: %s", editsFile().c_str(), why.c_str());
    return;
  }
  const char* const keys[2] = {"music", "sounds"};
  for (int k = 0; k < 2; k++) {
    const Json* m = j.get(keys[k]);
    if (m == nullptr || m->type != Json::Object) continue;
    for (const auto& [from, to] : m->fields) {
      if (to.type == Json::String && !from.empty() && !to.s.empty()) sEdits[k].emplace_back(from, to.s);
    }
  }
}

void saveEdits() {
  if (sUserPath.empty()) return;
  const char* const keys[2] = {"music", "sounds"};
  std::vector<port_json::JsonField> top;
  for (int k = 0; k < 2; k++) {
    std::vector<port_json::JsonField> m;
    for (const auto& [from, to] : sEdits[k]) m.push_back({from, port_json::string(to)});
    top.push_back({keys[k], port_json::object(m)});
  }
  const std::string text = port_json::dump(port_json::object(top));
  if (!portfile::writeAll(editsFile(), text.data(), text.size())) {
    SDL_Log("randomizer: can't write %s: %s", editsFile().c_str(), SDL_GetError());
  }
}

// The music and sound shuffle (the settings': cosmetic, so as they are now,
// never the session's) and the editor's replacements, into the game's tables
void applySound() {
  readEdits();
  const PortRandoSettings& s = sSettings;
  const bool on = s.enabled && (s.music || s.sounds);
  portSoundShuffleSet(on ? ttyd_rando::seedHash(s.seed[0] != '\0' ? s.seed : "sound") | 1ull : 0ull, on && s.music,
                      on && s.sounds);
  portSoundEditorClear();
  for (int k = 0; k < 2; k++) {
    for (const auto& [from, to] : sEdits[k]) portSoundEditorSet(k, from.c_str(), to.c_str());
  }
}

} // namespace

extern "C" {

// Every version: the apworld's places are the US disc's, and ttyd_ap_data.h has
// the JP and PAL discs' too (the few that differ there are left out).
int portRandoSupported(void) { return 1; }

// The TTYD AP mod's open world: the mod and its messages are the US version's.
int portRandoApWorldSupported(void) { return std::strcmp(PORT_GAME_ID, "G8ME01") == 0; }

PortRandoSettings* portRandoSettings(const char* userPath) {
  if (sUserPath.empty() && userPath != nullptr) sUserPath = userPath;
  readSettings();
  return &sSettings;
}

void portRandoSaveSettings(void) {
  if (sUserPath.empty() || sForRun) return; // (the session's settings: never this player's file)
  const std::string text = fileTextOf(sSettings);
  if (!portfile::writeAll(settingsFile(), text.data(), text.size())) {
    SDL_Log("randomizer: can't write %s: %s", settingsFile().c_str(), SDL_GetError());
  }
  if (sBooted) applySound(); // (cosmetic: at once)
}

void portRandoNewSeed(void) {
  if (sForRun) return; // (the session's seed is the host's: a new one here would differ from the other side's)
  const uint64_t entropy = SDL_GetTicksNS() ^ (uint64_t(std::time(nullptr)) << 20) ^ uint64_t(uintptr_t(&sSettings));
  std::snprintf(sSettings.seed, sizeof(sSettings.seed), "%s", ttyd_rando::seedText(entropy).c_str());
  portRandoSaveSettings();
}

const char* portRandoCheck(void) {
  const ttyd_rando::Result r = ttyd_rando::generate(optionsOf(sSettings));
  if (!r.ok) {
    sCheck = "Can't: " + r.error;
  } else {
    sCheck = std::to_string(r.shuffled) + " locations take part, " + std::to_string(r.moved) + " items moved";
    if (r.attempts > 1) sCheck += " (" + std::to_string(r.attempts) + " tries)";
  }
  return sCheck.c_str();
}

const char* portRandoActive(void) { return sActive.c_str(); }

const char* portRandoPermalink(void) {
  char head[24];
  std::snprintf(head, sizeof(head), "PC1-%07X-", unsigned(codeOf(sSettings)));
  sPermalink = head + std::string(sSettings.seed);
  return sPermalink.c_str();
}

const char* portRandoApplyPermalink(const char* text) {
  std::string t = text != nullptr ? text : "";
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
  if (t.size() < 13 || t.compare(0, 4, "PC1-") != 0 || t[11] != '-') {
    return "Not a permalink (they start with PC1-).";
  }
  char* end = nullptr;
  const std::string hexPart = t.substr(4, 7);
  const unsigned long code = std::strtoul(hexPart.c_str(), &end, 16);
  if (end == nullptr || *end != '\0' || code > kCodeBits) return "Not a permalink: its settings can't be read.";
  const std::string seed = t.substr(12);
  if (seed.size() >= sizeof(sSettings.seed)) return "Not a permalink: its seed is too long.";
  if (sForRun) return "The session's settings can't be changed here.";
  applyCode(sSettings, uint32_t(code));
  std::snprintf(sSettings.seed, sizeof(sSettings.seed), "%s", seed.c_str());
  sCheck.clear();
  portRandoSaveSettings();
  return nullptr;
}

int portRandoPresetCount(void) { return int(std::size(kBuiltIn) + sPresets.size()); }

static const Preset* presetAt(int i) {
  if (i < 0) return nullptr;
  if (size_t(i) < std::size(kBuiltIn)) return &kBuiltIn[i];
  i -= int(std::size(kBuiltIn));
  return size_t(i) < sPresets.size() ? &sPresets[size_t(i)] : nullptr;
}

const char* portRandoPresetName(int i) {
  const Preset* p = presetAt(i);
  return p != nullptr ? p->name.c_str() : "";
}

const char* portRandoPresetNote(int i) {
  const Preset* p = presetAt(i);
  return p != nullptr ? p->note.c_str() : "";
}

int portRandoPresetBuiltIn(int i) {
  const Preset* p = presetAt(i);
  return p != nullptr && p->builtIn ? 1 : 0;
}

void portRandoApplyPreset(int i) {
  const Preset* p = presetAt(i);
  if (p == nullptr || sForRun) return;
  applyCode(sSettings, p->code);
  sCheck.clear();
  portRandoSaveSettings();
}

void portRandoSavePreset(const char* name) {
  if (name == nullptr || name[0] == '\0') return;
  const uint32_t code = codeOf(sSettings);
  for (Preset& p : sPresets) {
    if (p.name == name) {
      p.code = code;
      portRandoSaveSettings();
      return;
    }
  }
  sPresets.push_back({name, "", code, false});
  portRandoSaveSettings();
}

void portRandoDeletePreset(int i) {
  const int k = i - int(std::size(kBuiltIn));
  if (k < 0 || size_t(k) >= sPresets.size()) return;
  sPresets.erase(sPresets.begin() + k);
  portRandoSaveSettings();
}

int portRandoRunMystery(void) { return sRunMystery ? 1 : 0; }

// The new open-world file's rules (port/open_world.h), as the settings have them
void portOpenArmRules(PortOpenRules* out) {
  const ttyd_rando::Options o = optionsOf(sSettings);
  out->level = unsigned(o.logicLevel);
  out->tricks = o.tricks;
  out->extra = o.extra;
  out->entrances = o.entrances;
}

// A new file, with Paper Cut's open world on: the file takes the settings'
// seed (a new one when it is empty, kept), checked and its spoiler written.
int portOpenArmNewFile(char* seed, int seedMax, unsigned* pools, int* goal) {
  const PortRandoSettings& s = sSettings;
  if (!sRead || !s.enabled || !s.openWorld || !portRandoSupported()) return 0;
  if (portReplayActive() && !sForRun) {
    // this player's own randomizer.json, which the replay doesn't have (a replay from before
    // replays kept the settings): a new file there is the game's own
    SDL_Log("randomizer: open world: a new file in a replay without the run's settings: not stamped");
    return 0;
  }
  if (s.seed[0] == '\0') portRandoNewSeed();
  const ttyd_rando::Options o = optionsOf(s);
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  if (!r.ok) {
    SDL_Log("randomizer: open world: seed %s: %s", s.seed, r.error.c_str());
    sActive = std::string("seed ") + s.seed + " could not be made: " + r.error;
    return -1;
  }
  std::snprintf(seed, size_t(seedMax), "%s", s.seed);
  *pools = o.pools;
  *goal = o.goalStars;
  sActive = std::string("the new file: seed ") + seed + ", Paper Cut's open world (" + ttyd_rando::poolsText(o.pools | ttyd_rando::kKeyItems) +
            "), " + std::to_string(r.moved) + " items moved";
  if (s.spoiler) {
    const std::string path = writeSpoiler(seed, r.spoiler);
    if (!path.empty()) sActive += "; spoiler log: " + path;
  }
  SDL_Log("randomizer: %s", sActive.c_str());
  return 1;
}

int portRandoShared(void) { return sForRun ? 1 : 0; }

void portRandoBoot(void) { sBooted = true; }

void portRandoApplySound(const char* userPath) {
  portRandoSettings(userPath);
  applySound();
}

int portRandoSoundEditCount(int sfx) {
  readEdits();
  return int(sEdits[sfx != 0].size());
}

const char* portRandoSoundEditFrom(int sfx, int i) {
  const auto& e = sEdits[sfx != 0];
  return i >= 0 && size_t(i) < e.size() ? e[size_t(i)].first.c_str() : "";
}

const char* portRandoSoundEditTo(int sfx, int i) {
  const auto& e = sEdits[sfx != 0];
  return i >= 0 && size_t(i) < e.size() ? e[size_t(i)].second.c_str() : "";
}

void portRandoSoundEditSet(int sfx, const char* from, const char* to) {
  readEdits();
  if (from == nullptr || from[0] == '\0') return;
  auto& e = sEdits[sfx != 0];
  e.erase(std::remove_if(e.begin(), e.end(), [&](const auto& p) { return p.first == from; }), e.end());
  if (to != nullptr && to[0] != '\0') e.emplace_back(from, to);
  saveEdits();
  applySound();
}

int portRandoApModFound(void) {
  for (int i = 0; i < portModsCount(); i++) {
    const PortModInfo* m = portModsGet(i);
    if (m == nullptr || !m->enabled || m->code == PORT_MOD_CODE_NONE) continue;
    const std::string id = lower(m->id ? m->id : "") + " " + lower(m->name ? m->name : "");
    if (id.find("ttydap") != std::string::npos || id.find("ttyd-ap") != std::string::npos ||
        id.find("ttyd_ap") != std::string::npos || id.find("archipelago") != std::string::npos) {
      return 1;
    }
  }
  return 0;
}

} // extern "C"

namespace {
ttyd_rando::Options sRunOptions; // what this run applied (the tracker's)
std::vector<ttyd_rando::Placement> sRunPlacements;
bool sRunMade = false;
} // namespace

bool portRandoRunSeed(ttyd_rando::Options& options, std::vector<ttyd_rando::Placement>& placements) {
  if (!sRunMade) return false;
  options = sRunOptions;
  placements = sRunPlacements;
  return true;
}

bool portRandoStartRun(const char* userPath, PortRandoRun& out) {
  portRandoSettings(userPath);
  const PortRandoSettings& s = sSettings;
  if (!s.enabled || s.openWorld) return false; // (Paper Cut's open world is a file's, not a run's: portOpenArmNewFile)
  if (!portRandoSupported()) {
    SDL_Log("randomizer: on in the settings, but its location data is the US version's (this is %s)", PORT_GAME_ID);
    return false;
  }
  if (s.seed[0] == '\0') portRandoNewSeed(); // on with no seed: one now, kept
  const ttyd_rando::Options o = optionsOf(s);
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  if (!r.ok) {
    SDL_Log("randomizer: seed %s: %s", s.seed, r.error.c_str());
    sActive = std::string("seed ") + s.seed + " could not be made: " + r.error;
    return false;
  }
  out.seed = s.seed;
  sRunText = textOf(s);
  out.openWorld = o.logic == ttyd_rando::kOpenWorld;
  for (const ttyd_rando::Placement& p : r.placements) out.placements.emplace_back(p.location, p.item);
  sRunOptions = o;
  sRunPlacements = r.placements;
  sRunMade = true;
  sRunMystery = s.mystery != 0;
  sActive = std::string("seed ") + s.seed + ": " + ttyd_rando::poolsText(o.pools | (out.openWorld ? ttyd_rando::kKeyItems : 0u)) +
            (out.openWorld ? " (open world)" : "") + ", " + std::to_string(r.moved) + " items moved";
  std::string spoiler;
  if (s.spoiler) {
    spoiler = writeSpoiler(s.seed, r.spoiler);
    if (!spoiler.empty()) sActive += "; spoiler log: " + spoiler;
  }
  SDL_Log("randomizer: %s", sActive.c_str());
  return true;
}

std::string portRandoSessionText(const char* userPath) {
  portRandoSettings(userPath);
  if (sSettings.enabled && sSettings.seed[0] == '\0' && !sForRun) {
    portRandoNewSeed(); // (made here once, and kept: every side plays the same seed)
  }
  return textOf(sSettings);
}

std::string portRandoRunText(bool* booted) {
  *booted = sBooted;
  return sRunText;
}

std::string portRandoRunTextOf(const std::string& settingsText) {
  const PortRandoSettings mine = sSettings;
  sSettings = kDefaults;
  parseSettings(std::vector<uint8_t>(settingsText.begin(), settingsText.end()), "the session's settings");
  const PortRandoSettings theirs = sSettings;
  sSettings = mine;
  // (as portRandoStartRun: on, not the open world's, on the US version)
  return theirs.enabled && !theirs.openWorld && portRandoSupported() ? textOf(theirs) : std::string();
}

void portRandoSettingsForRun(const std::string& text) {
  sSettings = kDefaults;
  if (text.empty()) {
    sForRun = false; // this player's own randomizer.json again
    sRead = false;
    readSettings();
    return;
  }
  sRead = true; // (never this player's file meanwhile)
  sForRun = true;
  parseSettings(std::vector<uint8_t>(text.begin(), text.end()), "the session's settings");
}
