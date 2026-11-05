// The Modes tab's quiz party (port/include/port/party.h, port/src/aurora/quiz_aurora.c): what it is, and its
// start, here and in the Start menu's Settings tab (Modes); it begins once the menu is closed.
#include "port/companion_menu.h"
#include "port/online.h"
#include "port/party.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <imgui.h>

#include <string>

namespace {

constexpr const char* kTab = "Modes";

std::string sMessage;

void section(int inGame) {
  ImGui::TextWrapped("%s", portTr("Trivia for up to four players on one screen, about the game itself: what an item "
                                  "restores or sells for, a badge's BP, the enemies' HP, Attack and Defense. Each "
                                  "player answers with {A}, {B}, {X} or {Y} on their own controller; a right answer is worth "
                                  "100 points and up to 50 more for being quick. Ten questions to a round."));
  ImGui::BeginDisabled(!inGame || portQuizActive() || portOnlineActive()); // (online: the Start menu's form)
  if (ImGui::Button(portTrLabel("Start a quiz party##quiz"))) {
    const char* why = nullptr;
    sMessage = portQuizStart(&why) ? portTr("The quiz begins once this page is closed.") : portTr(why);
  }
  ImGui::EndDisabled();
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Start it while a file is in play."));
  }
  int score = 0;
  const int winner = portQuizLastWinner(&score);
  if (winner == 0) {
    ImGui::Text(portTr("The last quiz: a draw at %d points."), score);
  } else if (winner > 0) {
    ImGui::Text(portTr("The last quiz: player %d won with %d points."), winner, score);
  }
  ImGui::TextDisabled("%s", portTr("The field stands still while it's up. Not during a replay."));
  if (portOnlineActive()) {
    ImGui::TextDisabled("%s", portTr("Online, start it from the Start menu (Settings, Modes), so every game of the "
                                     "session starts it together."));
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("Start a quiz party", "Game trivia for up to four players, once the menu closes.", nullptr, 0,
                        portQuizActive() ? PORT_PAUSE_ROW_DIM : 0);
  return 1;
}

const char* formAct(int id, int dir) {
  if (id != 0 || dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  const char* why = nullptr;
  return portQuizStart(&why) ? "The quiz begins once the menu is closed." : why;
}

const PortPauseForm kForm = {kTab, "Quiz party", formRows, formAct};

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Quiz party", nullptr, 0, section,
                           "quiz party trivia questions players controllers quizmo game show");
    portPauseAddForm(&kForm);
  }
} sRegister;

} // namespace
