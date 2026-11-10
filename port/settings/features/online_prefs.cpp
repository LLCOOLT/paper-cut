// Online's preferences (port/settings/online_prefs.h) and the Start menu's Online tab
// options that only read or change them (port/include/port/online_menu.h): what online
// play (port/include/port/statesync.h) and the boards use; the relay's room list; and the
// settings page's word on where online play is.
#include "../online_prefs.h"
#include "../file_util.h"
#include "port/detach.h"
#include "port/online.h"
#include "port/http.h"
#include "port/online_menu.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "port/versions.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
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

namespace portonline {

Prefs gPrefs;

namespace {

std::string prefsPath() { return std::string(portSettingsUserPath()) + "/online.txt"; }

bool writeFile(const std::string& path, const std::string& data) {
  SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
  if (io == nullptr) {
    return false;
  }
  const bool ok = SDL_WriteIO(io, data.data(), data.size()) == data.size();
  SDL_CloseIO(io);
  return ok;
}

// a player's name as the room shows it: at most 24 bytes of whole UTF-8 characters, up to the first control
// character; "Player" when nothing's left (the game's keyboard takes 24 too; the room's names 31)
void fitName(char* name, size_t cap) {
  size_t i = 0;
  while (i < cap && static_cast<unsigned char>(name[i]) >= 0x20) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    const size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    size_t k = 1;
    while (k < len && i + k < cap && (static_cast<unsigned char>(name[i + k]) & 0xC0) == 0x80) {
      k++;
    }
    if (k < len || i + len > 24) {
      break;
    }
    i += len;
  }
  if (i < cap) {
    name[i] = '\0';
  }
  if (name[0] == '\0') {
    std::snprintf(name, cap, "%s", "Player");
  }
}

} // namespace

void loadPrefs() {
  if (gPrefs.loaded) {
    return;
  }
  gPrefs.loaded = true;
  const char* user = SDL_getenv("USERNAME") != nullptr ? SDL_getenv("USERNAME") : SDL_getenv("USER");
  std::snprintf(gPrefs.name, sizeof(gPrefs.name), "%s", user != nullptr ? user : "Player");
  fitName(gPrefs.name, sizeof(gPrefs.name));
  const std::string text = portfile::readFile(prefsPath());
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
      std::snprintf(gPrefs.name, sizeof(gPrefs.name), "%s", v.c_str());
      fitName(gPrefs.name, sizeof(gPrefs.name));
    } else if (k == "address") {
      std::snprintf(gPrefs.address, sizeof(gPrefs.address), "%s", v.c_str());
    } else if (k == "port") {
      gPrefs.port = std::clamp(std::atoi(v.c_str()), 1024, 65535);
    } else if (k == "list") {
      gPrefs.listMine = v == "1";
    }
  }
}

void savePrefs() {
  writeFile(prefsPath(), std::string("name=") + gPrefs.name + "\naddress=" + gPrefs.address +
                             "\nport=" + std::to_string(gPrefs.port) + "\nlist=" + (gPrefs.listMine ? "1" : "0") +
                             "\n");
}

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

} // namespace portonline

using portonline::gPrefs;
using portonline::loadPrefs;
using portonline::savePrefs;

extern "C" int portOnlineMenuHasRelay(void) { return portOnlineRelay()[0] != '\0' ? 1 : 0; }

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
    found = text.size() < 64 ? portonline::strictRoomCode(text.c_str()) : std::string();
  }
  std::snprintf(out, static_cast<size_t>(size), "%s", found.c_str());
  return found.empty() ? 0 : 1;
}

extern "C" const char* portOnlineMenuName(void) {
  loadPrefs();
  return gPrefs.name;
}

extern "C" void portOnlineMenuSetName(const char* name) {
  loadPrefs();
  std::snprintf(gPrefs.name, sizeof(gPrefs.name), "%s", name);
  portonline::fitName(gPrefs.name, sizeof(gPrefs.name));
  savePrefs();
}

extern "C" int portOnlineMenuPort(void) {
  loadPrefs();
  return gPrefs.port;
}

extern "C" void portOnlineMenuSetPort(int port) {
  loadPrefs();
  gPrefs.port = std::clamp(port, 1024, 65535);
  savePrefs();
}

extern "C" const char* portOnlineMenuLastTyped(void) {
  loadPrefs();
  return gPrefs.address;
}

namespace {

// the room list: fetched on a thread of its own when asked
struct ListedRoom {
  std::string code, name;
  int players = 0, max = 2; // "1/4" (an older relay: the count alone, of two)
};
// (left allocated at exit: the detached fetch thread may still be waiting on a relay as the program quits)
std::mutex& sListLock = *new std::mutex;
std::vector<ListedRoom>& sListed = *new std::vector<ListedRoom>;
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
  portCrashThreadInit(); // (a stack overflow here reported too)
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
      r.code = portonline::strictRoomCode(line.substr(0, a).c_str());
      r.players = std::atoi(line.c_str() + a + 1);
      const size_t slash = line.find('/', a + 1);
      r.max = slash != std::string::npos && slash < b ? std::clamp(std::atoi(line.c_str() + slash + 1), 2, 4) : 2;
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

extern "C" int portOnlineMenuListMine(void) {
  loadPrefs();
  return gPrefs.listMine ? 1 : 0;
}

extern "C" void portOnlineMenuSetListMine(int on) {
  loadPrefs();
  gPrefs.listMine = on != 0;
  savePrefs();
}

// the list of the rooms this build can join (portStateSyncListVersion: "sync-p" and the protocol, any
// build of it; 0.4.x's rooms, listed under their release, aren't asked for)
extern "C" void portStateSyncListRefresh(void) {
  std::vector<std::string> urls;
  std::string v;
  for (const char* c = portStateSyncListVersion(); *c != '\0'; c++) {
    v += *c == '+' ? std::string("%2B") : std::string(1, *c); // (a query's + is a space)
  }
  for (int i = 0; portOnlineRelayAt(i) != nullptr; i++) {
    const std::string relay = portOnlineRelayAt(i);
    if (relay.compare(0, 6, "wss://") == 0) { // (https only: a test's ws:// relay has no list here)
      char game[8];
      std::snprintf(game, sizeof(game), "%.6s", portVersionSelf()->id);
      urls.push_back("https://" + relay.substr(6) + "/rooms?game=" + game + "&v=" + v);
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

extern "C" int portOnlineMenuListMax(int i) {
  std::lock_guard<std::mutex> lock(sListLock);
  return sListState == PORT_ROOM_LIST_READY && i >= 0 && i < static_cast<int>(sListed.size()) ? sListed[i].max : 2;
}

// The settings page's Multiplayer tab: where online play is (it isn't on this page). Leaving is
// the Start menu's too: each player's own game, so anyone leaves from there.
namespace {

// (online play is the Start menu's Multiplayer tab now, port/src/aurora/pause_online_aurora.c: no settings
// section of its own)
} // namespace
