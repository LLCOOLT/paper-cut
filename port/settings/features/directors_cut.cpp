// Director's Cut (Modes > Director's Cut; port/include/port/directors_cut.h):
// the switch, and the Journal's page on each restored thing, on the settings
// page and in the Start menu.
#include "port/directors_cut.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <cstdio>

namespace {

void section(int inGame) {
  (void)inGame;
  for (int i = 0; i < portDirectorsCutPages(); i++) {
    if (ImGui::TreeNode(portTr(portDirectorsCutPageTitle(i)))) {
      ImGui::TextWrapped("%s", portTr(portDirectorsCutPageText(i)));
      ImGui::TreePop();
    }
  }
}

int rows(PortPauseRow* out, int max) {
  int n = 0;
  out[n++] = PortPauseRow{"Director's Cut journal", nullptr, {}, {}, nullptr, -1, PORT_PAUSE_ROW_HEADING};
  for (int i = 0; i < portDirectorsCutPages() && n < max; i++) {
    out[n++] = PortPauseRow{portDirectorsCutPageTitle(i), portDirectorsCutPageText(i), {}, {}, nullptr, i,
                            portDirectorsCut() ? 0u : unsigned(PORT_PAUSE_ROW_DIM)};
  }
  return n;
}

const char* act(int id, int dir) {
  (void)dir;
  return id >= 0 && id < portDirectorsCutPages() ? portDirectorsCutPageText(id) : nullptr;
}

const PortPauseForm kForm = {"Modes", "Director's Cut journal", rows, act};

const bool sAdded = [] {
  portDirectorsCut(); // declares the setting
  portSettingsAddToggle("Modes", "Director's Cut",
                        "Content the game's code still has but never uses, put back: seven cut badges sold by "
                        "Dazzle and Boggly Woods' unused enemy groups. Each has a page in the Director's Cut "
                        "journal below.",
                        PORT_SETTING_CHANGES_PLAY | PORT_SETTING_CHANGES_SAVES, "directors_cut",
                        "director's cut directors restored unused cut content badges enemies mega jump super charge "
                        "triple dip");
  portSettingsAddSection("Modes", "Director's Cut journal", nullptr, 0, section,
                         "director's cut journal restored unused cut content");
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace
