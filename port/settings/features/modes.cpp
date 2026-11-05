// The "Modes" tab (port/include/port/modes.h): the boss rush, boss refights and
// the endless Glitz Pit, started from here or from the Start menu's Settings tab (Modes,
// port/include/port/pause_forms.h) while a file is in play; the run's time or round over
// the picture; the best ones kept in <user>/modes.txt.
#include "port/companion_menu.h"
#include "port/layout.h"
#include "port/online.h"
#include "port/pause_forms.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr const char* kTab = "Modes";
constexpr const char* kLoadouts[PORT_LOADOUT_COUNT] = {"As you are", "Mid-game", "Late game"};

// Bests: the boss rush's time per loadout (ms), the endless Pit's round.
unsigned long long sBestRush[PORT_LOADOUT_COUNT];
int sBestRound;
bool sBestsRead;
std::string sMessage; // why a start didn't happen, or the last run's end

std::string bestsFile() { return std::string(portSettingsUserPath()) + "/modes.txt"; }

void readBests() {
  if (sBestsRead) {
    return;
  }
  sBestsRead = true;
  if (FILE* f = std::fopen(bestsFile().c_str(), "r")) {
    char key[32];
    unsigned long long v;
    while (std::fscanf(f, "%31s %llu", key, &v) == 2) {
      for (int i = 0; i < PORT_LOADOUT_COUNT; i++) {
        if (std::string(key) == "rush" + std::to_string(i)) {
          sBestRush[i] = v;
        }
      }
      if (std::string(key) == "pit") {
        sBestRound = int(v);
      }
    }
    std::fclose(f);
  }
}

void writeBests() {
  if (FILE* f = std::fopen(bestsFile().c_str(), "w")) {
    for (int i = 0; i < PORT_LOADOUT_COUNT; i++) {
      std::fprintf(f, "rush%d %llu\n", i, sBestRush[i]);
    }
    std::fprintf(f, "pit %d\n", sBestRound);
    std::fclose(f);
  }
}

std::string clock(unsigned long long ms) {
  char text[32];
  std::snprintf(text, sizeof(text), "%llu:%02llu.%01llu", ms / 60000, ms / 1000 % 60, ms / 100 % 10);
  return text;
}

// The game's text (Latin-1 on the US build, with <tags>) as UTF-8 for ImGui.
std::string plain(const char* text) {
  std::string out;
  bool tag = false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); p != nullptr && *p != '\0'; p++) {
    if (*p == '<') {
      tag = true;
    } else if (*p == '>') {
      tag = false;
    } else if (!tag) {
      if (*p == '\n') {
        out += ' ';
      } else if (*p < 0x80) {
        out += char(*p);
      } else if (PORT_GAME_ID[3] != 'J') {
        out += char(0xC0 | (*p >> 6));
        out += char(0x80 | (*p & 0x3F));
      }
    }
  }
  return out;
}

