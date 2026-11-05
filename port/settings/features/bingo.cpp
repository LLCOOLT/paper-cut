// The Modes tab's Bingo (port/include/port/party.h, docs/MODES.md): the card
// and its seed, the tracker over the picture, and lockout through a bingo
// room on the relay (port/relay/src/index.js, "/bingo/<ROOM>").
//
// Lockout: everyone in a room plays the same card (the room keeps the seed
// the first player brought) on their own game; a square done is claimed from
// the room, and the room gives it to whoever claimed it first, so every
// player sees the same owners whatever the network does. Only squares a
// player owns count for their lines. The room is asked over HTTPS every few
// seconds while it's open (and at once when a square is done): nothing goes
// through the game, so online co-op's lockstep is untouched. In an online
// co-op session only the host's game claims (both games see the same squares
// done).
#include "port/http.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/party.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kTab = "Modes";
constexpr int kPollMs = 5000;

char sSeedInput[PORT_BINGO_SEED_MAX + 1] = "";
std::string sMessage;

// --- the room ---------------------------------------------------------------

struct RoomState {
  std::string seed;               // the room's card
  std::vector<std::string> names; // its players, in the order they came
  int you = -1;                   // this player's place in names
  int owner[PORT_BINGO_CELLS];    // -1 nobody, else a place in names
};

std::mutex sLock;
char sRoom[16] = "";  // the room's code ("" none)
char sName[32] = "";  // this player's name in it
bool sInRoom = false; // joined (the main thread's)
std::string sRoomError;
RoomState sGot;            // the last answer
std::atomic<int> sGotCount{0};
std::atomic<bool> sBusy{false};
Uint64 sNextAsk = 0;
int sAskedChanges = -1;
int sSeenCount = 0;

std::string relayBase() {
  for (int i = 0; portOnlineRelayAt(i) != nullptr; i++) {
    const std::string relay = portOnlineRelayAt(i);
    if (relay.compare(0, 6, "wss://") == 0) {
      return "https://" + relay.substr(6);
    }
  }
  return "";
}

