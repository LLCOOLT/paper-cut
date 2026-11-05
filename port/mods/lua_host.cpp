// Lua mods' host (port/include/port/lua_mods.h, docs/LUA_MODDING.md): one
// sandboxed Lua state per mod, its events, the host's part of its API (ttyd,
// hud, rules, require) and the console. The game's part (player, items,
// flags, map, battle) is port/src/aurora/lua_game_aurora.c's.
//
// The sandbox: base (without dofile, loadfile; load takes text only), string
// (without dump), table, math, utf8 and coroutine. No io, os, package or
// debug: no files, no network, no clock, no C modules. require reads the
// mod's own lua/ files. Each state has a memory cap; each call into it an
// instruction budget (a count hook): past it, every instruction errors, so a
// pcall can't swallow it. An error stops that mod alone.
//
// Online lockstep: Lua's seeds are fixed (libs/lua/lua_port.h), its maths
// are the game's fdlibm, math.random is seeded from the mod's id, and the
// mods run only when both players' sets hash the same (the hello's lua);
// see sessionFull(). Events come from the game frame (never the drawing), so
// every player's mods see the same calls in the same order.
#include "port/lua_mods.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/replay.h"
#include "evt_asm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include "lua_port_api.h"

extern "C" {
// the looks (port/include/port/costume.h, the game's side)
int portSkinCount(void);
const char* portSkinName(int i);
int portSkinOf(int party);
int portCostumeCount(void);
const char* portCostumeName(int i);
int portCostumeCurrent(void);
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t kMemoryCap = 32u << 20;   // bytes per mod
constexpr int kHookEvery = 1000;           // instructions between budget checks
constexpr long kCallBudget = 1000;         // checks per call: 1M instructions
constexpr long kMainBudget = 10000;        // its main.lua (and require): 10M
constexpr int kReloadEvery = 30;           // frames between hot-reload checks
constexpr size_t kConsoleLines = 300;

struct LuaMod {
  PortLuaModSpec spec;
  std::string hash;     // of spec (portLuaHashOf over just this one)
  lua_State* L = nullptr;
  size_t memory = 0;
  unsigned created = 0; // blocks its state made (their creation numbers)
  unsigned long matchSteps = 0; // pattern steps not charged to the budget yet
  long budget = 0;
  bool overBudget = false;
  int depth = 0;        // a call into it is running (events then skip it)
  int state = PORT_LUA_NONE;
  std::string error;
  std::set<std::string> events; // the events it has handlers for
  int keepSave = LUA_NOREF, keepLoad = LUA_NOREF; // ttyd.keep's (registry refs)
  // looks.set_skin / set_costume: per partner (1-7), "" their own look; nullopt the Wardrobe's
  std::array<std::optional<std::string>, 8> skins;
  std::optional<std::string> costume;
  std::vector<std::pair<std::string, int>> rules;
  // hot reload: its lua/ folder's newest change and file count
  SDL_Time newest = 0;
  int fileCount = 0;
};

std::vector<std::unique_ptr<LuaMod>> sMods;
std::vector<PortLuaModSpec> sPending;
bool sHavePending;
std::mutex sPendingLock;
std::atomic<bool> sReloadAll{false};
std::string sHash;
unsigned sFrame;
int sDispatching; // events running (nested ones too)

// The HUD: drawn by the mods in the game frame, shown by the drawing.
struct HudItem {
  bool text;
  float x, y, w, h, size;
  unsigned rgba;
  bool shadow;
  std::string s;
};
std::vector<HudItem> sHudBuild, sHudShown;
std::mutex sHudLock;

// The console.
struct ConsoleLine {
  std::string text;
  bool error;
};
std::deque<ConsoleLine> sConsole;
std::string sLastError;
Uint64 sLastErrorAt;
std::mutex sConsoleLock;

void console(const std::string& text, bool error) {
  SDL_Log("lua: %s", text.c_str());
  std::lock_guard lock(sConsoleLock);
  // a long text a line each
  size_t at = 0;
  while (at <= text.size()) {
    size_t eol = text.find('\n', at);
    if (eol == std::string::npos) {
      eol = text.size();
    }
    sConsole.push_back({text.substr(at, eol - at), error});
    at = eol + 1;
  }
  while (sConsole.size() > kConsoleLines) {
    sConsole.pop_front();
  }
  if (error) {
    sLastError = text.substr(0, text.find('\n'));
    sLastErrorAt = SDL_GetTicks();
  }
}

uint64_t fnv(uint64_t h, const void* data, size_t size) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; i++) {
    h = (h ^ p[i]) * 0x100000001b3ull;
  }
  return h;
}

uint64_t fnvString(uint64_t h, const std::string& s) {
  const uint32_t n = uint32_t(s.size());
  h = fnv(h, &n, sizeof(n));
  return fnv(h, s.data(), s.size());
}

LuaMod*& modOf(lua_State* L) { return *static_cast<LuaMod**>(lua_getextraspace(L)); }

// --- the online session ---

// The mods may run fully: no session, or one where every hello carries the
// same set as this side's (the hellos both sides sent: they decide alike).
bool sessionFull() {
  if (!portOnlineActive() && !portOnlineWatching()) {
    return true;
  }
  const PortOnlineHello* me = portOnlineMe();
  const PortOnlineHello* peer = portOnlinePeer();
  if (me == nullptr || peer == nullptr || std::strcmp(me->lua, peer->lua) != 0) {
    return false;
  }
  if (portOnlineWatching() || portOnlineSeating()) { // (a seat too: the guest's hash from W_SETUP)
    const PortOnlineHello* guest = portOnlineWatchedGuest();
    return guest != nullptr && std::strcmp(me->lua, guest->lua) == 0;
  }
  return true;
}

bool sFull = true; // sessionFull(), as of this frame's start
bool sOnline;      // a session runs (no hot reload, no Reload)

bool runs(const LuaMod& m) { return m.L != nullptr && (sFull || m.spec.hudOnline); }

// --- calling into a state ---

void countHook(lua_State* L, lua_Debug*);

[[noreturn]] void pastBudget(lua_State* L, LuaMod* m) {
  if (!m->overBudget) {
    m->overBudget = true;
    // from now every instruction errors (here and on the main thread), so a
    // pcall can't catch it and go on
    lua_sethook(L, countHook, LUA_MASKCOUNT, 1);
    if (L != m->L) {
      lua_sethook(m->L, countHook, LUA_MASKCOUNT, 1);
    }
  }
  luaL_error(L, "took too long (past its limit of instructions for one call)");
  std::abort(); // (luaL_error doesn't return)
}

void countHook(lua_State* L, lua_Debug*) {
  LuaMod* m = modOf(L);
  if (!m->overBudget && --m->budget > 0) {
    return;
  }
  pastBudget(L, m);
}

// Pattern matching's steps (libs/lua/lua_port_match.c: it runs in C, outside
// the count hook), an instruction each, from the same budget.
void matchHook(lua_State* L, unsigned long steps) {
  LuaMod* m = modOf(L);
  m->matchSteps += steps;
  m->budget -= long(m->matchSteps / kHookEvery);
  m->matchSteps %= kHookEvery;
  if (m->overBudget || m->budget <= 0) {
    pastBudget(L, m);
  }
}