// Each frame of play: the run's end recorded; its state over the picture.
void frame(void*) {
  static int sLastFinished;
  // $TTYD_MODE_TEST=rush[:loadout], pit or refight:<boss>[:loadout]: started
  // in the field, 30 s into the run (past a $TTYD_MAP warp; tests)
  static bool sTested;
  if (const char* test = std::getenv("TTYD_MODE_TEST"); test != nullptr && !sTested && SDL_GetTicks() > 30000) {
    const char* why = nullptr;
    const std::string t(test);
    if (t.rfind("refight:", 0) == 0) {
      const size_t colon = t.find(':', 8);
      const int loadout = colon != std::string::npos ? std::atoi(t.c_str() + colon + 1) : PORT_LOADOUT_OWN;
      sTested = portModeRefight(std::atoi(t.c_str() + 8), loadout, &why) != 0;
    } else {
      const bool rush = t.rfind("rush", 0) == 0;
      const int loadout = rush && test[4] == ':' ? std::atoi(test + 5) : PORT_LOADOUT_MID;
      sTested = portModeStart(rush ? PORT_MODE_BOSS_RUSH : PORT_MODE_ENDLESS_PIT, loadout, &why) != 0;
    }
    if (!sTested && why != nullptr) {
      std::printf("[modes] test not started: %s\n", why);
    }
  }
  const PortModeStatus* s = portModeStatus();
  if (s->mode == PORT_MODE_NONE) {
    sLastFinished = 0;
    return;
  }
  readBests();
  if (s->finished && !sLastFinished) {
    sLastFinished = s->finished;
    if (s->mode == PORT_MODE_REFIGHT) {
      sMessage = std::string(s->next) + (s->finished == 1 ? ": beaten again in " + clock(s->ms) + "." : ": left.");
    } else if (s->mode == PORT_MODE_BOSS_RUSH) {
      if (s->finished == 1) {
        unsigned long long& best = sBestRush[s->loadout];
        const bool record = !portReplayPlaying() && (best == 0 || s->ms < best); // (a replay's run isn't yours)
        best = record ? s->ms : best;
        sMessage = "Boss rush done in " + clock(s->ms) + (record ? ": a new best!" : ".");
      } else {
        sMessage = "Boss rush over at " + std::string(s->next) + " (" + std::to_string(s->step) + " of " +
                   std::to_string(s->steps) + ").";
      }
    } else {
      const int rounds = s->step - 1;
      const bool record = !portReplayPlaying() && rounds > sBestRound;
      sBestRound = record ? rounds : sBestRound;
      sMessage = "Endless Glitz Pit: " + std::to_string(rounds) + " rounds won" + (record ? ", a new best!" : ".");
    }
    if (s->mode != PORT_MODE_REFIGHT) {
      writeBests();
    }
  }
  std::string line;
  if (s->finished) {
    line = sMessage;
  } else if (s->mode == PORT_MODE_REFIGHT) {
    line = "Refight: " + std::string(s->next) + "   " + clock(s->ms);
  } else if (s->mode == PORT_MODE_BOSS_RUSH) {
    line = "Boss " + std::to_string(s->step) + "/" + std::to_string(s->steps) + ": " + s->next + "   " + clock(s->ms);
  } else {
    line = "Round " + std::to_string(s->step) + ": " + s->next + "   " + clock(s->ms);
  }
  // the box as wide as its text (a long boss name or condition), right-aligned in the 640 x 480 picture
  const bool cond = s->condition != nullptr && !s->finished;
  const std::string condText = cond ? plain(s->condition) : std::string();
  float w = ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line.c_str()).x;
  if (cond) {
    const float cw = ImGui::GetFont()->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, condText.c_str()).x;
    w = cw > w ? cw : w;
  }
  // the text is scaled by the picture's height, x by its width (a picture wider or narrower than 4:3)
  PortLayout layout;
  if (portLayoutGet(&layout) && layout.picW > 0.0f && layout.picH > 0.0f) {
    w *= (layout.picH / 480.0f) / (layout.picW / 640.0f);
  }
  w += 16.0f;
  const float x = w < 624.0f ? 632.0f - w : 8.0f;
  portModHostDrawRect(x, 8.0f, w, cond ? 44.0f : 22.0f, 0x00000090u);
  portModHostDrawText(x + 6.0f, 10.0f, 14.0f, 0xFFFFFFFFu, line.c_str());
  if (cond) {
    portModHostDrawText(x + 6.0f, 30.0f, 13.0f, 0xFFE080FFu, condText.c_str());
  }
}

int sRushLoadout = PORT_LOADOUT_MID;   // the boss rush's loadout: the page's and the Start menu's
int sRefightLoadout = PORT_LOADOUT_OWN; // a refight's

