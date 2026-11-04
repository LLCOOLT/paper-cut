// Discord status (System > Discord status; docs/DISCORD_STATUS.md): what the
// player is doing (the area, in battle or exploring, co-op or online) on their
// Discord profile, through the Discord app's own local IPC: no SDK, no library.
//
//   Windows  the named pipes \\.\pipe\discord-ipc-0 .. 9
//   Linux    unix sockets $XDG_RUNTIME_DIR, $TMPDIR, /tmp (each, then each +
//            /snap.discord, then + /app/com.discordapp.Discord) /discord-ipc-0 .. 9
//   macOS    $TMPDIR/discord-ipc-0 .. 9
//
// A frame is a little-endian u32 opcode, a u32 length, then that much JSON: op
// 0 the handshake, op 1 a command (SET_ACTIVITY), op 2 Discord closing, op 3/4
// ping and pong. A thread of its own does the talking, so the game never waits
// on Discord: it reconnects quietly every 15 s while Discord isn't there, and
// sends the activity only when it changed, at most once every 15 s (Discord's
// own rate limit). The game's side (the map, a battle) is discord_aurora.c.
// $TTYD_DISCORD_IPC: one pipe or socket to use instead (tests).
//
// The Discord application's ID is the build's (-DTTYD_DISCORD_APP_ID=...,
// PORT_DISCORD_APP_ID): without one the feature is inert and its setting
// hidden. Phones, tablets and the web build have no Discord app to talk to:
// compiled out there.
#include "port/online.h"
#include "port/settings.h"
#include "port/tr.h"

#include <dolphin/pad.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define PORT_DISCORD 0
#else
#define PORT_DISCORD 1
#endif

#ifndef PORT_DISCORD_APP_ID
#define PORT_DISCORD_APP_ID ""
#endif

#if PORT_DISCORD

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

extern "C" int portDiscordGame(char* map, int size, int* inBattle); // discord_aurora.c
extern "C" int portDiscordStory(int* chapter, int* level);            // discord_aurora.c

namespace {

constexpr const char* kAppId = PORT_DISCORD_APP_ID;
constexpr auto kRetry = std::chrono::seconds(15);    // reconnecting while Discord isn't there
constexpr auto kMinGap = std::chrono::seconds(15);   // between two SET_ACTIVITYs
constexpr auto kReadyWait = std::chrono::seconds(5); // for Discord's READY after the handshake

int* sOn;

// --- what to show: the area, by the map's prefix (src/data/mapdata.c's areas) ---

struct Area {
  const char* prefix;
  const char* name;
};
const Area kAreas[] = {
    {"aaa", "Mario's house"},     {"gor", "Rogueport"},          {"tik", "Rogueport Sewers"},
    {"hei", "Petal Meadows"},     {"nok", "Petalburg"},          {"gon", "Hooktail Castle"},
    {"win", "Boggly Woods"},      {"mri", "the Great Tree"},     {"tou", "Glitzville"},
    {"usu", "Twilight Town"},     {"gra", "Twilight Trail"},     {"jin", "Creepy Steeple"},
    {"muj", "Keelhaul Key"},      {"dou", "Pirate's Grotto"},    {"rsh", "the Excess Express"},
    {"hom", "Riverside Station"}, {"eki", "Riverside Station"},  {"pik", "Poshley Heights"},
    {"bom", "Fahr Outpost"},      {"moo", "the Moon"},           {"aji", "the X-Naut Fortress"},
    {"las", "the Palace of Shadow"}, {"jon", "the Pit of 100 Trials"}, {"kpa", "Playing as Bowser"},
    {"yuu", "the Pianta Parlor"}, {"qiz", "a quiz show"},
};

// the area's name for a map ("nok_00": "Petalburg"), or NULL (title screen, menus, test maps)
const char* areaName(const char* map) {
  for (const Area& a : kAreas) {
    if (std::strncmp(map, a.prefix, 3) == 0) {
      return a.name;
    }
  }
  return nullptr;
}

void appendJson(std::string& out, const char* text) {
  out += '"';
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0'; p++) {
    if (*p == '"' || *p == '\\') {
      out += '\\';
      out += char(*p);
    } else if (*p < 0x20) {
      char esc[8];
      std::snprintf(esc, sizeof esc, "\\u%04x", *p);
      out += esc;
    } else {
      out += char(*p); // UTF-8 as it is
    }
  }
  out += '"';
}

