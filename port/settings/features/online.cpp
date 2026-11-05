// Online co-op's lobby (port/include/port/online.h): the launcher's Online
// tab, before the game starts, where the host waits and the guest joins.
//
// The host's setup, sent once the guest is here: the game clock's start, the
// input delay (from the round trip), how the game starts (the title, or the
// host's quick resume save), the host's settings.txt and memory card files,
// its versus set-up (port/include/port/versus.h: the session's, so the
// set-up page and a fight start from the same one in every game) and the
// Master Quest set its settings name (port/include/port/master_quest.h).
// The guest plays on the host's card in <user>/online (CARDSetBasePath, so
// its own saves aren't touched) and on the host's settings for this run,
// except the ones only it sees or hears (kLocal: display, sound, controls,
// its menus); nothing is saved to its settings.txt meanwhile. Then both
// start: the clock virtual from the host's start, the session's lockstep
// from the first frame.
//
// A watcher (port/include/port/online.h) joins the same way with "Watch":
// the host's setup put in place as a guest's, then both players' input from
// the host. One-click join: an invite link (invite.cpp) or a room code on the
// clipboard, read only as the Online tab opens and never sent anywhere,
// offered with a question ("Join?"), never joined by itself.
#include "port/http.h"
#include "port/lua_mods.h"
#include "port/mods.h"
#include "port/master_quest.h"
#include "port/online.h"
#include "port/online_menu.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"
#include "port/versus.h"

#include <SDL3/SDL.h>
#include <aurora/card.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void CARDSetBasePath(const char* path, int chan); // Aurora (dolphin/card.h)
void portLockstepBegin(long long base);             // os_time_aurora.c
long long portRealTime(void);
void portWarpRequestResume(const char* path, int slot); // warp_pc.c
int portOnlineTakeInvite(char* out, size_t size);       // invite.cpp
}

namespace {

// settings only this player sees or hears: kept the guest's own (prefixes)
constexpr const char* kLocal[] = {
    "framerate", "vsync", "fullscreen", "widescreen", "resolution", "fps", "overlay_", "no_pop_in", "tex_filter",
    "msaa", "side_bars", "wide_hud", "volume", "mute", "music_", "sfx_", "ambience_", "background_input",
    "cloud_sync", "rumble", "phone_rumble", "touch_", "auto_run", "stick_fixed", "tap_a", "orientation",
    "safe_area", "ff_", "launcher_skip", "dump_textures", "key2_", "menu_language", "speedrun_timer", "timer_",
    "split_category", "livesplit", "memory_watch", "achievements", "button_prompts", "aspect_", "hud_scale",
    "text_scale", "aniso", "lod_bias", "texture_upscale", "supersample", "post_filter", "vrr", "perf_overlay",
    "hdr", "collectible_tracker", "sell_helper", "menu_mouse", "quick_resume", "save_backups", "sharp_text",
    "update_check", "window_", "jukebox", "cosmetic", "mirror", "free_cam", "input_display", "second_screen",
    "companion", "ac_cue", "controller_haptics", "crowd_", "custom_music", "settings_hint", "game_sound_mode", "speakers",
    "controller_light", "discord_status", "tts", "captions", "colorblind", "rewind_key", "rewind_pad",
    "paper_shadows", "paper_physics", "paper_edges", "launcher_popup",
    "battery_saver", "partner_wheel",
};

bool local(const std::string& key) {
  for (const char* p : kLocal) {
    if (key.compare(0, std::strlen(p), p) == 0) {
      return true;
    }
  }
  return false;
}

std::string readFile(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::string out;
  if (data != nullptr) {
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
  }
  return out;
}

bool writeFile(const std::string& path, const std::string& data) {
  SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
  if (io == nullptr) {
    return false;
  }
  const bool ok = SDL_WriteIO(io, data.data(), data.size()) == data.size();
  SDL_CloseIO(io);
  return ok;
}

// --- the setup's bytes ---

struct Writer {
  std::string b;
  void u8(unsigned v) { b.push_back(static_cast<char>(v)); }
  void u32(unsigned v) {
    for (int i = 0; i < 4; i++) {
      u8((v >> (8 * i)) & 255);
    }
  }
  void u64(unsigned long long v) {
    u32(static_cast<unsigned>(v));
    u32(static_cast<unsigned>(v >> 32));
  }
  void bytes(const std::string& s) {
    u32(static_cast<unsigned>(s.size()));
    b += s;
  }
};

struct Reader {
  const unsigned char* p;
  size_t left;
  bool ok = true;
  unsigned u8() {
    if (left < 1) {
      ok = false;
      return 0;
    }
    left--;
    return *p++;
  }
  unsigned u32() {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
      v |= u8() << (8 * i);
    }
    return v;
  }
  unsigned long long u64() {
    const unsigned long long lo = u32();
    return lo | (static_cast<unsigned long long>(u32()) << 32);
  }
  std::string bytes() {
    const unsigned n = u32();
    if (!ok || n > left) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(p), n);
    p += n;
    left -= n;
    return s;
  }
};

// the card's folder of this game (Aurora's GCI folder) under base, or Aurora's own
std::string cardDir(const char* base) {
  char path[1024];
  char game[5];
  std::memcpy(game, portVersionSelf()->id, 4);
  game[4] = '\0';
  aurora_card_get_path(game, AURORA_CARD_GCI_DIRECTORY, 0, path, sizeof(path));
  std::string dir = path;
  if (base != nullptr) {
    // the same folders (USA/Card A) under base
    const std::string user = portSettingsUserPath();
    if (dir.compare(0, user.size(), user) == 0) {
      dir = std::string(base) + dir.substr(user.size());
    }
  }
  return dir;
}

std::string sessionDir() { return std::string(portSettingsUserPath()) + "/online"; }

// --- the lobby's own state ---

struct Lobby {
  bool loaded = false;
  char name[32] = "";
  char address[128] = "";
  int port = PORT_ONLINE_DEFAULT_PORT;
  int startMode = 0; // 0 the title, 1 quick resume's save
  bool listMine = false; // the Start menu's "List my room" (the room list)
  unsigned long long base = 0;
  int delay = 3;
  bool begun = false;
} sLobby;

// The Start menu's host starting (online_menu.h): the host's "setup" that only says the
// games start again, the session then beginning from their start-up (a guest in the
// launcher joins again as they do)
constexpr char kMenuGo[] = "PCMENU-GO1";
// a guest or watcher here told that (applySetup): joining again until then, a try each 1.5 s
Uint64 sRetryUntil = 0, sRetryNext = 0;
// a launcher's host whose guest starts its game again (PORT_ONLINE_BYE_REJOIN): starting by
// itself when it's back, until then; when it came back
Uint64 sAutoStartUntil = 0, sPeerAt = 0;

bool isMenuGo(const void* data, size_t len) {
  return len == sizeof(kMenuGo) - 1 && std::memcmp(data, kMenuGo, len) == 0;
}

std::string prefsPath() { return std::string(portSettingsUserPath()) + "/online.txt"; }

void loadPrefs() {
  if (sLobby.loaded) {
    return;
  }
  sLobby.loaded = true;
  const char* user = SDL_getenv("USERNAME") != nullptr ? SDL_getenv("USERNAME") : SDL_getenv("USER");
  std::snprintf(sLobby.name, sizeof(sLobby.name), "%s", user != nullptr ? user : "Player");
  const std::string text = readFile(prefsPath());
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "name") {
      std::snprintf(sLobby.name, sizeof(sLobby.name), "%s", v.c_str());
    } else if (k == "address") {
      std::snprintf(sLobby.address, sizeof(sLobby.address), "%s", v.c_str());
    } else if (k == "port") {
      sLobby.port = std::clamp(std::atoi(v.c_str()), 1024, 65535);
    } else if (k == "list") {
      sLobby.listMine = v == "1";
    }
  }
}

