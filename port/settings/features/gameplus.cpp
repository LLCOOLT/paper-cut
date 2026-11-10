// Enhancements and accessibility (port/include/port/gameplus.h): the settings
// page's controls for the options, all off by default, and the quick-swap
// control's keyboard key and controller button.
#include "port/bindings.h"
#include "port/brothers.h"
#include "port/gameplus.h"
#include "port/fast_travel.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cstdio>

// Hints for coins (port/settings/features/qol.cpp): the Journal's rows after n, and their act
int portQolJournalRows(PortPauseRow* out, int n, int max);
const char* portQolJournalAct(int id, int dir);

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
  bool closed = false;
  for (int i = 0; i < portFastTravelCount(); i++) {
    const bool open = portFastTravelUnlocked(i);
    closed = closed || !open;
    ImGui::BeginDisabled(!open);
    if (ImGui::Button(portFastTravelName(i))) {
      portFastTravelRequest(i);
    }
    ImGui::EndDisabled();
    if ((i + 1) % 4 != 0 && i + 1 < portFastTravelCount()) {
      ImGui::SameLine();
    }
  }
  // the greyed ones: why (Mario can't leave from here now, or not reached yet), as the Start menu's rows say
  if (const char* why = portTravelDepartRefusal(); why != nullptr) {
    ImGui::TextDisabled("%s", portTr(why));
  } else if (closed) {
    ImGui::TextDisabled("%s", portTr("Not yet: walk there once (or the story keeps it closed now)."));
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
constexpr const char* kSecondScreen = "Not on the second screen: use the game's Start menu.";

// why a hub can't be travelled to now (fast travel's rules, port/include/port/fast_travel.h)
const char* travelWhyNot(int) {
  const char* why = portTravelDepartRefusal();
  return why != nullptr ? why : "Not yet: walk there once (or the story keeps it closed now).";
}

int travelRows(PortPauseRow* out, int max) {
  if (max <= 0) {
    return 0; // (no room for even the first row)
  }
  int n = 0;
  if (!portGameplus("fast_travel")) {
    out[n++] = portPauseRow("Turn on Fast travel above to use it.", nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  for (int i = 0; i < portFastTravelCount() && n < max; i++) {
    const bool open = portFastTravelUnlocked(i);
    out[n++] = portPauseRow(portFastTravelName(i),
                            open ? "Travel there: the trip starts once the menu is closed." : travelWhyNot(i),
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
    return travelWhyNot(id);
  }
  portFastTravelRequest(id);
  return "The trip starts once the menu is closed.";
}

int journalRows(PortPauseRow* out, int max) {
  if (max <= 0) {
    return 0; // (no room for even the first row)
  }
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
    // the first few, then how many more: the whole list (up to 30) ran past the row's buffer and the page's
    // edge, cut mid-number (the full settings page lists them all)
    constexpr int kShown = 6; // (two-digit numbers and a long label, as Japanese has, still clear the page's middle)
    int count = 1;
    const char* cut = nullptr;
    for (const char* p = j.troublesOpen; *p != '\0'; p++) {
      if (*p == ',') {
        if (++count == kShown + 1) {
          cut = p;
        }
      }
    }
    if (cut == nullptr) {
      std::snprintf(r.value, sizeof(r.value), "#%s", j.troublesOpen);
    } else {
      std::snprintf(r.value, sizeof(r.value), "#%.*s... (+%d)", static_cast<int>(cut - j.troublesOpen), j.troublesOpen,
                    count - kShown);
    }
    out[n++] = r;
  }
  add("Badges found", j.badges, j.badgesTotal);
  add("Recipes cooked", j.recipes, j.recipesTotal);
  add("Tattle Log", j.tattles, j.tattlesTotal);
  add("Star Pieces held", j.starPieces, 0);
  add("Shine Sprites held", j.shineSprites, 0);
  return portQolJournalRows(out, n, max); // Hints for coins (qol.cpp)
}

const char* journalAct(int id, int dir) { return portQolJournalAct(id, dir); }

const PortPauseForm kTravelForm = {kTab, "Travel to", travelRows, travelAct};
const PortPauseForm kJournalForm = {kTab, "Journal", journalRows, journalAct};

struct Register {
  Register() {
    portGameplusDeclare();
    gPortQuickSwapHeld = quickSwapHeld;
    portSettingsAddToggle(kTab, "Partner quick-swap",
                          "Switches to the next partner in the field without the pause menu: {SWAP}, or the "
                          "on-screen swap button.",
                          kPlay, "quick_swap", "partner switch swap change party");
    portSettingsAddToggle(kTab, "Save Blocks save at once",
                          "A Save Block saves to the current file without asking.", kPlay,
                          "save_no_prompt", "save block prompt question memory card");
    static const char* const kBag[] = {"Original: 10, 20 with the Strange Sack", "20 without the Strange Sack",
                                       "Remake: 15, 20 with the Strange Sack"};
    portSettingsAddChoice(kTab, "Bag size",
                          "How many items Mario can carry. A save holding more than 10 items without the Strange "
                          "Sack may not load right on a GameCube.",
                          kSaves, "bigger_bag", kBag, 3, "bigger bag items inventory strange sack capacity size remake");
    portSettingsAddToggle(kTab, "Partners never at 0 HP",
                          "After a battle, a partner who fell is back at 1 HP.", kPlay, "partner_revive",
                          "partner hp revive faint knocked out");
    portSettingsAddToggle(kTab, "Level up with the D-pad",
                          "The level-up screen (HP, FP, BP) also takes the {DPAD}.", kPlay,
                          "levelup_dpad", "level up dpad stick hp fp bp");
    portSettingsAddToggle(kTab, "Healing items heal every partner",
                          "In the field, healing one partner heals all of them.", kPlay,
                          "heal_all_partners", "heal item partner mushroom hp");
    static const char* const kSkip[] = {"Off", "Seen only", "Always"};
    portSettingsAddChoice(kTab, "Skip scenes",
                          "These scenes go faster or straight to their end: the Crystal Star fanfare and map "
                          "reveal, Zess T.'s cooking, the Glitz Pit's match scenes, the train and blimp tickets, "
                          "inn rests, Merlon's upgrades, Merlee's charms, Cortez's ship, the Excess Express trips, "
                          "the prologue, the Palace of Shadow's key stands and globe, and a boss's intro after a "
                          "Game Over (played fast). Seen only: once seen on this file (the prologue and the "
                          "palace: on any file). Always: the first time too. What a scene gives is still given, "
                          "and where to go next is still said.",
                          0, "skip_scenes", kSkip, 3,
                          "skip cutscene scene crystal star cooking zess inn charm ship train boss prologue always");
    static const char* const kRogueport[] = {"Off", "Offer", "Always"};
    portSettingsAddChoice(kTab, "Start in Rogueport",
                          "A new file can start in Rogueport's plaza: the opening skipped (the letter, the ship, the "
                          "harbour and Lord Crump), Goombella just joined and what the opening gives given. Off: the "
                          "file menu as the original. Offer: {A} from the beginning, {Y} in Rogueport. Always: new "
                          "files start in Rogueport. Not in Console-accurate mode.",
                          0, "rogueport_start", kRogueport, 3,
                          "start new file rogueport skip opening prologue intro goombella quick start");
    portSettingsAddToggle(kTab, "Fast travel",
                          "In the Journal's map, {A} on a place travels to a room Mario has been in, by a way "
                          "in he has used; or to a town from Travel to. Only trips he could make on foot now: "
                          "never out of a stretch the story keeps him in.", kPlay,
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
                          "Keeps a save state of each room and battle as you enter it; on the Game Over screen, {Y} "
                          "goes back to it. A short pause at each room's start.",
                          kPlay, "gameover_retry", "game over retry continue save state death");
    portSettingsAddToggle(kTab, "Remake balance",
                          "Star Power as the remake gives it (more from the crowd, less from commands, Danger and "
                          "BINGO), Power Bounce's timing narrowing twice as fast, First Attack at 0 BP, Hammer "
                          "Throw a fixed 2 damage through defense.",
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
                          "the text calls them by their name. Bowser breathes fire for the hammer and has Fire Breath in "
                          "battle. Saves are not changed.",
                          0, "playable_luigi", kPlayAs, 4,
                          "play as character luigi peach bowser koopa princess mario green brother");
    portSettingsAddToggle(kTab, "Luigi's higher jump",
                          "While Luigi is picked, his jump goes a little higher and hangs a little longer at the "
                          "top, as in his own games. Battles are unchanged.",
                          kPlay, "luigi_high_jump", "luigi jump higher float hang air");
    portSettingsSetShown("luigi_high_jump", [] { return portGameplus("playable_luigi") == 1 ? 1 : 0; });
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
    // (By command type: Timing assist by command type below, port/settings/features/dojo.cpp)
    static const char* const kAssist[] = {"Off", "Wider windows", "Automatic", "By command type"};
    portSettingsAddChoice("Accessibility", "Action commands: attacks",
                          "Wider timing for attack commands, or all done for you.", kPlay,
                          "ac_attack_assist", kAssist, 4, "action command assist timing auto attack");
    portSettingsAddChoice("Accessibility", "Action commands: guards",
                          "Wider Guard and Superguard windows, or every attack guarded for you (a Superguard is "
                          "still up to you).",
                          kPlay, "ac_guard_assist", kAssist, 4, "action command assist guard superguard auto");
  }
} sRegister;

} // namespace

// Play as while Brothers mode is on: the pick is kept but not used (luigi_pc.c's portPlayAs)
extern "C" const char* portSettingWhyLocked(const int* value) {
  if (value != nullptr && value == portSettingsFind("playable_luigi") && portBrothersSetting()) {
    return "Not while Brothers mode is on.";
  }
  return nullptr;
}
