// Message log (Accessibility > Message log; L on the keyboard): the last 200
// pages of the message windows, who said them when the game says so, to
// scroll back through: newest at the bottom, over the game, which is held
// where it is meanwhile (frame advance's hold, as photo mode). Online the
// other player's game can't be held: the log still opens, the game going on
// under it. The pages come from message_text.cpp, as text to speech's do.
//
//   wheel, Up/Down, PageUp/PageDown, Home/End   scroll
//   stick / d-pad (while held)                  scroll
//   L, Esc / B                                  back to the game
#include "port/bindings.h"
#include "port/online.h"
#include "port/practice.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include "../launcher_style.h"
#include "../message_text.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

extern "C" void portSettingsToggle(void);          // settings_ui.cpp

namespace {

constexpr size_t kKeep = 200;

struct Entry {
  std::string speaker; // UTF-8, empty when the game doesn't say
  std::string text;    // UTF-8
  std::string game;    // the words as the game has them (its encoding: the Start menu's page)
};
std::deque<Entry> sLog;

struct {
  bool open, held, wasHeld, toBottom, fromPage;
  float scroll; // lines to scroll this frame (keys, stick)
  Uint64 last;
} sView;

// who says it: the speaking NPC's name in the message data, when it is one a
// player can read (the game keeps its own, often Japanese, names there: those
// are left out)
std::string speakerOf(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return "";
  }
  for (const char* c = name; *c != '\0'; c++) {
    const unsigned char u = static_cast<unsigned char>(*c);
    if (u < 0x20 || u >= 0x7F) {
      return "";
    }
  }
  return name;
}

void add(const void*, const std::string& words, const char* speaker, bool fresh) {
  if (!fresh) {
    return; // paged back to: already here
  }
  Entry e{speakerOf(speaker), msgtext::utf8(words.c_str()), words};
  if (e.text.empty()) {
    return;
  }
  sLog.push_back(std::move(e));
  while (sLog.size() > kKeep) {
    sLog.pop_front();
  }
  static const bool sPrint = SDL_getenv("TTYD_MESSAGE_LOG_PRINT") != nullptr; // tests: each page on stdout
  if (sPrint) {
    std::printf("[message log] %s%s%s\n", sLog.back().speaker.c_str(), sLog.back().speaker.empty() ? "" : ": ",
                sLog.back().text.c_str());
    std::fflush(stdout);
  }
}

void open() {
  if (sView.open) {
    return;
  }
  sView.open = true;
  sView.toBottom = true;
  sView.scroll = 0.f;
  sView.last = SDL_GetTicksNS();
  // held where it is, unless online (the other player's game would wait)
  sView.held = !portOnlineActive();
  if (sView.held) {
    sView.wasHeld = portFrameAdvanceHeld() != 0;
    portFrameAdvanceHold(1);
  }
}

void close() {
  if (!sView.open) {
    return;
  }
  sView.open = false;
  if (sView.held) {
    portFrameAdvanceHold(sView.wasHeld ? 1 : 0);
    sView.held = false;
  }
}

SDL_Gamepad* pad() { return SDL_GetGamepadFromPlayerIndex(0); }

// Tests: $TTYD_MESSAGE_LOG_TEST=<pages>: once that many pages are in the log,
// the log open 30 presented frames later for 8 seconds (a window capture
// shows it: the game's own pictures leave the overlays out)
void testFrame() {
  static const char* spec = SDL_getenv("TTYD_MESSAGE_LOG_TEST");
  static int frame, at = -1;
  static Uint64 openedAt;
  if (spec == nullptr) {
    return;
  }
  if (at < 0) {
    if (int(sLog.size()) < std::max(1, std::atoi(spec))) {
      return;
    }
    at = 30;
  }
  frame++;
  if (frame == at) {
    open();
    openedAt = SDL_GetTicks();
    std::printf("[message log] test: open at %.1f s, %d pages, %s\n", double(openedAt) / 1000.0, int(sLog.size()),
                sView.held ? "held" : "not held");
    std::fflush(stdout);
  } else if (frame > at && openedAt != 0 && SDL_GetTicks() - openedAt > 8000) {
    openedAt = 0;
    close();
    std::printf("[message log] test: closed\n");
    std::fflush(stdout);
  }
}