int traceback(lua_State* L) {
  const char* msg = lua_tostring(L, 1);
  if (msg == nullptr) {
    msg = luaL_typename(L, 1);
    msg = lua_pushfstring(L, "(an error object: %s)", msg);
  }
  luaL_traceback(L, L, msg, 1);
  return 1;
}

void closeMod(LuaMod& m) {
  if (m.L != nullptr) {
    lua_close(m.L);
    m.L = nullptr;
  }
  m.events.clear();
  m.rules.clear();
  m.keepSave = m.keepLoad = LUA_NOREF;
  m.skins = {};
  m.costume.reset();
}

void fail(LuaMod& m, const std::string& why) {
  m.state = PORT_LUA_FAILED;
  m.error = why.substr(0, why.find('\n'));
  console(m.spec.id + " stopped: " + why, true);
  closeMod(m);
}

// The function and its nargs arguments on the stack: called with the budget,
// nresults left on the stack (true), or the mod stopped (false, stack as before).
bool call(LuaMod& m, int nargs, int nresults, long budget = kCallBudget) {
  lua_State* L = m.L;
  const int base = lua_gettop(L) - nargs;
  lua_pushcfunction(L, traceback);
  lua_insert(L, base);
  m.budget = budget;
  m.matchSteps = 0;
  m.overBudget = false;
  lua_sethook(L, countHook, LUA_MASKCOUNT, kHookEvery);
  m.depth++;
  sDispatching++;
  const int r = lua_pcall(L, nargs, nresults, base);
  sDispatching--;
  m.depth--;
  if (r != LUA_OK) {
    std::string why = r == LUA_ERRMEM ? "ran out of memory (past its " + std::to_string(kMemoryCap >> 20) + " MB)"
                                      : (lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "an error");
    lua_settop(L, base - 1);
    fail(m, why);
    return false;
  }
  lua_remove(L, base);
  return true;
}

// The function on the top called, its arguments pushed by push (their count)
// inside the protected call: pushing a string or a table can run out of
// memory, which outside a protected call would end the game (Lua's panic).
// after (if any) then sees the results, still protected, and none are kept.
struct Job {
  int (*push)(lua_State* L, void* user);
  void (*after)(lua_State* L, void* user);
  void* user;
  int nresults;
};

int trampoline(lua_State* L) {
  Job* j = static_cast<Job*>(lua_touserdata(L, 1));
  const int n = j->push != nullptr ? j->push(L, j->user) : 0;
  lua_call(L, n, j->nresults);
  if (j->after != nullptr) {
    j->after(L, j->user);
    return 0;
  }
  return j->nresults;
}

bool callWith(LuaMod& m, Job& job) {
  lua_State* L = m.L;
  lua_pushcfunction(L, trampoline); // (a light C function: nothing to allocate)
  lua_insert(L, -2);
  lua_pushlightuserdata(L, &job);
  lua_insert(L, -2);
  return call(m, 2, job.after != nullptr ? 0 : job.nresults);
}

// --- the sandbox ---

void* alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* m = static_cast<LuaMod*>(ud);
  if (ptr == nullptr) {
    osize = 0;
  }
  if (nsize == 0) {
    lua_port_free(ptr);
    m->memory -= osize;
    return nullptr;
  }
  if (nsize > osize && m->memory - osize + nsize > kMemoryCap) {
    return nullptr;
  }
  // (numbered: tables, functions and coroutines hash by it, libs/lua/lua_port.h)
  void* p = lua_port_realloc(ptr, nsize, &m->created);
  if (p != nullptr) {
    m->memory = m->memory - osize + nsize;
  }
  return p;
}

// --- the same text on every computer ---
// Lua writes a table, function or coroutine as its address ("table:
// 0000021C..."), which differs on each computer: a mod that used it as a
// key, or printed it into its state, would split online games. Here it is
// the value's creation number instead (libs/lua/lua_port_api.h), in tostring,
// print, string.format's %s and %p, and the HUD.

bool addressed(int type) {
  return type == LUA_TTABLE || type == LUA_TFUNCTION || type == LUA_TTHREAD || type == LUA_TUSERDATA ||
         type == LUA_TLIGHTUSERDATA;
}

// "0x0000002a" (or "builtin": a light C function, which has none)
void pushId(lua_State* L, int idx) {
  const unsigned id = lua_port_objid(L, idx);
  if (id == 0) {
    lua_pushliteral(L, "builtin");
    return;
  }
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08x", id);
  lua_pushstring(L, text);
}

// luaL_tolstring, but by creation number: the string pushed, and returned
const char* toText(lua_State* L, int idx, size_t* len) {
  idx = lua_absindex(L, idx);
  if (!addressed(lua_type(L, idx))) {
    return luaL_tolstring(L, idx, len);
  }
  if (luaL_getmetafield(L, idx, "__tostring") != LUA_TNIL) {
    lua_pop(L, 1);
    return luaL_tolstring(L, idx, len); // (the mod's own)
  }
  const int named = luaL_getmetafield(L, idx, "__name"); // (pushes nothing when none)
  std::string kind = named == LUA_TSTRING ? lua_tostring(L, -1) : luaL_typename(L, idx);
  if (named != LUA_TNIL) {
    lua_pop(L, 1);
  }
  pushId(L, idx);
  kind += ": ";
  kind += lua_tostring(L, -1);
  lua_pop(L, 1);
  lua_pushlstring(L, kind.data(), kind.size());
  return lua_tolstring(L, -1, len);
}

int luaToString(lua_State* L) {
  luaL_checkany(L, 1);
  toText(L, 1, nullptr);
  return 1;
}

// string.format: %s of a table (...) and %p of anything given as its number
// first (here, so the strings are gone before Lua's own format may raise an
// error: errors skip C++ destructors), then Lua's own format
void prepareFormat(lua_State* L) {
  size_t fl;
  const char* fmt = luaL_checklstring(L, 1, &fl);
  const int top = lua_gettop(L);
  std::string out;
  bool changed = false;
  int arg = 1;
  for (size_t i = 0; i < fl; i++) {
    out += fmt[i];
    if (fmt[i] != '%') {
      continue;
    }
    if (i + 1 < fl && fmt[i + 1] == '%') {
      out += '%';
      i++;
      continue;
    }
    size_t j = i + 1;
    while (j < fl && fmt[j] != '\0' && std::strchr("-+ #0123456789.", fmt[j]) != nullptr) {
      j++;
    }
    if (j >= fl) {
      break; // (Lua's own error)
    }
    out.append(fmt + i + 1, j - i - 1);
    char c = fmt[j];
    if (++arg <= top) {
      const int type = lua_type(L, arg);
      if (c == 's' && addressed(type)) {
        toText(L, arg, nullptr);
        lua_replace(L, arg);
        changed = true;
      } else if (c == 'p' && (addressed(type) || type == LUA_TSTRING)) {
        pushId(L, arg);
        lua_replace(L, arg);
        c = 's'; // (the flags %p takes, '-' and a width, are %s's too)
        changed = true;
      }
    }
    out += c;
    i = j;
  }
  if (changed) {
    lua_pushlstring(L, out.data(), out.size());
    lua_replace(L, 1);
  }
}

int luaFormat(lua_State* L) {
  prepareFormat(L);
  const int top = lua_gettop(L);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, top, 1);
  return 1;
}

int luaPrint(lua_State* L) {
  std::string line;
  const int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    size_t len;
    const char* s = toText(L, i, &len);
    if (i > 1) {
      line += '\t';
    }
    line.append(s, len);
    lua_pop(L, 1);
  }
  console("[" + modOf(L)->spec.id + "] " + line, false);
  return 0;
}

// load(chunk, name, mode, env): text only
int luaLoad(lua_State* L) {
  if (lua_gettop(L) < 3) {
    lua_settop(L, 3);
  }
  lua_pushliteral(L, "t");
  lua_replace(L, 3);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

// math.randomseed(): without a seed, the mod's own (the same everywhere)
int luaRandomSeed(lua_State* L) {
  if (lua_gettop(L) == 0) {
    lua_pushinteger(L, lua_Integer(fnvString(0xcbf29ce484222325ull, modOf(L)->spec.id) & 0x7fffffffffffull));
  }
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

char kLoadedKey, kHandlersKey;

// require "a.b": the mod's lua/a/b.lua, run once
int luaRequire(lua_State* L) {
  const std::string name = luaL_checkstring(L, 1);
  bool ok = !name.empty() && name.front() != '.' && name.back() != '.' && name.find("..") == std::string::npos;
  for (char c : name) {
    ok &= (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
          c == '.';
  }
  if (!ok) {
    return luaL_error(L, "require: \"%s\" isn't a module name (letters, digits, _ and -, parts split by .)",
                      name.c_str());
  }
  lua_rawgetp(L, LUA_REGISTRYINDEX, &kLoadedKey);
  lua_getfield(L, -1, name.c_str());
  if (!lua_isnil(L, -1)) {
    return 1;
  }
  lua_pop(L, 1);
  std::string path = name;
  std::replace(path.begin(), path.end(), '.', '/');
  path = "lua/" + path + ".lua";
  const LuaMod* m = modOf(L);
  const auto it = m->spec.files.find(path);
  if (it == m->spec.files.end()) {
    return luaL_error(L, "require: the mod has no %s", path.c_str());
  }
  if (luaL_loadbufferx(L, it->second.data(), it->second.size(), ("@" + path).c_str(), "t") != LUA_OK) {
    return lua_error(L);
  }
  lua_pushstring(L, name.c_str());
  lua_call(L, 1, 1);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, name.c_str());
  return 1;
}

// --- ttyd: events, options, constants ---

int luaOn(lua_State* L) {
  const char* event = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_rawgetp(L, LUA_REGISTRYINDEX, &kHandlersKey);
  if (lua_getfield(L, -1, event) != LUA_TTABLE) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, event);
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, lua_Integer(lua_rawlen(L, -2) + 1));
  modOf(L)->events.insert(event);
  return 0;
}

int luaOff(lua_State* L) {
  const char* event = luaL_checkstring(L, 1);
  lua_settop(L, 2); // (without a function, index 2 is nil: not the table pushed below)
  lua_rawgetp(L, LUA_REGISTRYINDEX, &kHandlersKey);
  if (lua_getfield(L, -1, event) != LUA_TTABLE) {
    return 0;
  }
  const lua_Integer n = lua_Integer(lua_rawlen(L, -1));
  lua_newtable(L);
  lua_Integer kept = 0;
  for (lua_Integer i = 1; i <= n; i++) {
    lua_rawgeti(L, -2, i);
    if (lua_isnoneornil(L, 2) || lua_rawequal(L, -1, 2)) {
      lua_pop(L, 1); // (without a function: every one goes)
    } else {
      lua_rawseti(L, -2, ++kept);
    }
  }
  if (kept == 0) {
    lua_pop(L, 1);
    lua_pushnil(L);
    modOf(L)->events.erase(event);
  }
  lua_setfield(L, -3, event); // (the handlers table, under the old list)
  return 0;
}

int luaOption(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  const int fallback = int(luaL_optinteger(L, 2, 0));
  const LuaMod* m = modOf(L);
  if (sOnline || portReplayActive()) {
    // online and in replays, the values the run's set was made with (its hash, which the
    // others' match): an option changed on the Mods tab meanwhile changes one game only
    const std::string& all = m->spec.options;
    const std::string want = std::string(key) + "=";
    for (size_t at = 0; at < all.size();) {
      const size_t end = std::min(all.find('\n', at), all.size());
      if (all.compare(at, want.size(), want) == 0) {
        lua_pushinteger(L, std::strtol(all.c_str() + at + want.size(), nullptr, 10));
        return 1;
      }
      at = end + 1;
    }
    lua_pushinteger(L, fallback);
    return 1;
  }
  lua_pushinteger(L, portModsOptionInt(m->spec.id.c_str(), key, fallback));
  return 1;
}