void savePrefs() {
  writeFile(prefsPath(), std::string("name=") + sLobby.name + "\naddress=" + sLobby.address +
                             "\nport=" + std::to_string(sLobby.port) + "\nlist=" + (sLobby.listMine ? "1" : "0") +
                             "\n");
}

PortOnlineHello hello() {
  PortOnlineHello me{};
  std::snprintf(me.version, sizeof(me.version), "%s", PORT_VERSION);
  std::snprintf(me.game, sizeof(me.game), "%.6s", portVersionSelf()->id);
  // the mods in use: the mods folder's names, sorted (both need the same)
  int count = 0;
  const std::string mods = std::string(portSettingsUserPath()) + "/mods";
  std::vector<std::string> names;
  if (char** found = SDL_GlobDirectory(mods.c_str(), "*", 0, &count)) {
    for (int i = 0; i < count; i++) {
      names.push_back(found[i]);
    }
    SDL_free(found);
  }
  std::sort(names.begin(), names.end());
  std::string list;
  for (const std::string& n : names) {
    list += (list.empty() ? "" : ",") + n;
  }
  std::snprintf(me.mods, sizeof(me.mods), "%s", list.c_str());
  std::snprintf(me.name, sizeof(me.name), "%s", sLobby.name);
  // the Lua mods' hash: they run online only when every side has the same
  // (port/include/port/lua_mods.h); the launcher's mods read first if they weren't
  portModsPreview(portSettingsUserPath());
  std::snprintf(me.lua, sizeof(me.lua), "%s", portLuaHash());
  return me;
}

std::string resumeSave(int* slot) {
  char path[1024], sidecar[1024];
  portResumePath(path, sizeof(path), portVersionSelf()->id, 0);
  portResumePath(sidecar, sizeof(sidecar), portVersionSelf()->id, 1);
  const std::string slotText = readFile(sidecar);
  *slot = !slotText.empty() && slotText[0] >= '0' && slotText[0] <= '3' ? slotText[0] - '0' : 3;
  return readFile(path);
}

// host: the setup, once the guest is here
void sendSetup() {
  Writer w;
  sLobby.base = static_cast<unsigned long long>(portRealTime());
  const int rtt = portOnlineRoundTrip();
  // the input's way across, in frames, and two to spare (and the relay's batches)
  sLobby.delay = std::clamp(2 + (rtt > 0 ? (rtt / 2 + 16) / 17 : 1) + portOnlineDelayExtra(), 2, 14);
  int slot = 3;
  const std::string resume = sLobby.startMode == 1 ? resumeSave(&slot) : std::string();
  portSettingsSave(); // the settings as they are now, in settings.txt
  w.u64(sLobby.base);
  w.u8(static_cast<unsigned>(sLobby.delay));
  w.u8(resume.empty() ? 0 : 1);
  w.u8(static_cast<unsigned>(slot));
  w.u8(1 | 2); // (what follows the resume save: 1, the versus set-up; 2, the Master Quest set)
  w.bytes(readFile(std::string(portSettingsUserPath()) + "/settings.txt"));
  // the card's files
  const std::string dir = cardDir(nullptr);
  int count = 0;
  std::vector<std::string> files;
  if (char** found = SDL_GlobDirectory(dir.c_str(), "*.gci", 0, &count)) {
    for (int i = 0; i < count; i++) {
      files.push_back(found[i]);
    }
    SDL_free(found);
  }
  w.u32(static_cast<unsigned>(files.size()));
  for (const std::string& f : files) {
    w.bytes(f);
    w.bytes(readFile(dir + "/" + f));
  }
  w.bytes(resume);
  // the versus set-up as this game has it, and this game on the same text as the others
  std::string versus(4096, char(0));
  versus.resize(static_cast<size_t>(portVersusSetupText(versus.data(), static_cast<unsigned>(versus.size()))));
  portVersusSetupForSession(versus.data(), static_cast<unsigned>(versus.size()), 0);
  w.bytes(versus);
  w.bytes(portMasterQuestSelectedText()); // Master Quest's set ("": the curated one), as the settings name it
  if (portOnlineSendSetup(w.b.data(), w.b.size()) && !resume.empty()) {
    char path[1024];
    portResumePath(path, sizeof(path), portVersionSelf()->id, 0);
    portWarpRequestResume(path, slot); // the host's own game starts there too
  }
}

// guest: the host's setup put in place
bool applySetup() {
  size_t len = 0;
  const void* data = portOnlineSetup(&len);
  if (isMenuGo(data, len)) {
    // the host started from its running game: it starts again, and hosts the same room
    // (or port) from its start-up; this one joins (or watches) it again there
    portOnlineCancel(nullptr);
    sRetryUntil = SDL_GetTicks() + 90000;
    sRetryNext = SDL_GetTicks() + 1500;
    return false;
  }
  Reader r{static_cast<const unsigned char*>(data), len};
  sLobby.base = r.u64();
  sLobby.delay = static_cast<int>(r.u8());
  const int mode = static_cast<int>(r.u8());
  const int slot = static_cast<int>(r.u8());
  const unsigned extra = r.u8();
  const std::string settings = r.bytes();
  const unsigned files = r.u32();
  // the host's card, in a folder of the session's own
  const std::string base = sessionDir();
  const std::string dir = cardDir(base.c_str());
  SDL_CreateDirectory(dir.c_str());
  int count = 0;
  if (char** old = SDL_GlobDirectory(dir.c_str(), "*.gci", 0, &count)) {
    for (int i = 0; i < count; i++) {
      SDL_RemovePath((dir + "/" + old[i]).c_str());
    }
    SDL_free(old);
  }
  for (unsigned i = 0; i < files && r.ok; i++) {
    const std::string name = r.bytes();
    const std::string bytes = r.bytes();
    if (name.find('/') == std::string::npos && name.find('\\') == std::string::npos && name.find("..") == std::string::npos) {
      writeFile(dir + "/" + name, bytes);
    }
  }
  const std::string resume = r.bytes();
  const std::string versus = (extra & 1) != 0 ? r.bytes() : std::string();
  const std::string masterQuest = (extra & 2) != 0 ? r.bytes() : std::string();
  if (!r.ok) {
    portOnlineCancel("The host's game didn't arrive whole.");
    return false;
  }
  CARDSetBasePath(base.c_str(), -1);
  // the host's settings for this run, but this player's own display, sound and controls
  size_t at = 0;
  while (at < settings.size()) {
    size_t end = settings.find('\n', at);
    if (end == std::string::npos) {
      end = settings.size();
    }
    std::string line = settings.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos || local(line.substr(0, eq))) {
      continue;
    }
    portSettingsSetForRun(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str());
  }
  portMasterQuestSetForRun(masterQuest.data(), masterQuest.size()); // the host's set, whatever this one has
  portSettingsHoldSaves(1);
  portSettingsApply();
  // the host's versus set-up for this run (not saved to this player's versus.txt)
  portVersusSetupForSession(versus.data(), static_cast<unsigned>(versus.size()), 1);
  if (mode == 1 && !resume.empty()) {
    const std::string path = base + "/resume.gci";
    writeFile(path, resume);
    portWarpRequestResume(path.c_str(), slot);
  }
  portOnlineGuestReady();
  return true;
}

