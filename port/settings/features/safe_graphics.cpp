// Safe graphics, the graphics API, and the flight record's offer (port/include/port/safe_graphics.h,
// port/include/port/flight.h), GitHub #39: a phone whose GPU took the whole device down.
#include "port/safe_graphics.h"
#include "port/companion_menu.h"
#include "port/display.h"
#include "port/flight.h"
#include "port/mem_guard.h"
#include "port/settings.h"
#include "port/soft_crash.h"
#include "port/tr.h"

#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

#if defined(__ANDROID__)
const char* kApis[] = {"Auto (Vulkan)", "Vulkan", "OpenGL ES"};
#elif defined(_WIN32)
const char* kApis[] = {"Auto (Direct3D 12)", "Direct3D 12", "Vulkan", "Direct3D 11"};
#endif

int* sSafe;
int* sApi;

// --- Safe graphics for the run: each setting at its safest, settings.txt keeping the player's ---

// a declared setting (kept in settings.txt as the player's) or a field (portSettingsSetForRun)
struct Hold {
  const char* key;
  int safe;
  bool field;
  bool mine; // held by this (not already by a guard): given back when Safe graphics goes off
  int was;
};
Hold sHolds[] = {
    {"supersample", 0, false, false, 0},     {"texture_upscale", 0, false, false, 0},
    {"texture_pack_memory", 1, false, false, 0}, {"aniso", 0, false, false, 0},
    {"post_filter", 0, false, false, 0},     {"bloom", 0, false, false, 0},
    {"rt_lighting", 0, false, false, 0},     {"paper_shadows", 0, false, false, 0},
    {"paper_edges", 0, false, false, 0},     {"stereo_3d", 0, false, false, 0},
    {"hdr", 0, false, false, 0},             {"resolution", 1, true, false, 0},
    {"no_pop_in", 0, true, false, 0},
};
char sFramerateWas[16];
bool sFramerateMine, sCompanionMine, sHolding;

int fieldValue(const char* key) {
  const PortSettings* s = portSettings();
  return std::strcmp(key, "resolution") == 0 ? s->resolution : s->noPopIn;
}

void hold() {
  if (sHolding) {
    return;
  }
  sHolding = true;
  std::string changed;
  for (Hold& h : sHolds) {
    h.mine = false;
    char text[16];
    std::snprintf(text, sizeof(text), "%d", h.safe);
    if (h.field) {
      h.was = fieldValue(h.key);
      if (h.was != h.safe) {
        portSettingsSetForRun(h.key, text);
        h.mine = true;
      }
    } else if (int* v = portSettingsFind(h.key)) {
      if (*v != h.safe) {
        if (!portSettingsExtraHeld(h.key)) {
          portSettingsExtraSaveAs(h.key, 1, *v);
          h.mine = true;
        }
        h.was = *v;
        *v = h.safe;
      }
    } else if (int raw = 0; portSettingsExtraRaw(h.key, &raw) && raw != h.safe) {
      // (read from settings.txt, not declared yet: forced, the read value kept for the file)
      if (!portSettingsExtraHeld(h.key)) {
        portSettingsExtraSaveAs(h.key, 1, raw);
        h.mine = true;
      }
      h.was = raw;
      portSettingsExtraForce(h.key, text);
    }
    if (h.mine) {
      changed += changed.empty() ? "" : ", ";
      changed += h.key;
    }
  }
  if (const int* up = portSettingsFind("texture_upscale")) {
    portTextureUpscaleSet(*up);
  }
  PortSettings* s = portSettings();
  sFramerateMine = std::strcmp(s->framerate, "60") != 0;
  if (sFramerateMine) {
    std::snprintf(sFramerateWas, sizeof(sFramerateWas), "%s", s->framerate);
    portSettingsSetForRun("framerate", "60");
    changed += changed.empty() ? "framerate" : ", framerate";
  }
  sCompanionMine = portCompanionMenuSuspend(1) != 0;
  portSettingsApply();
  std::printf("[gfx] safe graphics for the run: %s\n", changed.empty() ? "everything already at its safest" : changed.c_str());
  std::fflush(stdout);
  portFlightMark("safe graphics on for the run");
}

void release() {
  if (!sHolding) {
    return;
  }
  sHolding = false;
  for (Hold& h : sHolds) {
    if (!h.mine) {
      continue;
    }
    h.mine = false;
    char text[16];
    std::snprintf(text, sizeof(text), "%d", h.was);
    if (h.field) {
      portSettingsSetForRun(h.key, text);
    } else {
      portSettingsExtraSaveAs(h.key, 0, h.was);
      portSettingsExtraForce(h.key, text);
    }
  }
  if (const int* up = portSettingsFind("texture_upscale")) {
    portTextureUpscaleSet(*up);
  }
  if (sFramerateMine) {
    portSettingsSetForRun("framerate", sFramerateWas);
    sFramerateMine = false;
  }
  if (sCompanionMine) {
    portCompanionMenuSuspend(0);
    sCompanionMine = false;
  }
  portSettingsApply();
  std::printf("[gfx] safe graphics off: the player's own graphics back for the run\n");
  std::fflush(stdout);
  portFlightMark("safe graphics off");
}