void draw(int pageOpen) {
  testFrame();
  if (sView.fromPage && pageOpen) { // the settings page's button: the page closes, the log opens
    sView.fromPage = false;
    portSettingsToggle();
    open();
    return;
  }
  sView.fromPage = false;
  if (!sView.open || pageOpen) {
    return;
  }
  if (sView.held && !portFrameAdvanceHeld()) {
    sView.held = false; // something else let the game go (a save state's load)
  }
  const Uint64 now = SDL_GetTicksNS();
  const float dt = std::min(float(now - sView.last) / 1e9f, 0.1f);
  sView.last = now;
  if (sView.held) { // the controller is the log's while the game is held
    if (SDL_Gamepad* p = pad()) {
      float y = float(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.f;
      y = std::fabs(y) < 0.2f ? 0.f : y;
      y += float(SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) -
           float(SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP));
      sView.scroll += std::clamp(y, -1.f, 1.f) * 14.f * dt;
    }
  }

  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 display = io.DisplaySize;
  const float scale = launcherUiScale(1.f);
  launcherStyleInit(scale);
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(display.x - 32.f, std::max(display.x * 0.7f, 560.f * scale)),
                                  std::min(display.y - 32.f, std::max(display.y * 0.8f, 420.f * scale))),
                           ImGuiCond_Always);
  if (ImGui::Begin("Message log", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing)) {
    const ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    launcherPaperCard(ImGui::GetBackgroundDrawList(), at, ImVec2(at.x + size.x, at.y + size.y), scale);
    ImGui::PushFont(gLauncherFonts.body);
    ImGui::PushFont(gLauncherFonts.heading);
    ImGui::TextUnformatted(portTr("Message log"));
    ImGui::PopFont();
    const float closeWidth = ImGui::CalcTextSize(portTr("Close")).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth);
    bool closing = ImGui::Button(portTrLabel("Close##message_log"));
    ImGui::TextDisabled("%s", portTr(sView.held ? "The game waits while this is open. L, Esc or B: back to the game."
                                                : "Online, the game goes on under it. L or Esc: back to the game."));
    ImGui::Separator();
    ImGui::BeginChild("##message_log_pages", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
    ImGui::SetWindowFontScale(1.25f); // large, readable
    if (sLog.empty()) {
      ImGui::TextDisabled("%s", portTr("Nothing yet: what the message windows say shows here."));
    }
    const float wrap = ImGui::GetContentRegionAvail().x;
    for (const Entry& e : sLog) {
      if (!e.speaker.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(e.speaker.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
      ImGui::TextUnformatted(e.text.c_str());
      ImGui::PopTextWrapPos();
      ImGui::Spacing();
    }
    if (sView.scroll != 0.f) {
      ImGui::SetScrollY(ImGui::GetScrollY() + sView.scroll * ImGui::GetTextLineHeightWithSpacing());
      sView.scroll = 0.f;
    }
    if (sView.toBottom) {
      ImGui::SetScrollHereY(1.f); // newest at the bottom, in view
      sView.toBottom = false;
    }
    ImGui::EndChild();
    ImGui::PopFont();
    if (closing) {
      close();
    }
  }
  ImGui::End();
  launcherStyleEnd();
}

int event(const void* e) {
  const SDL_Event* ev = static_cast<const SDL_Event*>(e);
  if (ev->type == SDL_EVENT_KEY_DOWN) {
    const SDL_Scancode k = ev->key.scancode;
    if (!sView.open) {
      return 0; // (L opens it: the Message log binding, below)
    }
    switch (k) {
    case SDL_SCANCODE_ESCAPE:
      if (!ev->key.repeat) {
        close();
      }
      return 1;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_DOWN:
      if (!sView.held) {
        return 0; // online, the arrows are the game's
      }
      sView.scroll += k == SDL_SCANCODE_UP ? -2.f : 2.f;
      return 1;
    case SDL_SCANCODE_PAGEUP:
    case SDL_SCANCODE_PAGEDOWN:
      sView.scroll += k == SDL_SCANCODE_PAGEUP ? -10.f : 10.f;
      return 1;
    case SDL_SCANCODE_HOME:
      sView.scroll = -1e5f;
      return 1;
    case SDL_SCANCODE_END:
      sView.toBottom = true;
      return 1;
    default:
      return 0;
    }
  }
  if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && sView.open && sView.held &&
      ev->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
    close();
    return 1;
  }
  return 0;
}