void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr(text));
  ImGui::PopStyleColor();
}

const char* peerName() {
  const char* n = portOnlinePeer()->name;
  return n[0] != '\0' ? n : portTr("The other player");
}

} // namespace

namespace {

// a room's code: PORT_ONLINE_CODE_LENGTH of the characters nothing reads wrong
std::string newRoomCode() {
  const char* chars = PORT_ONLINE_CODE_CHARS;
  const int n = static_cast<int>(std::strlen(chars));
  std::string code;
  for (int i = 0; i < PORT_ONLINE_CODE_LENGTH; i++) {
    code += chars[SDL_rand(n)];
  }
  return code;
}

// what was typed to join: a room's code (letters and digits only), upper-cased, or ""
std::string asRoomCode(const char* typed) {
  std::string code;
  for (const char* p = typed; *p != '\0'; p++) {
    if (*p == ' ' || *p == '-') {
      continue;
    }
    if (!std::isalnum(static_cast<unsigned char>(*p))) {
      return "";
    }
    code += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
  }
  return code.size() == PORT_ONLINE_CODE_LENGTH ? code : "";
}

// a room's code exactly (PORT_ONLINE_CODE_LENGTH of PORT_ONLINE_CODE_CHARS, a dash
// allowed, spaces around), upper-cased without the dash, or "": the clipboard's test
std::string strictRoomCode(const char* text) {
  std::string code;
  const char* p = text;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
    p++;
  }
  for (; *p != '\0'; p++) {
    if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      break;
    }
    if (*p == '-') {
      continue;
    }
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    if (std::strchr(PORT_ONLINE_CODE_CHARS, c) == nullptr || code.size() >= PORT_ONLINE_CODE_LENGTH) {
      return "";
    }
    code += c;
  }
  for (; *p != '\0'; p++) {
    if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
      return "";
    }
  }
  return code.size() == PORT_ONLINE_CODE_LENGTH ? code : "";
}

// ROOM-SECRET, as shown
std::string shownCode(const std::string& code) {
  return code.size() == PORT_ONLINE_CODE_LENGTH
             ? code.substr(0, PORT_ONLINE_ROOM_HALF) + "-" + code.substr(PORT_ONLINE_ROOM_HALF)
             : code;
}

// what this guest (or watcher) joined, for a recovery's restart to join again
// (portOnlineRestartRejoin): "room" and the code, or "address" and host:port;
// a watcher's "watch-room", "watch-address"
std::string sRejoinKind, sRejoinTarget;