long long sStart; // the session's start (Unix seconds): the first time the game ran

// the activity's JSON for now ("null" to clear it), or "" before the game has started
std::string currentActivity() {
  if (sOn == nullptr || *sOn == 0) {
    return "null";
  }
  char map[32];
  int battle = 0;
  if (!portDiscordGame(map, int(sizeof map), &battle)) {
    return "";
  }
  if (sStart == 0) {
    sStart = (long long)std::time(nullptr);
  }
  const char* area = areaName(map);
  std::string details;
  if (area != nullptr) {
    details = area;
  } else if (map[0] == '\0' || std::strncmp(map, "sys", 3) == 0 || std::strncmp(map, "dmo", 3) == 0) {
    details = portTr("On the title screen");
  } else if (std::strncmp(map, "end", 3) == 0) {
    details = portTr("Watching the ending");
  } else {
    details = portTr("In the menus");
  }
  const int* coop = portSettingsFind("second_player");
  const bool twoPlayers = coop != nullptr && *coop && PADGetIndexForPort(1) >= 0;
  const char* state = battle               ? "In battle"
                      : portOnlineActive() ? "Playing online"
                      : twoPlayers         ? "Playing co-op"
                      : area != nullptr    ? "Exploring"
                                           : nullptr; // nothing to explore on the title screen
  // the second line: the chapter and Mario's level, then what he's doing
  std::string line;
  int chapter = 0, level = 0;
  if (area != nullptr && portDiscordStory(&chapter, &level)) {
    char buf[96];
    if (chapter == 0) {
      std::snprintf(buf, sizeof buf, "%s", portTr("Prologue"));
    } else if (chapter <= 8) {
      std::snprintf(buf, sizeof buf, portTr("Chapter %d"), chapter);
    } else {
      std::snprintf(buf, sizeof buf, "%s", portTr("Post-game"));
    }
    line = buf;
    if (level > 0) {
      std::snprintf(buf, sizeof buf, portTr("Lv %d"), level);
      line += " Â· "; // " · "
      line += buf;
    }
  }
  if (state != nullptr) {
    if (!line.empty()) {
      line += " Â· ";
    }
    line += portTr(state);
  }
  std::string a = "{\"details\":";
  appendJson(a, details.c_str());
  if (!line.empty()) {
    a += ",\"state\":";
    appendJson(a, line.c_str());
  }
  a += ",\"timestamps\":{\"start\":" + std::to_string(sStart) + "}";
  a += ",\"assets\":{\"large_image\":\"logo\",\"large_text\":\"Paper Cut\"}";
  a += ",\"instance\":false}";
  return a;
}

// --- the connection ---

// $TTYD_DISCORD_IPC: this pipe or socket only, instead of Discord's (tests, a
// stand-in beside a running Discord)
const char* testPath() {
  const char* p = std::getenv("TTYD_DISCORD_IPC");
  return p != nullptr && p[0] != '\0' ? p : nullptr;
}

#if defined(_WIN32)
using Handle = HANDLE;
const Handle kNone = INVALID_HANDLE_VALUE;
int processId() { return int(GetCurrentProcessId()); }

Handle openIpc() {
  if (const char* only = testPath()) {
    return CreateFileA(only, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  }
  for (int i = 0; i < 10; i++) {
    char name[64];
    std::snprintf(name, sizeof name, R"(\\.\pipe\discord-ipc-%d)", i);
    HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      return h;
    }
  }
  return kNone;
}

void closeIpc(Handle h) { CloseHandle(h); }

bool writeAll(Handle h, const void* data, size_t size) {
  const char* p = static_cast<const char*>(data);
  while (size > 0) {
    DWORD wrote = 0;
    if (!WriteFile(h, p, DWORD(size), &wrote, nullptr) || wrote == 0) {
      return false;
    }
    p += wrote;
    size -= wrote;
  }
  return true;
}

