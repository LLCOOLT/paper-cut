// The "Modes" tab (port/include/port/modes.h): the boss rush and the endless
// Glitz Pit, started from here while a file is in play; the run's time or
// round over the picture; the best ones kept in <user>/modes.txt.
#include "port/layout.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/settings.h"
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
  // $TTYD_MODE_TEST=rush[:loadout] or pit: started in the field, 30 s into
  // the run (past a $TTYD_MAP warp; tests)
  static bool sTested;
  if (const char* test = std::getenv("TTYD_MODE_TEST"); test != nullptr && !sTested && SDL_GetTicks() > 30000) {
    const char* why = nullptr;
    const bool rush = std::string(test).rfind("rush", 0) == 0;
    const int loadout = rush && test[4] == ':' ? std::atoi(test + 5) : PORT_LOADOUT_MID;
    sTested = portModeStart(rush ? PORT_MODE_BOSS_RUSH : PORT_MODE_ENDLESS_PIT, loadout, &why) != 0;
  }
  const PortModeStatus* s = portModeStatus();
  if (s->mode == PORT_MODE_NONE) {
    sLastFinished = 0;
    return;
  }
  readBests();
  if (s->finished && !sLastFinished) {
    sLastFinished = s->finished;
    if (s->mode == PORT_MODE_BOSS_RUSH) {
      if (s->finished == 1) {
        unsigned long long& best = sBestRush[s->loadout];
        const bool record = best == 0 || s->ms < best;
        best = record ? s->ms : best;
        sMessage = "Boss rush done in " + clock(s->ms) + (record ? ": a new best!" : ".");
      } else {
        sMessage = "Boss rush over at " + std::string(s->next) + " (" + std::to_string(s->step) + " of " +
                   std::to_string(s->steps) + ").";
      }
    } else {
      const int rounds = s->step - 1;
      const bool record = rounds > sBestRound;
      sBestRound = record ? rounds : sBestRound;
      sMessage = "Endless Glitz Pit: " + std::to_string(rounds) + " rounds won" + (record ? ", a new best!" : ".");
    }
    writeBests();
  }
  std::string line;
  if (s->finished) {
    line = sMessage;
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

void bossRushSection(int inGame) {
  static int sLoadout = PORT_LOADOUT_MID;
  readBests();
  ImGui::TextWrapped("Every boss in story order, one fight after another, timed: from Lord Crump and Blooper to "
                     "the Shadow Queen (16 fights). Fleeing a fight starts it again.");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::Combo("Loadout", &sLoadout, kLoadouts, PORT_LOADOUT_COUNT);
  ImGui::TextDisabled(sLoadout == PORT_LOADOUT_OWN  ? "Mario as he is in this file."
                      : sLoadout == PORT_LOADOUT_MID ? "Level 15: 30 HP, 25 FP, 21 BP, Super Boots and Hammer, every "
                                                       "partner, four Star Powers, eight items."
                                                     : "Level 30: 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, "
                                                       "every partner upgraded, all Star Powers, eight items.");
  ImGui::TextDisabled("The loadout is lent for the run: Mario is as he was after it. Your badges stay as they are.");
  const PortModeStatus* s = portModeStatus();
  ImGui::BeginDisabled(!inGame);
  if (s->mode != PORT_MODE_BOSS_RUSH || s->finished) {
    if (ImGui::Button("Start the boss rush")) {
      const char* why = nullptr;
      sMessage = portModeStart(PORT_MODE_BOSS_RUSH, sLoadout, &why) ? "" : why;
    }
  } else if (ImGui::Button("Stop the boss rush")) {
    portModeStop();
  }
  ImGui::EndDisabled();
  if (!inGame) {
    ImGui::TextDisabled("Start it while a file is in play.");
  }
  for (int i = 0; i < PORT_LOADOUT_COUNT; i++) {
    if (sBestRush[i] != 0) {
      ImGui::Text("Best (%s): %s", kLoadouts[i], clock(sBestRush[i]).c_str());
    }
  }
  if (!sMessage.empty() && s->mode != PORT_MODE_ENDLESS_PIT) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

void pitSection(int inGame) {
  readBests();
  ImGui::TextWrapped("The Glitz Pit's teams one after another, each match with a random condition (the "
                     "Pit's own); keep it or the run ends. The enemies get stronger each round.");
  const PortModeStatus* s = portModeStatus();
  const bool open = inGame && portModePitUnlocked();
  ImGui::BeginDisabled(!open);
  if (s->mode != PORT_MODE_ENDLESS_PIT || s->finished) {
    if (ImGui::Button("Start the endless Glitz Pit")) {
      const char* why = nullptr;
      sMessage = portModeStart(PORT_MODE_ENDLESS_PIT, PORT_LOADOUT_OWN, &why) ? "" : why;
    }
  } else if (ImGui::Button("Stop")) {
    portModeStop();
  }
  ImGui::EndDisabled();
  if (inGame && !portModePitUnlocked()) {
    ImGui::TextDisabled("It opens once the story's Glitz Pit is done (chapter 3).");
  }
  if (sBestRound > 0) {
    ImGui::Text("Best: %d rounds", sBestRound);
  }
  if (!sMessage.empty() && s->mode == PORT_MODE_ENDLESS_PIT) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Boss rush", nullptr, PORT_SETTING_CHANGES_PLAY, bossRushSection,
                           "boss rush bosses timer speedrun loadout modes");
    portSettingsAddSection(kTab, "Endless Glitz Pit", nullptr, PORT_SETTING_CHANGES_PLAY, pitSection,
                           "glitz pit endless arena rounds conditions modes");
    portModHostAddDraw(frame, nullptr);
  }
} sRegister;

} // namespace