void joinTyped() {
  const PortOnlineHello me = hello();
  const std::string code = asRoomCode(sLobby.address);
  if (!code.empty() && portOnlineRelay()[0] != '\0') {
    sRejoinKind = "room";
    sRejoinTarget = code;
    portOnlineJoinRoom(code.c_str(), &me);
    return;
  }
  // "host" or "host:port" (an IPv6 address in brackets, [::1]:47700)
  std::string host = sLobby.address;
  int port = PORT_ONLINE_DEFAULT_PORT;
  const size_t colon = host.rfind(':');
  if (colon != std::string::npos && host.find(':') == colon) {
    port = std::atoi(host.c_str() + colon + 1);
    host.resize(colon);
  } else if (!host.empty() && host[0] == '[') {
    const size_t close = host.find(']');
    if (close != std::string::npos) {
      if (close + 1 < host.size() && host[close + 1] == ':') {
        port = std::atoi(host.c_str() + close + 2);
      }
      host = host.substr(1, close - 1);
    }
  }
  sRejoinKind = "address";
  sRejoinTarget = (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":" + std::to_string(port);
  portOnlineJoin(host.c_str(), port, &me);
}

// "host" or "host:port" ([::1]:47700): its parts
void splitAddress(std::string host, std::string* outHost, int* outPort) {
  int port = PORT_ONLINE_DEFAULT_PORT;
  const size_t colon = host.rfind(':');
  if (colon != std::string::npos && host.find(':') == colon) {
    port = std::atoi(host.c_str() + colon + 1);
    host.resize(colon);
  } else if (!host.empty() && host[0] == '[') {
    const size_t close = host.find(']');
    if (close != std::string::npos) {
      if (close + 1 < host.size() && host[close + 1] == ':') {
        port = std::atoi(host.c_str() + close + 2);
      }
      host = host.substr(1, close - 1);
    }
  }
  *outHost = host;
  *outPort = port;
}

// watching what was typed: a room's code, or the host's address
void watchTyped() {
  const PortOnlineHello me = hello();
  const std::string code = asRoomCode(sLobby.address);
  if (!code.empty() && portOnlineRelay()[0] != '\0') {
    sRejoinKind = "watch-room";
    sRejoinTarget = code;
    portOnlineWatchRoom(code.c_str(), &me);
    return;
  }
  std::string host;
  int port = 0;
  splitAddress(sLobby.address, &host, &port);
  sRejoinKind = "watch-address";
  sRejoinTarget = (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":" + std::to_string(port);
  portOnlineWatch(host.c_str(), port, &me);
}

// joins (or watches) again what this player joined last (sRejoinKind, sRejoinTarget)
void joinAgain() {
  const PortOnlineHello me = hello();
  if (sRejoinKind == "room") {
    portOnlineJoinRoom(sRejoinTarget.c_str(), &me);
  } else if (sRejoinKind == "watch-room") {
    portOnlineWatchRoom(sRejoinTarget.c_str(), &me);
  } else if (!sRejoinTarget.empty()) {
    std::string host;
    int port = 0;
    splitAddress(sRejoinTarget, &host, &port);
    if (sRejoinKind == "watch-address") {
      portOnlineWatch(host.c_str(), port, &me);
    } else {
      portOnlineJoin(host.c_str(), port, &me);
    }
  }
}

// One-click join's offer: a room from an invite link (1) or the clipboard (2)
std::string sOffer;
int sOfferFrom = 0;
std::string sCopied; // the code this player copied (not offered back to them)
int sLobbyFrame = -10;

// each frame of the tab: an invite that came; the clipboard, as the tab opens
void checkOffers(int state) {
  char code[16];
  if (portOnlineTakeInvite(code, sizeof(code))) {
    sOffer = code;
    sOfferFrom = 1;
    std::snprintf(sLobby.address, sizeof(sLobby.address), "%s", shownCode(sOffer).c_str());
  }
  const int frame = ImGui::GetFrameCount();
  const bool opened = frame - sLobbyFrame > 1;
  sLobbyFrame = frame;
  if (opened && sOfferFrom != 1 && (state == PORT_ONLINE_IDLE || state == PORT_ONLINE_FAILED) &&
      portOnlineRelay()[0] != '\0' && SDL_HasClipboardText()) {
    // (read here only, as the tab opens; never sent anywhere)
    if (char* clip = SDL_GetClipboardText()) {
      const std::string found = std::strlen(clip) < 64 ? strictRoomCode(clip) : std::string();
      SDL_free(clip);
      if (!found.empty() && found != sCopied && found != asRoomCode(sLobby.address)) {
        sOffer = found;
        sOfferFrom = 2;
      }
    }
  }
}

} // namespace

extern "C" void portRestartForOnline(void); // restart.cpp
extern "C" int portRestartSupported(void);  // restart.cpp

// Online co-op's recovery (online_pc.c): this game started again to rejoin the
// session from the host's last save. The host writes that save and hosts the
// same room (or port, open to the network) with "Continue where you left off";
// the guest joins what it joined. The start-up path (portOnlineTestStart, below)
// takes it from the environment; the recoveries so far go with it. Doesn't return.
extern "C" void portOnlineRestartRejoin(int host, unsigned recoveries) {
  SDL_Environment* env = SDL_GetEnvironment();
  for (const char* name : {"TTYD_ONLINE_HOST", "TTYD_ONLINE_HOST_ROOM", "TTYD_ONLINE_JOIN", "TTYD_ONLINE_JOIN_ROOM",
                           "TTYD_ONLINE_CONTINUE", "TTYD_ONLINE_WATCH", "TTYD_ONLINE_WATCH_ROOM"}) {
    SDL_UnsetEnvironmentVariable(env, name);
  }
  if (host == 2) { // a watcher: watches again what it watched (the players' new session)
    if (sRejoinKind == "watch-room") {
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_WATCH_ROOM", sRejoinTarget.c_str(), true);
    } else if (!sRejoinTarget.empty()) {
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_WATCH", sRejoinTarget.c_str(), true);
    }
  } else if (host) {
    portResumeWriteNow();
    const std::string room = portOnlineRoom();
    if (!room.empty()) {
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_HOST_ROOM", room.c_str(), true);
    } else {
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_HOST", std::to_string(sLobby.port).c_str(), true);
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_LAN", "1", true); // (the guest is on another computer)
    }
    SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_CONTINUE", "1", true);
  } else if (sRejoinKind == "room") {
    SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_JOIN_ROOM", sRejoinTarget.c_str(), true);
  } else if (!sRejoinTarget.empty()) {
    SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_JOIN", sRejoinTarget.c_str(), true);
  }
  SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_RECOVERIES", std::to_string(recoveries).c_str(), true);
  portRestartForOnline();
}

// The launcher's Online tab (launcher_ui.cpp), for a disc of this build's
// version: true once both games start (the launcher then plays the disc).
bool portOnlineLobbyDraw(float scale) {
  loadPrefs();
  portOnlineLobbyPump();
  const int state = portOnlineLobbyState();
  checkOffers(state);
  const Uint64 now = SDL_GetTicks();
  if (portOnlineRejoinExpected()) { // (the guest joined from its running game: it starts again)
    sAutoStartUntil = now + 120000;
  }
  if (state != PORT_ONLINE_PEER_HERE) {
    sPeerAt = 0;
  }
  if (sRetryUntil != 0) { // the host started from its running game: joining it again
    if ((state >= PORT_ONLINE_WAITING_SETUP && state != PORT_ONLINE_FAILED) || now > sRetryUntil) {
      sRetryUntil = 0;
    } else {
      if ((state == PORT_ONLINE_IDLE || state == PORT_ONLINE_FAILED) && now >= sRetryNext) {
        sRetryNext = now + 1500;
        joinAgain();
      }
      ImGui::TextWrapped("%s", portTr("The host's game is starting again for the online game: joining it again..."));
      if (ImGui::Button(portTrLabel("Cancel##online_retry"))) {
        sRetryUntil = 0;
        portOnlineCancel(nullptr);
      }
      return false;
    }
  }
  const float field = ImGui::GetFontSize() * 14.f;
  ImGui::TextUnformatted(portTr("Play online"));
  note("Each player needs Paper Cut and their own disc of the same release. You play the same game together: "
       "the host is Mario, the one who joins plays the partner. Any computer, phone or tablet can play with any "
       "other.");
  ImGui::Dummy(ImVec2(0, 6.f * scale));

  if (state == PORT_ONLINE_IDLE || state == PORT_ONLINE_FAILED) {
    if (state == PORT_ONLINE_FAILED) {
      ImGui::TextWrapped("%s", portTr(portOnlineStatus()));
      ImGui::Dummy(ImVec2(0, 4.f * scale));
    }
    ImGui::SetNextItemWidth(field);
    if (ImGui::InputText(portTrLabel("Your name"), sLobby.name, sizeof(sLobby.name))) {
      savePrefs();
    }
    const bool relay = portOnlineRelay()[0] != '\0';
    ImGui::Dummy(ImVec2(0, 6.f * scale));
    ImGui::TextUnformatted(portTr("Host a game"));
    if (relay) {
      if (ImGui::Button(portTrLabel("Host"))) {
        const PortOnlineHello me = hello();
        portOnlineListRoom(nullptr); // (the room list is the Start menu's)
        portOnlineHostRoom(newRoomCode().c_str(), &me);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", portTr("a room code to give the other player"));
    }
    if (!relay || ImGui::TreeNode(portTrLabel("By address instead##online_by_address"))) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
      if (ImGui::InputInt(portTrLabel("Port##online_port"), &sLobby.port, 0)) {
        sLobby.port = std::clamp(sLobby.port, 1024, 65535);
        savePrefs();
      }
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel(relay ? "Host by address" : "Host"))) {
        const PortOnlineHello me = hello();
        portOnlineHost(sLobby.port, &me);
      }
      if (relay) {
        ImGui::TreePop();
      }
    }
    ImGui::Dummy(ImVec2(0, 6.f * scale));
    ImGui::TextUnformatted(portTr("Join a game"));
    ImGui::SetNextItemWidth(field);
    if (ImGui::InputText(portTrLabel(relay ? "Room code or address##online_address" : "Address##online_address"),
                         sLobby.address, sizeof(sLobby.address))) {
      savePrefs();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(sLobby.address[0] == '\0');
    if (ImGui::Button(portTrLabel("Join"))) {
      sOffer.clear();
      joinTyped();
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Watch##online_watch"))) {
      sOffer.clear();
      watchTyped();
    }
    ImGui::EndDisabled();
    note("Watch: see the two players' game as it goes, a few seconds behind, without playing.");
    if (!sOffer.empty() && relay) {
      // one-click join: asked, never done by itself
      ImGui::Dummy(ImVec2(0, 6.f * scale));
      const std::string shown = shownCode(sOffer);
      ImGui::Text(portTr(sOfferFrom == 1 ? "Join room %s?" : "Join room %s from the clipboard?"), shown.c_str());
      if (ImGui::Button(portTrLabel("Join##online_offer"))) {
        std::snprintf(sLobby.address, sizeof(sLobby.address), "%s", shown.c_str());
        sOffer.clear();
        joinTyped();
      }
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Watch##online_offer_watch"))) {
        std::snprintf(sLobby.address, sizeof(sLobby.address), "%s", shown.c_str());
        sOffer.clear();
        watchTyped();
      }
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Not now##online_offer"))) {
        sOffer.clear();
      }
    }
    return false;
  }

  if (state == PORT_ONLINE_LISTENING && std::strcmp(portOnlineLinkName(), "direct") != 0) {
    // a room: its code once it's open (until then its relay, and so the code, may change)
    ImGui::TextWrapped("%s", portTr(portOnlineRoom()[0] != '\0' ? "Waiting for the other player to join..."
                                                                 : "Opening a room..."));
  }
  if (state == PORT_ONLINE_LISTENING && portOnlineRoom()[0] != '\0') {
    ImGui::Text("%s", portTr("Room code:"));
    ImGui::SameLine();
    // ROOM-SECRET: the two halves (the relay sees only the first)
    const std::string room = portOnlineRoom();
    const std::string shown = room.substr(0, PORT_ONLINE_ROOM_HALF) + "-" + room.substr(PORT_ONLINE_ROOM_HALF);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(shown.c_str());
    ImGui::SetWindowFontScale(1.f);
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Copy##online_code"))) {
      SDL_SetClipboardText(shown.c_str());
      sCopied = room;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Copy invite link##online_link"))) {
      // papercut://join/K7P2-QX9M: opens Paper Cut on the other player's Online tab, asking "Join?"
      SDL_SetClipboardText((std::string("papercut://join/") + shown).c_str());
      sCopied = room;
    }
    note("Give this code to the other player. Paper Cut connects you directly when it can, and through its relay "
         "when it can't: nothing to set up on your router.");
  } else if (state == PORT_ONLINE_LISTENING && std::strcmp(portOnlineLinkName(), "direct") == 0) {
    char address[64];
    ImGui::TextWrapped("%s", portTr("Waiting for the other player to join..."));
    if (portOnlineLocalAddress(address, sizeof(address))) {
      ImGui::Text(portTr("Your address: %s (port %d)"), address, sLobby.port);
    }
    note("On the same network, that address is enough. Over the internet, forward that port on your router to "
         "this computer and give your public address, or both join a VPN such as Tailscale or ZeroTier and use "
         "its address.");
  } else if (state == PORT_ONLINE_CONNECTING || state == PORT_ONLINE_HANDSHAKE) {
    ImGui::TextWrapped("%s", portTr("Connecting..."));
  } else if (state == PORT_ONLINE_WAITING_SETUP && portOnlineWatching()) {
    ImGui::TextWrapped("%s", portTr("Connected: waiting for the players to start."));
    note("You'll watch from the start of their game.");
  } else if (state == PORT_ONLINE_PEER_HERE) {
    const int rtt = portOnlineRoundTrip();
    ImGui::Text(portTr("%s joined."), peerName());
    if (rtt >= 0) {
      ImGui::SameLine();
      ImGui::TextDisabled(portTr("(round trip %d ms)"), rtt);
    }
    if (portOnlineRoom()[0] != '\0') {
      ImGui::TextDisabled("%s", portTr(std::strcmp(portOnlineLinkName(), "relay") == 0
                                           ? "Through the relay (no direct way between you found yet)."
                                           : "Connected directly."));
    }
    int slot = 0;
    const bool canContinue = !resumeSave(&slot).empty();
    ImGui::RadioButton(portTrLabel("Start from the title##online_start"), &sLobby.startMode, 0);
    ImGui::BeginDisabled(!canContinue);
    ImGui::RadioButton(portTrLabel("Continue where you left off##online_start"), &sLobby.startMode, 1);
    ImGui::EndDisabled();
    if (!canContinue) {
      sLobby.startMode = 0;
    }
    if (portOnlineWatchers() > 0) {
      ImGui::TextDisabled(portTr("%d watching"), portOnlineWatchers());
    }
    if (sPeerAt == 0) {
      sPeerAt = now;
    }
    if (sAutoStartUntil > now) {
      // the guest joined from its running game and started it again: the session at once
      // (the direct path's search given its time, as the start-up's does)
      ImGui::TextDisabled("%s", portTr("They started their game again to play: starting..."));
      if (rtt >= 0 && (portOnlineRoom()[0] == '\0' || std::strcmp(portOnlineLinkName(), "relay") != 0 ||
                       now - sPeerAt > 9000)) {
        sAutoStartUntil = 0;
        sendSetup();
      }
    }
    if (ImGui::Button(portTrLabel("Start"))) {
      sAutoStartUntil = 0;
      sendSetup();
    }
    ImGui::SameLine();
  } else if (state == PORT_ONLINE_WAITING_SETUP) {
    ImGui::Text(portTr("Connected to %s: waiting for them to start."), peerName());
  } else if (state == PORT_ONLINE_GOT_SETUP) {
    applySetup();
  } else if (state == PORT_ONLINE_WAITING_READY || state == PORT_ONLINE_WAITING_START) {
    ImGui::TextWrapped("%s", portTr("Starting..."));
  } else if (state == PORT_ONLINE_STARTED && !sLobby.begun) {
    sLobby.begun = true;
    portLockstepBegin(static_cast<long long>(sLobby.base));
    portOnlineBegin(sLobby.delay);
    return true;
  }
  if (ImGui::Button(portTrLabel("Cancel##online"))) {
    portOnlineCancel("The other player cancelled.");
    portSettingsHoldSaves(0);
  }
  return false;
}