int luaConst(lua_State* L) {
  long long value;
  if (port_evt::constantValue(luaL_checkstring(L, 1), value)) {
    lua_pushinteger(L, lua_Integer(value));
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int luaMessage(lua_State* L) {
  const char* tag = luaL_checkstring(L, 1);
  const char* text = luaL_optstring(L, 2, nullptr);
  if (portLuaWritesAllowed()) {
    portModsSetMessage(tag, text);
  }
  return 0;
}

int luaFrame(lua_State* L) {
  lua_pushinteger(L, sFrame);
  return 1;
}

int luaOnline(lua_State* L) {
  lua_pushboolean(L, sOnline);
  return 1;
}

int luaCanChange(lua_State* L) {
  lua_pushboolean(L, portLuaWritesAllowed());
  return 1;
}

// --- save states and rewind: ttyd.keep(save, load) ---

// ttyd.keep(save, load): save() returns what to keep with a save state (or
// rewind's), load(value, kind) gets it back when that state is loaded;
// ttyd.keep(nil) stops
int luaKeep(lua_State* L) {
  LuaMod* m = modOf(L);
  luaL_unref(L, LUA_REGISTRYINDEX, m->keepSave);
  luaL_unref(L, LUA_REGISTRYINDEX, m->keepLoad);
  m->keepSave = m->keepLoad = LUA_NOREF;
  if (lua_isnoneornil(L, 1)) {
    return 0;
  }
  luaL_checktype(L, 1, LUA_TFUNCTION);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  m->keepSave = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pushvalue(L, 2);
  m->keepLoad = luaL_ref(L, LUA_REGISTRYINDEX);
  return 0;
}

constexpr size_t kKeepCap = 1u << 20;     // bytes one mod may keep with a state
constexpr size_t kSnapBytes = 16u << 20;  // every state's kept values together
constexpr size_t kSnapCount = 4096;
constexpr int kKeepDepth = 32;

// A value as bytes: nil, booleans, numbers, strings and tables of them.
bool pack(lua_State* L, int idx, std::string& out, int depth, std::set<const void*>& seen, std::string& why) {
  idx = lua_absindex(L, idx);
  if (out.size() > kKeepCap) {
    why = "more than " + std::to_string(kKeepCap >> 20) + " MB";
    return false;
  }
  switch (lua_type(L, idx)) {
  case LUA_TNIL:
    out += 'n';
    return true;
  case LUA_TBOOLEAN:
    out += lua_toboolean(L, idx) ? 't' : 'f';
    return true;
  case LUA_TNUMBER:
    if (lua_isinteger(L, idx)) {
      const lua_Integer v = lua_tointeger(L, idx);
      out += 'i';
      out.append(reinterpret_cast<const char*>(&v), sizeof(v));
    } else {
      const lua_Number v = lua_tonumber(L, idx);
      out += 'd';
      out.append(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    return true;
  case LUA_TSTRING: {
    size_t len;
    const char* s = lua_tolstring(L, idx, &len);
    const uint32_t n = uint32_t(len);
    out += 's';
    out.append(reinterpret_cast<const char*>(&n), sizeof(n));
    out.append(s, len);
    return true;
  }
  case LUA_TTABLE: {
    const void* p = lua_topointer(L, idx);
    if (depth >= kKeepDepth || !seen.insert(p).second) {
      why = depth >= kKeepDepth ? "tables more than " + std::to_string(kKeepDepth) + " deep" : "a table inside itself";
      return false;
    }
    if (!lua_checkstack(L, 3)) {
      why = "too deep";
      return false;
    }
    out += 'T';
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
      if (!pack(L, -2, out, depth + 1, seen, why) || !pack(L, -1, out, depth + 1, seen, why)) {
        lua_pop(L, 2);
        return false;
      }
      lua_pop(L, 1);
    }
    out += 'e';
    seen.erase(p);
    return true;
  }
  default:
    why = std::string("a ") + luaL_typename(L, idx) + " (only nil, booleans, numbers, strings and tables keep)";
    return false;
  }
}

// back: the value pushed (true), or nothing (false: the bytes are cut short)
bool unpack(lua_State* L, const std::string& in, size_t& at, int depth) {
  if (at >= in.size() || depth > kKeepDepth || !lua_checkstack(L, 3)) {
    return false;
  }
  const char tag = in[at++];
  switch (tag) {
  case 'n':
    lua_pushnil(L);
    return true;
  case 't':
  case 'f':
    lua_pushboolean(L, tag == 't');
    return true;
  case 'i':
  case 'd': {
    if (at + 8 > in.size()) {
      return false;
    }
    if (tag == 'i') {
      lua_Integer v;
      std::memcpy(&v, in.data() + at, sizeof(v));
      lua_pushinteger(L, v);
    } else {
      lua_Number v;
      std::memcpy(&v, in.data() + at, sizeof(v));
      lua_pushnumber(L, v);
    }
    at += 8;
    return true;
  }
  case 's': {
    uint32_t n;
    if (at + 4 > in.size()) {
      return false;
    }
    std::memcpy(&n, in.data() + at, sizeof(n));
    at += 4;
    if (at + n > in.size()) {
      return false;
    }
    lua_pushlstring(L, in.data() + at, n);
    at += n;
    return true;
  }
  case 'T':
    lua_newtable(L);
    while (at < in.size() && in[at] != 'e') {
      if (!unpack(L, in, at, depth + 1)) {
        lua_pop(L, 1);
        return false;
      }
      if (!unpack(L, in, at, depth + 1)) {
        lua_pop(L, 2);
        return false;
      }
      if (lua_isnil(L, -2)) {
        lua_pop(L, 2); // (can't be: nil keys don't pack)
      } else {
        lua_rawset(L, -3);
      }
    }
    if (at >= in.size()) {
      lua_pop(L, 1);
      return false;
    }
    at++;
    return true;
  default:
    return false;
  }
}

// load(value, kind)'s arguments (a Job's push): the value made again (nil
// when there's none, or it's cut short), and the kind
struct LoadArgs {
  const std::string* value;
  const char* kind;
};

int pushLoadArgs(lua_State* L, void* user) {
  const LoadArgs* a = static_cast<const LoadArgs*>(user);
  size_t at = 0;
  if (a->value->empty() || !unpack(L, *a->value, at, 0)) {
    lua_pushnil(L);
  }
  lua_pushstring(L, a->kind);
  return 2;
}

// The kept values: per state's number, every mod's (id, hash, value) in turn.
constexpr int kSnapPins = 16; // the save-state slots
struct Snap {
  std::string data;
  uint64_t used = 0; // (for dropping the oldest)
};
std::map<unsigned, Snap> sSnaps;
size_t sSnapTotal;
unsigned sSnapNext, sSnapLast;
uint64_t sSnapClock;
unsigned sSnapPins[kSnapPins];
unsigned sSnapPending;
int sSnapPendingKind = -1;

void appendString(std::string& out, const std::string& s) {
  const uint32_t n = uint32_t(s.size());
  out.append(reinterpret_cast<const char*>(&n), sizeof(n));
  out += s;
}

bool readString(const std::string& in, size_t& at, std::string& s) {
  uint32_t n;
  if (at + 4 > in.size()) {
    return false;
  }
  std::memcpy(&n, in.data() + at, 4);
  at += 4;
  if (at + n > in.size()) {
    return false;
  }
  s.assign(in.data() + at, n);
  at += n;
  return true;
}

bool pinned(unsigned id) {
  return std::find(std::begin(sSnapPins), std::end(sSnapPins), id) != std::end(sSnapPins);
}

void dropOldSnaps() {
  while (sSnapTotal > kSnapBytes || sSnaps.size() > kSnapCount) {
    auto oldest = sSnaps.end();
    for (auto it = sSnaps.begin(); it != sSnaps.end(); ++it) {
      if (!pinned(it->first) && (oldest == sSnaps.end() || it->second.used < oldest->second.used)) {
        oldest = it;
      }
    }
    if (oldest == sSnaps.end()) {
      return; // (every one held by a slot)
    }
    sSnapTotal -= oldest->second.data.size();
    sSnaps.erase(oldest);
  }
}

// At the frame's start: a loaded state's values back to the mods, then "state_load".
void applySnap() {
  if (sSnapPendingKind < 0) {
    return;
  }
  const int kind = sSnapPendingKind;
  const auto found = sSnaps.find(sSnapPending);
  sSnapPendingKind = -1;
  const std::string data = found != sSnaps.end() ? found->second.data : std::string();
  if (found != sSnaps.end()) {
    found->second.used = ++sSnapClock;
  }
  const char* kindName = kind == PORT_LUA_STATE_REWIND ? "rewind" : "state";
  for (auto& mp : sMods) {
    LuaMod& m = *mp;
    if (m.L == nullptr || m.keepLoad == LUA_NOREF || m.depth > 0) {
      continue;
    }
    std::string value;
    for (size_t at = 0; at < data.size();) {
      std::string id, hash, v;
      if (!readString(data, at, id) || !readString(data, at, hash) || !readString(data, at, v)) {
        break;
      }
      if (id == m.spec.id && hash == m.hash) {
        value = std::move(v);
        break;
      }
    }
    lua_rawgeti(m.L, LUA_REGISTRYINDEX, m.keepLoad);
    LoadArgs args{&value, kindName};
    Job job{pushLoadArgs, nullptr, &args, 0};
    callWith(m, job);
  }
  portLuaEvent("state_load", "s", kindName);
}

// --- hud ---

unsigned color(lua_State* L, int index, unsigned fallback) {
  return lua_isnoneornil(L, index) ? fallback : unsigned(luaL_checkinteger(L, index) & 0xFFFFFFFF);
}

int luaHudText(lua_State* L) {
  HudItem h{};
  h.text = true;
  h.x = float(luaL_checknumber(L, 1));
  h.y = float(luaL_checknumber(L, 2));
  size_t len;
  const char* s = toText(L, 3, &len);
  h.s.assign(s, std::min<size_t>(len, 512));
  lua_pop(L, 1); // (luaL_tolstring pushed it: the optional arguments' places again)
  h.size = float(luaL_optnumber(L, 4, 18));
  h.rgba = color(L, 5, 0xFFFFFFFF);
  h.shadow = lua_isnoneornil(L, 6) || lua_toboolean(L, 6);
  if (sHudBuild.size() < 2048) {
    sHudBuild.push_back(std::move(h));
  }
  return 0;
}

int luaHudRect(lua_State* L) {
  HudItem h{};
  h.x = float(luaL_checknumber(L, 1));
  h.y = float(luaL_checknumber(L, 2));
  h.w = float(luaL_checknumber(L, 3));
  h.h = float(luaL_checknumber(L, 4));
  h.rgba = color(L, 5, 0x000000A0);
  if (sHudBuild.size() < 2048) {
    sHudBuild.push_back(std::move(h));
  }
  return 0;
}

// --- rules: the challenge rules (port/src/common/challenge_pc.c) ---

int luaRuleSet(lua_State* L) {
  const std::string key = luaL_checkstring(L, 1);
  auto& rules = modOf(L)->rules;
  rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const auto& r) { return r.first == key; }), rules.end());
  if (!lua_isnoneornil(L, 2)) {
    rules.emplace_back(key, lua_isboolean(L, 2) ? lua_toboolean(L, 2) : int(luaL_checkinteger(L, 2)));
  }
  return 0;
}

int luaRuleGet(lua_State* L) {
  int value;
  if (portLuaRule(luaL_checkstring(L, 1), &value)) {
    lua_pushinteger(L, value);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

// --- looks: partner skins and Mario's costume (docs/COSTUMES.md) ---
// How things look only, each player's own: any mod may choose, online too.
// The choice is the first running mod's, over the Wardrobe's, while it runs
// (resolved each frame into sSkinChoice / sCostumeChoice for the drawing).

std::atomic<int> sSkinChoice[8] = {-2, -2, -2, -2, -2, -2, -2, -2};
std::atomic<int> sCostumeChoice{-1};

void resolveLooks();

int partyArg(lua_State* L, int index) {
  const lua_Integer p = luaL_checkinteger(L, index);
  luaL_argcheck(L, p >= 1 && p <= 7, index, "a partner: 1 (Goombella) to 7 (Ms. Mowz)");
  return int(p);
}

int namesList(lua_State* L, int count, const char* (*name)(int)) {
  lua_createtable(L, count, 0);
  for (int i = 0; i < count; i++) {
    lua_pushstring(L, name(i));
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

// Skins are the user folder's own and off online and in replays (skin_pc.c):
// there, none to list or choose, so a mod sees the same on every computer.
bool skinsShared() { return !portOnlineActive() && !portReplayActive(); }

int luaLooksSkins(lua_State* L) { return namesList(L, skinsShared() ? portSkinCount() : 0, portSkinName); }

int luaLooksSkin(lua_State* L) {
  const int skin = portSkinOf(partyArg(L, 1));
  if (skin >= 0) {
    lua_pushstring(L, portSkinName(skin));
  } else {
    lua_pushnil(L);
  }
  return 1;
}

// looks.set_skin(partner, name | false | nil): that skin, their own look,
// or the Wardrobe's again; false when there's no skin of that name
int luaLooksSetSkin(lua_State* L) {
  LuaMod* m = modOf(L);
  const int party = partyArg(L, 1);
  if (lua_isnoneornil(L, 2)) {
    m->skins[size_t(party)].reset();
  } else if (lua_isboolean(L, 2) && !lua_toboolean(L, 2)) {
    m->skins[size_t(party)] = std::string();
  } else {
    const char* name = luaL_checkstring(L, 2);
    int found = -1;
    for (int i = 0; skinsShared() && i < portSkinCount() && found < 0; i++) {
      found = std::strcmp(portSkinName(i), name) == 0 ? i : -1;
    }
    if (found < 0) {
      lua_pushboolean(L, 0);
      return 1;
    }
    m->skins[size_t(party)] = std::string(name);
  }
  resolveLooks();
  lua_pushboolean(L, 1);
  return 1;
}

int luaLooksCostumes(lua_State* L) { return namesList(L, portCostumeCount(), portCostumeName); }

int luaLooksCostume(lua_State* L) {
  const int c = portCostumeCurrent();
  if (c > 0) {
    lua_pushstring(L, portCostumeName(c - 1));
  } else {
    lua_pushnil(L);
  }
  return 1;
}

// looks.set_costume(name | false | nil): that costume, none, or the Wardrobe's again
int luaLooksSetCostume(lua_State* L) {
  LuaMod* m = modOf(L);
  if (lua_isnoneornil(L, 1)) {
    m->costume.reset();
  } else if (lua_isboolean(L, 1) && !lua_toboolean(L, 1)) {
    m->costume = std::string();
  } else {
    const char* name = luaL_checkstring(L, 1);
    int found = -1;
    for (int i = 0; i < portCostumeCount() && found < 0; i++) {
      found = std::strcmp(portCostumeName(i), name) == 0 ? i : -1;
    }
    if (found < 0) {
      lua_pushboolean(L, 0);
      return 1;
    }
    m->costume = std::string(name);
  }
  resolveLooks();
  lua_pushboolean(L, 1);
  return 1;
}

void resolveLooks() {
  for (int party = 1; party <= 7; party++) {
    int choice = -2;
    for (const auto& m : sMods) {
      if (m->L == nullptr || !m->skins[size_t(party)]) {
        continue;
      }
      const std::string& name = *m->skins[size_t(party)];
      choice = -1;
      for (int i = 0; i < portSkinCount() && !name.empty(); i++) {
        if (name == portSkinName(i)) {
          choice = i;
          break;
        }
      }
      break;
    }
    sSkinChoice[party] = choice;
  }
  int costume = -1;
  for (const auto& m : sMods) {
    if (m->L == nullptr || !m->costume) {
      continue;
    }
    costume = 0;
    for (int i = 0; i < portCostumeCount() && !m->costume->empty(); i++) {
      if (*m->costume == portCostumeName(i)) {
        costume = i + 1;
        break;
      }
    }
    break;
  }
  sCostumeChoice = costume;
}

void setFuncs(lua_State* L, const char* table, const luaL_Reg* funcs) {
  lua_newtable(L);
  luaL_setfuncs(L, funcs, 0);
  lua_setglobal(L, table);
}

// A new state for m: the sandbox, the API, then its main.lua.
void openMod(LuaMod& m) {
  m.memory = 0;
  m.created = 0;
  m.keepSave = m.keepLoad = LUA_NOREF;
  m.events.clear();
  m.rules.clear();
  m.error.clear();
  lua_State* L = lua_newstate(alloc, &m);
  if (L == nullptr) {
    fail(m, "its Lua state can't be made");
    return;
  }
  m.L = L;
  modOf(L) = &m;
  const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},          {LUA_COLIBNAME, luaopen_coroutine},
                           {LUA_TABLIBNAME, luaopen_table},    {LUA_STRLIBNAME, luaopen_string},
                           {LUA_MATHLIBNAME, luaopen_math},    {LUA_UTF8LIBNAME, luaopen_utf8}};
  for (const luaL_Reg& lib : libs) {
    luaL_requiref(L, lib.name, lib.func, 1);
    lua_pop(L, 1);
  }
  // no files: dofile, loadfile; no bytecode: string.dump, load's "b"
  lua_pushnil(L);
  lua_setglobal(L, "dofile");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
  lua_getglobal(L, "string");
  lua_pushnil(L);
  lua_setfield(L, -2, "dump");
  // find, match, gmatch, gsub with a step budget; format by creation number
  lua_port_openmatch(L, matchHook);
  lua_getfield(L, -1, "format");
  lua_pushcclosure(L, luaFormat, 1);
  lua_setfield(L, -2, "format");
  lua_pop(L, 1);
  lua_pushcfunction(L, luaToString);
  lua_setglobal(L, "tostring");
  lua_getglobal(L, "load");
  lua_pushcclosure(L, luaLoad, 1);
  lua_setglobal(L, "load");
  lua_pushcfunction(L, luaPrint);
  lua_setglobal(L, "print");
  lua_pushcfunction(L, luaRequire);
  lua_setglobal(L, "require");
  lua_getglobal(L, "math");
  lua_getfield(L, -1, "randomseed");
  lua_pushcclosure(L, luaRandomSeed, 1);
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, "randomseed");
  lua_call(L, 0, 0); // seeded from its id
  lua_pop(L, 1);
  lua_newtable(L);
  lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoadedKey);
  lua_newtable(L);
  lua_rawsetp(L, LUA_REGISTRYINDEX, &kHandlersKey);

  const luaL_Reg ttyd[] = {{"on", luaOn},           {"off", luaOff},     {"log", luaPrint},
                           {"option", luaOption},   {"const", luaConst}, {"message", luaMessage},
                           {"frame", luaFrame},     {"online", luaOnline}, {"can_change", luaCanChange},
                           {"keep", luaKeep},       {nullptr, nullptr}};
  lua_newtable(L);
  luaL_setfuncs(L, ttyd, 0);
  lua_pushstring(L, PORT_GAME_ID);
  lua_setfield(L, -2, "game");
  lua_pushstring(L, m.spec.id.c_str());
  lua_setfield(L, -2, "mod_id");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "api");
  lua_setglobal(L, "ttyd");
  const luaL_Reg hud[] = {{"text", luaHudText}, {"rect", luaHudRect}, {nullptr, nullptr}};
  setFuncs(L, "hud", hud);
  lua_getglobal(L, "hud");
  lua_pushinteger(L, 640);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, 480);
  lua_setfield(L, -2, "height");
  lua_pop(L, 1);
  const luaL_Reg rules[] = {{"set", luaRuleSet}, {"get", luaRuleGet}, {nullptr, nullptr}};
  setFuncs(L, "rules", rules);
  const luaL_Reg looks[] = {{"skins", luaLooksSkins},       {"skin", luaLooksSkin},
                            {"set_skin", luaLooksSetSkin}, {"costumes", luaLooksCostumes},
                            {"costume", luaLooksCostume},  {"set_costume", luaLooksSetCostume},
                            {nullptr, nullptr}};
  setFuncs(L, "looks", looks);
  portLuaGameOpen(L); // player, items, flags, map, battle

  const auto main = m.spec.files.find("lua/main.lua");
  if (main == m.spec.files.end()) {
    fail(m, "has no lua/main.lua");
    return;
  }
  if (luaL_loadbufferx(L, main->second.data(), main->second.size(), "@lua/main.lua", "t") != LUA_OK) {
    fail(m, lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "lua/main.lua can't be read");
    return;
  }
  m.state = PORT_LUA_RUNNING;
  if (call(m, 0, 0, kMainBudget)) {
    console(m.spec.id + ": running", false);
  }
}

