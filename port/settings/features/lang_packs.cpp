// Language > Language packs (port/include/port/tr.h, docs/LANGUAGE_PACKS.md):
// the packs read from the mods folder, what each does and what is wrong with
// it, Reload, and the template a translator starts from.
#include "port/pause_forms.h"
#include "port/offscreen.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <cstdio>
#include <string>

using portui::hint;

namespace {

const ImVec4 kAmber(1.0f, 0.75f, 0.3f, 1.0f);

constexpr const char* kBuiltIn[][2] = {{"en", "English"}, {"de", "Deutsch"}, {"fr", "Français"},
                                       {"es", "Español"}, {"it", "Italiano"}, {"ja", "日本語"}};

std::string sWritten; // what the template button did

void section(int) {
  hint("A language pack translates Paper Cut's own menus: a folder in the mods folder holding a lang folder "
       "of text files. It adds a language to Menu language, or changes the texts of one that is there.");
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
    char why[1200];
    if (!portOpenFolder(dir.c_str())) {
      sWritten = portOpenError(why, sizeof(why), dir.c_str(), true);
    }
  }
#endif
  if (!sWritten.empty()) {
    hint(sWritten.c_str());
  }
  if (portTrPacksNeedRestart()) {
    hint("New letters for the menus' fonts show after a restart.");
    portRestartButton(); // (port/include/port/restart.h)
  }
  const int n = portTrPackCount();
  if (n == 0) {
    hint("No language packs yet.");
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
    hint(line);
    if (p->problems[0] != '\0') {
      ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
      ImGui::TextWrapped("%s", p->problems);
      ImGui::PopStyleColor();
    }
    ImGui::Unindent();
  }
}

// The Start menu's form (port/include/port/pause_forms.h): Reload, the template, the folder, then each pack
enum { kReload = 1000, kTemplate, kFolder, kRestart };

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  out[n++] = portPauseRow("Reload language packs", "Reads the mods folder's language packs again.", nullptr, kReload);
  out[n++] = portPauseRow("Make a language pack template",
                          "Writes a pack for a translator to start from in the mods folder: each text in English, with "
                          "room for its translation.",
                          nullptr, kTemplate);
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  out[n++] = portPauseRow("Open the mods folder", "Shows the mods folder; each pack is a folder in it.",
                          nullptr, kFolder);
#endif
  if (portTrPacksNeedRestart() && portRestartSupported()) {
    out[n++] = portPauseRow("Restart for the new letters",
                            "New letters for the menus' fonts show after a restart.", nullptr, kRestart,
                            PORT_PAUSE_ROW_CONFIRM);
  }
  const int count = portTrPackCount();
  if (count == 0) {
    out[n++] = portPauseRow("No language packs yet.",
                            "A language pack translates Paper Cut's own menus: a folder in the mods folder holding a "
                            "lang folder of text files.",
                            nullptr, -1, PORT_PAUSE_ROW_DIM);
  }
  for (int i = 0; i < count && n < max; i++) {
    const PortTrPackInfo* p = portTrPack(i);
    PortPauseRow r = portPauseRow(nullptr,
                                  p->problems[0] != '\0'
                                      ? "Some of its lines couldn't be read: Make a language pack template shows the "
                                        "format."
                                      : "A language pack in the mods folder: its texts translated, and those not yet.",
                                  nullptr, i, p->problems[0] != '\0' ? PORT_PAUSE_ROW_DIM : 0);
    char named[sizeof(r.text)]; // (the pack's name as pack.txt has it, UTF-8, in the game's encoding)
    std::snprintf(named, sizeof(named), "%s%s%s%s", p->name, p->code[0] != '\0' ? " (" : "", p->code,
                  p->code[0] != '\0' ? ")" : "");
    portTextForGame(named, r.text, sizeof(r.text));
    std::snprintf(r.value, sizeof(r.value), "%d/%d", p->strings, p->strings + p->untranslated);
    out[n++] = r;
  }
  return n;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  switch (id) {
  case kReload:
    portTrReloadPacks();
    return "Language packs read again.";
  case kTemplate: {
    int added = 0;
    const char* path = portTrWriteTemplate(&added);
    portTrReloadPacks();
    return path == nullptr ? "The template could not be written."
           : added == 0    ? "The template already has every text."
                           : "Written to the mods folder: the texts to translate are in it.";
  }
  case kFolder: {
    const std::string folder = std::string(portSettingsUserPath()) + "/mods";
    SDL_CreateDirectory(folder.c_str());
    return portOpenFolder(folder.c_str()) ? nullptr
                                          : "Couldn't open the folder: it is the mods folder in Paper Cut's user folder.";
  }
  case kRestart:
    portRestartRequest(1);
    return nullptr;
  default:
    return nullptr;
  }
}

const PortPauseForm kForm = {"Language", "Language packs", formRows, formAct, PORT_PAUSE_FORM_OWN};

struct Register {
  Register() {
    portPauseAddForm(&kForm);
    portSettingsAddSection("Language", "Language packs", nullptr, 0, section,
                           "language pack translate translation template mod font reload");
  }
} sRegister;

} // namespace
