// The code-mod host (port/include/port/mod_host.h): hook slots, the API a
// mod gets (port/include/ttyd_mod/ttyd_mod.h) and its services, and loading
// mods' libraries.
//
// Hooks: each hookable game function (port/mods/hooks.spec) dispatches
// through portHookSlots (port/include/port/hook_slot.h). The host's own
// hooks come first (frames on marioStMain, shops on evt_shop_setup, battles
// on Btl_UnitSetup), then the mods' in their order. A mod's code can't be
// unloaded: turning one off or on takes effect at the next start.
#include "port/hook_slot.h"
#include "port/hook_list.h"
#include "port/layout.h"
#include "port/mod_host.h"
#include "../archipelago/ttyd_ap.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// The mod API as the game provides it: ttyd_mod.h's struct, declared here
// without the game's headers (the same layout: every member a pointer or int).
struct ModApi {
  int version;
  const char* gameId;
  const char* modId;
  const char* dataDir;
  void (*log)(const char* fmt, ...);
  int (*hookPre)(int slot, void* fn);
  int (*hookPost)(int slot, void* fn);
  int (*hookReplace)(int slot, void* fn);
  void* (*original)(int slot);
  void (*onFrame)(void (*fn)(void*), void* user);
  void (*onFrameEnd)(void (*fn)(void*), void* user);
  void (*onDraw)(void (*fn)(void*), void* user);
  void (*drawText)(float x, float y, float size, unsigned int rgba, const char* text);
  void (*drawRect)(float x, float y, float w, float h, unsigned int rgba);
  void (*setMessage)(const char* tag, const char* text);
  void (*onArea)(void (*fn)(void*, const char*, int), void* user);
  void (*onShop)(void (*fn)(void*, const char*, void*, int), void* user);
  void (*onBattleStart)(void (*fn)(void*, void*), void* user);
  int (*optionInt)(const char* key, int fallback);
  void* (*find)(const char* name);
};

using ModInit = int (*)(const ModApi*);

#define PORT_HOOK_FILE(file) extern "C" void portHookInit_##file(void);
PORT_HOOK_FILES
#undef PORT_HOOK_FILE

extern "C" {
PortHookSlot portHookSlots[PORT_HOOK_COUNT];
}

// Mods built into the game (TTYD_BUILTIN_MODS): port/src/aurora/builtin_mods_gen.c.
extern "C" {
struct PortBuiltinMod {
  const char* id;
  ModInit init;
  const char* json;
};
extern const PortBuiltinMod portBuiltinMods[];
extern const int portBuiltinModCount;
}

