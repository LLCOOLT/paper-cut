// The "Randomizer" tab (port/archipelago/ttyd_rando.h, docs/RANDOMIZER.md):
// the built-in item randomizer's seed and what it shuffles, for the next
// start's game; what this run's game was given.
#include "port/settings.h"
#include "port/tr.h"
#include "../../archipelago/ttyd_ap.h"
#include "../../archipelago/ttyd_rando.h"
#include "port/open_world.h"
#include "../../archipelago/rando_gen.h"
#include "port/rando_tracker.h"
#include "port/sound_shuffle.h"
#include "../settings_ui.h"
#include <imgui.h>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

using portui::hint;

namespace {

constexpr const char* kTab = "Randomizer";

void warn(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
  ImGui::TextWrapped("%s", portTr(text));
  ImGui::PopStyleColor();
}

std::string sCheck; // the last "Check this seed"
char sPresetName[48];
char sPaste[96];
std::string sPasteNote;
int sPreset = 0;

// How strict the logic is, and the tricks it may count on
void logicRows(PortRandoSettings* s, bool& changed) {
  static const char* const kLevels[] = {"Glitchless", "Tricks", "No logic"};
  int level = s->logicLevel >= 0 && s->logicLevel <= 2 ? s->logicLevel : 0;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  if (ImGui::BeginCombo(portTrLabel("Logic"), portTr(kLevels[level]))) {
    for (int i = 0; i < 3; i++) {
      if (ImGui::Selectable(portTr(kLevels[i]), i == level)) {
        s->logicLevel = i;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  hint("Glitchless: every item is placed where the game can be played to it without tricks. Tricks: the tricks "
       "chosen below count too, so an item may wait behind one. No logic: items go anywhere, and a seed may not be "
       "finishable.");
  if (s->logicLevel == 1) {
    for (const ttyd_rando::TrickInfo& t : ttyd_rando::tricks()) {
      bool on = (s->tricks & int(t.bit)) != 0;
      ImGui::Indent();
      if (ImGui::Checkbox(portTrLabel(t.name), &on)) {
        s->tricks = on ? s->tricks | int(t.bit) : s->tricks & ~int(t.bit);
        changed = true;
      }
      ImGui::Unindent();
    }
    hint("Yoshi Skip: the west sewers' gap to Twilight Town's and Fahr Outpost's pipes without Yoshi. Early Ultra "
         "Hammer: Rogueport's roof chest with the Super Boots' spin jump. Palace Skip: from the palace's first rooms "
         "to the Shadow Queen. Paper Cut's open world only; the TTYD AP mod's logic has its own.");
  }
}

// Named presets and the permalink
void presetRows(PortRandoSettings* s, bool& changed) {
  ImGui::TextUnformatted(portTr("Presets"));
  const int count = portRandoPresetCount();
  if (sPreset >= count) sPreset = 0;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  if (ImGui::BeginCombo(portTrLabel("Preset"),
                        portRandoPresetBuiltIn(sPreset) ? portTr(portRandoPresetName(sPreset)) : portRandoPresetName(sPreset))) {
    for (int i = 0; i < count; i++) {
      const char* name = portRandoPresetBuiltIn(i) ? portTr(portRandoPresetName(i)) : portRandoPresetName(i);
      ImGui::PushID(i);
      if (ImGui::Selectable(name, i == sPreset)) sPreset = i;
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Use preset"))) {
    portRandoApplyPreset(sPreset);
    sCheck.clear();
  }
  if (!portRandoPresetBuiltIn(sPreset)) {
    ImGui::SameLine();
    static int sConfirmDelete = -1; // pressed once: pressed again to delete
    if (sConfirmDelete != sPreset) {
      if (ImGui::Button(portTrLabel("Delete preset"))) sConfirmDelete = sPreset;
    } else {
      if (ImGui::Button(portTrLabel("Delete it for good"))) {
        portRandoDeletePreset(sPreset);
        sConfirmDelete = -1;
      }
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Keep it"))) sConfirmDelete = -1;
    }
  } else if (portRandoPresetNote(sPreset)[0] != '\0') {
    hint(portRandoPresetNote(sPreset));
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::InputText(portTrLabel("Name"), sPresetName, sizeof(sPresetName));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Save as a preset")) && sPresetName[0] != '\0') {
    portRandoSavePreset(sPresetName);
    sPresetName[0] = '\0';
  }
  hint("A preset is everything that makes the game but the seed: applying one keeps the seed.");

  ImGui::TextUnformatted(portTr("Permalink"));
  const char* link = portRandoPermalink();
  ImGui::TextWrapped("%s", link);
  if (ImGui::Button(portTrLabel("Copy the permalink"))) ImGui::SetClipboardText(link);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
  ImGui::InputText(portTrLabel("Paste a permalink"), sPaste, sizeof(sPaste));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Use it"))) {
    const char* why = portRandoApplyPermalink(sPaste);
    sPasteNote = why != nullptr ? portTr(why) : portTr("The seed and settings are the permalink's.");
    if (why == nullptr) {
      sPaste[0] = '\0';
      sCheck.clear();
    }
  }
  if (!sPasteNote.empty()) hint(sPasteNote.c_str());
  hint("One text for the seed and every choice that makes the game: share it, and the same game comes out on any "
       "computer or phone of the same version.");
  (void)s;
  (void)changed;
}

// The check tracker (port/include/port/rando_tracker.h): every place the seed
// in play shuffled, by area, with a search box
char sSearch[64];
int sTrackerFrame = -1000;

bool matches(const char* text, const char* needle) {
  if (needle[0] == '\0') return true;
  for (const char* t = text; *t != '\0'; t++) {
    size_t k = 0;
    while (needle[k] != '\0' && t[k] != '\0' &&
           std::tolower(static_cast<unsigned char>(t[k])) == std::tolower(static_cast<unsigned char>(needle[k]))) {
      k++;
    }
    if (needle[k] == '\0') return true;
  }
  return false;
}

void trackerRows(int inGame) {
  if (!inGame || !ImGui::CollapsingHeader(portTrLabel("Tracker"))) return;
  if (ImGui::GetFrameCount() - sTrackerFrame > 30) { // (twice a second: it reads the game's flags and pouch)
    portTrackerRefresh();
    sTrackerFrame = ImGui::GetFrameCount();
  }
  if (!portTrackerAvailable()) {
    hint("No seed is being played: start a new file with the randomizer on.");
    return;
  }
  int found = 0, reachable = 0;
  const int total = portTrackerTotals(&found, &reachable);
  if (portTrackerHasLogic()) {
    ImGui::Text(portTr("Found %d of %d places; %d more reachable now."), found, total, reachable);
  } else {
    ImGui::Text(portTr("Found %d of %d places."), found, total);
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::InputText(portTrLabel("Search"), sSearch, sizeof(sSearch));
  static bool onlyReachable = false;
  if (portTrackerHasLogic()) {
    ImGui::SameLine();
    ImGui::Checkbox(portTrLabel("Reachable now only"), &onlyReachable);
  }
  const ImVec4 colors[3] = {ImVec4(0.85f, 0.4f, 0.35f, 1.0f), ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                            ImVec4(0.55f, 0.55f, 0.55f, 1.0f)};
  for (int a = 0; a < portTrackerAreaCount(); a++) {
    const PortTrackerArea* ar = portTrackerArea(a);
    std::vector<int> shown;
    for (int i = 0; i < portTrackerPlaceCount(a); i++) {
      const int state = portTrackerPlaceState(a, i);
      if (onlyReachable && portTrackerHasLogic() && state != PORT_TRACKER_REACHABLE) continue;
      if (!matches(portTrackerPlaceName(a, i), sSearch) && !matches(portTrackerPlaceRoom(a, i), sSearch) &&
          !matches(ar->name, sSearch)) {
        continue;
      }
      shown.push_back(i);
    }
    if (shown.empty()) continue;
    char head[160];
    std::snprintf(head, sizeof(head), "%s  %d/%d%s###area%d", ar->name, ar->found, ar->total,
                  portTrackerHasLogic() && ar->reachable > 0 ? (" +" + std::to_string(ar->reachable)).c_str() : "", a);
    if (sSearch[0] != '\0') ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!ImGui::TreeNode(head)) continue;
    for (int i : shown) {
      const int state = portTrackerPlaceState(a, i);
      ImGui::PushStyleColor(ImGuiCol_Text, colors[state >= 0 && state <= 2 ? state : 0]);
      ImGui::BulletText("%s: %s", portTrackerPlaceRoom(a, i), portTrackerPlaceName(a, i));
      ImGui::PopStyleColor();
    }
    ImGui::TreePop();
  }
  hint("Green: reachable now with what Mario holds. Red: not yet. Grey: found. Place names are the TTYD apworld's.");
}

// The sound editor (port/include/port/sound_shuffle.h): music or sound effects
// replaced by name, browsed from the game's own tables
int sEditKind = 0; // 0 music, 1 sound effects
char sEditFilter[48];
std::string sEditPick, sEditFrom, sEditTo;

void soundEditorRows() {
  if (!ImGui::CollapsingHeader(portTrLabel("Sound editor"))) return;
  hint("Replace a piece of music, or a sound effect, with another of the game's: pick one in the list, press "
       "\"Replace this\", pick its replacement, press \"With this\", then \"Add\". It applies at once and every start, "
       "over the shuffle; it changes only what is heard.");
  if (ImGui::RadioButton(portTrLabel("Music"), sEditKind == 0)) sEditKind = 0;
  ImGui::SameLine();
  if (ImGui::RadioButton(portTrLabel("Sound effects"), sEditKind == 1)) sEditKind = 1;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::InputText(portTrLabel("Filter"), sEditFilter, sizeof(sEditFilter));
  const int count = sEditKind ? portPsndSfxCount() : portPsndBgmCount();
  if (ImGui::BeginChild("##names", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f), true)) {
    int shown = 0;
    for (int i = 1; i < count && shown < 400; i++) {
      const char* name = sEditKind ? portPsndSfxName(i) : portPsndBgmName(i);
      if (name[0] == '\0' || !matches(name, sEditFilter)) continue;
      shown++;
      if (ImGui::Selectable(name, sEditPick == name)) sEditPick = name;
    }
  }
  ImGui::EndChild();
  if (ImGui::Button(portTrLabel("Replace this")) && !sEditPick.empty()) sEditFrom = sEditPick;
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("With this")) && !sEditPick.empty()) sEditTo = sEditPick;
  ImGui::Text("%s -> %s", sEditFrom.empty() ? "?" : sEditFrom.c_str(), sEditTo.empty() ? "?" : sEditTo.c_str());
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Add")) && !sEditFrom.empty() && !sEditTo.empty()) {
    portRandoSoundEditSet(sEditKind, sEditFrom.c_str(), sEditTo.c_str());
    sEditFrom.clear();
    sEditTo.clear();
  }
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < portRandoSoundEditCount(k); i++) {
      ImGui::PushID(k * 10000 + i);
      if (ImGui::SmallButton("x")) {
        const std::string from = portRandoSoundEditFrom(k, i);
        portRandoSoundEditSet(k, from.c_str(), "");
        ImGui::PopID();
        break;
      }
      ImGui::SameLine();
      ImGui::Text("%s -> %s", portRandoSoundEditFrom(k, i), portRandoSoundEditTo(k, i));
      ImGui::PopID();
    }
  }
}

void section(int inGame) {
  PortRandoSettings* s = portRandoSettings(portSettingsUserPath());
  bool changed = false;
  hint("Shuffles the game's items among their places from a seed, with no Archipelago server: turn it on, pick "
       "the seed and what moves, then start the game and begin a new file. The same seed and choices always give "
       "the same game, so a seed can be shared.");

  const char* active = portRandoActive();
  if (active[0] != '\0') {
    ImGui::TextWrapped(portTr("This run: %s"), active);
  } else if (inGame) {
    ImGui::TextWrapped("%s", portTr("This run: not randomized."));
  }
  if (portOpenOn()) {
    ImGui::TextWrapped(portTr("This file: Paper Cut's open world, seed %s"), portOpenFileSeed());
  }
  if (portApSeedPath()[0] != '\0') {
    warn("An Archipelago seed is chosen on the Archipelago tab; it is used instead of this one.");
    if (ImGui::Button(portTrLabel("Use the built-in randomizer instead"))) {
      portApSetSeed("");
    }
  }
  ImGui::Separator();
  // online or in a replay the settings are the session's (the host's, the recording's): shown, not changed
  const bool shared = portRandoShared() != 0;
  if (shared) {
    warn("These are this session's settings (online play's or the replay's): change yours after it.");
  }
  // (changing them here would stamp another seed on this side's next new file)
  ImGui::BeginDisabled(shared);
  bool enabled = s->enabled != 0;
  if (ImGui::Checkbox(portTrLabel("Randomize the game from the next start"), &enabled)) {
    s->enabled = enabled;
    changed = true;
  }
  hint("Every file played while it is on gets this seed's items, so start a new file for it, and turn it off "
       "again for your other files.");

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  if (ImGui::InputText(portTrLabel("Seed"), s->seed, sizeof(s->seed))) {
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("New seed"))) {
    portRandoNewSeed();
    sCheck.clear();
  }
  if (s->seed[0] == '\0') {
    hint("Any text. Empty: a random one is made at the start (and kept here).");
  }

  ImGui::TextUnformatted(portTr("What is shuffled"));
  auto box = [&](const char* label, int& v) {
    bool b = v != 0;
    if (ImGui::Checkbox(label, &b)) {
      v = b;
      changed = true;
    }
  };
  box("Badges", s->badges);
  box("Items", s->items);
  box("Star Pieces", s->starPieces);
  box("Shine Sprites", s->shineSprites);
  box("Trouble rewards", s->troubles);
  box("Shop stock", s->shops);
  box("Mystery items", s->mystery);
  hint("Trouble rewards: the troubles that hand over an item take part (the ones that pay coins don't). Shop "
       "stock: the shops' items swap shelves among all the shops, each at its new item's own price (badges stay "
       "out: a shop would sell one again and again). Mystery items: an item lying in a place shows as a Mystery "
       "until it is picked up.");
  box("Paper Cut's open world: chapters 1 to 8 in any order", s->openWorld);
  if (s->openWorld) {
    hint("Key items and partners move too (Koops, Flurrie, Yoshi, Vivian, Bobbery, the curses, the keys, the "
         "tickets, the boots and hammers...), placed so the goal's Crystal Stars can always be reached, and each "
         "chapter's story runs on its own: after the prologue, every chapter opens by its items (the pipes, the "
         "blimp and train tickets, the Old Letter for Flavio's ship, the Ultra Hammer for chapter 7's pipe), in "
         "whatever order they allow. Rogueport's own scenes follow how many chapters are done. No mod needed. It "
         "is the file's, not the run's: each new file started while this is on takes the seed (the file keeps "
         "it), and other files stay as they are. Badges, items or Star Pieces must be shuffled too.");
    int goal = s->openGoal > 0 && s->openGoal < 7 ? s->openGoal : 7;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::SliderInt(portTrLabel("Crystal Stars for the goal"), &goal, 1, 7)) {
      s->openGoal = goal >= 7 ? 0 : goal;
      changed = true;
    }
    hint("The goal: this many Crystal Stars, from any chapters. The logic makes sure that many can be reached; a "
         "notice says when they are, and the Thousand-Year Door opens with them (chapter 8, the palace, leads to "
         "the end). The others may need items that lie past the goal.");
    if (portOpenOn() && portOpenFileGoal() > 0) {
      ImGui::TextWrapped(portTr("This file's goal: %d Crystal Stars"), portOpenFileGoal());
    }
    box("Shuffle the chapter gates", s->gates);
    hint("The sewers' four chapter pipes (to Petal Meadows, Boggly Woods, Twilight Town and Fahr Outpost) lead to "
         "one another's chapters, both ways, by the seed; each pipe still needs what its own room needs, and the "
         "logic places the items by where the pipes lead. The spoiler log says which goes where.");
    box("Shuffle the room doors", s->rooms);
    hint("The doors between the rooms of a town or an open field lead to other rooms of the same area, both ways, "
         "always so that every place can still be walked to.");
    box("Mix the ways between areas", s->mixWays);
    hint("The doors that are the only way from one area into the next lead into one another's areas, both ways. "
         "Each asks for what both of its doors asked for, and the logic places the items by it.");
    box("Shuffle the blimp, the ship and the train", s->transports);
    hint("The blimp, the ship and the train take Mario to one another's destinations. Each still asks for its own "
         "ticket, and the title card shows the chapter he actually arrives in.");
  } else if (portRandoApWorldSupported()) {
    box("Key items (open world: needs the TTYD AP mod)", s->keyItems);
  }
  if (s->keyItems && !s->openWorld && portRandoApWorldSupported()) {
    hint("Key items, partners, the curses, hammer and boots move too, placed by the TTYD apworld's logic so the "
         "Shadow Queen can always be reached. That logic is the TTYD AP mod's open world (chapters open by "
         "items, not the story), so the mod must be installed as a code mod (Mods tab, docs/MODDING.md); "
         "without it the game can't be finished. Badges, items or Star Pieces must be shuffled too.");
    if (!portRandoApModFound()) {
      warn("No enabled code mod looks like the TTYD AP mod.");
    }
  } else {
    hint("Without key items the story is the game's own: only things the story never asks for move (badges, "
         "items nothing needs, Star Pieces, Shine Sprites, in the places the game hands them out itself), so "
         "every seed can be finished. No mod needed.");
  }
  if (s->openWorld || (s->keyItems && portRandoApWorldSupported())) {
    logicRows(s, changed);
  }
  box("Write a spoiler log", s->spoiler);
  hint("At the start, to the randomizer folder in the user folder (spoiler_<seed>.txt): where every item went "
       "and, for the open world, the order the logic expects key items to be found in.");
  box("Music shuffle", s->music);
  box("Sound shuffle", s->sounds);
  hint("Music: the areas' themes swap among themselves, and the battle and boss themes among theirs (fanfares and "
       "scenes' music stay). Sounds: each sound effect plays another of its own family (enemies', Mario's...). From "
       "the seed, while the randomizer is on; only what is heard changes, at once.");
  if (shared) changed = false;
  ImGui::Separator();
  presetRows(s, changed);
  ImGui::EndDisabled();
  trackerRows(inGame);
  ImGui::BeginDisabled(shared);
  soundEditorRows();
  ImGui::EndDisabled();

  if (ImGui::Button(portTrLabel("Check this seed"))) {
    sCheck = portRandoCheck();
  }
  if (!sCheck.empty()) {
    ImGui::SameLine();
    ImGui::TextWrapped("%s", sCheck.c_str());
  }
  if (inGame) {
    hint("Changes apply the next time the game starts.");
  }
  if (changed) {
    sCheck.clear();
    portRandoSaveSettings();
  }
}

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Built-in randomizer", nullptr,
                           PORT_SETTING_NEXT_START | PORT_SETTING_CHANGES_PLAY, section,
                           "randomizer random seed shuffle items badges key items star pieces shine sprites spoiler "
                           "log archipelago logic new file open world chapters partners tricks glitchless yoshi skip "
                           "ultra hammer palace skip preset permalink");
  }
} sRegister;

} // namespace