void section(int inGame) {
  ImGui::BeginDisabled(!inGame);
  if (ImGui::Button(portTrLabel("Open the message log##message_log"))) {
    sView.fromPage = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled(portTr("%d pages kept"), int(sLog.size()));
}

// The Start menu's form (port/include/port/pause_forms.h): the log read on a page in the game's windows, in its
// own font and encoding, newest at the bottom (the Start menu already holds the game)
int logLines(char (*out)[PORT_PAUSE_LINE], int max) {
  std::vector<std::string> lines;
  for (const Entry& e : sLog) {
    const std::string page = e.speaker.empty() ? e.game : e.speaker + ": " + e.game;
    char wrapped[2048];
    portPauseWrapCols(page.c_str(), wrapped, sizeof(wrapped), 44);
    for (char* line = wrapped; line != nullptr;) {
      char* end = std::strchr(line, '\n');
      if (end != nullptr) {
        *end = '\0';
      }
      lines.emplace_back(line);
      line = end != nullptr ? end + 1 : nullptr;
    }
    lines.emplace_back(); // (a gap between pages)
  }
  if (!lines.empty()) {
    lines.pop_back();
  }
  const size_t from = lines.size() > size_t(max) ? lines.size() - size_t(max) : 0;
  int n = 0;
  for (size_t i = from; i < lines.size(); i++) {
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", lines[i].c_str());
  }
  if (n == 0 && max > 0) {
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", portTrGame("Nothing yet: the message windows' pages show here."));
  }
  return n;
}

const char* logHelp() { return "Up and down scroll, left and right a page at a time. B: back."; }

const PortPauseInfo kLogPage = {"Message log", logLines, nullptr, logHelp, nullptr, nullptr, 1};

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("Read the message log",
                        "The last 200 pages of the message windows, to read again at your own pace: newest at the "
                        "bottom.",
                        nullptr, 0);
  std::snprintf(out[0].value, sizeof(out[0].value), portTrGame("%d pages kept"), int(sLog.size()));
  return 1;
}

const char* formAct(int, int dir) {
  if (dir == 0) {
    portPauseInfoOpen(&kLogPage);
  }
  return nullptr;
}

const PortPauseForm kForm = {"Accessibility", "Message log", formRows, formAct};

const bool sAdded = [] {
  portSettingsAddSection("Accessibility", "Message log",
                         "The last 200 pages of the message windows, to read again at your own pace: newest at the "
                         "bottom. L on the keyboard opens it too. The game waits while it is open (online it goes on).",
                         PORT_SETTING_IN_GAME, section,
                         "message log history backlog transcript conversation dialogue text read again scroll back");
  portSettingsAddOverlay(draw, event);
  // the Message log binding (Controls > Bindings, L): open, and closed again
  portBindingsSetHandler(PORT_BIND_MESSAGE_LOG, [](int, int repeat) {
    if (!repeat) {
      sView.open ? close() : open();
    }
    return 1;
  });
  msgtext::listen(add, nullptr);
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace
