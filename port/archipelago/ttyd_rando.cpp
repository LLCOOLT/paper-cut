// The built-in randomizer (port/archipelago/ttyd_rando.h): its settings, the
// seed generated at start, the spoiler log.
#include "ttyd_rando.h"
#include "rando_gen.h"

#include "port/json.h"
#include "port/mods.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

using port_json::Json;

namespace {

PortRandoSettings sSettings = {0, "", 1, 1, 1, 1, 0, 1, 0, 0};
std::string sUserPath;
bool sRead = false;
std::string sActive; // this run's
std::string sCheck;

std::string settingsFile() { return sUserPath + "/randomizer.json"; }

void readSettings() {
  if (sRead || sUserPath.empty()) return;
  sRead = true;
  size_t size = 0;
  void* data = SDL_LoadFile(settingsFile().c_str(), &size);
  if (data == nullptr) return;
  std::vector<uint8_t> bytes(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  SDL_free(data);
  Json j;
  std::string why;
  if (!port_json::parseJson(bytes, j, why)) {
    SDL_Log("randomizer: %s: %s", settingsFile().c_str(), why.c_str());
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
  std::snprintf(sSettings.seed, sizeof(sSettings.seed), "%s", j.str("seed").c_str());
}

ttyd_rando::Options optionsOf(const PortRandoSettings& s) {
  ttyd_rando::Options o;
  o.seed = s.seed;
  o.pools = (s.badges ? ttyd_rando::kBadges : 0u) | (s.items ? ttyd_rando::kItems : 0u) |
            (s.starPieces ? ttyd_rando::kStarPieces : 0u) | (s.shineSprites ? ttyd_rando::kShineSprites : 0u) |
            (s.keyItems ? ttyd_rando::kKeyItems : 0u);
  o.logic = s.openWorld ? ttyd_rando::kPaperCut : s.keyItems ? ttyd_rando::kOpenWorld : ttyd_rando::kVanillaStory;
  o.goalStars = s.openGoal;
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
  std::string out = text;
  out += "\nGame: " + std::string(PORT_GAME_ID) + "\n";
  if (!SDL_SaveFile(path.c_str(), out.data(), out.size())) {
    SDL_Log("randomizer: can't write %s: %s", path.c_str(), SDL_GetError());
    return "";
  }
  return path;
}

} // namespace

extern "C" {

int portRandoSupported(void) { return std::strcmp(PORT_GAME_ID, "G8ME01") == 0; }

PortRandoSettings* portRandoSettings(const char* userPath) {
  if (sUserPath.empty() && userPath != nullptr) sUserPath = userPath;
  readSettings();
  return &sSettings;
}

void portRandoSaveSettings(void) {
  if (sUserPath.empty()) return;
  const PortRandoSettings& s = sSettings;
  const Json j = port_json::object({{"enabled", port_json::boolean(s.enabled != 0)},
                                    {"seed", port_json::string(s.seed)},
                                    {"badges", port_json::boolean(s.badges != 0)},
                                    {"items", port_json::boolean(s.items != 0)},
                                    {"star_pieces", port_json::boolean(s.starPieces != 0)},
                                    {"shine_sprites", port_json::boolean(s.shineSprites != 0)},
                                    {"key_items", port_json::boolean(s.keyItems != 0)},
                                    {"spoiler", port_json::boolean(s.spoiler != 0)},
                                    {"open_world", port_json::boolean(s.openWorld != 0)},
                                    {"open_goal", port_json::number(s.openGoal)}});
  const std::string text = port_json::dump(j);
  if (!SDL_SaveFile(settingsFile().c_str(), text.data(), text.size())) {
    SDL_Log("randomizer: can't write %s: %s", settingsFile().c_str(), SDL_GetError());
  }
}

void portRandoNewSeed(void) {
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

// A new file, with Paper Cut's open world on: the file takes the settings'
// seed (a new one when it is empty, kept), checked and its spoiler written.
int portOpenArmNewFile(char* seed, int seedMax, unsigned* pools, int* goal) {
  const PortRandoSettings& s = sSettings;
  if (!sRead || !s.enabled || !s.openWorld || !portRandoSupported()) return 0;
  if (s.seed[0] == '\0') portRandoNewSeed();
  const ttyd_rando::Options o = optionsOf(s);
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  if (!r.ok) {
    SDL_Log("randomizer: open world: seed %s: %s", s.seed, r.error.c_str());
    sActive = std::string("seed ") + s.seed + " could not be made: " + r.error;
    return 0;
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
  out.openWorld = o.logic == ttyd_rando::kOpenWorld;
  for (const ttyd_rando::Placement& p : r.placements) out.placements.emplace_back(p.location, p.item);
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
