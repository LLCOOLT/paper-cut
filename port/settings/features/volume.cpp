// The Audio tab's mixer and the volume keys (port/src/common/audio_mix_pc.c,
// settings_aurora.c): Master, Music, Sound effects and Ambience in one block,
// each a row of a speaker button (mute and unmute; its waves show the level),
// the name and a slider across the rest of the row. Every slider is heard as
// it moves, takes the mouse wheel, arrow keys and a controller's d-pad, and
// Ctrl+click (or a double-click) to type a value. The volume keys work in the
// game and the launcher: F9 quieter, F10 louder (Shift: a step of 1), F3 mute,
// with a bar shown on the screen for a moment.
#include "port/bindings.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

struct Channel {
  const char* name;
  const char* volumeKey; // nullptr: the master volume (PortSettings::volume)
  const char* muteKey;
  const char* tip;
};

constexpr Channel kChannels[] = {
    {"Master", nullptr, "mute", "Everything the game plays."},
    {"Music", "music_volume", "music_mute", "The songs."},
    {"Sound effects", "sfx_volume", "sfx_mute", "Jumps, hits, menus, voices' blips: anything not music or ambience."},
    {"Ambience", "ambience_volume", "ambience_mute", "Wind, water, crowds and other sounds of a place."},
};
constexpr int kCount = sizeof(kChannels) / sizeof(kChannels[0]);

int* volumeOf(const Channel& c) {
  return c.volumeKey != nullptr ? portSettingsFind(c.volumeKey) : &portSettings()->volume;
}

Uint64 sShownAt; // the volume bar on the screen since (ms), 0 never

void changed(bool apply) {
  if (apply) {
    portSettingsApply(); // the master volume and mute are applied by settings_aurora.c
  }
  portSettingsSave();
}

// A speaker: its body, and 0 to 3 waves for the level, or a cross when muted.
void drawSpeaker(ImDrawList* dl, ImVec2 c, float size, int level, bool muted, ImU32 col) {
  const float s = size * 0.5f;
  const ImVec2 body[] = {ImVec2(c.x - s * 0.9f, c.y - s * 0.3f), ImVec2(c.x - s * 0.45f, c.y - s * 0.3f),
                         ImVec2(c.x + s * 0.05f, c.y - s * 0.75f), ImVec2(c.x + s * 0.05f, c.y + s * 0.75f),
                         ImVec2(c.x - s * 0.45f, c.y + s * 0.3f), ImVec2(c.x - s * 0.9f, c.y + s * 0.3f)};
  dl->AddConvexPolyFilled(body, 6, col);
  const float th = std::max(1.5f, size * 0.08f);
  if (muted) {
    const float x = c.x + s * 0.55f, r = s * 0.3f;
    dl->AddLine(ImVec2(x - r, c.y - r), ImVec2(x + r, c.y + r), col, th);
    dl->AddLine(ImVec2(x - r, c.y + r), ImVec2(x + r, c.y - r), col, th);
    return;
  }
  const int waves = level <= 0 ? 0 : level < 34 ? 1 : level < 67 ? 2 : 3;
  for (int i = 0; i < waves; i++) {
    const float r = s * (0.35f + 0.27f * static_cast<float>(i));
    dl->PathArcTo(ImVec2(c.x + s * 0.1f, c.y), r, -0.8f, 0.8f, 10);
    dl->PathStroke(col, 0, th);
  }
}