// Tests: $TTYD_ONLINE_HOST=<port> or $TTYD_ONLINE_JOIN=<address>:<port>, the
// lobby's steps without its page, before the game starts (main_aurora.c): the
// host starts as soon as the guest is there, from the title. Returns once
// both games start (or gives up after a minute).
// $TTYD_ONLINE_WATCH_ROOM=<code> or $TTYD_ONLINE_WATCH=<address>:<port>: a watcher
// the same way (trying again for a while: the host may not be there yet).
static bool testWatch(void) {
  const char* room = SDL_getenv("TTYD_ONLINE_WATCH_ROOM");
  const char* address = SDL_getenv("TTYD_ONLINE_WATCH");
  if (room == nullptr && address == nullptr) {
    return false;
  }
  loadPrefs();
  if (SDL_getenv("TTYD_ONLINE_RECOVERIES") == nullptr) {
    std::snprintf(sLobby.name, sizeof(sLobby.name), "%s", "Test watcher");
  }
  const PortOnlineHello me = hello();
  const std::string code = room != nullptr ? asRoomCode(room) : "";
  std::string host;
  int port = 0;
  if (address != nullptr) {
    splitAddress(address, &host, &port);
  }
  sRejoinKind = room != nullptr ? "watch-room" : "watch-address";
  sRejoinTarget = room != nullptr ? code : std::string(address);
  auto start = [&]() {
    if (room != nullptr) {
      portOnlineWatchRoom(code.c_str(), &me);
    } else {
      portOnlineWatch(host.c_str(), port, &me);
    }
  };
  const Uint64 until = SDL_GetTicks() + 120000;
  start();
  while (portOnlineLobbyState() != PORT_ONLINE_STARTED && SDL_GetTicks() < until) {
    portOnlineLobbyPump();
    // (failed; or the host is starting its game again: applySetup)
    if (portOnlineLobbyState() == PORT_ONLINE_FAILED || portOnlineLobbyState() == PORT_ONLINE_IDLE) {
      SDL_Delay(1000);
      start();
    } else if (portOnlineLobbyState() == PORT_ONLINE_GOT_SETUP) {
      applySetup();
    }
    SDL_PumpEvents(); // (the window answers meanwhile)
    SDL_Delay(5);
  }
  if (portOnlineLobbyState() != PORT_ONLINE_STARTED) {
    std::printf("[online] test: not watching (%s)\n", portOnlineStatus());
    std::fflush(stdout);
    return true;
  }
  std::printf("[online] test: watching\n");
  std::fflush(stdout);
  portLockstepBegin(static_cast<long long>(sLobby.base));
  portOnlineBegin(sLobby.delay);
  return true;
}

extern "C" void portOnlineTestStart(void) {
  if (testWatch()) {
    return;
  }
  // (a code as a player would type it: with or without its dash)
  static std::string sHostRoom, sJoinRoom;
  sHostRoom = SDL_getenv("TTYD_ONLINE_HOST_ROOM") != nullptr ? asRoomCode(SDL_getenv("TTYD_ONLINE_HOST_ROOM")) : "";
  sJoinRoom = SDL_getenv("TTYD_ONLINE_JOIN_ROOM") != nullptr ? asRoomCode(SDL_getenv("TTYD_ONLINE_JOIN_ROOM")) : "";
  const char* hostRoom = sHostRoom.empty() ? nullptr : sHostRoom.c_str();
  const char* joinRoom = sJoinRoom.empty() ? nullptr : sJoinRoom.c_str();
  const char* hostPort = hostRoom != nullptr ? hostRoom : SDL_getenv("TTYD_ONLINE_HOST");
  const char* join = joinRoom != nullptr ? joinRoom : SDL_getenv("TTYD_ONLINE_JOIN");
  if (hostPort == nullptr && join == nullptr) {
    return;
  }
  loadPrefs();
  if (SDL_getenv("TTYD_ONLINE_RECOVERIES") == nullptr) { // (a recovery's restart keeps the player's own name)
    std::snprintf(sLobby.name, sizeof(sLobby.name), "%s", hostPort != nullptr ? "Test host" : "Test guest");
  }
  const PortOnlineHello me = hello();
  if (hostRoom != nullptr) {
    portOnlineHostRoom(hostRoom, &me);
  } else if (joinRoom != nullptr) {
    sRejoinKind = "room";
    sRejoinTarget = joinRoom;
    // the host may not be in the room yet: try again for a while
    const Uint64 until = SDL_GetTicks() + 60000;
    portOnlineJoinRoom(joinRoom, &me);
    while (portOnlineLobbyState() != PORT_ONLINE_STARTED && SDL_GetTicks() < until) {
      portOnlineLobbyPump();
      // (failed; or the host is starting its game again: applySetup)
      if (portOnlineLobbyState() == PORT_ONLINE_FAILED || portOnlineLobbyState() == PORT_ONLINE_IDLE) {
        SDL_Delay(500);
        portOnlineJoinRoom(joinRoom, &me);
      } else if (portOnlineLobbyState() == PORT_ONLINE_GOT_SETUP) {
        applySetup();
      }
      SDL_PumpEvents(); // (the window answers meanwhile)
      SDL_Delay(5);
    }
  } else if (hostPort != nullptr) {
    sLobby.port = std::atoi(hostPort);
    portOnlineHost(sLobby.port, &me);
  } else {
    std::string address = join;
    const size_t colon = address.rfind(':');
    const int port = colon != std::string::npos ? std::atoi(address.c_str() + colon + 1) : PORT_ONLINE_DEFAULT_PORT;
    sRejoinKind = "address";
    sRejoinTarget = join;
    if (colon != std::string::npos) {
      address.resize(colon);
    }
    if (address.size() > 2 && address.front() == '[' && address.back() == ']') {
      address = address.substr(1, address.size() - 2); // an IPv6 address in brackets
    }
    // the host may not be listening yet: try again for a while
    const Uint64 until = SDL_GetTicks() + 60000;
    portOnlineJoin(address.c_str(), port, &me);
    while (portOnlineLobbyState() != PORT_ONLINE_STARTED && SDL_GetTicks() < until) {
      portOnlineLobbyPump();
      // (failed; or the host is starting its game again: applySetup)
      if (portOnlineLobbyState() == PORT_ONLINE_FAILED || portOnlineLobbyState() == PORT_ONLINE_IDLE) {
        SDL_Delay(500);
        portOnlineJoin(address.c_str(), port, &me);
      } else if (portOnlineLobbyState() == PORT_ONLINE_GOT_SETUP) {
        applySetup();
      }
      SDL_PumpEvents(); // (the window answers meanwhile)
      SDL_Delay(5);
    }
  }
  const Uint64 until = SDL_GetTicks() + 60000;
  Uint64 peerAt = 0;
  while (hostPort != nullptr && portOnlineLobbyState() != PORT_ONLINE_STARTED && SDL_GetTicks() < until) {
    portOnlineLobbyPump();
    if (portOnlineLobbyState() == PORT_ONLINE_PEER_HERE && portOnlineRoundTrip() >= 0) {
      // a room: the direct path's search given its time (as a player would, reading the page)
      if (peerAt == 0) {
        peerAt = SDL_GetTicks();
      }
      if (hostRoom == nullptr || std::strcmp(portOnlineLinkName(), "relay") != 0 ||
          SDL_GetTicks() - peerAt > 9000) {
        // from the title, or (a recovery's restart) from the host's last save
        sLobby.startMode = SDL_getenv("TTYD_ONLINE_CONTINUE") != nullptr ? 1 : 0;
        std::printf("[online] test: the session goes %s\n", portOnlineLinkName());
        std::fflush(stdout);
        sendSetup();
      }
    } else if (portOnlineLobbyState() == PORT_ONLINE_FAILED) {
      // (a restart's room may still have this game's old connection for a moment: again)
      std::printf("[online] test: hosting again (%s)\n", portOnlineStatus());
      std::fflush(stdout);
      SDL_Delay(1000);
      if (hostRoom != nullptr) {
        portOnlineHostRoom(hostRoom, &me);
      } else {
        portOnlineHost(sLobby.port, &me);
      }
    }
    SDL_PumpEvents(); // (the window answers meanwhile)
    SDL_Delay(5);
  }
  if (portOnlineLobbyState() != PORT_ONLINE_STARTED) {
    std::printf("[online] test: no session (%s)\n", portOnlineStatus());
    std::fflush(stdout);
    return;
  }
  portLockstepBegin(static_cast<long long>(sLobby.base));
  portOnlineBegin(sLobby.delay);
}

// --- The Start menu's Online tab (port/include/port/online_menu.h) ---

