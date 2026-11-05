// The "what's new" page (port/include/port/whats_new.h): this version's
// release notes as a compact list. Here: its row in the Start menu's
// Settings tab (General), which opens its page in the game's windows
// (port/src/aurora/whats_new_aurora.c), and the same list on the settings
// page's General tab.
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/whats_new.h"

#include <imgui.h>

#include <cstdio>

namespace {

void section(int inGame) {
  (void)inGame;
  char head[96];
  std::snprintf(head, sizeof(head), portTr("What's new in Paper Cut %s"), portWhatsNewVersion);
  ImGui::TextUnformatted(head);
  if (portWhatsNewCount == 0) {
    ImGui::TextDisabled("%s", portTr("No notes for this version."));
    return;
  }
  for (int i = 0; i < portWhatsNewCount; i++) {
    const PortWhatsNewLine& l = portWhatsNewLines[i];
    if (l.kind == PORT_WHATS_NEW_ITEM) {
      ImGui::Bullet();
      ImGui::SameLine();
      ImGui::TextWrapped("%s", l.utf8);
    } else {
      ImGui::Spacing();
      ImGui::TextDisabled("%s", l.utf8);
    }
  }
}

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = PortPauseRow{"What's new", "This version's new features and fixes, from its release notes.", {}, {},
                        nullptr, 0, 0};
  std::snprintf(out[0].value, sizeof(out[0].value), "%s", portWhatsNewVersion);
  return 1;
}

const char* formAct(int id, int dir) {
  (void)id;
  if (dir == 0) {
    portPauseOpenPage(&gPortWhatsNewPage, 0);
  }
  return nullptr;
}

const PortPauseForm kForm = {"General", "What's new", formRows, formAct};

const bool sAdded = [] {
  portSettingsAddSection("General", "What's new", "This version's new features and fixes, from its release notes.",
                         0, section, "what's new whats new changes changelog release notes version update");
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace
