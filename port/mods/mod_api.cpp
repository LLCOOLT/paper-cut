// The code-mod host (port/include/port/mod_host.h): hook slots, the API a
// mod gets (port/include/ttyd_mod/ttyd_mod.h) and its services, and loading
// mods' libraries.
//
// Hooks: each hookable game function (port/mods/hooks.spec) dispatches
// through portHookSlots (port/include/port/hook_slot.h). The host's own
// hooks come first (frames on marioStMain, shops on evt_shop_setup, battles
// on Btl_UnitSetup), then the mods' in their order. A mod's code can't be
// unloaded: turning one off or on takes effect at the next start.
#include "port/file_pc.h"
#include "port/hook_slot.h"
#include "port/hook_list.h"
#include "port/layout.h"
#include "port/lua_mods.h"
#include "port/mod_host.h"
#include "../archipelago/ttyd_ap.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
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
  bool failed = false; // its init failed: what it registered was taken out, and it can't add more
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
  if (s.body == nullptr) {
    SDL_Log("mods: hook slot %d is no longer used (its function left the list)", slot);
    return -1;
  }
  if (kind == 0 || kind == 1) {
    int& count = kind == 0 ? s.preCount : s.postCount;
    if (count >= PORT_HOOK_MAX) {
      // (said out loud: a feature whose hook is dropped here silently never runs; 0.5.0's
      // marioStMain after-hooks once passed the old 16, and the title menu's, versus' and the
      // story tests' frame hooks were lost)
      SDL_Log("mods: hook slot %d is full (%d %s-hooks): one more is not added", slot, PORT_HOOK_MAX,
              kind == 0 ? "before" : "after");
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

// Which mod took which slot (the mod manager's hooks and conflicts): each
// running mod gets its own hook functions, bound to its index, as log does.
struct HookUse {
  int mod, slot, kind;
  void* fn;
};
std::vector<HookUse> sHookUses;
const char* sSlotNames[PORT_HOOK_COUNT];

int useHook(int mod, int kind, int slot, void* fn) {
  if (mod < int(sRunning.size()) && sRunning[size_t(mod)]->failed) {
    return -1; // (a mod whose init failed: nothing of it runs)
  }
  const int r = addHook(kind, slot, fn);
  if (r != 0 && kind == 2 && slot >= 0 && slot < PORT_HOOK_COUNT) {
    SDL_Log("mods: hook slot %d is already replaced by another mod", slot);
  }
  if (r == 0) {
    sHookUses.push_back({mod, slot, kind, fn});
  }
  return r;
}

template <int N>
int hookPreAt(int slot, void* fn) {
  return useHook(N, 0, slot, fn);
}
template <int N>
int hookPostAt(int slot, void* fn) {
  return useHook(N, 1, slot, fn);
}
template <int N>
int hookReplaceAt(int slot, void* fn) {
  return useHook(N, 2, slot, fn);
}
template <int... N>
constexpr auto hookTable(std::integer_sequence<int, N...>) {
  using Fn = int (*)(int, void*);
  return std::array<std::array<Fn, 3>, sizeof...(N)>{std::array<Fn, 3>{hookPreAt<N>, hookPostAt<N>, hookReplaceAt<N>}...};
}
constexpr auto kHook = hookTable(std::make_integer_sequence<int, kMaxCode>{});
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

void apiSetMessage(const char* tag, const char* text) { portModsSetModMessage(tag, text); } // (a mod's: the budget)

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
#ifdef PORT_HOOK_PROFILE
// A build with -DPORT_HOOK_PROFILE=1 (tools/port/gen_hooks.py): each slot's
// calls per frame, to $TTYD_HOOK_PROFILE every 600 frames, the most first.
void profileFrame() {
  static unsigned frames;
  const char* path = getenv("TTYD_HOOK_PROFILE");
  if (path == nullptr || ++frames % 600 != 0) {
    return;
  }
  std::vector<std::pair<unsigned, int>> counts;
  for (int i = 0; i < PORT_HOOK_COUNT; i++) {
    if (portHookSlots[i].calls != 0) {
      counts.push_back({portHookSlots[i].calls, i});
    }
  }
  std::sort(counts.rbegin(), counts.rend());
  if (FILE* f = portFopen(path, "w")) {
    fprintf(f, "# calls per frame over %u frames\n", frames);
    for (const auto& [calls, slot] : counts) {
      fprintf(f, "%10.2f %s\n", double(calls) / frames, sSlotNames[slot] != nullptr ? sSlotNames[slot] : "?");
    }
    fclose(f);
  }
}
#endif

int framePreHook() {
#ifdef PORT_HOOK_PROFILE
  profileFrame();
#endif
  portApFrame(); // an Archipelago session's sync
  fire(sFrame);
  return 0;
}
void framePost() { fire(sFrameEnd); }

// The host's own drawing (a function-local list: features add theirs from
// their static initializers, before this file's statics may exist).
std::vector<Callback<void (*)(void*)>>& hostDraw() {
  static std::vector<Callback<void (*)(void*)>> list;
  return list;
}

// How much was registered before a mod's init, to take its part out again
// if the init fails (its hooks in sHookUses from there, the callbacks past
// these sizes: init runs on the main thread, so they are all its own).
struct Registered {
  size_t hooks, frame, frameEnd, draw, area, shop, battle;
};

Registered registered() {
  return {sHookUses.size(), sFrame.size(), sFrameEnd.size(), sDraw.size(), sArea.size(), sShop.size(), sBattle.size()};
}

void removeHook(const HookUse& u) {
  PortHookSlot& s = portHookSlots[u.slot];
  if (u.kind == 0 || u.kind == 1) {
    int& count = u.kind == 0 ? s.preCount : s.postCount;
    void** list = u.kind == 0 ? s.pre : s.post;
    for (int i = count - 1; i >= 0; i--) {
      if (list[i] == u.fn) {
        std::memmove(&list[i], &list[i + 1], sizeof(list[0]) * size_t(count - i - 1));
        list[--count] = nullptr;
        break;
      }
    }
  } else if (s.replace == u.fn) {
    s.replace = nullptr;
  }
  s.active = s.preCount > 0 || s.postCount > 0 || s.replace != nullptr;
}

void rollBack(const Registered& at, int mod) {
  for (size_t i = sHookUses.size(); i > at.hooks; i--) {
    if (sHookUses[i - 1].mod == mod) {
      removeHook(sHookUses[i - 1]);
    }
  }
  sHookUses.resize(at.hooks);
  sFrame.resize(at.frame);
  sFrameEnd.resize(at.frameEnd);
  sDraw.resize(at.draw);
  sArea.resize(at.area);
  sShop.resize(at.shop);
  sBattle.resize(at.battle);
}

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
  a.hookPre = kHook[sRunning.size()][0];
  a.hookPost = kHook[sRunning.size()][1];
  a.hookReplace = kHook[sRunning.size()][2];
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
  const Registered before = registered();
  const int result = init(&raw->api);
  if (result != 0) {
    // what the half-started mod registered goes again: its hooks would run
    // (and hold a replace slot) though the Mods tab says it isn't running
    raw->failed = true;
    rollBack(before, int(sRunning.size()) - 1);
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
#define PORT_HOOK(index, name) sSlotNames[index] = #name;
  PORT_HOOK_LIST
#undef PORT_HOOK
  addHook(0, kSlot_marioStMain, reinterpret_cast<void*>(framePreHook));
  addHook(1, kSlot_marioStMain, reinterpret_cast<void*>(framePost));
  portModGameInit();
}

void portModHostAddHook(int kind, int slot, void* fn) {
  // Paper Cut's own features: a hook that doesn't fit is a feature silently off (the frame slot once held
  // more than PORT_HOOK_MAX: the last ones added, the test hooks among them, never ran), so say it loudly
  if (addHook(kind, slot, fn) != 0) {
    std::fprintf(stderr, "[hooks] FULL: Paper Cut's own hook (kind %d) on slot %d not added (PORT_HOOK_MAX %d)\n",
                 kind, slot, PORT_HOOK_MAX);
  }
}

const char* portModHostHookUse(int i, const char** slotName, int* kind) {
  if (i < 0 || i >= int(sHookUses.size())) {
    return nullptr;
  }
  const HookUse& u = sHookUses[size_t(i)];
  *slotName = sSlotNames[u.slot] != nullptr ? sSlotNames[u.slot] : "?";
  *kind = u.kind;
  return u.mod < int(sRunning.size()) ? sRunning[size_t(u.mod)]->id.c_str() : "?";
}

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

void portModHostAddDraw(void (*fn)(void*), void* user) { hostDraw().push_back({fn, user}); }

void portModHostDrawText(float x, float y, float size, unsigned int rgba, const char* text) {
  apiDrawText(x, y, size, rgba, text);
}

void portModHostDrawRect(float x, float y, float w, float h, unsigned int rgba) { apiDrawRect(x, y, w, h, rgba); }

void portModsDraw(void) {
  if (sDraw.empty() && hostDraw().empty()) {
    return;
  }
  sLayoutValid = portLayoutGet(&sLayout) != 0;
  fire(hostDraw());
  fire(sDraw);
}

void portModsAreaEvent(const char* area, int entered) {
  if (entered) {
    portApAreaLinked(area); // an Archipelago seed's items, before the mods see the area
  }
  fire(sArea, area, entered);
  portLuaEvent(entered ? "area_enter" : "area_leave", "s", area); // Lua mods (port/include/port/lua_mods.h)
}

void portModsShopEvent(const char* map, void* items, int count) { fire(sShop, map, items, count); }

void portModsBattleEvent(void* battleWork) { fire(sBattle, battleWork); }

} // extern "C"