// One mod's: its id, files (in path order), its online kind and the option
// values its code reads (each player's own, so they count too).
std::string modHash(const PortLuaModSpec& spec) {
  uint64_t h = fnvString(0xcbf29ce484222325ull, spec.id);
  for (const auto& [path, data] : spec.files) {
    h = fnvString(fnvString(h, path), data);
  }
  h = fnvString(h, spec.options);
  h ^= spec.hudOnline ? 1u : 0u;
  char text[24];
  std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)h);
  return text;
}

// Hot reload: a folder mod's lua/ files, and their newest change.
void scanFolder(const std::string& root, const std::string& rel, std::map<std::string, std::string>* files,
                SDL_Time& newest, int& count) {
  std::vector<std::string> names;
  const std::string dir = root + "/" + rel;
  SDL_EnumerateDirectory(
      dir.c_str(),
      [](void* user, const char*, const char* fname) -> SDL_EnumerationResult {
        static_cast<std::vector<std::string>*>(user)->emplace_back(fname);
        return SDL_ENUM_CONTINUE;
      },
      &names);
  for (const std::string& name : names) {
    const std::string relPath = rel + "/" + name;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo((root + "/" + relPath).c_str(), &info)) {
      continue;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
      scanFolder(root, relPath, files, newest, count);
    } else if (info.type == SDL_PATHTYPE_FILE && relPath.size() > 4 &&
               relPath.compare(relPath.size() - 4, 4, ".lua") == 0) {
      newest = std::max(newest, info.modify_time);
      count++;
      if (files != nullptr) {
        size_t size = 0;
        if (void* data = SDL_LoadFile((root + "/" + relPath).c_str(), &size)) {
          (*files)[relPath].assign(static_cast<const char*>(data), size);
          SDL_free(data);
        }
      }
    }
  }
}

void recomputeHash() {
  std::vector<PortLuaModSpec> specs;
  for (const auto& m : sMods) {
    specs.push_back(m->spec);
  }
  sHash = portLuaHashOf(specs);
}

void hotReload() {
  bool changed = false;
  for (auto& m : sMods) {
    if (m->spec.folder.empty()) {
      continue;
    }
    SDL_Time newest = 0;
    int count = 0;
    scanFolder(m->spec.folder, "lua", nullptr, newest, count);
    if (newest == m->newest && count == m->fileCount) {
      continue;
    }
    m->newest = newest;
    m->fileCount = count;
    std::map<std::string, std::string> files;
    scanFolder(m->spec.folder, "lua", &files, newest, count);
    if (files == m->spec.files) {
      continue; // touched, not changed
    }
    console(m->spec.id + ": its files changed, starting it again", false);
    m->spec.files = std::move(files);
    m->hash = modHash(m->spec);
    closeMod(*m);
    m->state = PORT_LUA_NONE;
    openMod(*m);
    changed = true;
  }
  if (changed) {
    recomputeHash();
  }
}

