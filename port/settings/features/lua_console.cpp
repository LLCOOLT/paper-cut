// Lua mods' console (port/include/port/lua_mods.h, docs/LUA_MODDING.md):
// what the mods print and their errors, over the game (Lua console, F4, in
// Controls > Bindings), with Reload (every Lua mod started again from its
// files); a short notice when a mod stops on an error; and the Mods tab's
// section with the same two buttons.
#include "port/bindings.h"
#include "port/lua_mods.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <string>

namespace {

bool sOpen;
bool sFocused; // the console had the focus at its last drawing (Escape and B close it: portLuaConsoleFocused)
bool sToBottom;
int sSeen; // lines when last drawn (new ones scroll to the bottom)
Uint64 sReloadedAt; // the last Start again's (its "Started again." for a few seconds)

// Online and in a replay the Lua mods the run started with stay (lua_host.cpp): no starting them again
bool reloadLocked() { return portStateSyncLinked() != 0 || portReplayActive() != 0; }

// Reload (the console's) or Start every Lua mod again (the section's): started again, or why not
void reloadButton(const char* label, bool console) {
  const bool locked = reloadLocked();
  ImGui::BeginDisabled(locked);
  if (ImGui::Button(portTrLabel(label))) {
    portLuaReloadAll();
    sReloadedAt = SDL_GetTicks();
  }
  ImGui::EndDisabled();
  if (locked) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("Online or in a replay, Lua mods can't start again until it's over."));
  } else if (sReloadedAt != 0 && SDL_GetTicks() - sReloadedAt < 4000) {
    ImGui::SameLine();
    ImGui::TextUnformatted(portTr("Started again."));
  } else if (console) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("Reload starts every Lua mod again from its files (not online)."));
  }
}

void consoleWindow() {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.05f, display.y * 0.55f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(display.x * 0.6f, display.y * 0.4f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(portTrLabel("Lua console###lua_console"), &sOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
    sFocused = false;
    ImGui::End();
    return;
  }
  // the way back while it has the focus (Escape, B with the page open, Android's back, the mouse's back): closed
  const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if (focused && sFocused && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
    sOpen = false;
  }
  sFocused = focused && sOpen;
  if (ImGui::Button(portTrLabel("Clear##lua_console"))) {
    portLuaConsoleClear();
  }
  ImGui::SameLine();
  reloadButton("Reload##lua_console", true);
  ImGui::Separator();
  ImGui::BeginChild("##lua_console_lines", ImVec2(0.f, 0.f), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);
  const int n = portLuaConsoleCount();
  if (n == 0) {
    ImGui::TextDisabled("%s", portTr("Nothing yet: what Lua mods print, and their errors, show here."));
  }
  for (int i = 0; i < n; i++) {
    int error = 0;
    const char* line = portLuaConsoleLine(i, &error);
    if (line == nullptr) {
      break;
    }
    if (error) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
    }
    ImGui::TextUnformatted(line);
    if (error) {
      ImGui::PopStyleColor();
    }
  }
  if (n != sSeen || sToBottom) {
    ImGui::SetScrollHereY(1.f);
    sSeen = n;
    sToBottom = false;
  }
  ImGui::EndChild();
  ImGui::End();
}

// A mod stopped: a line at the top for a few seconds.
void notice() {
  unsigned long long at = 0;
  const char* error = portLuaLastError(&at);
  if (error == nullptr || SDL_GetTicks() - at > 6000) {
    return;
  }
  const std::string text = std::string(portTr("A Lua mod stopped (Lua console: {LUA}):")) + " " + error;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 size = ImGui::CalcTextSize(text.c_str());
  const ImVec2 at0(12.f, 12.f);
  dl->AddRectFilled(at0, ImVec2(at0.x + size.x + 16.f, at0.y + size.y + 10.f), IM_COL32(40, 10, 10, 210), 4.f);
  dl->AddText(ImVec2(at0.x + 8.f, at0.y + 5.f), IM_COL32(255, 200, 190, 255), text.c_str());
}

void draw(int pageOpen) {
  (void)pageOpen;
  if (sOpen) {
    consoleWindow();
  } else {
    sFocused = false;
    notice();
  }
}

void section(int inGame) {
  if (ImGui::Button(portTrLabel(sOpen ? "Close the Lua console" : "Open the Lua console"))) {
    sOpen = !sOpen;
    sToBottom = true;
  }
  if (inGame) {
    ImGui::SameLine();
    reloadButton("Start every Lua mod again", false);
  }
}

const bool sAdded = [] {
  portSettingsAddSection("Mods", "Lua mods",
                         "Mods with a lua folder run Lua scripts (docs/LUA_MODDING.md): each in a sandbox of its own, "
                         "with no file or network access. A folder mod starts again by itself when its Lua files "
                         "change. Online, they run only when both players have the same Lua mods.",
                         0, section, "lua script scripting console reload hot mods code print error");
  portSettingsAddOverlay(draw, nullptr);
  portBindingsSetHandler(PORT_BIND_LUA_CONSOLE, [](int down, int repeat) {
    if (down && !repeat) {
      sOpen = !sOpen;
      sToBottom = true;
    }
    return 1;
  });
  return true;
}();

} // namespace

// settings_ui.cpp: the console has the focus, so Escape (and B with the page open) is its, not the page's
// or a game menu's
extern "C" int portLuaConsoleFocused(void) { return sOpen && sFocused ? 1 : 0; }