// One row of the mixer; returns true while its slider is being dragged.
void channelRow(const Channel& c) {
  int* volume = volumeOf(c);
  int* mute = portSettingsDeclare(c.muteKey, 0, 0, 1);
  if (volume == nullptr) {
    volume = portSettingsDeclare(c.volumeKey, 100, 0, 100);
  }
  ImGui::PushID(c.name);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  const float h = ImGui::GetFrameHeight();
  if (ImGui::InvisibleButton("mute", ImVec2(h, h))) {
    *mute = !*mute;
    if (!*mute && *volume == 0) {
      *volume = 50; // unmuting a slider at the bottom: something to hear
    }
    changed(c.volumeKey == nullptr);
  }
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (hovered || ImGui::IsItemFocused()) {
    dl->AddRectFilled(lo, hi, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), h * 0.25f);
  }
  const ImU32 col = ImGui::GetColorU32(*mute ? ImGuiCol_TextDisabled : ImGuiCol_Text);
  drawSpeaker(dl, ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f), h * 0.7f, *volume, *mute != 0, col);
  if (hovered) {
    ImGui::SetTooltip("%s %s", *mute ? "Unmute" : "Mute", c.name);
  }

  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  if (*mute) {
    ImGui::TextDisabled(portTr("%s (muted)"), c.name);
  } else {
    ImGui::TextUnformatted(c.name);
  }
  if (ImGui::IsItemHovered() && c.tip != nullptr) {
    ImGui::SetTooltip("%s", c.tip);
  }

  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (*mute) {
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
  }
  bool moved = ImGui::SliderInt("##v", volume, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
  bool done = ImGui::IsItemDeactivatedAfterEdit();
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    ImGui::SetKeyboardFocusHere(-1); // a double-click types a value, as Ctrl+click does
  }
  // the mouse wheel over a slider moves it by 5 (the page scrolls elsewhere)
  if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.f && !ImGui::IsItemActive()) {
    *volume = std::clamp(*volume + (ImGui::GetIO().MouseWheel > 0.f ? 5 : -5), 0, 100);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    moved = done = true;
  }
  if (*mute) {
    ImGui::PopStyleVar();
  }
  if (moved && *mute && *volume > 0) {
    *mute = 0; // moving a muted slider unmutes it
  }
  if (moved && c.volumeKey == nullptr) {
    portSettingsApply(); // the master volume is heard as it moves (the others are read by the mixer)
  }
  if (done) {
    changed(false);
  }
  ImGui::PopID();
}

void mixerSection(int) {
  PortSettings* s = portSettings();
  if (ImGui::BeginTable("mixer", 3, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("mute", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Sound effects (muted)").x);
    ImGui::TableSetupColumn("slider", ImGuiTableColumnFlags_WidthStretch);
    for (const Channel& c : kChannels) {
      channelRow(c);
      if (c.volumeKey == nullptr) {
        // the master row, then a little room before the parts of the mix
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetStyle().ItemSpacing.y);
      }
    }
    ImGui::EndTable();
  }
  if (ImGui::Button(portTrLabel("Reset to 100%"))) {
    for (const Channel& c : kChannels) {
      *volumeOf(c) = 100;
      *portSettingsDeclare(c.muteKey, 0, 0, 1) = 0;
    }
    changed(true);
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::TextWrapped("%s", portTr("Tap a speaker to mute or unmute. The device's own volume buttons still set its volume."));
#else
  ImGui::TextWrapped("%s", portTr("Click a speaker to mute or unmute. Keys anywhere: F9 quieter, F10 louder (with Shift, by 1), "
                     "F3 mute."));
#endif
  ImGui::PopStyleColor();
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  bool bg = s->muteUnfocused != 0;
  if (ImGui::Checkbox(portTrLabel("Mute while in the background"), &bg)) {
    s->muteUnfocused = bg ? 1 : 0;
    changed(true);
  }
#else
  (void)s;
#endif
}

// The Start menu's form (port/include/port/pause_forms.h): each part's mute (the volumes are the tab's own
// rows), and Reset to 100%
constexpr int kResetRow = 100;

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  static const char* const kMuteLabels[kCount] = {"Mute everything", "Mute the music", "Mute sound effects",
                                                  "Mute ambience"};
  for (int i = 0; i < kCount && n < max - 1; i++) {
    const int* mute = portSettingsDeclare(kChannels[i].muteKey, 0, 0, 1);
    out[n++] = portPauseRow(kMuteLabels[i], kChannels[i].tip, *mute ? "On" : "Off", i);
  }
  out[n++] = portPauseRow("Reset to 100%", "Every volume back to 100%, nothing muted.", nullptr, kResetRow,
                          PORT_PAUSE_ROW_CONFIRM);
  return n;
}

const char* formAct(int id, int dir) {
  if (id == kResetRow) {
    if (dir == 0) {
      for (const Channel& c : kChannels) {
        *volumeOf(c) = 100;
        *portSettingsDeclare(c.muteKey, 0, 0, 1) = 0;
      }
      changed(true);
    }
    return nullptr;
  }
  if (id >= 0 && id < kCount) {
    int* mute = portSettingsDeclare(kChannels[id].muteKey, 0, 0, 1);
    *mute = !*mute;
    changed(true);
  }
  return nullptr;
}

const PortPauseForm kForm = {"Audio", "Volume", formRows, formAct};

// The bar on the screen after a volume key: the speaker, the level and its percentage.
void drawBar(int pageOpen) {
  if (sShownAt == 0 || pageOpen) {
    return;
  }
  const Uint64 age = SDL_GetTicks() - sShownAt;
  constexpr Uint64 kShow = 1400, kFade = 300;
  if (age >= kShow + kFade) {
    sShownAt = 0;
    return;
  }
  const float alpha = age < kShow ? 1.f : 1.f - static_cast<float>(age - kShow) / kFade;
  const PortSettings* s = portSettings();
  const int* mute = portSettingsFind("mute");
  const bool muted = mute != nullptr && *mute != 0;
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float u = std::max(1.f, std::min(display.x, display.y) / 480.f); // scale with the window
  const ImVec2 size(220.f * u, 44.f * u);
  const ImVec2 at((display.x - size.x) * 0.5f, display.y * 0.1f);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const auto a = [&](int v) { return static_cast<int>(v * alpha); };
  dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(20, 20, 24, a(200)), 10.f * u);
  const ImU32 fg = IM_COL32(255, 255, 255, a(muted ? 140 : 240));
  drawSpeaker(dl, ImVec2(at.x + 24.f * u, at.y + size.y * 0.5f), 26.f * u, s->volume, muted, fg);
  const float x0 = at.x + 48.f * u, x1 = at.x + size.x - 56.f * u, y = at.y + size.y * 0.5f, th = 6.f * u;
  dl->AddRectFilled(ImVec2(x0, y - th * 0.5f), ImVec2(x1, y + th * 0.5f), IM_COL32(255, 255, 255, a(60)), th);
  if (s->volume > 0) {
    dl->AddRectFilled(ImVec2(x0, y - th * 0.5f), ImVec2(x0 + (x1 - x0) * s->volume / 100.f, y + th * 0.5f), fg, th);
  }
  char text[16];
  std::snprintf(text, sizeof(text), muted ? "Muted" : "%d%%", s->volume);
  ImFont* font = ImGui::GetFont();
  const float fs = 16.f * u;
  const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
  dl->AddText(font, fs, ImVec2(at.x + size.x - 12.f * u - ts.x, y - ts.y * 0.5f), fg, text);
}

// the volume's bindings (Controls > Bindings; F9 down, F10 up, F3 mute): dir
// -1, +1, or 0 for mute; with Shift held, steps of 1 instead of 5
int volumeKey(int dir, int repeat) {
  PortSettings* s = portSettings();
  int* mute = portSettingsDeclare("mute", 0, 0, 1);
  const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1 : 5;
  if (dir != 0) {
    s->volume = std::clamp(s->volume + dir * step, 0, 100);
    if (dir > 0) {
      *mute = 0;
    }
  } else if (!repeat) {
    *mute = !*mute;
  } else {
    return 1;
  }
  portSettingsApply();
  if (!repeat) {
    portSettingsSave();
  } else {
    // held: saved once it's let go would need the key-up; a save every few steps is enough
    static int sRepeats;
    if (++sRepeats % 4 == 0) {
      portSettingsSave();
    }
  }
  sShownAt = SDL_GetTicks();
  if (sShownAt == 0) {
    sShownAt = 1;
  }
  return 1;
}

struct VolumeSettings {
  VolumeSettings() {
    // declared here too, so the launcher's settings have them before the game starts
    portSettingsDeclare("music_volume", 100, 0, 100);
    portSettingsDeclare("sfx_volume", 100, 0, 100);
    portSettingsDeclare("ambience_volume", 100, 0, 100);
    portSettingsDeclare("mute", 0, 0, 1);
    portSettingsDeclare("music_mute", 0, 0, 1);
    portSettingsDeclare("sfx_mute", 0, 0, 1);
    portSettingsDeclare("ambience_mute", 0, 0, 1);
    portSettingsAddSection("Audio", "Volume", nullptr, 0, mixerSection,
                           "sound loud quiet mute master music bgm songs sfx sound effects ambience environment "
                           "background focus f9 f10 f3 keys");
    portSettingsAddOverlay(drawBar, nullptr);
    portPauseAddForm(&kForm);
    portBindingsSetHandler(PORT_BIND_VOLUME_DOWN, [](int, int repeat) { return volumeKey(-1, repeat); });
    portBindingsSetHandler(PORT_BIND_VOLUME_UP, [](int, int repeat) { return volumeKey(1, repeat); });
    portBindingsSetHandler(PORT_BIND_MUTE, [](int, int repeat) { return volumeKey(0, repeat); });
  }
} sVolume;

} // namespace
