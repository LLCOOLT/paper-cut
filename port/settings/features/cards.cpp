// The Modes tab's Tattle cards (port/include/port/party.h, port/src/aurora/cards_aurora.c): the deck builder
// (the file's tattled enemies, their numbers and stars), the duels, and the Start menu's rows for them. A duel
// begins once the menu (or this page) is closed and Mario is free.
#include "port/companion.h"
#include "port/companion_menu.h"
#include "port/online.h"
#include "port/party.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr const char* kTab = "Modes";

std::string sMessage;

std::string name(int unit) {
  char out[96];
  portCompanionUtf8(portPartyEnemyName(unit), out, static_cast<int>(sizeof(out)), 0);
  return out;
}

int starsOf(const std::vector<int>& deck) {
  int n = 0;
  for (int u : deck) {
    n += portCardStars(u);
  }
  return n;
}

void deckSection() {
  int built[PORT_CARDS_DECK];
  const int n = portCardsBuilt(built);
  std::vector<int> deck(built, built + n);
  bool changed = false;
  ImGui::Text(portTr("Your deck: %d of %d cards, %d of %d stars"), static_cast<int>(deck.size()), PORT_CARDS_DECK,
              starsOf(deck), PORT_CARDS_STARS);
  int inUse[PORT_CARDS_DECK];
  const int used = portCardsDeck(inUse);
  if (used != n || !std::equal(inUse, inUse + used, built)) {
    ImGui::TextDisabled("%s", portTr("Not a full deck of this file's cards: the duels use the strongest that fit."));
  }
  if (ImGui::Button(portTrLabel("The strongest deck##cards"))) {
    std::vector<int> best(inUse, inUse + used);
    portCardsSetDeck(best.data(), static_cast<int>(best.size()));
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Empty the deck##cards"))) {
    portCardsSetDeck(nullptr, 0);
  }
  if (!ImGui::BeginTable("##cards", 6,
                         ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV,
                         ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 12.0f))) {
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn(portTr("Deck"), ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(portTr("Enemy"));
  ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(portTr("ATK"), ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(portTr("DEF"), ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(portTr("Stars"), ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableHeadersRow();
  for (int u = 1; u < PORT_PARTY_UNITS; u++) {
    if (!portCardsOwned(u)) {
      continue;
    }
    const PortCardStats card = portCardStats(u);
    const PortCardStats* s = &card;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const auto at = std::find(deck.begin(), deck.end(), u);
    bool in = at != deck.end();
    char id[24];
    std::snprintf(id, sizeof(id), "##card%d", u);
    const bool full = deck.size() >= PORT_CARDS_DECK || starsOf(deck) + portCardStars(u) > PORT_CARDS_STARS;
    ImGui::BeginDisabled(!in && full);
    if (ImGui::Checkbox(id, &in)) {
      if (in) {
        deck.push_back(u);
      } else {
        deck.erase(std::find(deck.begin(), deck.end(), u));
      }
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name(u).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%d", s->hp);
    ImGui::TableNextColumn();
    ImGui::Text("%d", s->atk);
    ImGui::TableNextColumn();
    ImGui::Text("%d", s->def);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(std::string(static_cast<size_t>(portCardStars(u)), '*').c_str());
  }
  ImGui::EndTable();
  if (changed) {
    portCardsSetDeck(deck.data(), static_cast<int>(deck.size()));
  }
}

void section(int inGame) {
  ImGui::TextWrapped("%s", portTr("Every enemy in the Tattle Log is a card with its own HP, Attack and Defense, and a "
                                  "cost of 1 to 5 stars. A deck is up to 8 cards and 20 stars. A duel is best of "
                                  "five: both play a card at once, unseen, and the two trade hits (Attack less "
                                  "Defense, 1 at least; the cheaper card strikes first) until one falls. The card "
                                  "players sit at Rogueport's Westside; a second controller or, online, the other "
                                  "player can duel too: then both draft five cards in turn from the same twelve."));
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Build a deck and duel while a file is in play."));
    return;
  }
  deckSection();
  ImGui::SeparatorText(portTr("Duels"));
  const bool online = portOnlineActive() != 0;
  ImGui::BeginDisabled(online || !portCardsAtTable() || portCardsActive());
  for (int i = 0; i < PORT_DUEL_NPCS; i++) {
    char label[96];
    std::snprintf(label, sizeof(label), "%s##duel%d", portTr(portCardsOpponent(i)), i);
    if (ImGui::Button(label)) {
      const char* why = nullptr;
      sMessage = portCardsDuel(i, &why) ? portTr("The duel begins once this page is closed.") : portTr(why);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(portTr("%d wins"), portCardsWins(i));
    if (i + 1 < PORT_DUEL_NPCS) {
      ImGui::SameLine();
    }
  }
  ImGui::EndDisabled();
  if (!portCardsAtTable()) {
    ImGui::TextDisabled("%s", portTr("The card players sit at Rogueport's Westside."));
  }
  if (int* talk = portSettingsFind("cards_talk")) {
    bool on = *talk != 0;
    if (ImGui::Checkbox(portTrLabel("Talking to them starts a duel too##cards_talk"), &on)) {
      *talk = on ? 1 : 0;
      portSettingsSave();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("(the rookie is a mouse, the regular a Goomba, the champion a Bandit; Z leaves "
                                     "a duel)"));
  }
  ImGui::BeginDisabled(online || portCardsActive());
  if (ImGui::Button(portTrLabel("Duel the second controller##duel"))) {
    const char* why = nullptr;
    sMessage = portCardsDuel(PORT_DUEL_PLAYER, &why) ? portTr("The duel begins once this page is closed.")
                                                       : portTr(why);
  }
  ImGui::EndDisabled();
  if (online) {
    ImGui::TextDisabled("%s", portTr("Online, start duels from the Start menu (Settings, Modes), so every game "
                                     "starts them together. The host duels the card players, with the host's deck."));
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// --- the Start menu ---------------------------------------------------------------

int deckLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int deck[PORT_CARDS_DECK];
  const int n = portCardsDeck(deck);
  int lines = 0;
  for (int i = 0; i < n && lines < max; i++) {
    const PortCardStats card = portCardStats(deck[i]);
    const PortCardStats* s = &card;
    std::snprintf(out[lines++], PORT_PAUSE_LINE, "%s  HP %d  ATK %d  DEF %d  %s", portPartyEnemyName(deck[i]),
                  s->hp, s->atk, s->def, std::string(static_cast<size_t>(portCardStars(deck[i])), '*').c_str());
  }
  if (lines == 0 && max > 0) {
    std::snprintf(out[lines++], PORT_PAUSE_LINE, "%s", portTrGame("No cards yet: tattle some enemies."));
  }
  return lines;
}

const PortPauseInfo kDeckInfo = {"Your Tattle card deck", deckLines, nullptr, nullptr, nullptr, nullptr, 0};

enum { kRowDeck = 100, kRowPlayer = 200 };

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < PORT_DUEL_NPCS + 2) {
    return 0;
  }
  const bool online = portOnlineActive() != 0;
  int deck[PORT_CARDS_DECK];
  const int cards = portCardsDeck(deck);
  PortPauseRow r = portPauseRow("Your Tattle card deck", "The cards the duels against the card players use (built on "
                                "the full settings page's Modes tab).", nullptr, kRowDeck);
  std::snprintf(r.value, sizeof(r.value), portTrGame("%d cards"), cards);
  out[n++] = r;
  for (int i = 0; i < PORT_DUEL_NPCS; i++) { // (online: the host's controller and deck, the others watch)
    PortPauseRow d = portPauseRow(nullptr, "A Tattle card duel: it begins once the menu is closed. The card players "
                                  "sit at Rogueport's Westside.", nullptr, i,
                                  portCardsAtTable() && !portCardsActive() ? 0 : PORT_PAUSE_ROW_DIM);
    std::snprintf(d.text, sizeof(d.text), portTrGame("Duel: %s"), portTrGame(portCardsOpponent(i)));
    std::snprintf(d.value, sizeof(d.value), portTrGame("%d wins"), portCardsWins(i));
    out[n++] = d;
  }
  PortPauseRow p = portPauseRow(online ? "Duel the other player" : "Duel the second controller",
                                "Both draft five cards in turn from the same twelve, then duel: it begins once the "
                                "menu is closed.",
                                nullptr, kRowPlayer, portCardsActive() ? PORT_PAUSE_ROW_DIM : 0);
  out[n++] = p;
  return n;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (id == kRowDeck) {
    portPauseInfoOpen(&kDeckInfo);
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  const char* why = nullptr;
  const int opponent = id == kRowPlayer ? PORT_DUEL_PLAYER : id;
  return portCardsDuel(opponent, &why) ? "The duel begins once the menu is closed." : why;
}

const PortPauseForm kForm = {kTab, "Tattle cards", formRows, formAct};

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Tattle cards", nullptr, 0, section,
                           "tattle cards deck duel card game westside enemies log draft");
    portPauseAddForm(&kForm);
    portSettingsDeclare("cards_talk", 1, 0, 1); // talking to the Westside's card players duels (cards_aurora.c)
  }
} sRegister;

} // namespace
