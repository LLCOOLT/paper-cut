// Online play's wait over the picture (vi_aurora.c's portOnlineHoldFrame): while the game is held
// for the other player ("Waiting for LT...", "LT minimized the game.") or a reconnect, a small
// notice at the bottom of the window, so a player in full screen sees why the picture stopped
// (the window's title says it too). Dear ImGui's, over everything: never the game's font, which
// takes the game's random numbers. backdrop: the picture couldn't be kept (a second notice in the
// same wait), so a dark screen behind it.
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

#include "../launcher_style.h"

#include <algorithm>
#include <cfloat>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// $TTYD_TEST_MINIMIZE=<frame>:<ms> (tests, vi_aurora.c): the window put away and shown again,
// without taking the focus from anything. On Windows it is hidden: a minimized window there keeps
// playing (Aurora pauses only for a hidden one), as phones and Macs do in the background.
extern "C" void portTestWindowMinimized(int minimized) {
#ifdef _WIN32
  SDL_Window* w = aurora_get_window();
  HWND hwnd = w != nullptr ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(w),
                                                                      SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
                           : nullptr;
  if (hwnd != nullptr) {
    ShowWindow(hwnd, minimized ? SW_HIDE : SW_SHOWNOACTIVATE);
  }
#else
  SDL_Window* w = aurora_get_window();
  if (w != nullptr) {
    if (minimized) {
      SDL_MinimizeWindow(w);
    } else {
      SDL_RestoreWindow(w);
    }
  }
#endif
}

extern "C" void portOnlineHoldOverlay(const char* utf8, int backdrop) {
  if (utf8 == nullptr || utf8[0] == '\0' || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  if (display.x <= 0.f || display.y <= 0.f) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (backdrop) {
    dl->AddRectFilled(ImVec2(0.f, 0.f), display, IM_COL32(12, 12, 16, 255));
  }
  // the launcher's and the settings page's round bold letters (the system's Japanese merged in)
  ImFont* font = gLauncherFonts.heading != nullptr ? gLauncherFonts.heading : ImGui::GetFont();
  // about a twentieth of the window's height, as the game's own notices
  const float size = std::clamp(display.y / 22.f, 16.f, 64.f);
  const float wrap = display.x * 0.8f;
  const ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, wrap, utf8);
  const float padX = size * 0.8f, padY = size * 0.45f;
  const float w = text.x + 2.f * padX, h = text.y + 2.f * padY;
  const float x = (display.x - w) * 0.5f;
  const float y = backdrop ? (display.y - h) * 0.5f : display.y - h - display.y * 0.08f;
  dl->AddRectFilled(ImVec2(x + 3.f, y + 4.f), ImVec2(x + w + 3.f, y + h + 4.f), IM_COL32(0, 0, 0, 90), size * 0.4f);
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), kPaper, size * 0.4f);
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(59, 36, 22, 200), size * 0.4f, 0, std::max(1.f, size / 14.f));
  dl->AddText(font, size, ImVec2(x + padX, y + padY), kInk, utf8, nullptr, wrap);
}