namespace {

struct Menu {
  bool driving = false;   // a lobby the Start menu started (menuFrame pumps it)
  bool host = false;      // this side hosts
  bool byAddress = false; // by address, not a room
  bool watch = false;     // only to watch
  Uint64 restartAt = 0;   // the games start again for the session: this one then
  int restartAs = -1;     // as the host (1), the guest (0), a watcher (2)
  std::string room;       // the room's code, kept for the restart
  std::string reason;     // the last failure (English)
  std::string shown, address;
} sMenu;

// The session's start-up, from this running game (online_menu.h): as online co-op's
// recovery does, the environment says what to host or join, and this program starts
// again (restart.cpp). Doesn't return, unless it couldn't start again.
void menuRestart(int as) {
  SDL_Environment* env = SDL_GetEnvironment();
  for (const char* name : {"TTYD_ONLINE_HOST", "TTYD_ONLINE_HOST_ROOM", "TTYD_ONLINE_JOIN", "TTYD_ONLINE_JOIN_ROOM",
                           "TTYD_ONLINE_CONTINUE", "TTYD_ONLINE_WATCH", "TTYD_ONLINE_WATCH_ROOM", "TTYD_ONLINE_LAN"}) {
    SDL_UnsetEnvironmentVariable(env, name);
  }
  if (as == 1) {
    if (!sMenu.room.empty()) {
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_HOST_ROOM", sMenu.room.c_str(), true);
    } else {
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_HOST", std::to_string(sLobby.port).c_str(), true);
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_LAN", "1", true); // (the guest is on another computer)
    }
    if (sLobby.startMode == 1 && portResumeWriteNow()) { // where the host is now: quick resume's last save
      SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_CONTINUE", "1", true);
    }
  } else {
    const bool room = sRejoinKind == "room" || sRejoinKind == "watch-room";
    const char* name = as == 2 ? (room ? "TTYD_ONLINE_WATCH_ROOM" : "TTYD_ONLINE_WATCH")
                               : (room ? "TTYD_ONLINE_JOIN_ROOM" : "TTYD_ONLINE_JOIN");
    SDL_SetEnvironmentVariable(env, name, sRejoinTarget.c_str(), true);
  }
  // (set: the start-up keeps the player's own name, and counts no recovery)
  SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_RECOVERIES", "0", true);
  std::printf("[online] menu: starting again for the session, as the %s\n",
              as == 1 ? "host" : as == 2 ? "watcher" : "guest");
  std::fflush(stdout);
  portRestartForOnline();
  sMenu.reason = "This game couldn't start again for the online game.";
}

bool menuCan() { return portRestartSupported() != 0; }

// each presented frame (an overlay of the settings page's): the Start menu's lobby
void menuFrame(int) {
  if (!sMenu.driving) {
    return;
  }
  if (portOnlineActive()) {
    sMenu.driving = false; // (a session: not the lobby's any more)
    return;
  }
  portOnlineLobbyPump();
  if (sMenu.restartAt != 0) {
    if (SDL_GetTicks() >= sMenu.restartAt) {
      sMenu.restartAt = 0;
      sMenu.driving = false;
      // the room's connection closed properly first: a relay keeps a host that just vanished
      // ("That room has a host") until it notices, and the restarted host opens the same room
      portOnlineCancel(nullptr);
      menuRestart(sMenu.restartAs);
    }
    return;
  }
  const int state = portOnlineLobbyState();
  if (state == PORT_ONLINE_FAILED) {
    sMenu.reason = portOnlineStatus();
    sMenu.driving = false;
    return;
  }
  if (!sMenu.host && state == PORT_ONLINE_GOT_SETUP) {
    size_t len = 0;
    const void* data = portOnlineSetup(&len);
    if (sMenu.watch) {
      // a watcher: its game starts again, and watches from there (the players' game so far first)
      portOnlineCancel(nullptr);
      sMenu.restartAs = 2;
    } else if (isMenuGo(data, len)) {
      // the host started from its running game too: both start again, the host hosting
      portOnlineCancel(nullptr);
      sMenu.restartAs = 0;
    } else {
      // a host in the launcher: it waits while this game starts again, and starts by itself
      portOnlineCancel(PORT_ONLINE_BYE_REJOIN);
      sMenu.restartAs = 0;
    }
    sMenu.restartAt = SDL_GetTicks() + 300;
  }
}

const bool sMenuAdded = [] {
  portSettingsAddOverlay(menuFrame, nullptr);
  return true;
}();

// the room list: fetched on a thread of its own when asked
struct ListedRoom {
  std::string code, name;
  int players = 0;
};
std::mutex sListLock;
std::vector<ListedRoom> sListed;
int sListState = PORT_ROOM_LIST_NONE;
int sListOnline = -1;

std::string percentDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

void fetchList(std::vector<std::string> urls) {
  std::vector<ListedRoom> rooms;
  int online = 0, answered = 0, old = 0;
  for (const std::string& url : urls) {
    std::vector<uint8_t> body;
    std::string error;
    if (!port_http::fetch(url, body, 64 * 1024, error, "text/plain")) {
      std::printf("[online] room list: %s: %s\n", url.c_str(), error.c_str());
      std::fflush(stdout);
      old += error.find("404") != std::string::npos ? 1 : 0; // (a relay from before the room list)
      continue;
    }
    const std::string text(body.begin(), body.end());
    if (text.compare(0, 6, "rooms ") != 0) {
      old++;
      continue;
    }
    answered++;
    size_t at = 0;
    bool first = true;
    while (at < text.size()) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) {
        end = text.size();
      }
      const std::string line = text.substr(at, end - at);
      at = end + 1;
      if (first) {
        online += std::max(0, std::atoi(line.c_str() + 6));
        first = false;
        continue;
      }
      const size_t a = line.find(' ');
      const size_t b = a == std::string::npos ? a : line.find(' ', a + 1);
      if (b == std::string::npos) {
        continue;
      }
      ListedRoom r;
      r.code = strictRoomCode(line.substr(0, a).c_str());
      r.players = std::atoi(line.c_str() + a + 1);
      r.name = percentDecode(line.substr(b + 1)).substr(0, 31);
      if (!r.code.empty() && rooms.size() < 64) {
        rooms.push_back(r);
      }
    }
  }
  std::printf("[online] room list: %d rooms, %d online (%d relays answered, %d without the list)\n",
              static_cast<int>(rooms.size()), online, answered, old);
  std::fflush(stdout);
  std::lock_guard<std::mutex> lock(sListLock);
  sListed = rooms;
  sListOnline = answered > 0 ? online : -1;
  sListState = answered > 0 ? PORT_ROOM_LIST_READY : old > 0 ? PORT_ROOM_LIST_OLD_RELAY : PORT_ROOM_LIST_FAILED;
}

} // namespace

extern "C" int portOnlineMenuPhase(void) {
  if (portOnlineActive()) {
    return portOnlineWatching() ? PORT_ONLINE_MENU_WATCHING : PORT_ONLINE_MENU_PLAYING;
  }
  if (sMenu.restartAt != 0) {
    return PORT_ONLINE_MENU_STARTING;
  }
  if (!sMenu.driving) {
    return sMenu.reason.empty() ? PORT_ONLINE_MENU_OFF : PORT_ONLINE_MENU_FAILED;
  }
  switch (portOnlineLobbyState()) {
    case PORT_ONLINE_LISTENING:
      return portOnlineRoom()[0] != '\0' || sMenu.byAddress ? PORT_ONLINE_MENU_HOSTING : PORT_ONLINE_MENU_OPENING;
    case PORT_ONLINE_CONNECTING:
    case PORT_ONLINE_HANDSHAKE:
      return sMenu.host ? PORT_ONLINE_MENU_HOSTING : PORT_ONLINE_MENU_JOINING;
    case PORT_ONLINE_PEER_HERE:
      return PORT_ONLINE_MENU_PEER_HERE;
    case PORT_ONLINE_WAITING_SETUP:
      return sMenu.watch ? PORT_ONLINE_MENU_WATCH_WAIT : PORT_ONLINE_MENU_JOINED;
    case PORT_ONLINE_GOT_SETUP:
    case PORT_ONLINE_WAITING_READY:
    case PORT_ONLINE_WAITING_START:
    case PORT_ONLINE_STARTED:
      return PORT_ONLINE_MENU_STARTING;
    case PORT_ONLINE_FAILED:
      return PORT_ONLINE_MENU_FAILED;
    default:
      return PORT_ONLINE_MENU_OFF;
  }
}

extern "C" const char* portOnlineMenuReason(void) { return sMenu.reason.c_str(); }

extern "C" const char* portOnlineMenuCode(void) {
  const std::string room = portOnlineRoom();
  sMenu.shown = room.size() == PORT_ONLINE_CODE_LENGTH ? shownCode(room) : std::string();
  return sMenu.shown.c_str();
}

extern "C" const char* portOnlineMenuAddress(void) {
  char address[64];
  sMenu.address.clear();
  if (sMenu.driving && sMenu.host && sMenu.byAddress && portOnlineLocalAddress(address, sizeof(address))) {
    sMenu.address = std::string(address) + ":" + std::to_string(sLobby.port);
  }
  return sMenu.address.c_str();
}

