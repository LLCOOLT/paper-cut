// Small options' controls (port/include/port/toggles.h): level sync on the
// Gameplay tab, the fun toggles and chaos mode on the Modes tab, page-turn
// transitions on Display, battery saver on Graphics; and chaos mode's notice
// over the picture (Dear ImGui, no game font: no random numbers drawn).
#include "port/overlay_place.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "port/tr.h"
#include <imgui.h>
#include <cfloat>
#include <cstdio>

namespace {

constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;

// chaos mode: the effect just started, at the top of the screen
void chaosNotice(int pageOpen) {
  int ms = 0;
  const char* name = portChaosEffect(&ms);
  if (name == nullptr || pageOpen || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  char text[128];
  std::snprintf(text, sizeof(text), "%s %s", portTr("Chaos:"), portTr(name));
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float fs = ImGui::GetFontSize() * 1.4f;
  const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImVec2 at((display.x - size.x) * 0.5f, portOverlayTopClear(display.y * 0.08f, display.x, display.y, 16.f)); // (under the HUD's rows)
  const int alpha = ms < 500 ? ms * 255 / 500 : 255;
  dl->AddRectFilled(ImVec2(at.x - 14.f, at.y - 8.f), ImVec2(at.x + size.x + 14.f, at.y + size.y + 8.f),
                    IM_COL32(120, 20, 140, alpha * 200 / 255), 10.f);
  dl->AddText(ImGui::GetFont(), fs, at, IM_COL32(255, 240, 120, alpha), text);
}

const bool sAdded = [] {
  // level sync
  portSettingsDeclare("level_sync", 0, 0, 2);
  static const char* const kLevelSync[] = {"Off", "Post-game and New Game+", "Always"};
  portSettingsAddChoice("Gameplay", "Enemies match your level",
                        "Enemies weaker than Mario are raised to his level, so fights stay a challenge and pay full "
                        "Star Points. Post-game and New Game+: only after the credits or when Mario is far ahead of "
                        "the chapter.",
                        kPlay, "level_sync", kLevelSync, 3,
                        "level sync scaling enemies scale post-game new game plus ng+ difficulty");

  // the fun toggles
  portFunDeclare();
  portSettingsAddToggle("Modes", "Big heads", "Everyone's head drawn nearly twice as big.", 0, "fun_big_heads",
                        "fun silly big heads bobblehead");
  portSettingsAddToggle("Modes", "Cool Mode", "Sunglasses on everyone: Mario, his friends, every enemy and boss.", 0,
                        "fun_cool", "fun silly cool mode sunglasses shades glasses");
  portSettingsAddToggle("Modes", "Moon gravity", "Mario jumps and walks as on the Moon, everywhere.", kPlay,
                        "fun_moon_gravity", "fun silly moon gravity low float jump");
  portSettingsAddToggle("Modes", "Paper-thin from every side",
                        "Everyone a sliver of paper, even seen from the front.", 0, "fun_paper_thin",
                        "fun silly paper thin flat sliver");
  portSettingsAddToggle("Modes", "Luigi's voice for Mario", "Mario's steps, jumps and landings sound as Luigi's.", 0,
                        "fun_luigi_voice", "fun silly luigi voice sound mario");
  portSettingsAddToggle("Modes", "Spinning paper", "Everyone turns round and round.", 0, "fun_spin",
                        "fun silly spin turn rotate");

  // chaos mode
  portSettingsDeclare("chaos_mode", 0, 0, 1);
  portSettingsAddToggle("Modes", "Chaos mode",
                        "Every 30 seconds something happens: giant or tiny Mario, flipped controls, the camera "
                        "upside down, coin rain, a Bob-omb, and in battle the crowd's tricks. Online, the same for "
                        "everyone (the host's setting).",
                        kPlay, "chaos_mode", "chaos random effect crowd control silly fun");
  portSettingsAddOverlay(chaosNotice, nullptr);

  // page-turn transitions
  portSettingsDeclare("page_turn", 0, 0, 3);
  static const char* const kPages[] = {"Fade (original)", "Pop-up book page", "Page turned over", "Folded paper"};
  portSettingsAddChoice("Display", "Room transitions",
                        "Doors, pipes and room changes turn the old room away like a page, with the game's own "
                        "page animations, instead of fading to black.",
                        kPlay, "page_turn", kPages, 4, "page turn transition fade door pipe book fold paper wipe");

  // battery saver
  portSettingsDeclare("battery_saver", 0, 0, 2);
  static const char* const kBattery[] = {"Off", "On battery", "Always"};
  portSettingsAddChoice("Graphics", "Battery saver",
                        "Saves battery: 30 frames a second at half resolution, while the game keeps its full speed. "
                        "Not while playing online.",
                        0, "battery_saver", kBattery, 3, "battery saver power laptop steam deck phone 30 fps");

  // keep game speed when the GPU falls behind (vi_aurora.c)
  portSettingsDeclare("keep_game_speed", 1, 0, 1);
  portSettingsAddToggle("Graphics", "Keep game speed when graphics lag",
                        "When the graphics card can't keep up, the game keeps its real speed and only the picture "
                        "gets less smooth. Off: the whole game slows down instead. Console-accurate mode always "
                        "slows down.",
                        0, "keep_game_speed", "keep game speed slow gpu frame skip catch up lag performance fps");
  return true;
}();

} // namespace