// The pending set in place: unchanged mods keep their state.
void swapIn(std::vector<PortLuaModSpec> specs) {
  std::vector<std::unique_ptr<LuaMod>> old = std::move(sMods);
  sMods.clear();
  for (auto& spec : specs) {
    const std::string hash = modHash(spec);
    auto same = std::find_if(old.begin(), old.end(), [&](const auto& m) {
      return m != nullptr && m->spec.id == spec.id && m->hash == hash && m->state == PORT_LUA_RUNNING;
    });
    if (same != old.end()) {
      sMods.push_back(std::move(*same));
      continue;
    }
    auto m = std::make_unique<LuaMod>();
    m->spec = std::move(spec);
    m->hash = hash;
    if (!m->spec.folder.empty()) {
      scanFolder(m->spec.folder, "lua", nullptr, m->newest, m->fileCount);
    }
    sMods.push_back(std::move(m));
  }
  for (auto& m : old) {
    if (m != nullptr) {
      closeMod(*m);
    }
  }
  // (the new ones start in order, after every old one closed)
  for (auto& m : sMods) {
    if (m->L == nullptr && m->state == PORT_LUA_NONE) {
      openMod(*m);
    }
  }
  recomputeHash();
}

void pushArgs(lua_State* L, const char* fmt, va_list ap) {
  for (const char* f = fmt; f != nullptr && *f != '\0'; f++) {
    switch (*f) {
    case 'i':
    case 'u':
      lua_pushinteger(L, va_arg(ap, int));
      break;
    case 'n':
      lua_pushnumber(L, va_arg(ap, double));
      break;
    case 'b':
      lua_pushboolean(L, va_arg(ap, int));
      break;
    case 's': {
      const char* s = va_arg(ap, const char*);
      if (s != nullptr) {
        lua_pushstring(L, s);
      } else {
        lua_pushnil(L);
      }
      break;
    }
    default:
      lua_pushnil(L);
      break;
    }
  }
}

// An event's arguments (a Job's push): fmt's, then the filter's number.
struct Args {
  const char* fmt;
  va_list* ap;
  int* filter;
  int nargs;
};

int pushEventArgs(lua_State* L, void* user) {
  const Args* a = static_cast<const Args*>(user);
  luaL_checkstack(L, a->nargs + 2, "the event's arguments");
  pushArgs(L, a->fmt, *a->ap);
  if (a->filter != nullptr) {
    lua_pushinteger(L, *a->filter);
  }
  return a->nargs;
}

// portLuaFilterTable's: the event's arguments, then the table (and a copy of
// it under the function, for tableAfter)
struct TableArgs {
  Args args;
  PortLuaTablePush push;
  PortLuaTableRead read;
  void* user;
};

int pushTableArgs(lua_State* L, void* user) {
  TableArgs* t = static_cast<TableArgs*>(user);
  luaL_checkstack(L, t->args.nargs + 4, "the event's arguments");
  t->push(L, t->user);
  luaL_checktype(L, -1, LUA_TTABLE);
  lua_insert(L, 2); // (under the function: the trampoline's 1 is the job)
  pushArgs(L, t->args.fmt, *t->args.ap);
  lua_pushvalue(L, 2);
  return t->args.nargs + 1;
}

void tableAfter(lua_State* L, void* user) {
  const TableArgs* t = static_cast<const TableArgs*>(user);
  if (sFull && t->read != nullptr) {
    t->read(L, lua_istable(L, -1) ? lua_gettop(L) : 2, t->user);
  }
}

// Every running mod's handlers of event; filter: the value after the
// arguments, replaced by each handler's number; table (portLuaFilterTable):
// a table after them instead, read back after each.
void dispatch(const char* event, const char* fmt, va_list ap, int* filter, TableArgs* table = nullptr) {
  const int nargs = int(fmt != nullptr ? std::strlen(fmt) : 0) + (filter != nullptr ? 1 : 0);
  for (size_t i = 0; i < sMods.size(); i++) {
    LuaMod& m = *sMods[i];
    if (!runs(m) || m.depth > 0 || m.events.count(event) == 0) {
      continue;
    }
    lua_State* L = m.L;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kHandlersKey);
    if (lua_getfield(L, -1, event) != LUA_TTABLE) {
      lua_pop(L, 2);
      continue;
    }
    lua_remove(L, -2);
    const lua_Integer n = lua_Integer(lua_rawlen(L, -1));
    for (lua_Integer h = 1; h <= n && m.L != nullptr; h++) {
      if (lua_rawgeti(L, -1, h) != LUA_TFUNCTION) {
        lua_pop(L, 1);
        continue;
      }
      va_list copy; // (a local: its address is a va_list* everywhere)
      va_copy(copy, ap);
      Args args{fmt, &copy, filter, nargs};
      Job job{pushEventArgs, nullptr, &args, 1};
      if (table != nullptr) {
        table->args = args;
        job = Job{pushTableArgs, tableAfter, table, 1};
      }
      const bool ok = callWith(m, job);
      va_end(copy);
      if (!ok) {
        break; // stopped (its state is gone)
      }
      if (table != nullptr) {
        continue; // (tableAfter took the result)
      }
      if (filter != nullptr && sFull && lua_isnumber(L, -1)) {
        // clamped to an int, as every computer converts alike (a cast of one out of
        // range, math.huge or a NaN, differs between x86 and ARM: online's lockstep)
        if (lua_isinteger(L, -1)) {
          const lua_Integer v = lua_tointeger(L, -1);
          *filter = int(std::clamp<lua_Integer>(v, INT_MIN, INT_MAX));
        } else {
          const lua_Number v = lua_tonumber(L, -1);
          if (v == v) { // (a NaN: the value as it was)
            *filter = v <= double(INT_MIN) ? INT_MIN : v >= double(INT_MAX) ? INT_MAX : int(v);
          }
        }
      }
      lua_pop(L, 1);
    }
    if (m.L != nullptr) {
      lua_pop(L, 1);
    }
  }
}

// The console's Reload, the game's frame: a fresh set of the same mods.
void reloadAll() {
  for (auto& m : sMods) {
    if (!m->spec.folder.empty()) {
      std::map<std::string, std::string> files;
      SDL_Time newest = 0;
      int count = 0;
      scanFolder(m->spec.folder, "lua", &files, newest, count);
      m->spec.files = std::move(files);
      m->newest = newest;
      m->fileCount = count;
      m->hash = modHash(m->spec);
    }
    closeMod(*m);
    m->state = PORT_LUA_NONE;
  }
  for (auto& m : sMods) {
    openMod(*m);
  }
  recomputeHash();
  console("every Lua mod started again", false);
}

void drawHud(void*) {
  std::lock_guard lock(sHudLock);
  for (const HudItem& h : sHudShown) {
    if (h.text) {
      if (h.shadow) {
        portModHostDrawText(h.x + 1.5f, h.y + 1.5f, h.size, 0x00000000u | ((h.rgba & 0xFF) * 3 / 4), h.s.c_str());
      }
      portModHostDrawText(h.x, h.y, h.size, h.rgba, h.s.c_str());
    } else {
      portModHostDrawRect(h.x, h.y, h.w, h.h, h.rgba);
    }
  }
}

struct Register {
  Register() { portModHostAddDraw(drawHud, nullptr); }
} sRegister;

} // namespace

void portLuaSetMods(std::vector<PortLuaModSpec> mods) {
  std::lock_guard lock(sPendingLock);
  sHash = portLuaHashOf(mods); // (what the hello carries from now)
  sPending = std::move(mods);
  sHavePending = true;
}