extern "C" const char* portOnlineMenuPeer(void) {
  const int phase = portOnlineMenuPhase();
  if (phase == PORT_ONLINE_MENU_OFF || phase == PORT_ONLINE_MENU_FAILED || phase == PORT_ONLINE_MENU_OPENING ||
      phase == PORT_ONLINE_MENU_HOSTING || phase == PORT_ONLINE_MENU_JOINING) {
    return "";
  }
  return portOnlinePeer()->name;
}

extern "C" int portOnlineMenuPing(void) {
  const int phase = portOnlineMenuPhase();
  return phase == PORT_ONLINE_MENU_PEER_HERE || phase == PORT_ONLINE_MENU_JOINED || phase == PORT_ONLINE_MENU_PLAYING
             ? portOnlineRoundTrip()
             : -1;
}

extern "C" const char* portOnlineMenuLink(void) { return portOnlineLinkName(); }

extern "C" int portOnlineMenuWatchers(void) { return portOnlineWatchers(); }

extern "C" int portOnlineMenuCan(void) { return menuCan() ? 1 : 0; }

extern "C" int portOnlineMenuHasRelay(void) { return portOnlineRelay()[0] != '\0' ? 1 : 0; }

extern "C" void portOnlineMenuHost(int byAddress) {
  loadPrefs();
  if (!menuCan() || portOnlineActive()) {
    return;
  }
  const PortOnlineHello me = hello();
  sMenu = Menu{};
  sMenu.driving = true;
  sMenu.host = true;
  sMenu.byAddress = byAddress != 0 || portOnlineRelay()[0] == '\0';
  if (sMenu.byAddress) {
    portOnlineHost(sLobby.port, &me);
  } else {
    portOnlineListRoom(sLobby.listMine ? sLobby.name : nullptr);
    portOnlineHostRoom(newRoomCode().c_str(), &me);
  }
  menuFrame(0);
}

extern "C" void portOnlineMenuJoin(const char* text, int watch) {
  loadPrefs();
  if (!menuCan() || portOnlineActive() || text == nullptr || text[0] == '\0') {
    return;
  }
  sMenu = Menu{};
  sMenu.driving = true;
  sMenu.watch = watch != 0;
  std::snprintf(sLobby.address, sizeof(sLobby.address), "%s", text);
  savePrefs();
  sMenu.byAddress = asRoomCode(sLobby.address).empty() || portOnlineRelay()[0] == '\0';
  if (sMenu.watch) {
    watchTyped();
  } else {
    joinTyped();
  }
  menuFrame(0);
}

extern "C" void portOnlineMenuStart(void) {
  if (!sMenu.driving || !sMenu.host || portOnlineLobbyState() != PORT_ONLINE_PEER_HERE) {
    return;
  }
  sMenu.room = portOnlineRoom();
  if (portOnlineSendSetup(kMenuGo, sizeof(kMenuGo) - 1)) {
    // (a moment for the word to reach the guest and the watchers, then this game starts again)
    sMenu.restartAs = 1;
    sMenu.restartAt = SDL_GetTicks() + 1500;
  }
}

extern "C" void portOnlineMenuLeave(void) {
  if (portOnlineActive()) {
    portOnlineLeave();
  } else if (portOnlineLobbyState() != PORT_ONLINE_IDLE) {
    portOnlineCancel(sMenu.host ? "The host closed the room." : "The other player cancelled.");
  }
  sMenu = Menu{};
}

extern "C" int portOnlineMenuCopy(int link) {
  const std::string room = portOnlineRoom();
  if (room.size() != PORT_ONLINE_CODE_LENGTH) {
    return 0;
  }
  const std::string shown = shownCode(room);
  SDL_SetClipboardText(link ? (std::string("papercut://join/") + shown).c_str() : shown.c_str());
  sCopied = room;
  return 1;
}

extern "C" int portOnlineMenuPaste(char* out, int size) {
  std::string found;
  out[0] = '\0';
  if (!SDL_HasClipboardText()) {
    return 0;
  }
  if (char* clip = SDL_GetClipboardText()) { // (read only when asked; never sent anywhere)
    std::string text = clip;
    SDL_free(clip);
    if (text.compare(0, 16, "papercut://join/") == 0) {
      text = text.substr(16);
    }
    found = text.size() < 64 ? strictRoomCode(text.c_str()) : std::string();
  }
  std::snprintf(out, static_cast<size_t>(size), "%s", found.c_str());
  return found.empty() ? 0 : 1;
}

extern "C" const char* portOnlineMenuName(void) {
  loadPrefs();
  return sLobby.name;
}

extern "C" void portOnlineMenuSetName(const char* name) {
  loadPrefs();
  std::snprintf(sLobby.name, sizeof(sLobby.name), "%s", name);
  savePrefs();
}

extern "C" int portOnlineMenuPort(void) {
  loadPrefs();
  return sLobby.port;
}

extern "C" void portOnlineMenuSetPort(int port) {
  loadPrefs();
  sLobby.port = std::clamp(port, 1024, 65535);
  savePrefs();
}

extern "C" int portOnlineMenuStartMode(void) { return sLobby.startMode; }

extern "C" void portOnlineMenuSetStartMode(int mode) { sLobby.startMode = mode != 0 ? 1 : 0; }

extern "C" int portOnlineMenuListMine(void) {
  loadPrefs();
  return sLobby.listMine ? 1 : 0;
}

extern "C" void portOnlineMenuSetListMine(int on) {
  loadPrefs();
  sLobby.listMine = on != 0;
  savePrefs();
}

extern "C" const char* portOnlineMenuLastTyped(void) {
  loadPrefs();
  return sLobby.address;
}

extern "C" void portOnlineMenuListRefresh(void) {
  std::vector<std::string> urls;
  for (int i = 0; portOnlineRelayAt(i) != nullptr; i++) {
    const std::string relay = portOnlineRelayAt(i);
    if (relay.compare(0, 6, "wss://") == 0) { // (https only: a test's ws:// relay has no list here)
      char game[8];
      std::snprintf(game, sizeof(game), "%.6s", portVersionSelf()->id);
      urls.push_back("https://" + relay.substr(6) + "/rooms?game=" + game + "&v=" + PORT_VERSION);
    }
  }
  {
    std::lock_guard<std::mutex> lock(sListLock);
    if (sListState == PORT_ROOM_LIST_LOADING) {
      return;
    }
    if (urls.empty()) {
      sListState = PORT_ROOM_LIST_FAILED;
      return;
    }
    sListState = PORT_ROOM_LIST_LOADING;
  }
  std::thread(fetchList, urls).detach();
}

extern "C" int portOnlineMenuListState(void) {
  std::lock_guard<std::mutex> lock(sListLock);
  return sListState;
}

extern "C" int portOnlineMenuListOnline(void) {
  std::lock_guard<std::mutex> lock(sListLock);
  return sListOnline;
}

extern "C" int portOnlineMenuListCount(void) {
  std::lock_guard<std::mutex> lock(sListLock);
  return sListState == PORT_ROOM_LIST_READY ? static_cast<int>(sListed.size()) : 0;
}

extern "C" int portOnlineMenuListGet(int i, char* code, int codeSize, char* name, int nameSize, int* players) {
  std::lock_guard<std::mutex> lock(sListLock);
  if (sListState != PORT_ROOM_LIST_READY || i < 0 || i >= static_cast<int>(sListed.size())) {
    return 0;
  }
  std::snprintf(code, static_cast<size_t>(codeSize), "%s", sListed[i].code.c_str());
  std::snprintf(name, static_cast<size_t>(nameSize), "%s", sListed[i].name.c_str());
  *players = sListed[i].players;
  return 1;
}
