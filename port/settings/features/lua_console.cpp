// Lua mods' console (port/include/port/lua_mods.h, docs/LUA_MODDING.md):
// what the mods print and their errors, over the game (Lua console, F4, in
// Controls > Bindings), with Reload (every Lua mod started again from its
// files); a short notice when a mod stops on an error; and the Mods tab's
// section with the same two buttons.
#include "port/bindings.h"
#include "port/lua_mods.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <string>

namespace {

bool sOpen;
bool sToBottom;
int sSeen; // lines when last drawn (new ones scroll to the bottom)

void consoleWindow() {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.05f, display.y * 0.55f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(display.x * 0.6f, display.y * 0.4f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(portTrLabel("Lua console###lua_console"), &sOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
    ImGui::End();
    return;
  }
  if (ImGui::Button(portTrLabel("Clear##lua_console"))) {
    portLuaConsoleClear();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Reload##lua_console"))) {
    portLuaReloadAll();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", portTr("Reload starts every Lua mod again from its files (not online)."));
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
  const std::string text = std::string(portTr("A Lua mod stopped (Lua console: F4):")) + " " + error;
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
    if (ImGui::Button(portTrLabel("Start every Lua mod again"))) {
      portLuaReloadAll();
    }
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
