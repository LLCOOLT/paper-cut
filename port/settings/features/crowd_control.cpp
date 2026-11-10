// Crowd Control in plain chat (port/include/port/crowd.h): the chat of a
// Twitch channel the player names, read anonymously (Twitch's IRC over a
// WebSocket, as a "justinfan" guest: no account, no partner program), and
// viewers' commands drive the battle audience:
//   !gift !rock   someone in the stands throws a good item, or a rock or a can
//   !star !drain  a little Star Power, or a little taken away
//   !bingo        the Bingo cards filled: the spin comes at the next turn
//   !cheer        the crowd cheers and jumps
//   !leave !join  a few of the crowd leave, or a few more come
// Off by default; one command per cooldown, each viewer one per three
// cooldowns; only in battles. The channel is in <user>/crowd_control.txt.
// $TTYD_CROWD_TEST=1: no chat, a viewer "test" sends each command in turn
// (once per cooldown), to see them work.
#include "port/crowd.h"
#include "port/mod_host.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../../archipelago/websocket.h"
#include <SDL3/SDL.h>
#include "../file_util.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace {

constexpr const char* kTab = "Streaming";
constexpr const char* kNames[PORT_CROWD_COUNT] = {"gift", "rock", "star", "drain", "bingo", "cheer", "leave", "join"};
constexpr const char* kSays[PORT_CROWD_COUNT] = {"a gift from the stands", "a rock from the stands",
                                                 "a little Star Power",    "Star Power drained",
                                                 "Bingo!",                 "the crowd cheers",
                                                 "some of the crowd leave", "more of the crowd come"};
constexpr bool kHarmful[PORT_CROWD_COUNT] = {false, true, false, true, false, false, true, false};

int* sOn;       // crowd_control
int* sCooldown; // crowd_cooldown, seconds
int* sHarmful;  // crowd_harmful: !rock, !drain and !leave too

using Clock = std::chrono::steady_clock;

// What the chat thread and the frame share, below. Left allocated at exit on purpose: a chat
// thread still in a connect() (being waited for off the frame's thread) may use them while
// the statics are destroyed.
std::mutex& sLock = *new std::mutex;
std::string& sStatus = *new std::string("Off.");
unsigned sStatusGen; // (under sLock) the chat whose status shows: an old one's late words are dropped
struct Notice {
  Clock::time_point at;
  std::string text;
};
std::deque<Notice>& sNotices = *new std::deque<Notice>; // the last commands taken, for the picture and the page
Clock::time_point sLastCommand;   // the last command taken (the cooldown)
std::map<std::string, Clock::time_point>& sLastBy = *new std::map<std::string, Clock::time_point>; // each viewer's last
std::atomic<bool> sInBattle;      // a battle with an audience is on (the frame says)

std::string channelFile() { return std::string(portSettingsUserPath()) + "/crowd_control.txt"; }

