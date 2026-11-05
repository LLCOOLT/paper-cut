// The "Randomizer" tab (port/archipelago/ttyd_rando.h, docs/RANDOMIZER.md):
// the built-in item randomizer's seed and what it shuffles, for the next
// start's game; what this run's game was given.
#include "port/settings.h"
#include "port/tr.h"
#include "../../archipelago/ttyd_ap.h"
#include "../../archipelago/ttyd_rando.h"
#include "port/open_world.h"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace {

constexpr const char* kTab = "Randomizer";

void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void warn(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

std::string sCheck; // the last "Check this seed"

void section(int inGame) {
  if (!portRandoSupported()) {
    warn("The randomizer knows where the items are in the US version only (its location data is the TTYD "
         "apworld's, made for the US disc), so it is off on this version. Play the US version to use it.");
    return;
  }
  PortRandoSettings* s = portRandoSettings(portSettingsUserPath());
  bool changed = false;
  note("Shuffles the game's items among their places from a seed, with no Archipelago server: turn it on, pick "
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

  bool enabled = s->enabled != 0;
  if (ImGui::Checkbox(portTrLabel("Randomize the game from the next start"), &enabled)) {
    s->enabled = enabled;
    changed = true;
  }
  note("Every file played while it is on gets this seed's items, so start a new file for it, and turn it off "
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
    note("Any text. Empty: a random one is made at the start (and kept here).");
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
  box("Paper Cut's open world: chapters 1 and 2 in any order", s->openWorld);
  if (s->openWorld) {
    note("Key items and partners move too (Koops, Flurrie, the curses, the keys, the Super Boots...), placed so both "
         "chapters' Crystal Stars can always be reached, and each chapter's story runs on its own: after the "
         "prologue, Petal Meadows and Boggly Woods are both open, in whatever order the items allow. No mod "
         "needed. It is the file's, not the run's: each new file started while this is on takes the seed (the "
         "file keeps it), and other files stay as they are. Badges, items or Star Pieces must be shuffled too. "
         "Chapters 3 to 8 come in later versions.");
  } else {
    box("Key items (open world: needs the TTYD AP mod)", s->keyItems);
  }
  if (s->keyItems && !s->openWorld) {
    note("Key items, partners, the curses, hammer and boots move too, placed by the TTYD apworld's logic so the "
         "Shadow Queen can always be reached. That logic is the TTYD AP mod's open world (chapters open by "
         "items, not the story), so the mod must be installed as a code mod (Mods tab, docs/MODDING.md); "
         "without it the game can't be finished. Badges, items or Star Pieces must be shuffled too.");
    if (!portRandoApModFound()) {
      warn("No enabled code mod looks like the TTYD AP mod.");
    }
  } else {
    note("Without key items the story is the game's own: only things the story never asks for move (badges, "
         "items nothing needs, Star Pieces, Shine Sprites, in the places the game hands them out itself), so "
         "every seed can be finished. No mod needed.");
  }
  box("Write a spoiler log", s->spoiler);
  note("At the start, to the randomizer folder in the user folder (spoiler_<seed>.txt): where every item went "
       "and, for the open world, the order the logic expects key items to be found in.");

  if (ImGui::Button(portTrLabel("Check this seed"))) {
    sCheck = portRandoCheck();
  }
  if (!sCheck.empty()) {
    ImGui::SameLine();
    ImGui::TextWrapped("%s", sCheck.c_str());
  }
  if (inGame) {
    note("Changes apply the next time the game starts.");
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
                           "log archipelago logic new file open world chapters partners");
  }
} sRegister;

} // namespace
