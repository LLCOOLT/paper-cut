// More after the story (port/include/port/postgame_extra.h), on the Modes tab
// and in the Start menu's Settings tab: Whacka in the Pit and the Pit Mover,
// exhibition matches, the returning champion and Nighttail, the Gallery and
// the Gold Medal, and the smaller post-game pieces' settings; on the Audio
// tab the talk blips and the battle theme for each area. Every one is off by
// default; those that change play are locked by Console-accurate and kept
// out of online sessions and replays by the game side.
#include "port/postgame_extra.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace {

constexpr const char* kTab = "Modes";
constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;
constexpr const char* kSecondScreen = "Not on the second screen: use the game's Start menu.";

std::string sMessage;

void say(int ok, const char* why, const char* done) {
  sMessage = ok ? portTr(done) : why != nullptr ? portTr(why) : ""; // (never "" looked up)
}

// --- Whacka and the Pit Mover ---

void pitSection(int inGame) {
  ImGui::TextWrapped("%s", portTr("Once Bonetail has fallen, Whacka waits at the bottom of the Pit of 100 Trials "
                                  "for a fight of his own. Each win gives a Whacka Bump, and after the first the "
                                  "Whacka on Keelhaul Key never runs out of bumps."));
  if (!inGame) {
    return;
  }
  ImGui::TextDisabled("%s", portTr(portPitWhackaBeaten() ? "Whacka has been beaten in the Pit on this file."
                                                          : "Whacka hasn't been beaten in the Pit on this file yet."));
  if (ImGui::Button(portTrLabel("Take the Pit Mover to the 50th floor##pitmover"))) {
    const char* why = nullptr;
    say(portPitMoverGo(&why), why, "Down to the 50th floor once the menu is closed.");
  }
}

int pitRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("Pit Mover: to the 50th floor",
                        "From the Pit's entrance, straight to the 50th floor, once the Pit has been cleared.",
                        nullptr, 0, portPitMoverReady() ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
  return 1;
}

