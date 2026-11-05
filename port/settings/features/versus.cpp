// The Modes tab's "Versus battle" (port/include/port/versus.h): what it is,
// where its set-up is (the Start menu, in the game's windows), and a start
// with the set-up as it was last; who won the last fight.
#include "port/online.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versus.h"
#include <imgui.h>
#include <string>

namespace {

std::string sMessage;

void versusSection(int inGame) {
  ImGui::TextWrapped("%s", portTr("Mario and his partners against Luigi and his, two players on one screen, in the Glitz "
                                  "Pit's ring. Each side has a level to share between HP, FP and BP, up to three "
                                  "partners to swap between, badges up to its BP and a few items."));
  ImGui::TextDisabled("%s", portTr("Set both sides up in the Start menu: the Settings tab's Versus battle button. Mario's "
                                   "player is the first controller, Luigi's the second."));
  // online: one player's button alone would start it in one game only; the session's start it from the
  // Start menu's page, which both games run in lockstep
  const bool online = portOnlineActive() != 0;
  ImGui::BeginDisabled(!inGame || portVersusOn() || online);
  if (ImGui::Button(portTrLabel("Fight with the last set-up"))) {
    const char* why = nullptr;
    sMessage = portVersusStart(&why) ? "" : portTr(why);
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Climb the ladder"))) {
    const char* why = nullptr;
    sMessage = portVersusLadderStart(&why) ? "" : portTr(why);
  }
  ImGui::EndDisabled();
  if (online) {
    ImGui::TextDisabled("%s", portTr("Online, start it from the Start menu's Versus battle page: the guest plays Luigi's side."));
  } else if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Start it while a file is in play."));
  }
  ImGui::TextDisabled("%s", portTr("The ladder: Mario's side as set up against the computer's Luigi, fight after fight, "
                                   "each Luigi a level higher and harder to beat, until Mario falls."));
  int best = 0, last = -1;
  const int rung = portVersusLadder(&best, &last);
  const int winner = portVersusWinner();
  if (rung > 0) {
    ImGui::Text("%s %d", portTr("On the ladder, rung"), rung);
  } else if (winner >= 0 && !portVersusOn() && last < 0) {
    ImGui::Text("%s", portTr(winner == 0 ? "The last fight: Mario's side won." : "The last fight: Luigi's side won."));
  }
  if (rung == 0 && last >= 0) {
    ImGui::Text("%s %d", portTr("The last ladder, rungs won:"), last);
  }
  if (best > 0) {
    ImGui::Text("%s %d", portTr("The most rungs won:"), best);
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

struct Register {
  Register() {
    portSettingsAddSection("Modes", "Versus battle", nullptr, PORT_SETTING_CHANGES_PLAY, versusSection,
                           "versus vs luigi two players pvp battle fight glitz pit modes");
  }
} sRegister;

} // namespace
