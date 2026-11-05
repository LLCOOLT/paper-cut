// Console-accurate mode (port/include/port/toggles.h): one switch that puts
// the game back as the GameCube plays it. Turned on, every setting marked
// "changes play" (each feature's, and Run speed), and the ones that change the
// game's timing without being marked (Text speed, Faster text outside battle,
// Menu speed, Rewind), and the fun toggles, go back to the original game's value, and the page
// locks them while it is on. Their values as they were are kept in
// <user>/console_accurate.txt, and come back when it is turned off. The
// speedrun glitches are the game's own (Paper Cut never patches them out), so
// they are all there. Races and leaderboards ask portConsoleAccurate().
//
// Online, the host's settings travel as a whole: a guest gets its values and
// its switch together, and keeps none of them in console_accurate.txt.
#include "port/online.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" int portSettingsPlayKeys(const char** keys, int max); // settings_ui.cpp

namespace {

constexpr const char* kLabel = "Console-accurate mode";

// timing settings not marked "changes play", and the original game's value
// where it isn't the setting's default
struct Forced {
  const char* key;
  int original; // -1: the default
};
constexpr Forced kForced[] = {
    {"text_speed", 0},
    {"fast_field_text", 0},
    {"menu_speed", 0}, // (its default is Fast)
    {"rewind", 0},
    // the fun toggles that only change the picture or the sound (moon gravity is marked)
    {"fun_big_heads", 0},
    {"fun_paper_thin", 0},
    {"fun_luigi_voice", 0},
    {"fun_spin", 0},
};

int* sOn;

std::string savedFile() { return std::string(portSettingsUserPath()) + "/console_accurate.txt"; }

std::vector<std::string> lockedKeys() {
  const char* keys[256];
  const int n = portSettingsPlayKeys(keys, 256);
  std::vector<std::string> out;
  for (int i = 0; i < n; i++) {
    if (std::strcmp(keys[i], "console_accurate") != 0) {
      out.push_back(keys[i]);
    }
  }
  for (const Forced& f : kForced) {
    out.push_back(f.key);
  }
  return out;
}

// a setting's value now, by key (a declared one, or one of the fixed fields here)
bool valueOf(const std::string& key, int* v) {
  if (const int* p = portSettingsFind(key.c_str())) {
    *v = *p;
    return true;
  }
  PortSettings* s = portSettings();
  if (key == "text_speed") {
    *v = s->textSpeed;
  } else if (key == "run_speed") {
    *v = s->runSpeed;
  } else {
    return false;
  }
  return true;
}

int originalOf(const std::string& key) {
  for (const Forced& f : kForced) {
    if (key == f.key && f.original >= 0) {
      return f.original;
    }
  }
  int v = 0;
  if (!portSettingsDefault(key.c_str(), &v)) {
    v = 0;
  }
  return v;
}

// the locked settings at the original's values; returns how many were changed
int putBack() {
  int changed = 0;
  for (const std::string& key : lockedKeys()) {
    int now = 0;
    const int want = originalOf(key);
    if (valueOf(key, &now) && now != want) {
      portSettingsSetKey(key.c_str(), std::to_string(want).c_str());
      changed++;
    }
  }
  return changed;
}

// the values console-accurate mode is about to put back, kept for turning it off
void keepValues() {
  std::string text = "# Paper Cut: the settings console-accurate mode put back, restored when it is turned off\n";
  for (const std::string& key : lockedKeys()) {
    int v = 0;
    if (valueOf(key, &v)) {
      text += key + "=" + std::to_string(v) + "\n";
    }
  }
  SDL_SaveFile(savedFile().c_str(), text.data(), text.size());
}

void turnOn() {
  keepValues();
  putBack();
  *sOn = 1;
  portSettingsApply();
  portSettingsSave();
}

void turnOff() {
  size_t size = 0;
  char* data = static_cast<char*>(SDL_LoadFile(savedFile().c_str(), &size));
  *sOn = 0;
  if (data != nullptr) {
    std::string text(data, size);
    SDL_free(data);
    size_t at = 0;
    while (at < text.size()) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) {
        end = text.size();
      }
      std::string line = text.substr(at, end - at);
      at = end + 1;
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const size_t eq = line.find('=');
      if (line.empty() || line[0] == '#' || eq == std::string::npos) {
        continue;
      }
      portSettingsSetKey(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str());
    }
    SDL_RemovePath(savedFile().c_str());
  }
  portSettingsApply();
  portSettingsSave();
}

void section(int) {
  bool on = *sOn != 0;
  if (ImGui::Checkbox(portTrLabel("Console-accurate mode##console_accurate"), &on)) {
    if (on) {
      turnOn();
    } else {
      turnOff();
    }
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("The game as the GameCube plays it: every setting that changes play goes back to the "
                                  "original and is locked, with Text speed, Menu speed and Rewind. The speedrun "
                                  "glitches are the game's own and all still there. Turned off, your settings come "
                                  "back as they were. Races and leaderboards can require it."));
  ImGui::PopStyleColor();
}

// once a second while it is on: anything that changed a locked setting since
// (a preset, a mode's own change) is put back
void tick(int) {
  static Uint64 sNext;
  if (sOn == nullptr || *sOn == 0 || SDL_GetTicks() < sNext) {
    return;
  }
  sNext = SDL_GetTicks() + 1000;
  // turned on some other way than its switch (settings.txt, a preset or a
  // challenge code, TTYD_SET_): the values are kept first, as the switch keeps
  // them, so turning it off brings them back. Not on an online guest (its
  // settings are the host's for the session).
  static bool sKept = false;
  if (!sKept && !(portOnlineActive() && !portOnlineIsHost())) {
    sKept = true;
    if (!SDL_GetPathInfo(savedFile().c_str(), nullptr)) {
      keepValues();
    }
  }
  if (putBack() > 0) {
    portSettingsApply();
  }
}

const bool sAdded = [] {
  sOn = portSettingsDeclare("console_accurate", 0, 0, 1);
  portSettingsAddSection("Gameplay", kLabel, nullptr, PORT_SETTING_CHANGES_PLAY, section,
                         "console accurate original gamecube vanilla speedrun glitches race leaderboard authentic");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

bool forcedKey(const char* key) {
  for (const Forced& f : kForced) {
    if (std::strcmp(key, f.key) == 0) {
      return true;
    }
  }
  return std::strcmp(key, "run_speed") == 0;
}

} // namespace

// The settings page's item (settings_ui.cpp drawItem): drawn disabled while on.
extern "C" int portConsoleAccurateLocked(unsigned flags, const char* key, const char* label) {
  if (sOn == nullptr || *sOn == 0 || (label != nullptr && std::strcmp(label, kLabel) == 0) ||
      (key != nullptr && std::strcmp(key, "console_accurate") == 0)) {
    return 0;
  }
  return (flags & PORT_SETTING_CHANGES_PLAY) != 0 || (key != nullptr && forcedKey(key));
}

// The Start menu's Settings tab (pause_settings_aurora.c): a control, by its value, can't change.
extern "C" int portConsoleAccurateLockedValue(unsigned flags, const int* value) {
  if (sOn == nullptr || *sOn == 0 || value == sOn) {
    return 0;
  }
  if (flags & PORT_SETTING_CHANGES_PLAY) {
    return 1;
  }
  for (const Forced& f : kForced) {
    if (portSettingsFind(f.key) == value) {
      return 1;
    }
  }
  return value == &portSettings()->textSpeed || value == &portSettings()->runSpeed;
}