std::string percent(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
        c == '.') {
      out += static_cast<char>(c);
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

std::string unpercent(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

// the answer: "bingo <SEED>", "you <i>", "player <i> <name>", "claim <cell> <i>" lines
bool parse(const std::string& text, RoomState& out) {
  out = RoomState{};
  for (int& o : out.owner) {
    o = -1;
  }
  bool ok = false;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    char word[16] = "", a[64] = "", b[96] = "";
    const int n = std::sscanf(line.c_str(), "%15s %63s %95s", word, a, b);
    if (n >= 2 && std::strcmp(word, "bingo") == 0) {
      out.seed = a;
      ok = true;
    } else if (n >= 2 && std::strcmp(word, "you") == 0) {
      out.you = std::atoi(a);
    } else if (n >= 2 && std::strcmp(word, "player") == 0) {
      const int i = std::atoi(a);
      if (i >= 0 && i < 16) {
        if (static_cast<int>(out.names.size()) <= i) {
          out.names.resize(i + 1);
        }
        out.names[i] = n >= 3 ? unpercent(b) : "?";
      }
    } else if (n >= 3 && std::strcmp(word, "claim") == 0) {
      const int cell = std::atoi(a);
      if (cell >= 0 && cell < PORT_BINGO_CELLS) {
        out.owner[cell] = std::atoi(b);
      }
    }
  }
  return ok;
}

void ask(std::string url) {
  std::vector<uint8_t> body;
  std::string error;
  RoomState got;
  const bool ok = port_http::fetch(url, body, 64 * 1024, error) &&
                  parse(std::string(body.begin(), body.end()), got);
  {
    std::lock_guard<std::mutex> lock(sLock);
    if (ok) {
      sGot = got;
      sRoomError.clear();
    } else {
      sRoomError = error.empty() ? "The room's answer wasn't understood." : error;
    }
  }
  if (ok) {
    sGotCount++;
  }
  sBusy = false;
}

bool claiming() {
  return !portOnlineActive() || portOnlineIsHost();
}

// asks the room now: joining, a poll, and the squares done here that nobody has yet
void askRoom(bool force) {
  if (!sInRoom || sBusy) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  const int changes = portBingoChanges();
  if (!force && now < sNextAsk && changes == sAskedChanges) {
    return;
  }
  const std::string base = relayBase();
  if (base.empty()) {
    sRoomError = "This build has no relay.";
    return;
  }
  std::string url = base + "/bingo/" + sRoom + "?name=" + percent(sName) + "&seed=" + percent(portBingoSeed());
  if (claiming()) {
    std::string claims;
    std::lock_guard<std::mutex> lock(sLock);
    for (int i = 0; i < PORT_BINGO_CELLS; i++) {
      if (portBingoDone(i) && sGot.owner[i] < 0) {
        claims += (claims.empty() ? "" : ",") + std::to_string(i);
      }
    }
    if (!claims.empty()) {
      url += "&claim=" + claims;
    }
  }
  sNextAsk = now + kPollMs;
  sAskedChanges = changes;
  sBusy = true;
  std::thread(ask, url).detach();
}

// the room's answer put in place: its card (a different seed: the room's, marks cleared) and the owners
void takeAnswer() {
  const int count = sGotCount.load();
  if (count == sSeenCount) {
    return;
  }
  sSeenCount = count;
  RoomState got;
  {
    std::lock_guard<std::mutex> lock(sLock);
    got = sGot;
  }
  if (!got.seed.empty() && got.seed != portBingoSeed()) {
    portBingoNew(got.seed.c_str());
    std::snprintf(sSeedInput, sizeof(sSeedInput), "%s", got.seed.c_str());
    sMessage = portTr("The room's card is in play.");
    sNextAsk = 0; // its squares claimed at once
  }
  for (int i = 0; i < PORT_BINGO_CELLS; i++) {
    portBingoSetOwner(i, got.owner[i] < 0 ? 0 : got.owner[i] == got.you ? 1 : 2);
  }
}

void joinRoom() {
  int n = 0;
  for (char* p = sRoom; *p != '\0'; p++) {
    const char c = *p >= 'a' && *p <= 'z' ? static_cast<char>(*p - 32) : *p;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
      sRoom[n++] = c;
    }
  }
  sRoom[n] = '\0';
  if (n < 4) {
    sMessage = portTr("A room's code is 4 to 8 letters or digits.");
    return;
  }
  if (sName[0] == '\0') {
    std::snprintf(sName, sizeof(sName), "Player");
  }
  if (portBingoSeed()[0] == '\0') {
    portBingoNew(sSeedInput); // the room takes the first player's card
  }
  sInRoom = true;
  sSeenCount = sGotCount.load();
  {
    std::lock_guard<std::mutex> lock(sLock);
    sGot = RoomState{};
    for (int& o : sGot.owner) {
      o = -1;
    }
    sRoomError.clear();
  }
  portBingoSetLockout(1);
  askRoom(true);
  sMessage.clear();
}

void leaveRoom() {
  sInRoom = false;
  portBingoSetLockout(0);
}

// --- the page -----------------------------------------------------------------

std::string clock(unsigned long long ms) {
  char text[32];
  std::snprintf(text, sizeof(text), "%llu:%02llu:%02llu", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60);
  return text;
}

void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

ImU32 cellColour(int i) {
  const int owner = portBingoOwner(i);
  if (owner == 2) {
    return IM_COL32(150, 50, 50, 255);
  }
  if (portBingoDone(i) && (!portBingoLockout() || owner == 1)) {
    return IM_COL32(40, 130, 60, 255);
  }
  if (portBingoDone(i)) {
    return IM_COL32(110, 110, 40, 255); // done here, the room hasn't said yet
  }
  return IM_COL32(45, 45, 60, 255);
}