std::string portLuaHashOf(const std::vector<PortLuaModSpec>& mods) {
  if (mods.empty()) {
    return {};
  }
  uint64_t h = 0xcbf29ce484222325ull;
  for (const auto& spec : mods) {
    h = fnvString(h, modHash(spec));
  }
  char text[24];
  std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)h);
  return text;
}

extern "C" {

int portLuaActive(void) { return !sMods.empty() || sHavePending; }

void portLuaFrame(void) {
  sFrame++;
  sOnline = portOnlineActive() || portOnlineWatching();
  sFull = sessionFull();
  // Online and in a replay, the set the run started with stays: a mod turned on or off,
  // moved or reloaded from the Mods tab meanwhile (one player's alone, or not in the
  // recording) waits until the run is over. The first frame's is the start's own.
  static bool sFirstFrame = true;
  const bool locked = (sOnline || portReplayActive()) && !sFirstFrame;
  sFirstFrame = false;
  {
    std::vector<PortLuaModSpec> pending;
    bool have = false;
    {
      std::lock_guard lock(sPendingLock);
      if (sHavePending && !locked) {
        pending = std::move(sPending);
        sPending.clear();
        sHavePending = false;
        have = true;
      }
    }
    if (have) {
      swapIn(std::move(pending));
    }
  }
  if (!locked && !sOnline) {
    if (sReloadAll.exchange(false)) {
      reloadAll();
    } else if (sFrame % kReloadEvery == 0) {
      hotReload();
    }
  } else {
    sReloadAll = false;
  }
  sHudBuild.clear();
  applySnap(); // (a state loaded at the last frame's end)
  resolveLooks(); // (mods started, stopped or gone)
}

unsigned portLuaSnapTake(void) {
  std::string data;
  for (auto& mp : sMods) {
    LuaMod& m = *mp;
    if (!runs(m) || m.keepSave == LUA_NOREF || m.depth > 0) {
      continue;
    }
    lua_rawgeti(m.L, LUA_REGISTRYINDEX, m.keepSave);
    if (!call(m, 0, 1)) {
      continue; // (stopped)
    }
    std::string value, why;
    std::set<const void*> seen;
    if (!pack(m.L, -1, value, 0, seen, why)) {
      console(m.spec.id + ": what its ttyd.keep returned can't be kept with the state: " + why, true);
      value.clear();
    }
    lua_pop(m.L, 1);
    if (!value.empty()) {
      appendString(data, m.spec.id);
      appendString(data, m.hash);
      appendString(data, value);
    }
  }
  if (data.empty()) {
    return 0;
  }
  const auto last = sSnaps.find(sSnapLast);
  if (last != sSnaps.end() && last->second.data == data) {
    last->second.used = ++sSnapClock; // (unchanged since the last state: most of rewind's)
    return sSnapLast;
  }
  if (++sSnapNext == 0) {
    sSnapNext = 1;
  }
  sSnapTotal += data.size();
  sSnaps[sSnapNext] = Snap{std::move(data), ++sSnapClock};
  sSnapLast = sSnapNext;
  dropOldSnaps();
  return sSnapLast;
}

void portLuaSnapPut(unsigned id, int kind) {
  if (sMods.empty()) {
    return;
  }
  sSnapPending = id;
  sSnapPendingKind = kind;
}

void portLuaSnapPin(int slot, unsigned id) {
  if (slot >= 0 && slot < kSnapPins) {
    sSnapPins[slot] = id;
    dropOldSnaps();
  }
}

void portLuaFrameEnd(void) {
  portLuaEvent("hud", "");
  std::lock_guard lock(sHudLock);
  sHudShown.swap(sHudBuild);
}

void portLuaEvent(const char* event, const char* fmt, ...) {
  if (sMods.empty()) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  dispatch(event, fmt, ap, nullptr);
  va_end(ap);
}

int portLuaFilterInt(const char* event, int value, const char* fmt, ...) {
  if (sMods.empty()) {
    return value;
  }
  va_list ap;
  va_start(ap, fmt);
  dispatch(event, fmt, ap, &value);
  va_end(ap);
  return value;
}

void portLuaFilterTable(const char* event, PortLuaTablePush push, PortLuaTableRead read, void* user, const char* fmt,
                        ...) {
  if (sMods.empty() || push == nullptr) {
    return;
  }
  TableArgs table{{}, push, read, user};
  va_list ap;
  va_start(ap, fmt);
  dispatch(event, fmt, ap, nullptr, &table);
  va_end(ap);
}

const char* portLuaUnitKindName(int kind) { return port_evt::unitKindName(kind); }

int portLuaSkinChoice(int party) { return party >= 1 && party <= 7 ? sSkinChoice[party].load() : -2; }

int portLuaCostumeChoice(void) { return sCostumeChoice.load(); }

int portLuaListening(const char* event) {
  for (const auto& m : sMods) {
    if (runs(*m) && m->events.count(event) != 0) {
      return 1;
    }
  }
  return 0;
}

int portLuaWritesAllowed(void) { return sFull ? 1 : 0; }

int portLuaRule(const char* key, int* value) {
  if (!sFull) {
    return 0;
  }
  for (const auto& m : sMods) {
    if (m->L == nullptr) {
      continue;
    }
    for (const auto& [k, v] : m->rules) {
      if (k == key) {
        *value = v;
        return 1; // the first mod's
      }
    }
  }
  return 0;
}

int portLuaModState(const char* modId, const char** error) {
  *error = nullptr;
  for (const auto& m : sMods) {
    if (m->spec.id != modId) {
      continue;
    }
    if (m->state == PORT_LUA_FAILED) {
      *error = m->error.c_str();
      return PORT_LUA_FAILED;
    }
    if (m->L != nullptr && !sFull) {
      return m->spec.hudOnline ? PORT_LUA_HUD_ONLY : PORT_LUA_PAUSED;
    }
    return m->L != nullptr ? PORT_LUA_RUNNING : PORT_LUA_NONE;
  }
  return PORT_LUA_NONE;
}

int portLuaConsoleCount(void) {
  std::lock_guard lock(sConsoleLock);
  return int(sConsole.size());
}

const char* portLuaConsoleLine(int i, int* error) {
  static std::string line;
  std::lock_guard lock(sConsoleLock);
  if (i < 0 || i >= int(sConsole.size())) {
    return nullptr;
  }
  line = sConsole[size_t(i)].text;
  if (error != nullptr) {
    *error = sConsole[size_t(i)].error ? 1 : 0;
  }
  return line.c_str();
}

void portLuaConsoleClear(void) {
  std::lock_guard lock(sConsoleLock);
  sConsole.clear();
}

const char* portLuaLastError(unsigned long long* ticks) {
  static std::string copy;
  std::lock_guard lock(sConsoleLock);
  if (sLastError.empty()) {
    return nullptr;
  }
  copy = sLastError;
  *ticks = sLastErrorAt;
  return copy.c_str();
}

void portLuaReloadAll(void) { sReloadAll = true; }

const char* portLuaHash(void) {
  static std::string copy;
  std::lock_guard lock(sPendingLock);
  copy = sHash;
  return copy.c_str();
}

void portLuaDraw(void) { drawHud(nullptr); }

} // extern "C"