const char* pitAct(int id, int dir) {
  if (dir != 0 || id != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  const char* why = nullptr;
  return portPitMoverGo(&why) ? "Down to the 50th floor once the menu is closed." : why;
}

const PortPauseForm kPitForm = {kTab, "Pit Mover", pitRows, pitAct};

// --- exhibition matches and the post-game bosses ---

int sTeam;

void exhibitionSection(int inGame) {
  ImGui::TextWrapped("%s", portTr("A match against any Glitz Pit team ranked below Mario, with no conditions: the "
                                  "only way to meet Wings of Night and the Destructors again. Back where you "
                                  "started after it, won or lost."));
  if (!inGame) {
    return;
  }
  const char* preview = portTr(portExhibitionName(sTeam));
  if (ImGui::BeginCombo(portTrLabel("Team##exhibition"), preview)) {
    for (int i = 0; i < portExhibitionCount(); i++) {
      if (!portExhibitionOpen(i)) {
        continue;
      }
      if (ImGui::Selectable(portTr(portExhibitionName(i)), i == sTeam)) {
        sTeam = i;
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::Button(portTrLabel("Start the match##exhibition"))) {
    const char* why = nullptr;
    say(portExhibitionStart(sTeam, &why), why, "The match starts once the menu is closed.");
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", portTr("After the credits: the Golden Talon, the champion who left before Rawk Hawk, back "
                                  "for his belt; and Nighttail, who wakes in the Palace of Shadow."));
  if (ImGui::Button(portTrLabel("Fight the returning champion##champion"))) {
    const char* why = nullptr;
    say(portChampionStart(&why), why, "The match starts once the menu is closed.");
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Fight Nighttail##nighttail"))) {
    const char* why = nullptr;
    say(portNighttailStart(&why), why, "The fight starts once the menu is closed.");
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

int exhibitionRows(PortPauseRow* out, int max) {
  int n = 0;
  if (n < max) {
    out[n++] = portPauseRow("The returning champion", "After the credits: the Golden Talon, back for his belt.",
                            nullptr, 1000, portChampionOpen() ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
  }
  if (n < max) {
    out[n++] = portPauseRow("Nighttail", "After the credits: a super-boss in the Palace of Shadow.", nullptr, 1001,
                            portNighttailOpen() ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
  }
  if (n < max) {
    out[n++] = portPauseRow("Exhibition matches", nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
  }
  for (int i = 0; i < portExhibitionCount() && n < max; i++) {
    if (portExhibitionOpen(i)) {
      out[n++] = portPauseRow(portExhibitionName(i), "A match with no conditions; back where you started after it.",
                              nullptr, i, PORT_PAUSE_ROW_CONFIRM);
    }
  }
  return n;
}

const char* exhibitionAct(int id, int dir) {
  if (dir != 0 || id < 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  const char* why = nullptr;
  int ok;
  if (id == 1000) {
    ok = portChampionStart(&why);
  } else if (id == 1001) {
    ok = portNighttailStart(&why);
  } else {
    ok = portExhibitionStart(id, &why);
  }
  return ok ? "It starts once the menu is closed." : why;
}

const PortPauseForm kExhibitionForm = {kTab, "Exhibition matches", exhibitionRows, exhibitionAct};

// --- the Gallery and the Gold Medal ---

void gallerySection(int inGame) {
  ImGui::TextWrapped("%s", portTr("The disc's own enemy and boss models, turning round, chapter by chapter, on the "
                                  "Start menu's Settings tab (Modes, Gallery). A chapter opens once its story is "
                                  "done and half the Star Pieces of its areas are found. Your own pictures are in "
                                  "the Journal's Photos book."));
  if (!inGame) {
    return;
  }
  for (int c = 0; c < portGalleryChapters(); c++) {
    int total = 0;
    const int found = portGalleryStarPieces(c, &total);
    ImGui::Text("%s: %s (%s %d/%d)", portTr(portGalleryChapterName(c)), portTr(portGalleryOpen(c) ? "open" : "closed"),
                portTr("Star Pieces"), found, total);
  }
}

int galleryRows(PortPauseRow* out, int max) {
  int n = 0;
  for (int c = 0; c < portGalleryChapters() && n < max; c++) {
    PortPauseRow r = portPauseRow(portGalleryChapterName(c),
                                  "Opens once the chapter is done and half its Star Pieces are found.", nullptr, c,
                                  portGalleryOpen(c) ? 0 : PORT_PAUSE_ROW_DIM);
    int total = 0;
    const int found = portGalleryStarPieces(c, &total);
    std::snprintf(r.value, sizeof(r.value), "%d/%d", found, total);
    out[n++] = r;
  }
  if (n < max) {
    out[n++] = portPauseRow("The Photos book", "Your own pictures: in the Journal, under Photos.", nullptr, -1,
                            PORT_PAUSE_ROW_DIM);
  }
  return n;
}

const char* galleryAct(int id, int dir) {
  if (dir != 0 || id < 0) {
    return id < 0 ? "Your own pictures: in the Journal, under Photos." : nullptr;
  }
  if (!portGalleryOpen(id)) {
    return "Opens once the chapter is done and half its Star Pieces are found.";
  }
  portGalleryOpenPage(id);
  return nullptr;
}

const PortPauseForm kGalleryForm = {kTab, "Gallery", galleryRows, galleryAct};

void goldSection(int inGame) {
  ImGui::TextWrapped("%s", portTr("Every Tattle, every badge and every recipe: the Gold Medal, a plaque in the "
                                  "trophy room, and a gold look for Mario (and Luigi) in the Wardrobe."));
  if (!inGame) {
    return;
  }
  int t, tt, b, bt, r, rt;
  portJournalCounts(&t, &tt, &b, &bt, &r, &rt);
  ImGui::Text("%s %d/%d   %s %d/%d   %s %d/%d", portTr("Tattles"), t, tt, portTr("Badges"), b, bt, portTr("Recipes"), r,
              rt);
  if (portGoldMedal()) {
    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f), "%s", portTr("This file has the Gold Medal."));
  }
}

// --- the battle themes ---

const char* sThemeNames[32];

struct Register {
  Register() {
    portSettingsDeclare("pg_pit_whacka", 0, 0, 1);
    portSettingsAddToggle(kTab, "Whacka in the Pit",
                          "Once Bonetail has fallen, Whacka waits on the Pit of 100 Trials' 100th floor for a fight "
                          "of his own; a Whacka Bump for each win. Not online.",
                          kPlay, "pg_pit_whacka", "whacka pit 100 trials boss bump post game");
    portSettingsAddSection(kTab, "Pit Mover", nullptr, 0, pitSection, "pit mover 50th floor whacka post game");
    portPauseAddForm(&kPitForm);
    portSettingsAddSection(kTab, "Exhibition matches", nullptr, kPlay, exhibitionSection,
                           "glitz pit exhibition match team wings of night destructors champion nighttail super boss");
    portPauseAddForm(&kExhibitionForm);
    portSettingsAddSection(kTab, "Gallery", nullptr, 0, gallerySection, "gallery models turnaround star pieces photos");
    portPauseAddForm(&kGalleryForm);
    portSettingsAddSection(kTab, "Gold Medal", nullptr, 0, goldSection, "gold medal tattles badges recipes gold mario");

    portSettingsDeclare("pg_second_tattles", 0, 0, 1);
    portSettingsAddToggle(kTab, "Second-fight Tattles",
                          "Doopliss's second fight and the Shadow Queen's true form can be Tattled, in Paper Cut's "
                          "own words.",
                          kPlay, "pg_second_tattles", "tattle second fight doopliss shadow queen");
    portSettingsDeclare("pg_boo_quiz", 0, 0, 1);
    portSettingsAddToggle(kTab, "Boo quiz in Creepy Steeple",
                          "After the story, a Boo in the steeple's hall asks 15 questions; all right, and a prize.",
                          kPlay, "pg_boo_quiz", "boo quiz creepy steeple questions post game");
    portSettingsDeclare("pg_newsletter", 0, 0, 1);
    portSettingsAddToggle(kTab, "Newsletter Trouble",
                          "After chapter 7, the Rogueport paper's editor in the plaza needs three newsletters "
                          "delivered.",
                          kPlay, "pg_newsletter", "newsletter trouble delivery rogueport post game");
    portSettingsDeclare("pg_crane_prizes", 0, 0, 1);
    portSettingsAddToggle(kTab, "Crane game prizes",
                          "After the story, the X-Naut crane game's coins are prizes, a new batch each visit.", kPlay,
                          "pg_crane_prizes", "crane game x-naut fortress prizes post game");
    portSettingsDeclare("pg_buy_tokens", 0, 0, 1);
    portSettingsAddToggle(kTab, "Buy Pianta tokens",
                          "A Pianta in the Pianta Parlor sells tokens: 50 for 150 coins, or 250 and 500 at once.",
                          kPlay, "pg_buy_tokens", "pianta tokens parlor buy coins");
    portSettingsDeclare("pg_jp_recipes", 0, 0, 1);
    portSettingsAddToggle("Gameplay", "Japanese recipes",
                          "Zess T. cooks the Japanese release's Trial Stew from a Thunder Rage and a Thunder Bolt.",
                          kPlay, "pg_jp_recipes", "japanese recipe trial stew zess cooking");
    portSettingsDeclare("pg_jp_bad_ending", 0, 0, 1);
    portSettingsAddToggle("Gameplay", "Japanese bad ending",
                          "Say yes to the Shadow Queen and Mario stays in the room at her side, as in the Japanese "
                          "release.",
                          0, "pg_jp_bad_ending", "japanese bad ending shadow queen palace");
    portSettingsDeclare("pg_jp_hooktail_frogs", 0, 0, 1);
    portSettingsAddToggle("Gameplay", "Hooktail fears frogs",
                          "As in the Japanese release: Attack FX R's croak, and the texts about it, are a frog's, "
                          "not a cricket's.",
                          0, "pg_jp_hooktail_frogs", "hooktail frog cricket attack fx r japanese text");

    portSettingsDeclare("pg_talk_blips", 0, 0, 1);
    portSettingsAddToggle("Audio", "Talk blips",
                          "A blip every few letters while someone speaks, in a voice for each kind of speaker, made "
                          "from the game's own message sounds.",
                          0, "pg_talk_blips", "talk blips voices speech sounds");
    const int themes = portPgBattleThemeCount() < 32 ? portPgBattleThemeCount() : 32;
    for (int i = 0; i < themes; i++) {
      sThemeNames[i] = portPgBattleThemeName(i);
    }
    static char sKeys[PORT_PG_THEME_AREAS][32];
    static std::string sLabels[PORT_PG_THEME_AREAS];
    for (int a = 0; a < PORT_PG_THEME_AREAS; a++) {
      std::snprintf(sKeys[a], sizeof(sKeys[a]), "pg_battle_theme_%d", a);
      portSettingsDeclare(sKeys[a], 0, 0, themes - 1);
      sLabels[a] = std::string("Battle music: ") + portPgThemeAreaName(a);
      portSettingsAddChoice("Audio", sLabels[a].c_str(),
                            "The music of the usual battles here, from the disc's own battle tracks.", 0, sKeys[a],
                            sThemeNames, themes, "battle music theme area track");
    }
  }
} sRegister;

} // namespace
