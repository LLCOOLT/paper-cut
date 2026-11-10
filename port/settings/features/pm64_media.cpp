// Paper Mario 64 extras on the settings page (docs/PM64_EXTRAS.md): the enemies in the Pit and Master
// Quest, the Tattle Log's Paper Mario 64 page (the enemies met in battle, with the ROM's Tattles), and the
// Paper Mario 64 boss list (each boss on its own, a star on the beaten ones; the same at the Star Haven corner's
// board, pm64_board_aurora.c).
#include "port/companion_menu.h"
#include "port/modes.h"
#include "port/pause_forms.h"
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_foes.h"
#include "port/settings.h"
#include "port/tr.h"

#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// --- the Paper Mario 64 boss list ---

constexpr const char* kListLoadouts[PORT_LOADOUT_COUNT] = {"As you are", "Mid-game", "Late game"};
constexpr const char* kListLoadoutNotes[PORT_LOADOUT_COUNT] = {
    "Mario as he is in this file.",
    "Level 15: 30 HP, 25 FP, 21 BP, Super Boots and Hammer, every partner, four Star Powers, eight items.",
    "Level 30: 60 HP, 50 FP, 51 BP, Ultra Boots and Hammer, every partner upgraded, all Star Powers, eight items.",
};
constexpr const char* kListNote = "Each of Paper Mario's bosses on its own (scaled to your level, or as strong as the "
                                  "post-game: the setting below), then back where you were. Losing just brings you back. A star: beaten on this file. "
                                  "Also on the board at the Star Haven corner of Rogueport's plaza.";
// (Mario as he is first, as the game's own Boss refights: the loadouts are lent for the fight and given back)
int sListLoadout = PORT_LOADOUT_OWN;
// a boss with two versions (portPm64FightHasEarly: Jr. Troopa): 0 its last fight (the default), 1 its first
int sListEarly = 0;
constexpr const char* kListVersions[2] = {"Last fight", "First fight"};
constexpr const char* kListVersionNote =
    "Jr. Troopa: his last fight (three forms) or his first, the prologue's (plain Jr. Troopa, a few HP).";
std::string sListMessage;

// "brings Kooper and Bombette" (the name put in by hand: a language pack's text is no format); game: the game's
// encoding (the Start menu)
std::string bringsText(int fight, bool game) {
  int who[2];
  const int n = portPm64BossBrings(fight, who);
  if (n == 0) {
    return {};
  }
  std::string names = game ? portPm64PartnerGameName(who[0]) : portPm64PartnerName(who[0]);
  if (n > 1) {
    std::string both = game ? portTrGame("%s and %s") : portTr("%s and %s");
    const size_t a = both.find("%s");
    if (a != std::string::npos) {
      both.replace(a, 2, names);
      const size_t b = both.find("%s", a + names.size());
      if (b != std::string::npos) {
        both.replace(b, 2, game ? portPm64PartnerGameName(who[1]) : portPm64PartnerName(who[1]));
      }
      names = both;
    }
  }
  std::string text = game ? portTrGame("brings %s") : portTr("brings %s");
  const size_t at = text.find("%s");
  if (at != std::string::npos) {
    text.replace(at, 2, names);
  }
  return text;
}

// a five-pointed star at the cursor, the text's height: gold when beaten, an outline when not
void star(bool beaten) {
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.52f);
  ImVec2 pts[10];
  for (int i = 0; i < 10; i++) {
    const float r = (i % 2 == 0 ? 0.5f : 0.21f) * size;
    const float a = -1.5707963f + float(i) * 0.6283185f;
    pts[i] = ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a));
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (beaten) {
    const ImU32 gold = IM_COL32(255, 200, 40, 255);
    for (int i = 0; i < 10; i += 2) { // (the five points, then the middle)
      dl->AddTriangleFilled(pts[(i + 9) % 10], pts[i], pts[i + 1], gold);
    }
    ImVec2 inner[5] = {pts[1], pts[3], pts[5], pts[7], pts[9]};
    dl->AddConvexPolyFilled(inner, 5, gold);
  } else {
    dl->AddPolyline(pts, 10, ImGui::GetColorU32(ImGuiCol_TextDisabled), ImDrawFlags_Closed, 1.0f);
  }
  ImGui::Dummy(ImVec2(size, size));
}