void bossRushSection(int inGame) {
  int& sLoadout = sRushLoadout;
  readBests();
  ImGui::TextWrapped("%s", portTr("Every boss in story order, one fight after another, timed: from Lord Crump and Blooper to "
                     "the Shadow Queen (16 fights). Fleeing a fight starts it again."));
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::Combo(portTrLabel("Loadout"), &sLoadout, kLoadouts, PORT_LOADOUT_COUNT);
  ImGui::TextDisabled("%s", portTr(sLoadout == PORT_LOADOUT_OWN  ? "Mario as he is in this file."
                                   : sLoadout == PORT_LOADOUT_MID ? "Level 15: 30 HP, 25 FP, 21 BP, Super Boots and Hammer, every "
                                                                    "partner, four Star Powers, eight items."
                                                                  : "Level 30: 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, "
                                                                    "every partner upgraded, all Star Powers, eight items."));
  ImGui::TextDisabled("%s", portTr("The loadout is lent for the run: Mario is as he was after it. Your badges stay as they are."));
  const PortModeStatus* s = portModeStatus();
  ImGui::BeginDisabled(!inGame);
  if (s->mode != PORT_MODE_BOSS_RUSH || s->finished) {
    if (ImGui::Button(portTrLabel("Start the boss rush"))) {
      const char* why = nullptr;
      sMessage = portModeStart(PORT_MODE_BOSS_RUSH, sLoadout, &why) ? "" : why;
    }
  } else if (ImGui::Button(portTrLabel("Stop the boss rush"))) {
    portModeStop();
  }
  ImGui::EndDisabled();
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Start it while a file is in play."));
  }
  for (int i = 0; i < PORT_LOADOUT_COUNT; i++) {
    if (sBestRush[i] != 0) {
      ImGui::Text(portTr("Best (%s): %s"), kLoadouts[i], clock(sBestRush[i]).c_str());
    }
  }
  if (!sMessage.empty() && s->mode == PORT_MODE_BOSS_RUSH) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