void cardTable() {
  const float w = (ImGui::GetContentRegionAvail().x - 8.0f * 4.0f) / 5.0f;
  const float h = ImGui::GetTextLineHeight() * 4.2f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < PORT_BINGO_CELLS; i++) {
    if (i % 5 != 0) {
      ImGui::SameLine(0.0f, 8.0f);
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), cellColour(i), 6.0f);
    char text[160];
    portBingoCellText(i, 0, text, sizeof(text));
    ImGui::PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 6.0f, p.y + 4.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 12.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(p);
    char id[16];
    std::snprintf(id, sizeof(id), "##bingo%d", i);
    ImGui::Dummy(ImVec2(w, h));
    (void)id;
  }
}

void section(int inGame) {
  (void)inGame;
  takeAnswer();
  askRoom(false);
  ImGui::TextWrapped("%s", portTr("A 5x5 card of goals from a seed: items, partners, Crystal Stars, levels, logs and "
                                  "more, each marked by itself as this file reaches it. Five in a row, a column or a "
                                  "diagonal is a bingo. Share the seed and race a friend, or play lockout in a room: "
                                  "a square is the first claimer's."));
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  ImGui::InputTextWithHint(portTrLabel("##bingoseed"), portTr("Seed"), sSeedInput, sizeof(sSeedInput));
  ImGui::SameLine();
  ImGui::BeginDisabled(sInRoom);
  if (ImGui::Button(portTrLabel("New card##bingo"))) {
    portBingoNew(sSeedInput);
    std::snprintf(sSeedInput, sizeof(sSeedInput), "%s", portBingoSeed());
    sMessage.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("No card##bingo"))) {
    portBingoClear();
  }
  ImGui::EndDisabled();
  if (portBingoSeed()[0] == '\0') {
    hint(portTr("No card in play. An empty seed makes a random one."));
  } else {
    ImGui::Text(portTr("Card %s: %d squares, %d lines, %s"), portBingoSeed(), portBingoCount(), portBingoLines(),
                clock(portBingoElapsed()).c_str());
    if (portBingoFirstLine() != 0) {
      ImGui::SameLine();
      ImGui::Text(portTr("(first bingo at %s)"), clock(portBingoFirstLine()).c_str());
    }
    cardTable();
  }
  if (int* tracker = portSettingsFind("bingo_tracker")) {
    bool on = *tracker != 0;
    if (ImGui::Checkbox(portTrLabel("The tracker over the picture##bingo"), &on)) {
      *tracker = on ? 1 : 0;
      portSettingsSave();
    }
  }

  ImGui::SeparatorText(portTr("Lockout room"));
  if (!sInRoom) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::InputTextWithHint(portTrLabel("##bingoroom"), portTr("Room code"), sRoom, sizeof(sRoom),
                             ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::InputTextWithHint(portTrLabel("##bingoname"), portTr("Your name"), sName, sizeof(sName));
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Join##bingoroom"))) {
      joinRoom();
    }
    hint(portTr("Everyone types the same code (any 4 to 8 letters or digits). The first one in brings the card; the "
                "others get it when they join."));
  } else {
    std::lock_guard<std::mutex> lock(sLock);
    ImGui::Text(portTr("Room %s"), sRoom);
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Leave##bingoroom"))) {
      sInRoom = false;
    }
    for (size_t i = 0; i < sGot.names.size(); i++) {
      int owned = 0;
      for (int c = 0; c < PORT_BINGO_CELLS; c++) {
        owned += sGot.owner[c] == static_cast<int>(i);
      }
      ImGui::BulletText(portTr("%s: %d squares%s"), sGot.names[i].c_str(), owned,
                        static_cast<int>(i) == sGot.you ? portTr(" (you)") : "");
    }
    if (!sRoomError.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", portTr(sRoomError.c_str()));
    }
    if (!claiming()) {
      hint(portTr("In online co-op the host's game claims the squares for you both."));
    }
  }
  if (!sInRoom && portBingoLockout()) {
    leaveRoom();
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// the tracker: a small card at the bottom left, a square lit as it's done
void frame(void*) {
  static int sSeenChanges;
  static Uint64 sFlashUntil;
  takeAnswer();
  askRoom(false);
  const int* tracker = portSettingsFind("bingo_tracker");
  if (portBingoSeed()[0] == '\0' || tracker == nullptr || *tracker == 0) {
    return;
  }
  if (portBingoChanges() != sSeenChanges) {
    sSeenChanges = portBingoChanges();
    sFlashUntil = SDL_GetTicks() + 1500;
  }
  const float cell = 9.0f, gap = 2.0f;
  const float x0 = 8.0f, y0 = 480.0f - 8.0f - 5.0f * (cell + gap) - 16.0f;
  portModHostDrawRect(x0 - 4.0f, y0 - 4.0f, 5.0f * (cell + gap) + 6.0f + 70.0f, 5.0f * (cell + gap) + 24.0f,
                      0x00000090u);
  const bool flash = SDL_GetTicks() < sFlashUntil && (SDL_GetTicks() / 150) % 2 == 0;
  for (int i = 0; i < PORT_BINGO_CELLS; i++) {
    const int owner = portBingoOwner(i);
    unsigned rgba = 0x50506090u;
    if (owner == 2) {
      rgba = 0xD04040E0u;
    } else if (portBingoDone(i) && (!portBingoLockout() || owner == 1)) {
      rgba = flash ? 0xFFFFFFFFu : 0x40D060F0u;
    } else if (portBingoDone(i)) {
      rgba = 0xC0C040E0u;
    }
    portModHostDrawRect(x0 + (i % 5) * (cell + gap), y0 + (i / 5) * (cell + gap), cell, cell, rgba);
  }
  char line[64];
  std::snprintf(line, sizeof(line), portTr("Bingo %d"), portBingoLines());
  portModHostDrawText(x0 + 5.0f * (cell + gap) + 6.0f, y0, 13.0f, 0xFFFFFFFFu, line);
  std::snprintf(line, sizeof(line), "%d/25", portBingoCount());
  portModHostDrawText(x0 + 5.0f * (cell + gap) + 6.0f, y0 + 16.0f, 12.0f, 0xC0C0C0FFu, line);
  portModHostDrawText(x0, y0 + 5.0f * (cell + gap) + 2.0f, 11.0f, 0xC0C0C0FFu, portBingoSeed());
}

// --- the Start menu: the card as lines ------------------------------------------

int infoLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0;
  if (portBingoSeed()[0] == '\0') {
    if (max > 0) {
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", portTrGame("No card in play: make one on the Modes page."));
    }
    return n;
  }
  for (int i = 0; i < PORT_BINGO_CELLS && n < max; i++) {
    char text[128];
    portBingoCellText(i, 1, text, sizeof(text));
    const int owner = portBingoOwner(i);
    const char* mark = owner == 2 ? "x" : portBingoDone(i) ? "*" : "-";
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%c%d %s %s", 'A' + i / 5, i % 5 + 1, mark, text);
  }
  return n;
}