void bossListSection(int inGame) {
  if (portPm64Linked() == PM64_NONE) {
    ImGui::TextWrapped("%s", portTr("Link your Paper Mario ROM (Enhancements > Paper Mario 64) to see these."));
    return;
  }
  ImGui::TextWrapped("%s", portTr(kListNote));
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Choose one while a file is in play."));
    return;
  }
  if (!portPm64BossesReady()) {
    ImGui::TextDisabled("%s", portTr("Not during online play or a replay."));
    return;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::Combo(portTrLabel("Loadout##pm64list"), &sListLoadout, portTrList(kListLoadouts, PORT_LOADOUT_COUNT), PORT_LOADOUT_COUNT);
  ImGui::TextDisabled("%s", portTr(kListLoadoutNotes[sListLoadout]));
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::Combo(portTrLabel("Jr. Troopa##pm64ver"), &sListEarly, portTrList(kListVersions, 2), 2);
  ImGui::TextDisabled("%s", portTr(kListVersionNote));
  const PortModeStatus* s = portModeStatus();
  const bool running = s->mode != PORT_MODE_NONE && !s->finished;
  int beaten = 0;
  ImGui::BeginDisabled(running);
  for (int i = 0; i < PM64_BOSS_FIGHTS; i++) {
    const int f = portPm64FightAt(i); // (the Koopa Bros.' rematch after their first fight)
    ImGui::PushID(f);
    if (ImGui::Button(portTrLabel("Fight##pm64fight"), ImVec2(ImGui::GetFontSize() * 5.0f, 0.0f))) {
      const char* why = nullptr;
      sListMessage = portModePm64FightAs(f, sListLoadout, sListEarly, &why) ? "" : portTr(why);
    }
    ImGui::SameLine();
    star(portPm64BossBeaten(f) != 0);
    beaten += portPm64BossBeaten(f);
    ImGui::SameLine();
    ImGui::TextUnformatted(portTr(portPm64FightEnglish(f)));
    const std::string brings = bringsText(f, false);
    if (!brings.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "(%s)", brings.c_str());
    }
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  ImGui::Text(portTr("%d of %d beaten"), beaten, PM64_BOSS_FIGHTS);
  if (!sListMessage.empty()) {
    ImGui::TextWrapped("%s", sListMessage.c_str());
  }
}

// the Start menu's form: the same list in the game's windows
enum { kRowListLoadout = 1, kRowListVersion = 2, kRowListBoss = 10 };

int listRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < PM64_BOSS_FIGHTS + 2) {
    return 0;
  }
  const bool open = portPm64BossesReady() != 0;
  out[n++] = portPauseRow("Loadout", kListLoadoutNotes[sListLoadout], kListLoadouts[sListLoadout], kRowListLoadout,
                          open ? 0 : PORT_PAUSE_ROW_DIM);
  out[n++] = portPauseRow("Jr. Troopa", kListVersionNote, kListVersions[sListEarly], kRowListVersion,
                          open ? 0 : PORT_PAUSE_ROW_DIM);
  for (int i = 0; i < PM64_BOSS_FIGHTS; i++) {
    const int f = portPm64FightAt(i);
    PortPauseRow r = portPauseRow(nullptr, kListNote, nullptr, kRowListBoss + f,
                                  open ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
    std::snprintf(r.text, sizeof(r.text), "%s", portTrGame(portPm64FightEnglish(f)));
    const std::string brings = bringsText(f, true);
    if (!brings.empty()) {
      std::snprintf(r.value, sizeof(r.value), "%s", brings.c_str());
    } else if (portPm64BossBeaten(f)) {
      r.valueTr = "Beaten";
    }
    out[n++] = r;
  }
  return n;
}

const char* listAct(int id, int dir) {
  if (!portPm64BossesReady()) {
    return dir == 0 ? (portPm64Linked() == PM64_NONE ? "Link your Paper Mario ROM first."
                                                      : "Not during online play or a replay.")
                    : nullptr;
  }
  if (id == kRowListLoadout) {
    sListLoadout = (sListLoadout + (dir < 0 ? PORT_LOADOUT_COUNT - 1 : 1)) % PORT_LOADOUT_COUNT;
    return nullptr;
  }
  if (id == kRowListVersion) {
    sListEarly = !sListEarly;
    return nullptr;
  }
  if (dir != 0 || id < kRowListBoss || id >= kRowListBoss + PM64_BOSS_FIGHTS) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  const char* why = nullptr;
  return portModePm64FightAs(id - kRowListBoss, sListLoadout, sListEarly, &why) ? "The fight starts once the menu is closed."
                                                                                 : why;
}

const PortPauseForm kListForm = {"Enhancements", "Paper Mario 64 boss list", listRows, listAct};