namespace {

template <typename Fn>
struct Callback {
  Fn fn;
  void* user;
};

struct Running {
  std::string id;
  std::string dataDir;
  ModApi api{};
};

std::vector<std::unique_ptr<Running>> sRunning;
std::vector<Callback<void (*)(void*)>> sFrame, sFrameEnd, sDraw;
std::vector<Callback<void (*)(void*, const char*, int)>> sArea;
std::vector<Callback<void (*)(void*, const char*, void*, int)>> sShop;
std::vector<Callback<void (*)(void*, void*)>> sBattle;
std::vector<int> sReplacedBy(PORT_HOOK_COUNT, -1);
PortLayout sLayout;
bool sLayoutValid;

// log and optionInt need their mod also when called from a hook, where no
// callback scope says which: each running mod gets its own pair, bound to
// its index (up to kMaxCode mods with code).
constexpr int kMaxCode = 32;

template <int N>
void logAt(const char* fmt, ...) {
  char text[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  SDL_Log("[%s] %s", N < int(sRunning.size()) ? sRunning[N]->id.c_str() : "mod", text);
}

template <int N>
int optionAt(const char* key, int fallback) {
  return N < int(sRunning.size()) ? portModsOptionInt(sRunning[N]->id.c_str(), key, fallback) : fallback;
}

template <int... N>
constexpr auto logTable(std::integer_sequence<int, N...>) {
  return std::array<void (*)(const char*, ...), sizeof...(N)>{logAt<N>...};
}
template <int... N>
constexpr auto optionTable(std::integer_sequence<int, N...>) {
  return std::array<int (*)(const char*, int), sizeof...(N)>{optionAt<N>...};
}
constexpr auto kLog = logTable(std::make_integer_sequence<int, kMaxCode>{});
constexpr auto kOption = optionTable(std::make_integer_sequence<int, kMaxCode>{});

int addHook(int kind, int slot, void* fn) {
  if (slot < 0 || slot >= PORT_HOOK_COUNT || fn == nullptr) {
    return -1;
  }
  PortHookSlot& s = portHookSlots[slot];
  if (kind == 0 || kind == 1) {
    int& count = kind == 0 ? s.preCount : s.postCount;
    if (count >= PORT_HOOK_MAX) {
      return -1;
    }
    (kind == 0 ? s.pre : s.post)[count++] = fn;
  } else {
    if (s.replace != nullptr) {
      return -1; // a mod earlier in the order replaced it
    }
    s.replace = fn;
  }
  s.active = 1;
  return 0;
}

int apiHookPre(int slot, void* fn) { return addHook(0, slot, fn); }
int apiHookPost(int slot, void* fn) { return addHook(1, slot, fn); }
int apiHookReplace(int slot, void* fn) {
  const int r = addHook(2, slot, fn);
  if (r != 0 && slot >= 0 && slot < PORT_HOOK_COUNT) {
    SDL_Log("mods: hook slot %d is already replaced by another mod", slot);
  }
  return r;
}
void* apiOriginal(int slot) { return slot >= 0 && slot < PORT_HOOK_COUNT ? portHookSlots[slot].body : nullptr; }

void apiOnFrame(void (*fn)(void*), void* user) { sFrame.push_back({fn, user}); }
void apiOnFrameEnd(void (*fn)(void*), void* user) { sFrameEnd.push_back({fn, user}); }
void apiOnDraw(void (*fn)(void*), void* user) { sDraw.push_back({fn, user}); }
void apiOnArea(void (*fn)(void*, const char*, int), void* user) { sArea.push_back({fn, user}); }
void apiOnShop(void (*fn)(void*, const char*, void*, int), void* user) { sShop.push_back({fn, user}); }
void apiOnBattle(void (*fn)(void*, void*), void* user) { sBattle.push_back({fn, user}); }

// Game coordinates (640x480) to the window's, over the picture.
ImVec2 toScreen(float x, float y) {
  if (!sLayoutValid) {
    return ImVec2(x, y);
  }
  return ImVec2(sLayout.picX + x * sLayout.picW / 640.0f, sLayout.picY + y * sLayout.picH / 480.0f);
}

ImU32 color(unsigned int rgba) {
  return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

void apiDrawText(float x, float y, float size, unsigned int rgba, const char* text) {
  if (text == nullptr || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  const float scale = sLayoutValid ? sLayout.picH / 480.0f : 1.0f;
  ImGui::GetBackgroundDrawList()->AddText(ImGui::GetFont(), size * scale, toScreen(x, y), color(rgba), text);
}

void apiDrawRect(float x, float y, float w, float h, unsigned int rgba) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGui::GetBackgroundDrawList()->AddRectFilled(toScreen(x, y), toScreen(x + w, y + h), color(rgba));
}

void apiSetMessage(const char* tag, const char* text) { portModsSetMessage(tag, text); }

void* apiFind(const char* name) {
#ifdef _WIN32
  // the game executable's exports (CMakeLists.txt: every game symbol)
  return reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(nullptr), name));
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

template <typename List, typename... Args>
void fire(List& list, Args... args) {
  for (auto& c : list) {
    c.fn(c.user, args...);
  }
}

// The host's hooks on marioStMain (void(void): pre is int(void), post
// void(void)): the mods' frame callbacks.
int framePreHook() {
  portApFrame(); // an Archipelago session's sync
  fire(sFrame);
  return 0;
}
void framePost() { fire(sFrameEnd); }

Running* start(const char* modId, const char* dataDir, ModInit init, std::string& why) {
  if (sRunning.size() >= kMaxCode) {
    why = "is past the most mods with code at once (" + std::to_string(kMaxCode) + ")";
    return nullptr;
  }
  auto r = std::make_unique<Running>();
  r->id = modId;
  r->dataDir = dataDir;
  SDL_CreateDirectory(dataDir);
  ModApi& a = r->api;
  a.version = 1;
  a.gameId = PORT_GAME_ID;
  a.modId = r->id.c_str();
  a.dataDir = r->dataDir.c_str();
  a.log = kLog[sRunning.size()];
  a.hookPre = apiHookPre;
  a.hookPost = apiHookPost;
  a.hookReplace = apiHookReplace;
  a.original = apiOriginal;
  a.onFrame = apiOnFrame;
  a.onFrameEnd = apiOnFrameEnd;
  a.onDraw = apiOnDraw;
  a.drawText = apiDrawText;
  a.drawRect = apiDrawRect;
  a.setMessage = apiSetMessage;
  a.onArea = apiOnArea;
  a.onShop = apiOnShop;
  a.onBattleStart = apiOnBattle;
  a.optionInt = kOption[sRunning.size()];
  a.find = apiFind;
  Running* raw = r.get();
  sRunning.push_back(std::move(r));
  const int result = init(&raw->api);
  if (result != 0) {
    why = "its code failed to start (" + std::to_string(result) + ")";
    return nullptr;
  }
  SDL_Log("mods: %s: code running", modId);
  return raw;
}

} // namespace

extern "C" {

void portModHostInit(void) {
#define PORT_HOOK_FILE(file) portHookInit_##file();
  PORT_HOOK_FILES
#undef PORT_HOOK_FILE
#define PORT_HOOK(index, name) const int kSlot_##name = index;
  PORT_HOOK_LIST
#undef PORT_HOOK
  addHook(0, kSlot_marioStMain, reinterpret_cast<void*>(framePreHook));
  addHook(1, kSlot_marioStMain, reinterpret_cast<void*>(framePost));
  portModGameInit();
}

void portModHostAddHook(int kind, int slot, void* fn) { addHook(kind, slot, fn); }

const char* portModHostLoadLibrary(const char* modId, const char* libPath, const char* dataDir) {
  static std::string why;
  SDL_SharedObject* lib = SDL_LoadObject(libPath);
  if (lib == nullptr) {
    why = std::string("its code can't be loaded: ") + SDL_GetError();
    return why.c_str();
  }
  const auto init = reinterpret_cast<ModInit>(SDL_LoadFunction(lib, "ttyd_mod_init"));
  if (init == nullptr) {
    why = "its library has no ttyd_mod_init (TTYD_MOD_ENTRY)";
    return why.c_str();
  }
  why.clear();
  return start(modId, dataDir, init, why) != nullptr ? nullptr : why.c_str();
}

int portModHostHasBuiltin(const char* modId) {
  for (int i = 0; i < portBuiltinModCount; i++) {
    if (strcmp(portBuiltinMods[i].id, modId) == 0) {
      return 1;
    }
  }
  return 0;
}

const char* portModHostBuiltinId(int index) {
  return index >= 0 && index < portBuiltinModCount ? portBuiltinMods[index].id : nullptr;
}

const char* portModHostBuiltinJson(int index) {
  return index >= 0 && index < portBuiltinModCount ? portBuiltinMods[index].json : nullptr;
}

const char* portModHostStartBuiltin(const char* modId, const char* dataDir) {
  static std::string why;
  for (int i = 0; i < portBuiltinModCount; i++) {
    if (strcmp(portBuiltinMods[i].id, modId) == 0) {
      why.clear();
      return start(modId, dataDir, portBuiltinMods[i].init, why) != nullptr ? nullptr : why.c_str();
    }
  }
  return "is not built into this game";
}

void portModsDraw(void) {
  if (sDraw.empty()) {
    return;
  }
  sLayoutValid = portLayoutGet(&sLayout) != 0;
  fire(sDraw);
}

void portModsAreaEvent(const char* area, int entered) {
  if (entered) {
    portApAreaLinked(area); // an Archipelago seed's items, before the mods see the area
  }
  fire(sArea, area, entered);
}

void portModsShopEvent(const char* map, void* items, int count) { fire(sShop, map, items, count); }

void portModsBattleEvent(void* battleWork) { fire(sBattle, battleWork); }

} // extern "C"
