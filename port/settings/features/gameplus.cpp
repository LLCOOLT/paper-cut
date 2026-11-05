// Enhancements and accessibility (port/include/port/gameplus.h): the settings
// page's controls for the options, all off by default, and the quick-swap
// control's keyboard key and controller button.
#include "port/bindings.h"
#include "port/gameplus.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cstdio>

namespace {

constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;
constexpr unsigned kSaves = PORT_SETTING_CHANGES_PLAY | PORT_SETTING_CHANGES_SAVES;
constexpr const char* kTab = "Enhancements";

// Q on the keyboard, or a controller's left stick pressed in (Controls > Bindings)
int quickSwapHeld() { return portBindingsHeld(PORT_BIND_QUICK_SWAP); }

// Fast travel: a button per hub, those not reached yet greyed out
void fastTravel(int) {
  if (!portGameplus("fast_travel")) {
    ImGui::TextDisabled("%s", portTr("Turn on Fast travel above to use it."));
    return;
  }
  for (int i = 0; i < portFastTravelCount(); i++) {
    const bool open = portFastTravelUnlocked(i);
    ImGui::BeginDisabled(!open);
    if (ImGui::Button(portFastTravelName(i))) {
      portFastTravelRequest(i);
    }
    ImGui::EndDisabled();
    if ((i + 1) % 4 != 0 && i + 1 < portFastTravelCount()) {
      ImGui::SameLine();
    }
  }
  ImGui::TextDisabled("%s", portTr("In the field only; the trip starts once this page is closed."));
}

// Journal: the file's progress, from its flags
void journal(int) {
  PortJournal j;
  if (!portJournal(&j)) {
    ImGui::TextDisabled("%s", portTr("Shown while a game is in the field or a battle."));
    return;
  }
  ImGui::Text(portTr("Crystal Stars: %d of 7"), j.crystalStars);
  ImGui::Text(portTr("Trouble Center: %d of %d solved"), j.troubles, j.troublesTotal);
  if (j.troublesOpen[0] != '\0') {
    ImGui::TextWrapped(portTr("Not solved yet: #%s"), j.troublesOpen);
  }
  ImGui::Text(portTr("Badges found: %d of %d"), j.badges, j.badgesTotal);
  ImGui::Text(portTr("Recipes cooked: %d of %d"), j.recipes, j.recipesTotal);
  ImGui::Text(portTr("Tattle Log: %d of %d"), j.tattles, j.tattlesTotal);
  ImGui::Text(portTr("Star Pieces held: %d    Shine Sprites held: %d"), j.starPieces, j.shineSprites);
}

// The Start menu's forms (port/include/port/pause_forms.h): the hubs, and the file's progress
constexpr const char* kSecondScreen = "Not from the second screen: do it in the game's own Start menu.";

int travelRows(PortPauseRow* out, int max) {
  int n = 0;
  if (!portGameplus("fast_travel")) {
    out[n++] = portPauseRow("Turn on Fast travel above to use it.", nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  for (int i = 0; i < portFastTravelCount() && n < max; i++) {
    const bool open = portFastTravelUnlocked(i);
    out[n++] = portPauseRow(portFastTravelName(i),
                            open ? "Travel there: the trip starts once the menu is closed."
                                 : "Not reached yet: one more hub opens with each Crystal Star.",
                            nullptr, i, open ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
  }
  return n;
}

const char* travelAct(int id, int dir) {
  if (dir != 0 || id < 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  if (!portFastTravelUnlocked(id)) {
    return "Not reached yet: one more hub opens with each Crystal Star.";
  }
  portFastTravelRequest(id);
  return "The trip starts once the menu is closed.";
}

int journalRows(PortPauseRow* out, int max) {
  PortJournal j;
  int n = 0;
  if (max < 8) {
    return 0;
  }
  if (!portJournal(&j)) {
    out[n++] = portPauseRow("Shown while a game is in the field or a battle.", nullptr, nullptr, -1,
                            PORT_PAUSE_ROW_DIM);
    return n;
  }
  const auto add = [&](const char* label, int have, int total) {
    PortPauseRow r = portPauseRow(label, "This file's progress.", nullptr, n);
    if (total > 0) {
      std::snprintf(r.value, sizeof(r.value), portTrGame("%d of %d"), have, total);
    } else {
      std::snprintf(r.value, sizeof(r.value), "%d", have);
    }
    out[n++] = r;
  };
  add("Crystal Stars", j.crystalStars, 7);
  add("Trouble Center", j.troubles, j.troublesTotal);
  if (j.troublesOpen[0] != '\0') {
    PortPauseRow r = portPauseRow("Not solved yet", "The Trouble Center's jobs still open.", nullptr, n);
    std::snprintf(r.value, sizeof(r.value), "#%s", j.troublesOpen);
    out[n++] = r;
  }
  add("Badges found", j.badges, j.badgesTotal);
  add("Recipes cooked", j.recipes, j.recipesTotal);
  add("Tattle Log", j.tattles, j.tattlesTotal);
  add("Star Pieces held", j.starPieces, 0);
  add("Shine Sprites held", j.shineSprites, 0);
  return n;
}

const char* journalAct(int, int) { return nullptr; }

const PortPauseForm kTravelForm = {kTab, "Travel to", travelRows, travelAct};
const PortPauseForm kJournalForm = {kTab, "Journal", journalRows, journalAct};

struct Register {
  Register() {
    portGameplusDeclare();
    gPortQuickSwapHeld = quickSwapHeld;
    portSettingsAddToggle(kTab, "Partner quick-swap",
                          "Switches to the next partner in the field without the pause menu: Q, a controller's left "
                          "stick pressed in, or the on-screen swap button.",
                          kPlay, "quick_swap", "partner switch swap change party");
    portSettingsAddToggle(kTab, "Save Blocks save at once",
                          "A Save Block saves to the file being played without asking first.", kPlay,
                          "save_no_prompt", "save block prompt question memory card");
    portSettingsAddToggle(kTab, "Bigger bag",
                          "20 items without the Strange Sack. A save holding more than 10 items without it may "
                          "not load right on a GameCube.",
                          kSaves, "bigger_bag", "items inventory bag strange sack capacity");
    portSettingsAddToggle(kTab, "Partners never at 0 HP",
                          "After a battle, a partner who fell is back at 1 HP.", kPlay, "partner_revive",
                          "partner hp revive faint knocked out");
    portSettingsAddToggle(kTab, "Level up with the D-pad",
                          "The level-up screen (HP, FP or BP) also takes the D-pad, not only the stick.", kPlay,
                          "levelup_dpad", "level up dpad stick hp fp bp");
    portSettingsAddToggle(kTab, "Healing items heal every partner",
                          "In the field, an item used on a partner heals the other partners too.", kPlay,
                          "heal_all_partners", "heal item partner mushroom hp");
    portSettingsAddToggle(kTab, "Skip repeated scenes",
                          "Scenes already seen on this file go faster or straight to their end: the Crystal Star "
                          "fanfare and map reveal, Zess T.'s cooking, the Glitz Pit's match scenes, the train and "
                          "blimp tickets.",
                          0, "skip_scenes", "skip cutscene scene crystal star cooking zess");
    portSettingsAddToggle(kTab, "Fast travel",
                          "Travel to a hub from the settings page (All settings, Travel to): Rogueport, then one "
                          "more per Crystal Star.", kPlay,
                          "fast_travel", "fast travel warp hub teleport");
    portSettingsAddToggle(kTab, "Faster battle menus", "The battle command wheel opens and turns twice as fast.",
                          0, "fast_battle_menus", "battle menu speed wheel fast");
    portSettingsAddToggle(kTab, "Tattle without a turn",
                          "Tactics has a Tattle entry when an enemy on the field was tattled before: it reads their "
                          "tattles again, and the turn is still yours.",
                          kPlay, "free_tattle", "tattle goombella free turn tactics log");
    portSettingsAddToggle(kTab, "Battle pop-ups",
                          "GUARD! and SUPERGUARD! over Mario or the partner, a resisted status's icon bouncing off, "
                          "and status icons kept on screen while you pick a target.",
                          0, "battle_popups", "battle popup guard superguard status resist icon feedback");
    portSettingsAddToggle(kTab, "Retry on Game Over",
                          "Keeps a save state of each room and battle as you enter it; on the Game Over screen, Y "
                          "goes back to it. A short pause at each room's start.",
                          kPlay, "gameover_retry", "game over retry continue save state death");
    portSettingsAddToggle(kTab, "Remake balance",
                          "Star Power gain x1.5, Power Bounce's timing narrowing half as fast, First Attack at 0 "
                          "BP, Hammer Throw piercing defense.",
                          kPlay, "remake_balance", "remake balance star power power bounce first attack hammer throw");
    portSettingsAddToggle(kTab, "Pit of 100 Trials extras",
                          "Coins for each floor cleared (1 more per 10 floors down), and the Mover on every floor "
                          "once floor 50 has been cleared.",
                          kPlay, "pit_plus", "pit 100 trials coins mover");
    portSettingsAddToggle(kTab, "Glitz Pit kindness",
                          "A lost match costs no rank, and a match's bonus condition stays at the top of the screen.",
                          kPlay, "glitz_pit_kind", "glitz pit rank lose condition rule");
    /* (the key is still playable_luigi: 1 was Luigi when this was a toggle) */
    static const char* const kPlayAs[] = {"Mario", "Luigi", "Peach", "Bowser"};
    portSettingsAddChoice(kTab, "Play as",
                          "Who takes Mario's place, with Mario's moves, in the field, battles, menus and scenes; "
                          "the text calls them by their name. Saves are not changed.",
                          0, "playable_luigi", kPlayAs, 4,
                          "play as character luigi peach bowser koopa princess mario green brother");
    static const char* const kNewGamePlus[] = {"Off", "Level and stats", "Level, stats and badges",
                                               "Level, stats, badges and Tattle Log"};
    portSettingsAddChoice(kTab, "New Game+",
                          "A new file starts with what you choose here, taken from the file New Game+ from file "
                          "names. Choose before starting the new file.",
                          kPlay, "new_game_plus", kNewGamePlus, 4, "new game plus ng+ carry level badges tattle");
    portSettingsAddSlider(kTab, "New Game+ from file", nullptr, kPlay, "new_game_plus_from", 1, 4, "File %d",
                          "new game plus file");
    portSettingsAddSection(kTab, "Travel to", nullptr, PORT_SETTING_IN_GAME, fastTravel, "fast travel warp hub");
    portSettingsAddSection(kTab, "Journal", "This file's progress.", PORT_SETTING_IN_GAME, journal,
                           "journal progress completion trouble center badges recipes tattle star pieces");
    portPauseAddForm(&kTravelForm);
    portPauseAddForm(&kJournalForm);

    static const char* const kDifficulty[] = {"Original", "Easy (enemy HP 75 %, attacks 50 %)",
                                              "Hard (enemy HP 150 %, attacks 150 %)", "Custom"};
    portSettingsAddChoice("Gameplay", "Difficulty",
                          "Enemies' HP and the damage their attacks do. Custom uses Enemy HP (custom) and Enemy "
                          "attack (custom).", kPlay,
                          "difficulty", kDifficulty, 4, "difficulty easy hard enemy hp attack damage");
    portSettingsAddSlider("Gameplay", "Enemy HP (custom)", nullptr, kPlay, "difficulty_enemy_hp", 25, 300,
                          "%d %%", "difficulty enemy hp custom");
    portSettingsAddSlider("Gameplay", "Enemy attack (custom)", nullptr, kPlay, "difficulty_enemy_attack", 25,
                          300, "%d %%", "difficulty enemy attack damage custom");
    static const char* const kAssist[] = {"Off", "Wider windows", "Automatic"};
    portSettingsAddChoice("Accessibility", "Action commands: attacks",
                          "Wider timing for attack commands, or every attack command done for you.", kPlay,
                          "ac_attack_assist", kAssist, 3, "action command assist timing auto attack");
    portSettingsAddChoice("Accessibility", "Action commands: guards",
                          "Wider Guard and Superguard windows, or every attack guarded for you (a Superguard is "
                          "still up to you).",
                          kPlay, "ac_guard_assist", kAssist, 3, "action command assist guard superguard auto");
  }
} sRegister;

} // namespace