// what has arrived, without waiting; false when the pipe is closed
bool readSome(Handle h, std::string& into) {
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr)) {
      return false;
    }
    if (available == 0) {
      return true;
    }
    char buf[4096];
    DWORD got = 0;
    if (!ReadFile(h, buf, available < sizeof buf ? available : DWORD(sizeof buf), &got, nullptr)) {
      return false;
    }
    into.append(buf, got);
  }
}

#else
using Handle = int;
const Handle kNone = -1;
int processId() { return int(getpid()); }

Handle tryConnect(const std::string& path) {
  sockaddr_un addr{};
  if (path.size() >= sizeof addr.sun_path) {
    return kNone;
  }
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return kNone;
  }
#ifdef SO_NOSIGPIPE
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    close(fd);
    return kNone;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  return fd;
}

Handle openIpc() {
  if (const char* only = testPath()) {
    return tryConnect(only);
  }
  std::string bases[3];
  int n = 0;
#ifdef __APPLE__
  const char* names[] = {"TMPDIR"};
  const char* subs[] = {""};
#else
  const char* names[] = {"XDG_RUNTIME_DIR", "TMPDIR"};
  const char* subs[] = {"", "/snap.discord", "/app/com.discordapp.Discord"};
#endif
  for (const char* name : names) {
    const char* v = std::getenv(name);
    if (v != nullptr && v[0] != '\0') {
      bases[n] = v;
      while (bases[n].size() > 1 && bases[n].back() == '/') {
        bases[n].pop_back();
      }
      n++;
    }
  }
  bases[n++] = "/tmp";
  for (const char* sub : subs) {
    for (int b = 0; b < n; b++) {
      for (int i = 0; i < 10; i++) {
        const Handle h = tryConnect(bases[b] + sub + "/discord-ipc-" + std::to_string(i));
        if (h != kNone) {
          return h;
        }
      }
    }
  }
  return kNone;
}

void closeIpc(Handle h) { close(h); }

bool writeAll(Handle h, const void* data, size_t size) {
  const char* p = static_cast<const char*>(data);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (size > 0) {
#ifdef MSG_NOSIGNAL
    const ssize_t n = send(h, p, size, MSG_NOSIGNAL);
#else
    const ssize_t n = send(h, p, size, 0);
#endif
    if (n > 0) {
      p += n;
      size -= size_t(n);
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) &&
               std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } else {
      return false;
    }
  }
  return true;
}