void tattleSection(int) {
  if (portPm64Linked() == PM64_NONE) {
    ImGui::TextWrapped("%s", portTr("Link your Paper Mario ROM (Enhancements > Paper Mario 64) to see these."));
    return;
  }
  int seen = 0;
  for (int i = 0; i < portPm64EnemyCount(); i++) {
    seen += portPm64EnemySeen(i);
  }
  ImGui::Text(portTr("%d of %d met in battle"), seen, portPm64EnemyCount());
  for (int i = 0; i < portPm64EnemyCount(); i++) {
    char name[64];
    ImGui::PushID(i);
    if (!portPm64EnemySeen(i)) {
      ImGui::TextDisabled("%s", "???");
    } else if (ImGui::TreeNode(portPm64EnemyName(i, name, sizeof(name)))) {
      static char tattle[1024];
      const Pm64EnemyInfo* e = portPm64EnemyInfo(i);
      ImGui::Text(portTr("HP %d"), e->hp);
      ImGui::TextWrapped("%s", portPm64EnemyTattle(i, tattle, sizeof(tattle))[0] != 0 ? tattle : "");
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

struct Pm64MediaSettings {
  Pm64MediaSettings() {
    portSettingsDeclare("pm64_enemies", 0, 0, 1);
    portSettingsAddToggle("Enhancements", "Paper Mario 64 enemies",
                          "With your Paper Mario ROM linked: in the Pit of 100 Trials and in Master Quest, about "
                          "half the battles have Paper Mario's own enemies in place of their TTYD cousins (Goombas, "
                          "Koopas, Fuzzies, Clefts, Dry Bones, Magikoopas and more), drawn from your ROM with its "
                          "HP and Tattles; they fight with their cousins' moves. Also in the Enemy maker. Not online.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_enemies",
                          "paper mario 64 pm64 n64 enemies pit master quest goomba koopa tattle");
    static const char* const kStory[] = {"Off", "Mixed", "All"};
    portSettingsDeclare("pm64_story_enemies", 0, 0, 2);
    portSettingsAddChoice("Enhancements", "Paper Mario 64 enemies in the story",
                          "With your Paper Mario ROM linked: the story's fights can have Paper Mario's own enemies "
                          "that fit the area (Goombas and Koopas in Petal Meadows, Dry Bones in the Palace...), "
                          "wandering in the field as their sprites and fighting with Paper Mario's HP and attack "
                          "scaled to the chapter. Mixed: some fights, the same ones every time; All: every fight "
                          "they fit. Bosses and story-scripted fights stay as they are. From the next room.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_story_enemies", kStory, 3,
                          "paper mario 64 pm64 n64 enemies story fights field goomba koopa remix");
    portSettingsDeclare("pm64_bosses", 0, 0, 1);
    portSettingsAddToggle("Modes", "Paper Mario 64 bosses",
                          "With your Paper Mario ROM linked: the boss rush ends with Paper Mario's bosses, as strong "
                          "as the game's post-game, and they can be fought any time in Boss refights: Jr. Troopa, the "
                          "Koopa Bros., Tutankoopa, Tubba Blubba, General Guy, Lava Piranha, Huff N. Puff, the Crystal "
                          "King, Kammy Koopa and Bowser, drawn from your ROM. They also wait in Rogueport as the story "
                          "reaches them. Not online.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_bosses",
                          "paper mario 64 pm64 n64 bosses boss rush refight jr troopa bowser crystal king");
    // its sub-option: each boss standing in Rogueport from its point in the story (pm64_story_aurora.c)
    portSettingsDeclare("pm64_story_bosses", 1, 0, 1);
    portSettingsAddToggle("Modes", "Paper Mario 64 bosses in the story",
                          "With your Paper Mario ROM linked: each Paper Mario boss comes to Rogueport as the story "
                          "reaches it (Jr. Troopa once Goombella joins, the Koopa Bros. once Koops does, and so on; "
                          "the Crystal King, Kammy Koopa and Bowser near the end) and waits there until it's beaten. "
                          "Walk into it or talk to it to fight, scaled to your level; lose and it's still there. Off: "
                          "they're fought from the Paper Mario 64 boss list and the board.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_story_bosses",
                          "paper mario 64 pm64 n64 bosses story rogueport encounter jr troopa partners");
    portSettingsSetShown("pm64_story_bosses", [] {
      const int* bosses = portSettingsFind("pm64_bosses");
      return bosses != nullptr && *bosses != 0 ? 1 : 0;
    });
    // its sub-option (shown while it's on): each boss's own battle theme (port/include/port/pm64_music.h)
    portSettingsDeclare("pm64_boss_music", 1, 0, 1);
    portSettingsAddToggle("Modes", "Paper Mario 64 bosses play their own music",
                          "With your Paper Mario ROM linked: each Paper Mario boss fights to its own battle theme "
                          "from your ROM (the Koopa Bros.', Jr. Troopa's, Tubba Blubba's, Bowser's final battle and "
                          "the rest), whatever the Soundtrack setting. Off: the game's boss music. Only you hear it.",
                          0, "pm64_boss_music",
                          "paper mario 64 pm64 n64 bosses music battle theme song soundtrack");
    portSettingsSetShown("pm64_boss_music", [] {
      const int* bosses = portSettingsFind("pm64_bosses");
      return bosses != nullptr && *bosses != 0 ? 1 : 0;
    });
    portSettingsDeclare("pm64_badges", 0, 0, 1);
    portSettingsAddToggle("Enhancements", "Paper Mario 64 badges",
                          "With your Paper Mario ROM linked: Rowf comes from Toad Town to the Star Haven corner of "
                          "Rogueport's plaza with Paper Mario badges TTYD hasn't got: Dizzy Stomp, Mega Jump, Mega "
                          "Smash, Power Quake, Mega Quake, Triple Dip, Damage Dodge, Peekaboo, Pay-Off and Attack FX "
                          "A to E, with their names, descriptions and icons from your ROM. Online only when every "
                          "player has a ROM linked.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_badges",
                          "paper mario 64 pm64 n64 badges rowf merchant dizzy stomp peekaboo pay-off attack fx");
    portSettingsDeclare("pm64_recipes", 0, 0, 1);
    portSettingsAddToggle("Enhancements", "Tayce T.'s recipes",
                          "With your Paper Mario ROM linked: Zess T. cooks Paper Mario's dishes from its recipes "
                          "too: Fried Shroom, Hot Shroom, Yummy Meal, Deluxe Feast, Kooky Cookie, Super Soda and "
                          "more, with their names, descriptions, icons and effects from your ROM. Rowf (Star Haven "
                          "corner, Rogueport's plaza) sells the Paper Mario ingredients they need. In the Start "
                          "menu's recipe book. Online only when every player has a ROM linked.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_recipes",
                          "paper mario 64 pm64 n64 tayce cooking recipes zess dishes food rowf ingredients");
    portSettingsDeclare("pm64_star_spirits", 1, 0, 1);
    portSettingsAddToggle("Cosmetics", "Paper Mario 64 Star Spirits",
                          "With your Paper Mario ROM linked: while Mario does a Special move, one of the seven Star "
                          "Spirits looks on from the top of the screen, drawn from your ROM.",
                          0, "pm64_star_spirits", "paper mario 64 pm64 star spirits eldstar special move cameo");
    portSettingsDeclare("pm64_sfx", 1, 0, 1); // port/include/port/pm64_sfx.h
    portSettingsAddToggle("Audio", "Paper Mario sound effects",
                          "With your Paper Mario ROM linked: the Paper Mario partners, the Paper Mario enemies, the "
                          "Star Spirits and the Star Haven corner make Paper Mario's own sounds, read from your ROM: "
                          "Goombario's Headbonk, Kooper's shell, Bombette's fuse and blast, Parakarry's wings, Watt's "
                          "sparks, Sushie's splash, Lakilester's cloud and Spiny. Off: the game's own sounds. Only "
                          "you hear it.",
                          0, "pm64_sfx", "paper mario 64 pm64 n64 sound effects sfx partners goombario kooper");
    portSettingsDeclare("pm64_sfx_general", 0, 0, 1); // port/include/port/pm64_sfx.h
    portSettingsAddToggle("Audio", "Paper Mario sounds for everyday actions",
                          "With your Paper Mario ROM linked: Mario's jump and hammer, coins, hearts, flowers, Star "
                          "Points, items and the menus' cursor use Paper Mario's sounds, read from your ROM. Only "
                          "you hear it.",
                          0, "pm64_sfx_general", "paper mario 64 pm64 n64 sound effects sfx jump hammer coin menu");
    portSettingsAddSection("Enhancements", "Tattle Log: Paper Mario 64", nullptr, 0, tattleSection,
                           "paper mario 64 pm64 tattle log enemies goombario");
    portSettingsAddSection("Enhancements", "Paper Mario 64 boss list", nullptr, PORT_SETTING_CHANGES_PLAY,
                           bossListSection,
                           "paper mario 64 pm64 bosses list fight jr troopa koopa bros tubba bowser star partners");
    portPauseAddForm(&kListForm);
    portSettingsDeclare("pm64_boss_scale", 1, 0, 1); // (modes_aurora.c, pm64_bosses_aurora.c)
    portSettingsAddToggle("Enhancements", "Paper Mario 64 bosses: Scale to your level",
                          "A fight from the Paper Mario 64 boss list, the Star Haven corner's board or Boss "
                          "refights: the boss's HP and attack follow Mario's level against the chapter it comes "
                          "from in Paper Mario, from half of Paper Mario's own numbers for a low level up to "
                          "exactly them at that level or past it, never more. Off: as strong as the game's "
                          "post-game. The boss rush keeps its own rules.",
                          PORT_SETTING_CHANGES_PLAY, "pm64_boss_scale",
                          "paper mario 64 pm64 bosses scale level hp attack easier boss list");
  }
} sPm64Media;

} // namespace