void pitSection(int inGame) {
  readBests();
  ImGui::TextWrapped("%s", portTr("The Glitz Pit's teams one after another, each match with a random condition (the "
                     "Pit's own); keep it or the run ends. The enemies get stronger each round."));
  const PortModeStatus* s = portModeStatus();
  const bool open = inGame && portModePitUnlocked();
  ImGui::BeginDisabled(!open);
  if (s->mode != PORT_MODE_ENDLESS_PIT || s->finished) {
    if (ImGui::Button(portTrLabel("Start the endless Glitz Pit"))) {
      const char* why = nullptr;
      sMessage = portModeStart(PORT_MODE_ENDLESS_PIT, PORT_LOADOUT_OWN, &why) ? "" : why;
    }
  } else if (ImGui::Button(portTrLabel("Stop"))) {
    portModeStop();
  }
  ImGui::EndDisabled();
  if (inGame && !portModePitUnlocked()) {
    ImGui::TextDisabled("%s", portTr("It opens once the story's Glitz Pit is done (chapter 3)."));
  }
  if (sBestRound > 0) {
    ImGui::Text(portTr("Best: %d rounds"), sBestRound);
  }
  if (!sMessage.empty() && s->mode == PORT_MODE_ENDLESS_PIT) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

void refightSection(int inGame) {
  int& sLoadout = sRefightLoadout;
  ImGui::TextWrapped("%s", portTr("Any boss you've beaten in this file's story, fought again: then back to the room you were "
                     "in. Losing is a Game Over, as in the story (save first, or turn on Retry on Game Over)."));
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Choose one while a file is in play."));
    return;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::Combo(portTrLabel("Loadout##refight"), &sLoadout, kLoadouts, PORT_LOADOUT_COUNT);
  const PortModeStatus* s = portModeStatus();
  const bool running = s->mode != PORT_MODE_NONE && !s->finished;
  int beaten = 0;
  ImGui::BeginDisabled(running);
  for (int i = 0; i < portModeBossCount(); i++) {
    const bool open = portModeBossBeaten(i) != 0;
    beaten += open ? 1 : 0;
    ImGui::PushID(i);
    ImGui::BeginDisabled(!open);
    if (ImGui::Button(portTrLabel(open ? "Fight##fight" : "Not yet##fight"), ImVec2(ImGui::GetFontSize() * 5.0f, 0.0f))) {
      const char* why = nullptr;
      sMessage = portModeRefight(i, sLoadout, &why) ? "" : why;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (open) {
      ImGui::TextUnformatted(portModeBossName(i));
    } else {
      ImGui::TextDisabled("%s", portModeBossName(i));
    }
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  if (beaten == 0) {
    ImGui::TextDisabled("%s", portTr("No boss beaten yet on this file."));
  }
  if (s->mode == PORT_MODE_REFIGHT && !sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// --- the Start menu's forms (port/include/port/pause_forms.h): the same starts as the page's ---

// Online and in replays the page holds these (what changes play stays as the run began): the Start
// menu's rows are dimmed alike
constexpr const char* kHeld = "Not during online play or a replay.";
constexpr const char* kSecondScreen = "Not on the second screen: use the game's Start menu.";

bool held() { return portOnlineActive() || portReplayActive(); }

const char* const kLoadoutNotes[PORT_LOADOUT_COUNT] = {
    "Mario as he is in this file.",
    "Level 15: 30 HP, 25 FP, 21 BP, Super Boots and Hammer, every partner, four Star Powers, eight items.",
    "Level 30: 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, every partner upgraded, all Star Powers, eight items.",
};

// a loadout row: left and right (and A) change it
PortPauseRow loadoutRow(int loadout, int id) {
  return portPauseRow("Loadout", kLoadoutNotes[loadout], kLoadouts[loadout], id, held() ? PORT_PAUSE_ROW_DIM : 0);
}

void stepLoadout(int& loadout, int dir) {
  loadout = (loadout + (dir < 0 ? PORT_LOADOUT_COUNT - 1 : 1)) % PORT_LOADOUT_COUNT;
}

enum { kRowLoadout = 100, kRowStart = 101, kRowStop = 102, kRowBoss = 103, kRowFight = 104 };

bool running(int mode) {
  const PortModeStatus* s = portModeStatus();
  return s->mode == mode && !s->finished;
}

bool anyRunning() {
  const PortModeStatus* s = portModeStatus();
  return s->mode != PORT_MODE_NONE && !s->finished;
}

int rushRows(PortPauseRow* out, int max) {
  if (max < 2) {
    return 0;
  }
  readBests();
  out[0] = loadoutRow(sRushLoadout, kRowLoadout);
  if (running(PORT_MODE_BOSS_RUSH)) {
    out[1] = portPauseRow("Stop the boss rush", "The loadout is lent for the run: Mario is as he was after it. Your "
                          "badges stay as they are.", nullptr, kRowStop, PORT_PAUSE_ROW_CONFIRM);
    return 2;
  }
  PortPauseRow r = portPauseRow("Start the boss rush", "Every boss in story order, one fight after another, timed: "
                                "from Lord Crump and Blooper to the Shadow Queen (16 fights). Fleeing a fight starts "
                                "it again.", nullptr, kRowStart,
                                held() || anyRunning() ? PORT_PAUSE_ROW_DIM : PORT_PAUSE_ROW_CONFIRM);
  if (sBestRush[sRushLoadout] != 0) { // (this computer's best: only what's drawn differs between games)
    std::snprintf(r.value, sizeof(r.value), "%s", clock(sBestRush[sRushLoadout]).c_str());
  }
  out[1] = r;
  return 2;
}

const char* rushAct(int id, int dir) {
  if (held() && id != kRowStop) {
    return dir == 0 ? kHeld : nullptr;
  }
  if (id == kRowLoadout) {
    stepLoadout(sRushLoadout, dir);
    return nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  if (id == kRowStop) {
    portModeStop();
    return "The boss rush is over.";
  }
  if (anyRunning()) {
    return "Not while another mode runs.";
  }
  const char* why = nullptr;
  return portModeStart(PORT_MODE_BOSS_RUSH, sRushLoadout, &why) ? "The boss rush starts once the menu is closed."
                                                                : why;
}

const PortPauseForm kRushForm = {kTab, "Boss rush", rushRows, rushAct};

int sRefightBoss; // the boss chosen in the Start menu

int refightRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 3) {
    return 0;
  }
  const int count = portModeBossCount();
  sRefightBoss = count > 0 ? (sRefightBoss % count + count) % count : 0;
  const bool open = portModeBossBeaten(sRefightBoss) != 0;
  out[n++] = loadoutRow(sRefightLoadout, kRowLoadout);
  PortPauseRow b = portPauseRow("Boss", open ? "Left and right choose the boss." : "Beat this boss in the story first.",
                                nullptr, kRowBoss);
  std::snprintf(b.value, sizeof(b.value), "%s%s", portTrGame(portModeBossName(sRefightBoss)),
                open ? "" : portTrGame(" (not yet)"));
  out[n++] = b;
  out[n++] = portPauseRow("Fight", "Fought again, then back to this room. Losing is a Game Over, as in the story.",
                          nullptr, kRowFight,
                          open && !anyRunning() && !held() ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
  return n;
}

const char* refightAct(int id, int dir) {
  if (held()) {
    return dir == 0 ? kHeld : nullptr;
  }
  if (id == kRowLoadout) {
    stepLoadout(sRefightLoadout, dir);
    return nullptr;
  }
  if (id == kRowBoss) {
    sRefightBoss += dir < 0 ? -1 : 1; // (wrapped by the rows)
    return nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  if (anyRunning()) {
    return "Not while another mode runs.";
  }
  const char* why = nullptr;
  return portModeRefight(sRefightBoss, sRefightLoadout, &why) ? "The fight starts once the menu is closed." : why;
}

const PortPauseForm kRefightForm = {kTab, "Boss refights", refightRows, refightAct};

int pitRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  readBests();
  if (running(PORT_MODE_ENDLESS_PIT)) {
    out[0] = portPauseRow("Stop the endless Glitz Pit", "Mario stays where he is.", nullptr, kRowStop,
                          PORT_PAUSE_ROW_CONFIRM);
    return 1;
  }
  const bool open = portModePitUnlocked() != 0;
  PortPauseRow r = portPauseRow("Start the endless Glitz Pit",
                                open ? "The Glitz Pit's teams one after another, each match with a random condition "
                                       "(the Pit's own); keep it or the run ends. The enemies get stronger each round."
                                     : "It opens once the story's Glitz Pit is done (chapter 3).",
                                nullptr, kRowStart,
                                open && !held() && !anyRunning() ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
  if (sBestRound > 0) {
    std::snprintf(r.value, sizeof(r.value), portTrGame("Best: %d rounds"), sBestRound);
  }
  out[0] = r;
  return 1;
}

const char* pitAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (held() && id != kRowStop) {
    return kHeld;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  if (id == kRowStop) {
    portModeStop();
    return "The endless Glitz Pit is over.";
  }
  if (anyRunning()) {
    return "Not while another mode runs.";
  }
  const char* why = nullptr;
  return portModeStart(PORT_MODE_ENDLESS_PIT, PORT_LOADOUT_OWN, &why) ? "The first match starts once the menu is closed."
                                                                      : why;
}

const PortPauseForm kPitForm = {kTab, "Endless Glitz Pit", pitRows, pitAct};

struct Register {
  Register() {
    portPauseAddForm(&kRushForm);
    portPauseAddForm(&kRefightForm);
    portPauseAddForm(&kPitForm);
    portSettingsAddSection(kTab, "Boss rush", nullptr, PORT_SETTING_CHANGES_PLAY, bossRushSection,
                           "boss rush bosses timer speedrun loadout modes");
    portSettingsAddSection(kTab, "Boss refights", nullptr, PORT_SETTING_CHANGES_PLAY, refightSection,
                           "boss refight rematch replay fight again bosses modes");
    portSettingsAddSection(kTab, "Endless Glitz Pit", nullptr, PORT_SETTING_CHANGES_PLAY, pitSection,
                           "glitz pit endless arena rounds conditions modes");
    portModHostAddDraw(frame, nullptr);
  }
} sRegister;

} // namespace