// the toggle changed on a page (the settings', the Start menu's): held or given back at once
int sWas = -1; // the value last acted on (at start: portSafeGraphicsStart)

void watch(int) {
  if (sSafe == nullptr) {
    return;
  }
  if (sWas < 0) {
    sWas = *sSafe;
    return;
  }
  if (*sSafe != sWas) {
    sWas = *sSafe;
    if (*sSafe) {
      hold();
    } else {
      release();
    }
  }
}

const bool sAdded = [] {
  sSafe = portSettingsDeclare("safe_graphics", 0, 0, 1);
#if defined(_WIN32)
  sApi = portSettingsDeclare("gfx_backend", 0, 0, 3); // (Windows: Auto, Direct3D 12, Vulkan, Direct3D 11)
#else
  sApi = portSettingsDeclare("gfx_backend", 0, 0, 2);
#endif
  portSettingsAddToggle(
      "Graphics", "Safe graphics",
      "The least the graphics card is asked for, for a device whose screen slows right down or that restarts while "
      "playing: no anti-aliasing, supersampling, upscaling or effects, the picture at 480 lines, smaller graphics "
      "buffers. Most of it at once, the rest from the next start.",
      PORT_SETTING_NEXT_START, "safe_graphics", "safe graphics crash restart reset slow gpu driver compatibility");
#if defined(__ANDROID__)
  portSettingsAddChoice("Graphics", "Graphics API",
                        "How the game talks to the graphics chip. Try OpenGL ES if the game slows right down or the "
                        "device restarts with Vulkan.",
                        PORT_SETTING_NEXT_START, "gfx_backend", kApis, 3, "vulkan opengl gles api driver backend");
#elif defined(_WIN32)
  portSettingsAddChoice("Graphics", "Graphics API",
                        "How the game talks to the graphics card. Try Direct3D 11 if the screen goes white, the game "
                        "stops or lags badly with an older or Intel graphics driver. Safe graphics on Auto uses "
                        "Direct3D 11.",
                        PORT_SETTING_NEXT_START, "gfx_backend", kApis, 4, "direct3d d3d12 d3d11 vulkan api driver backend intel white");
#endif
  portSettingsAddOverlay(watch, nullptr);
  return true;
}();

// --- the record ---

std::string lastRecord() {
  const char* path = portFlightLastPath();
  std::string text;
  if (path == nullptr) {
    return text;
  }
  size_t size = 0;
  if (char* data = static_cast<char*>(SDL_LoadFile(path, &size))) {
    text.assign(data, size);
    SDL_free(data);
  }
  return text;
}

} // namespace

extern "C" void portSafeGraphicsStart(void) {
  sWas = sSafe != nullptr ? *sSafe : 0; // (one changed in the launcher's settings: held once the game draws)
  if (portSafeGraphicsAtStart()) { // (the setting as read at start, or $TTYD_SAFE_GRAPHICS)
    hold();
  }
}

void portFlightRecordButtons() {
  static bool sCopied;
  if (portFlightLastPath() == nullptr) {
    return;
  }
  if (ImGui::Button(portTrLabel(sCopied ? "Copied##flight_copy" : "Copy the last graphics record##flight_copy"))) {
    sCopied = SDL_SetClipboardText(lastRecord().c_str());
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("flight_record_last.txt in the save folder: what the graphics were doing as the "
                                  "last run ended, kept even when the device restarted. Paste it to whoever is "
                                  "helping you."));
  ImGui::PopStyleColor();
}

// The offer shows this start: Android (or $TTYD_FLIGHT_OFFER=1), after a run that stopped on screen with no crash
// report pending (soft_crash.cpp: the undo question and the crash-safe snapshots then go in it, one window only)
bool portFlightOfferShows() {
  const char* force = SDL_getenv("TTYD_FLIGHT_OFFER");
#if defined(__ANDROID__)
  const bool here = force == nullptr || force[0] != '0';
#else
  const bool here = force != nullptr && force[0] != '\0' && force[0] != '0';
#endif
  const bool stopped = portFlightStoppedOnScreen() || portMemGuardClosedOnScreen();
  return here && stopped && !portCrashReportPending();
}