const PortPauseInfo kInfo = {"Bingo card", infoLines, nullptr, nullptr, nullptr, nullptr, 0};

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 2) {
    return 0;
  }
  // the same row, and the same page opened, whether or not this computer has a card: the Start menu runs in
  // both games of an online session, and the card is each player's own (only what's drawn may differ)
  PortPauseRow r = portPauseRow("Show the bingo card", "Each square: * done, x taken by another player, - not yet.",
                                nullptr, 0);
  if (portBingoSeed()[0] != '\0') {
    std::snprintf(r.value, sizeof(r.value), "%d/25", portBingoCount());
  }
  out[n++] = r;
  return n;
}

const char* formAct(int id, int dir) {
  if (id != 0 || dir != 0) {
    return nullptr;
  }
  portPauseInfoOpen(&kInfo);
  return nullptr;
}

const PortPauseForm kForm = {kTab, "Bingo", formRows, formAct};

struct Register {
  Register() {
    portSettingsDeclare("bingo_tracker", 1, 0, 1);
    portSettingsAddSection(kTab, "Bingo", nullptr, 0, section,
                           "bingo card goals lockout race seed tracker squares room");
    portPauseAddForm(&kForm);
    portModHostAddDraw(frame, nullptr);
  }
} sRegister;

} // namespace