bool readSome(Handle h, std::string& into) {
  for (;;) {
    char buf[4096];
    const ssize_t n = recv(h, buf, sizeof buf, 0);
    if (n > 0) {
      into.append(buf, size_t(n));
    } else if (n == 0) {
      return false; // Discord closed it
    } else {
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
  }
}
#endif

bool sendFrame(Handle h, uint32_t op, const std::string& json) {
  const uint32_t len = uint32_t(json.size());
  unsigned char header[8];
  for (int i = 0; i < 4; i++) {
    header[i] = (unsigned char)(op >> (8 * i));
    header[4 + i] = (unsigned char)(len >> (8 * i));
  }
  return writeAll(h, header, sizeof header) && writeAll(h, json.data(), json.size());
}

// Discord's replies: kept only for READY, a close and a ping; the rest read and dropped
struct Replies {
  std::string buf;
  bool ready = false;
  bool closed = false;
  std::string reason; // its close's JSON
};

// false when the connection is over (closed, or Discord said so); what came before is still read
bool pump(Handle h, Replies& r) {
  const bool open = readSome(h, r.buf);
  while (r.buf.size() >= 8) {
    const auto* b = reinterpret_cast<const unsigned char*>(r.buf.data());
    const uint32_t op = b[0] | (b[1] << 8) | (b[2] << 16) | (uint32_t(b[3]) << 24);
    const uint32_t len = b[4] | (b[5] << 8) | (b[6] << 16) | (uint32_t(b[7]) << 24);
    if (len > (1u << 20)) {
      return false; // not Discord
    }
    if (r.buf.size() < 8 + size_t(len)) {
      break;
    }
    const std::string body = r.buf.substr(8, len);
    r.buf.erase(0, 8 + size_t(len));
    if (op == 1 && body.find("\"READY\"") != std::string::npos) {
      r.ready = true;
    } else if (op == 2) {
      r.closed = true; // a bad client ID, say
      r.reason = body;
      return false;
    } else if (op == 3 && !sendFrame(h, 4, body)) {
      return false;
    }
  }
  return open;
}

std::mutex sMutex;
std::condition_variable sWake;
std::string sWanted; // the activity to show ("null": none), set by the game's side

void worker() {
  using Clock = std::chrono::steady_clock;
  Handle h = kNone;
  Replies replies;
  Clock::time_point lastTry{}, connectedAt{}, lastSent{};
  std::string sent; // what Discord shows now, as sent on this connection
  std::string lastReason;
  unsigned nonce = 0;
  bool haveLastTry = false, haveSent = false;
  for (;;) {
    std::string wanted;
    {
      std::unique_lock<std::mutex> lock(sMutex);
      sWake.wait_for(lock, std::chrono::milliseconds(500));
      wanted = sWanted;
    }
    const auto now = Clock::now();
    if (wanted.empty() || (wanted == "null" && h == kNone)) {
      continue; // nothing to show yet, or nothing to clear
    }
    if (h == kNone) {
      if (haveLastTry && now - lastTry < kRetry) {
        continue;
      }
      haveLastTry = true;
      lastTry = now;
      h = openIpc();
      if (h == kNone) {
        continue;
      }
      replies = Replies{};
      std::string hello = "{\"v\":1,\"client_id\":";
      appendJson(hello, kAppId);
      hello += '}';
      if (!sendFrame(h, 0, hello)) {
        closeIpc(h);
        h = kNone;
        continue;
      }
      connectedAt = now;
      haveSent = false;
      sent.clear();
    }
    const bool wasReady = replies.ready;
    const bool open = pump(h, replies);
    if (replies.ready && !wasReady) {
      std::printf("[discord] connected\n");
      std::fflush(stdout);
    }
    if (!open) {
      // said once, not at every retry (Discord turning a wrong application ID away, say)
      if (replies.closed && replies.reason != lastReason) {
        lastReason = replies.reason;
        std::printf("[discord] closed by Discord: %s\n", replies.reason.c_str());
      } else if (replies.ready) {
        std::printf("[discord] disconnected\n");
      }
      std::fflush(stdout);
      closeIpc(h);
      h = kNone;
      lastTry = now; // and again in 15 s
      continue;
    }
    if (!replies.ready && now - connectedAt < kReadyWait) {
      continue; // its READY first (a little while, then on regardless)
    }
    if (wanted == sent || (haveSent && now - lastSent < kMinGap)) {
      continue;
    }
    std::string cmd = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(processId()) +
                      ",\"activity\":" + wanted + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
    if (!sendFrame(h, 1, cmd)) {
      closeIpc(h);
      h = kNone;
      lastTry = now;
      continue;
    }
    sent = wanted;
    lastSent = now;
    haveSent = true;
  }
}

bool sThreadStarted;

// each presented frame: what Discord should show, given to the thread when it changes
void tick(int pageOpen) {
  (void)pageOpen;
  static std::string last;
  std::string now = currentActivity();
  if (now == last) {
    return;
  }
  last = now;
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sWanted = now;
  }
  if (!sThreadStarted && !now.empty() && now != "null") {
    sThreadStarted = true;
    std::thread(worker).detach();
  }
  sWake.notify_one();
}

int shown() { return kAppId[0] != '\0'; }

const bool sAdded = [] {
  sOn = portSettingsDeclare("discord_status", 1, 0, 1);
  portSettingsAddToggle("General", "Discord status",
                        "While Discord runs on this computer, your profile shows that you're playing Paper Cut: "
                        "the area you're in, and whether you're in battle, exploring or playing co-op.",
                        0, "discord_status", "discord status rich presence profile activity playing");
  portSettingsSetShown("discord_status", shown);
  if (shown()) {
    portSettingsAddOverlay(tick, nullptr);
  }
  return true;
}();

} // namespace

#endif // PORT_DISCORD