// The launcher's offer, once, after a run that stopped on screen with no clean exit and no crash report (the
// flight record's verdict, or the memory guard's): Android only ($TTYD_FLIGHT_OFFER=1 anywhere, for tests).
// $TTYD_FLIGHT_OFFER_PRESS=safe|api|copy|close presses that after 30 frames (tests).
void portFlightOfferDraw(float scale) {
  static int sState = -1; // -1 not looked at, 0 not shown, 1 up, 2 done
  static std::string sRecord, sSaid;
  static int sFrames;
  static bool sCopied;
  static const char* sPress;
  if (sState == 0 || sState == 2) {
    return;
  }
  if (sState < 0) {
    if (!portFlightOfferShows()) {
      sState = 0;
      return;
    }
    sState = 1;
    sRecord = lastRecord();
    sPress = SDL_getenv("TTYD_FLIGHT_OFFER_PRESS");
    std::printf("[flight] offer shown: Safe graphics %s, graphics API %s\n", sSafe != nullptr && *sSafe ? "on" : "off",
                aurora_get_backend() == BACKEND_OPENGLES ? "OpenGL ES" : "Vulkan");
    std::fflush(stdout);
  }
  const std::string id = std::string(portTr("Paper Cut stopped while drawing")) + "##flight_offer";
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2((std::min)(display.x - 32.f, 600.f * scale), 0.f), ImGuiCond_Always);
  if (!ImGui::IsPopupOpen(id.c_str())) {
    ImGui::OpenPopup(id.c_str());
  }
  if (!ImGui::BeginPopupModal(id.c_str(), nullptr,
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }
  sFrames++;
  const char* press = sPress != nullptr && sFrames == 30 ? sPress : "";
  bool close = std::strcmp(press, "close") == 0;
  ImGui::TextWrapped("%s", portTr("Last time the game stopped while it was on screen, with no crash report: the "
                                  "device or its graphics driver ended it. If the picture slowed right down first, or "
                                  "the device restarted, try Safe graphics."));
  // a setting changed just before (the undo question) and the crash-safe snapshots: here, not a second window
  // (soft_crash.cpp); a snapshot picked closes the offer and the launcher continues from it
  if (portSoftCrashOfferRows(nullptr)) {
    close = true;
  }
  const bool safeOn = sSafe != nullptr && *sSafe != 0;
  if (!safeOn) {
    if (ImGui::Button(portTrLabel("Turn on Safe graphics##flight_safe")) || std::strcmp(press, "safe") == 0) {
      *sSafe = 1;
      hold();             // (the run's settings now: the launcher draws no overlays)
      portSettingsChangesQuiet(1); // (the offer's own answer: not a change to offer back after a crash)
      portSettingsSave(); // (settings.txt: Safe graphics on, the held settings the player's own)
      portSettingsChangesQuiet(0);
      sSaid = portTr("Safe graphics is on: most of it now, the rest from the next start.");
      std::printf("[flight] offer: Safe graphics on\n");
      std::fflush(stdout);
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
  }
#if defined(__ANDROID__)
  {
    const bool gles = aurora_get_backend() == BACKEND_OPENGLES;
    const int other = gles ? 1 : 2;
    if (sApi != nullptr && *sApi != other &&
        (ImGui::Button(portTrLabel(gles ? "Use Vulkan##flight_api" : "Use OpenGL ES##flight_api")) ||
         std::strcmp(press, "api") == 0)) {
      *sApi = other;
      portSettingsChangesQuiet(1);
      portSettingsSave();
      portSettingsChangesQuiet(0);
      sSaid = portTr("Close Paper Cut and open it again to switch.");
      std::printf("[flight] offer: graphics API %s from the next start\n", gles ? "Vulkan" : "OpenGL ES");
      std::fflush(stdout);
    }
    ImGui::SameLine();
  }
#endif
  if (ImGui::Button(portTrLabel(sCopied ? "Copied##flight_offer_copy" : "Copy the record##flight_offer_copy")) ||
      std::strcmp(press, "copy") == 0) {
    sCopied = SDL_SetClipboardText(sRecord.c_str());
    std::printf("[flight] offer: the record copied (%zu bytes)\n", sRecord.size());
    std::fflush(stdout);
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Not now##flight_offer"))) {
    close = true;
  }
  if (!sSaid.empty()) {
    ImGui::TextWrapped("%s", sSaid.c_str());
  }
  if (!sRecord.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", portTr("The record of what the graphics were doing, to paste to whoever is helping "
                                    "you (also in Settings > General > Crash reports):"));
    ImGui::PopStyleColor();
    ImGui::InputTextMultiline("##flight_record", const_cast<char*>(sRecord.c_str()), sRecord.size() + 1,
                              ImVec2(-1.f, ImGui::GetTextLineHeight() * 6.f), ImGuiInputTextFlags_ReadOnly);
  }
  if (sFrames > 1 && !ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
    close = true; // (B, Escape, Android's back)
  }
  if (close) {
    sState = 2;
    std::printf("[flight] offer closed\n");
    std::fflush(stdout);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}
