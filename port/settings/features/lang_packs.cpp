// System > Language packs (port/include/port/tr.h, docs/LANGUAGE_PACKS.md):
// the packs read from the mods folder, what each does and what is wrong with
// it, Reload, and the template a translator starts from.
#include "port/restart.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <cstdio>
#include <string>

namespace {

void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

const ImVec4 kAmber(1.0f, 0.75f, 0.3f, 1.0f);

constexpr const char* kBuiltIn[][2] = {{"en", "English"}, {"de", "Deutsch"}, {"fr", "Français"},
                                       {"es", "Español"}, {"it", "Italiano"}, {"ja", "日本語"}};

std::string sWritten; // what the template button did

void section(int) {
  note(portTr("A language pack translates Paper Cut's own menus: a folder in the mods folder holding a lang folder "
              "of text files. It adds a language to Menu language, or changes the texts of one that is there."));
  if (ImGui::Button(portTrLabel("Reload language packs"))) {
    portTrReloadPacks();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Make a language pack template"))) {
    int added = 0;
    const char* path = portTrWriteTemplate(&added);
    char line[1200];
    if (path == nullptr) {
      std::snprintf(line, sizeof(line), "%s", portTr("The template could not be written."));
    } else if (added == 0) {
      std::snprintf(line, sizeof(line), portTr("%s already has every text."), path);
    } else {
      std::snprintf(line, sizeof(line), portTr("Written: %s, with %d texts to translate."), path, added);
    }
    sWritten = line;
    portTrReloadPacks();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Open the mods folder##langpacks"))) {
    const std::string dir = std::string(portSettingsUserPath()) + "/mods";
    SDL_CreateDirectory(dir.c_str());
    SDL_OpenURL(("file:///" + dir).c_str());
  }
#endif
  if (!sWritten.empty()) {
    note(sWritten.c_str());
  }
  if (portTrPacksNeedRestart()) {
    note(portTr("Letters the menus' fonts didn't have yet show after a restart."));
    portRestartButton(); // (port/include/port/restart.h)
  }
  const int n = portTrPackCount();
  if (n == 0) {
    note(portTr("No language packs yet."));
  }
  for (int i = 0; i < n; i++) {
    const PortTrPackInfo* p = portTrPack(i);
    std::string what;
    if (p->code[0] == '\0' || (p->problems[0] != '\0' && p->strings == 0)) {
      what = p->name;
    } else if (p->adds) {
      what = std::string(p->name) + " (" + p->code + "): " + portTr("a language of its own");
    } else {
      const char* lang = p->code;
      for (const auto& b : kBuiltIn) {
        if (std::string(b[0]) == p->code) {
          lang = b[1];
        }
      }
      char line[256];
      std::snprintf(line, sizeof(line), portTr("changes %s"), lang);
      what = std::string(p->name) + " (" + p->code + "): " + line;
    }
    ImGui::BulletText("%s", what.c_str());
    ImGui::Indent();
    char line[256];
    std::snprintf(line, sizeof(line), portTr("mods/%s: %d texts translated, %d not translated yet."), p->folder,
                  p->strings, p->untranslated);
    note(line);
    if (p->problems[0] != '\0') {
      ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
      ImGui::TextWrapped("%s", p->problems);
      ImGui::PopStyleColor();
    }
    ImGui::Unindent();
  }
}

struct Register {
  Register() {
    portSettingsAddSection("Language", "Language packs", nullptr, 0, section,
                           "language pack translate translation template mod font reload");
  }
} sRegister;

} // namespace
