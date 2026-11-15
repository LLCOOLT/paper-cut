// Multiplayer > Connection icon and Online diagnostics (port/include/port/online_status.h,
// port/src/common/online_status_pc.c): the corner icon for an online game's connection (Always, When it's bad:
// the default, fair or poor or reconnecting; Never), and Copy diagnostics, the same short text for a report as
// the Multiplayer page's (the build, the protocol, the connection, the join's steps, the last online lines;
// names and the code's secret half left out) to the clipboard. Nothing is sent anywhere.
#include "port/online_status.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h"

#include <SDL3/SDL_clipboard.h>
#include <imgui.h>

namespace {

const char* kChoices[] = {"Always", "When it's bad", "Never"};
const char* sAnswer; // the last copy's answer (English)

void section(int inGame) {
  (void)inGame;
  if (ImGui::Button(portTrLabel("Copy diagnostics##online_diag"))) {
    static char text[12288];
    const size_t n = portOnlineDiagnostics(text, sizeof(text));
    sAnswer = n > 0 && SDL_SetClipboardText(text) ? "Copied: paste it into your report. Nothing was sent anywhere."
                                                  : "Couldn't copy it.";
  }
  if (sAnswer != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr(sAnswer));
  }
}

const bool sAdded = [] {
  portSettingsDeclare("online_quality_icon", 1, 0, 2);
  portSettingsAddChoice("Multiplayer", "Connection icon",
                        "Online, a small icon in the field and in battle: three bars for your connection (green good, "
                        "yellow fair, red poor) and Reconnecting... while it's down. When it's bad: only then.",
                        0, "online_quality_icon", kChoices, 3,
                        "online connection icon ping lag latency quality bars signal reconnecting network");
  portSettingsAddSection("Multiplayer", "Online diagnostics",
                         "Copy diagnostics: a short text about your online connection for a report (your build, the "
                         "connection, the join's steps and the last online lines; no names, no room code). It only "
                         "goes to the clipboard.",
                         0, section, "online diagnostics copy report log bug black screen join connection relay");
  return true;
}();

} // namespace