std::string readChannel() {
  if (const char* env = std::getenv("TTYD_CROWD_CHANNEL")) {
    return env;
  }
  size_t size = 0;
  void* data = SDL_LoadFile(channelFile().c_str(), &size);
  std::string text = data != nullptr ? std::string(static_cast<char*>(data), size) : std::string();
  SDL_free(data);
  std::string out;
  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      out += char(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

// The status line, from chat gen (only the newest chat's shows)
void setStatus(unsigned gen, const std::string& text) {
  std::lock_guard lock(sLock);
  if (gen == sStatusGen) {
    sStatus = text;
  }
}

// A chat line from viewer: a command, if it is one and its time has come
// (true: taken).
bool heard(const std::string& viewer, const std::string& text) {
  size_t b = text.find_first_not_of(' ');
  if (b == std::string::npos || text[b] != '!') {
    return false;
  }
  std::string word;
  for (size_t i = b + 1; i < text.size() && std::isalpha(static_cast<unsigned char>(text[i])); i++) {
    word += char(std::tolower(static_cast<unsigned char>(text[i])));
  }
  int command = -1;
  for (int c = 0; c < PORT_CROWD_COUNT; c++) {
    if (word == kNames[c]) {
      command = c;
    }
  }
  if (command >= 0 && kHarmful[command] && !*sHarmful && sInBattle) {
    static bool sTold[PORT_CROWD_COUNT]; // once for each command
    if (!sTold[command]) {
      sTold[command] = true;
      std::printf("[crowd] !%s refused: the harmful ones aren't allowed\n", kNames[command]);
    }
  }
  if (command < 0 || (kHarmful[command] && !*sHarmful) || !sInBattle) {
    return false;
  }
  const auto now = Clock::now();
  const auto cooldown = std::chrono::seconds(*sCooldown);
  std::lock_guard lock(sLock);
  if (now - sLastCommand < cooldown) {
    return false;
  }
  if (const auto it = sLastBy.find(viewer); it != sLastBy.end() && now - it->second < cooldown * 3) {
    return false;
  }
  sLastCommand = now;
  sLastBy[viewer] = now;
  portCrowdPush(command);
  std::printf("[crowd] heard !%s from %s (cooldown %d s, harmful %d)\n", kNames[command], viewer.c_str(), *sCooldown,
              *sHarmful);
  sNotices.push_back({now, viewer + ": " + kSays[command]});
  while (sNotices.size() > 8) {
    sNotices.pop_front();
  }
  return true;
}

// The chat connection: its own thread, reconnecting until stopped.
class Chat {
public:
  void start(const std::string& channel) {
    mChannel = channel;
    mStop = false;
    {
      std::lock_guard lock(sLock);
      mGen = ++sStatusGen; // this chat's status shows from now on
    }
#ifdef __EMSCRIPTEN__ // (one thread: nothing to read the chat with while the game runs)
    say(portTr("Reading a chat isn't in the browser yet."));
    return;
#endif
    mThread = std::thread([this] { run(); });
  }
  // Told to stop (its receive woken), without waiting for its thread. interrupt(), not close(): a close
  // here would wait on the server's close (WinHTTP) on the frame's thread, and free what the thread's
  // receive still uses (a handle, an fd number the next socket may get); the thread's socket closes
  // itself as it goes
  void requestStop() {
    mStop = true;
    std::lock_guard lock(mSocketLock);
    if (mSocket) {
      mSocket->interrupt();
    }
  }
  // Stopped, and its thread waited for off the frame's thread: one still in a
  // connect() can take until that connect's timeout to see the stop, and the
  // game would freeze meanwhile (turned off, or the channel changed, while connecting).
  // Its late status words are dropped (a newer chat's generation shows).
  static void stopAndForget(Chat* chat) {
    chat->requestStop();
    {
      std::lock_guard lock(sLock);
      if (chat->mGen == sStatusGen) { // no newer chat yet: it shows as off
        sStatusGen++;
        sStatus = "Off."; // (shown translated by the page)
      }
    }
    if (!chat->mThread.joinable()) {
      delete chat;
      return;
    }
    std::thread([chat] {
      chat->mThread.join();
      delete chat;
    }).detach();
  }
  const std::string& channel() const { return mChannel; }

private:
  void run() {
    if (std::getenv("TTYD_CROWD_TEST") != nullptr) {
      test();
      return;
    }
    std::mt19937 rng(std::random_device{}());
    while (!mStop) {
      auto socket = port_ap::WebSocket::create();
      {
        std::lock_guard lock(mSocketLock);
        mSocket = socket.get();
      }
      const std::string url = port_ap::WebSocket::secureSupported() ? "wss://irc-ws.chat.twitch.tv:443"
                                                                   : "ws://irc-ws.chat.twitch.tv:80";
      say(portTrf("Connecting to #%s...", mChannel.c_str()));
      std::string error;
      if (socket->connect(url, error)) {
        // a guest: Twitch lets anyone read a channel's chat as justinfan<number>
        socket->send("PASS SCHMOOPIIE");
        socket->send("NICK justinfan" + std::to_string(10000 + rng() % 89999));
        socket->send("JOIN #" + mChannel);
        std::string message;
        while (!mStop && socket->receive(message)) {
          read(*socket, message);
        }
        error = mStop ? "" : "the connection closed";
      }
      {
        std::lock_guard lock(mSocketLock);
        mSocket = nullptr;
      }
      if (mStop) {
        break;
      }
      say(portTrf("Not connected (%s); trying again in 10 seconds.", error.c_str()));
      for (int i = 0; i < 100 && !mStop; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  // One WebSocket message: IRC lines.
  void read(port_ap::WebSocket& socket, const std::string& message) {
    size_t at = 0;
    while (at < message.size()) {
      size_t end = message.find("\r\n", at);
      if (end == std::string::npos) {
        end = message.size();
      }
      const std::string line = message.substr(at, end - at);
      at = end + 2;
      if (line.rfind("PING", 0) == 0) {
        socket.send("PONG" + line.substr(4));
      } else if (line.find(" 366 ") != std::string::npos) { // end of the names list: joined
        say(portTrf("Reading the chat of #%s.", mChannel.c_str()));
      } else if (line.size() > 1 && line[0] == ':') {
        // ":viewer!viewer@viewer.tmi.twitch.tv PRIVMSG #channel :text"
        const size_t bang = line.find('!');
        const size_t privmsg = line.find(" PRIVMSG ");
        if (bang != std::string::npos && privmsg != std::string::npos) {
          const size_t text = line.find(" :", privmsg);
          if (text != std::string::npos) {
            heard(line.substr(1, bang - 1), line.substr(text + 2));
          }
        }
      }
    }
  }

  void test() {
    say(portTr("Test: a viewer named test sends each command in turn."));
    int next = 0;
    while (!mStop) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      {
        std::lock_guard lock(sLock);
        sLastBy.clear(); // the one test viewer: once per cooldown, not three
      }
      if (heard("test", std::string("!") + kNames[next % PORT_CROWD_COUNT])) {
        next++;
      }
    }
  }

  void say(const std::string& text) { setStatus(mGen, text); }

  std::string mChannel;
  unsigned mGen = 0;
  std::atomic<bool> mStop{false};
  std::thread mThread;
  std::mutex mSocketLock;
  port_ap::WebSocket* mSocket = nullptr;
};

// The chat: never destroyed at exit (its thread may still be in a connect(); a joinable
// std::thread destroyed would end the program)
Chat* sChat;

// The channel as last saved (under sLock): read from the file once, then changed by the page
// when its field is left or Enter pressed (not on each letter: each change restarts the chat)
std::string& sChannel = *new std::string;
bool sChannelKnown;

std::string currentChannel() {
  std::lock_guard lock(sLock);
  if (!sChannelKnown) {
    sChannel = readChannel();
    sChannelKnown = true;
  }
  return sChannel;
}

// Each frame of play: the connection as the settings say, whether a battle
// is on, and the commands taken lately over the picture.
void frame(void*) {
  static unsigned sFrames;
  static Clock::time_point sFramesAt;
  const auto now = Clock::now();
  const unsigned frames = portCrowdBattleFrames();
  if (frames != sFrames) {
    sFrames = frames;
    sFramesAt = now;
  }
  sInBattle = now - sFramesAt < std::chrono::milliseconds(500);

  const bool test = std::getenv("TTYD_CROWD_TEST") != nullptr;
  const std::string channel = *sOn ? currentChannel() : std::string();
  const bool want = *sOn && (!channel.empty() || test);
  if (sChat && (!want || sChat->channel() != channel)) {
    Chat::stopAndForget(sChat);
    sChat = nullptr;
  }
  if (want && !sChat) {
    sChat = new Chat();
    sChat->start(channel);
  }
  if (!*sOn) {
    return;
  }
  std::lock_guard lock(sLock);
  float y = 330.0f; // above the audience counter
  for (const Notice& n : sNotices) {
    const float age = std::chrono::duration<float>(now - n.at).count();
    if (age > 6.0f) {
      continue;
    }
    const unsigned alpha = unsigned(255 * std::min(1.0f, (6.0f - age) / 1.0f));
    portModHostDrawRect(24.0f, y - 2.0f, 8.0f + 7.5f * float(n.text.size()), 20.0f, 0x00000000u | (alpha * 160 / 255));
    portModHostDrawText(28.0f, y, 16.0f, 0xFFFFFF00u | alpha, n.text.c_str());
    y += 22.0f;
  }
}

void channelSection(int inGame) {
  static char sEdit[64];
  static bool sLoaded;
  if (!sLoaded) {
    std::snprintf(sEdit, sizeof(sEdit), "%s", currentChannel().c_str());
    sLoaded = true;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::InputText(portTrLabel("Twitch channel"), sEdit, sizeof(sEdit));
  // saved, and the chat changed, once the field is left or Enter pressed: not a restart and a
  // file write for each letter typed
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    portfile::writeAll(channelFile(), sEdit, std::strlen(sEdit), portfile::kNoSync);
    std::string typed = sEdit;
    std::string clean;
    for (char c : typed) { // as readChannel takes it
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
        clean += char(std::tolower(static_cast<unsigned char>(c)));
      }
    }
    std::lock_guard lock(sLock);
    if (std::getenv("TTYD_CROWD_CHANNEL") == nullptr) {
      sChannel = clean;
      sChannelKnown = true;
    }
  }
  ImGui::TextDisabled("%s", portTr("The channel's name, as in twitch.tv/<name>. Its chat is read as a guest: no account needed."));
  std::lock_guard lock(sLock);
  if (*sOn) {
    ImGui::TextWrapped("%s", inGame ? (sStatus == "Off." ? portTr("Off.") : sStatus.c_str()) : portTr("Connects when the game runs."));
  }
  ImGui::TextDisabled("%s", portTr("Viewers type: !gift !rock !star !drain !bingo !cheer !leave !join. In battles only."));
  if (!sNotices.empty()) {
    ImGui::TextUnformatted(portTr("Lately:"));
    for (auto it = sNotices.rbegin(); it != sNotices.rend(); ++it) {
      ImGui::BulletText("%s", it->text.c_str());
    }
  }
}

struct Register {
  Register() {
    sOn = portSettingsDeclare("crowd_control", 0, 0, 1);
    sCooldown = portSettingsDeclare("crowd_cooldown", 20, 5, 120);
    sHarmful = portSettingsDeclare("crowd_harmful", 1, 0, 1);
    portSettingsAddToggle(kTab, "Twitch chat drives the audience",
                          "Viewers' commands in your channel's chat make the crowd throw items, give or take a "
                          "little Star Power, fill the Bingo cards, cheer, leave or come.",
                          PORT_SETTING_CHANGES_PLAY, "crowd_control", "crowd control twitch chat stream viewers");
    portSettingsAddSlider(kTab, "Cooldown", "Seconds between commands (three times that per viewer).",
                          0, "crowd_cooldown", 5, 120, "%d s", "crowd control rate limit");
    portSettingsAddToggle(kTab, "Allow the harmful ones", "!rock, !drain and !leave.", PORT_SETTING_CHANGES_PLAY,
                          "crowd_harmful", "crowd control rocks drain");
    portSettingsAddSection(kTab, "Chat", nullptr, 0, channelSection, "crowd control twitch channel chat");
    portModHostAddDraw(frame, nullptr);
  }
} sRegister;

} // namespace
