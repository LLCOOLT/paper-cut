// The remake's comfort and rule changes (port/include/port/qol.h): their
// controls on the Enhancements tab, all off by default, the Partner hint's
// key, and two Start menu pages: Notes (the Trouble taken, the open ones) and
// Controls (what each key and button does, from the bindings table). The
// "Remake rules" preset (enhancement_presets.cpp, remake_rules.inc) turns
// the changes on together.
#include "port/bindings.h"
#include "port/companion.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
#include "port/practice.h"
#include "port/settings.h"
#include "port/tr.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "port/qol.h"
}

namespace {

constexpr const char* kTab = "Enhancements";
constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;

// The game's text cut to a row's field (snprintf): on the Japanese release, in Shift-JIS, a cut
// can leave a letter's first byte alone at the end, which the font shows as another letter
// with the terminator's byte: such a byte is taken off.
void fit(char* text) {
  if (PORT_GAME_ID[3] != 'J') {
    return;
  }
  const size_t n = std::strlen(text);
  for (size_t i = 0; i < n;) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
      if (i + 1 >= n) {
        text[i] = '\0';
        return;
      }
      i += 2;
    } else {
      i++;
    }
  }
}

// --- Notes ---

// the lines of text (game encoding), each a dimmed row
int addLines(PortPauseRow* out, int n, int max, const char* text, const char* note) {
  const char* p = text;
  while (*p != '\0' && n < max) {
    const char* end = std::strchr(p, '\n');
    const size_t len = end != nullptr ? size_t(end - p) : std::strlen(p);
    if (len > 0) {
      PortPauseRow r = portPauseRow(nullptr, note, nullptr, -1, PORT_PAUSE_ROW_DIM);
      std::snprintf(r.text, sizeof(r.text), "  %.*s", int(len), p);
      fit(r.text);
      out[n++] = r;
    }
    p += len + (end != nullptr ? 1 : 0);
  }
  return n;
}

int notesRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 4) {
    return 0;
  }
  if (portNotesTroubleSolved(1) < 0) {
    out[n++] = portPauseRow("Shown while a game is in the field or a battle.", nullptr, nullptr, -1,
                            PORT_PAUSE_ROW_DIM);
    return n;
  }
  char client[96], contents[512];
  out[n++] = portPauseRow("Trouble taken", nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
  const int taken = portNotesTroubleTaken();
  if (taken == 0) {
    out[n++] = portPauseRow("None: the Trouble Center in Rogueport has jobs.", nullptr, nullptr, -1,
                            PORT_PAUSE_ROW_DIM);
  } else {
    portNotesTroubleText(taken, client, sizeof(client), contents, sizeof(contents));
    PortPauseRow r = portPauseRow(nullptr, "The job you took at the Trouble Center.", nullptr, 100 + taken);
    std::snprintf(r.text, sizeof(r.text), "#%d %s", taken, client);
    fit(r.text);
    out[n++] = r;
    n = addLines(out, n, max, contents, "The job you took at the Trouble Center.");
  }
  // Merluvlee's readings, in her words (the disc's text), newest first
  char fortune[512];
  bool headed = false;
  for (int i = 0; i < PORT_FORTUNE_COUNT && n + 1 < max; i++) {
    if (!portNotesFortune(i, fortune, sizeof(fortune))) {
      continue;
    }
    if (!headed) {
      out[n++] = portPauseRow("Merluvlee's predictions", nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
      headed = true;
    }
    n = addLines(out, n, max, fortune, "What Merluvlee foretold, newest first.");
  }
  if (n < max) {
    out[n++] = portPauseRow("Open Troubles", nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
  }
  for (int id = 1; id <= 30 && n < max; id++) {
    if (portNotesTroubleSolved(id) != 0 || id == taken) {
      continue;
    }
    portNotesTroubleText(id, client, sizeof(client), contents, sizeof(contents));
    PortPauseRow r = portPauseRow(nullptr, "Not solved yet: its job may not be on the board yet.", nullptr, 100 + id);
    std::snprintf(r.text, sizeof(r.text), "#%d %s", id, client);
    fit(r.text);
    out[n++] = r;
  }
  return n;
}

const char* notesAct(int, int) { return nullptr; }

const PortPauseForm kNotesForm = {kTab, "Notes", notesRows, notesAct};

// --- Controls at a glance ---

int controlsRows(PortPauseRow* out, int max) {
  int n = 0;
  const int game = portBindingsGameRows();
  for (int row = 0; row < portBindingsRowCount() && n < max; row++) {
    if (row == 0 || row == game) {
      out[n++] = portPauseRow(row == 0 ? "The game's buttons" : "Paper Cut's", nullptr, nullptr, -1,
                              PORT_PAUSE_ROW_HEADING);
      if (n >= max) {
        break;
      }
    }
    PortPauseRow r = portPauseRow(portBindingsRowName(row), "Change them on the Bindings page.", nullptr, row);
    // a copy of the key's text: the pad's call reuses the buffer the first one returned
    const char* cell = portBindingsCellText(row, 0);
    const std::string key = cell != nullptr ? cell : "";
    const char* pad = portBindingsCellText(row, 2);
    std::snprintf(r.value, sizeof(r.value), "%s / %s", key.c_str(), pad != nullptr ? pad : "");
    out[n++] = r;
  }
  return n;
}

// (a list to read: A on a row says where they change, rather than nothing)
const char* controlsAct(int id, int dir) {
  return id >= 0 && dir == 0 ? "Change them on the Bindings page." : nullptr;
}

const PortPauseForm kControlsForm = {"Controls", "Controls at a glance", controlsRows, controlsAct};

// --- The recipe book ---

int recipeRows(PortPauseRow* out, int max) {
  int n = 0;
  char name[64], hint[96];
  for (int i = 0; i < portRecipeCount() && n < max; i++) {
    const int cooked = portRecipeAt(i, name, sizeof(name), hint, sizeof(hint));
    if (cooked < 0) {
      if (n == 0) {
        out[n++] = portPauseRow("Shown while a game is in the field or a battle.", nullptr, nullptr, -1,
                                PORT_PAUSE_ROW_DIM);
      }
      break;
    }
    PortPauseRow r = portPauseRow(nullptr, cooked ? "Zess T. has cooked it for you." : "Not cooked yet: a hint.",
                                  cooked ? "Cooked" : nullptr, i, cooked ? 0 : PORT_PAUSE_ROW_DIM);
    std::snprintf(r.text, sizeof(r.text), "%s", name);
    if (!cooked) {
      std::snprintf(r.value, sizeof(r.value), "%s", hint);
    }
    fit(r.text);
    fit(r.value);
    out[n++] = r;
  }
  return n;
}

const char* recipeAct(int, int) { return nullptr; }

const PortPauseForm kRecipeForm = {kTab, "Recipe book", recipeRows, recipeAct};

// --- The Glitz Pit roster: every fighter and rank, as the lobby's board (the game's text) ---

int rosterRows(PortPauseRow* out, int max) {
  int n = 0;
  const char* name = nullptr;
  const char* title = nullptr;
  int isPlayer = 0;
  for (int i = 0; n < max && portTouRoster(i, &name, &title, &isPlayer); i++) {
    PortPauseRow r = portPauseRow(nullptr, isPlayer ? "You." : nullptr, nullptr, -1,
                                  isPlayer ? 0 : PORT_PAUSE_ROW_DIM);
    std::snprintf(r.text, sizeof(r.text), "%d  %s  %s", i, name != nullptr ? name : "",
                  title != nullptr ? title : "");
    if (i == 0) {
      std::snprintf(r.text, sizeof(r.text), "%s  %s  %s", portTrGame("Champion"), name != nullptr ? name : "",
                    title != nullptr ? title : "");
    }
    fit(r.text);
    out[n++] = r;
  }
  if (n == 0 && max > 0) {
    out[n++] = portPauseRow("Shown in Glitzville, once the Glitz Pit has a ranking.", nullptr, nullptr, -1,
                            PORT_PAUSE_ROW_DIM);
  }
  static int sTold = -1; // once for each length (the tests' signal)
  if (sTold != n) {
    sTold = n;
    std::printf("[roster] Glitz Pit roster: %d rows\n", n);
  }
  return n;
}

const char* rosterAct(int, int) { return nullptr; }

const PortPauseForm kRosterForm = {kTab, "Glitz Pit roster", rosterRows, rosterAct};

// the settings page's copy of a form: its rows as text. gameText: the rows' own text and values are the
// game's (Latin-1, or the JP release's Shift-JIS: names, Troubles, dishes), turned to UTF-8 for ImGui;
// the Controls rows' key names are UTF-8 already.
void drawRows(int (*rows)(PortPauseRow*, int), bool gameText) {
  static PortPauseRow sRows[96];
  static char sText[256], sValue[256];
  const int n = rows(sRows, 96);
  for (int i = 0; i < n; i++) {
    const PortPauseRow& row = sRows[i];
    const char* text = row.label != nullptr ? portTr(row.label) : row.text;
    const char* value = row.valueTr != nullptr ? portTr(row.valueTr) : row.value;
    if (gameText && row.label == nullptr) {
      portCompanionUtf8(row.text, sText, sizeof(sText), 0);
      text = sText;
    }
    if (gameText && row.valueTr == nullptr && row.value[0] != '\0') {
      portCompanionUtf8(row.value, sValue, sizeof(sValue), 0);
      value = sValue;
    }
    if (row.flags & PORT_PAUSE_ROW_HEADING) {
      ImGui::SeparatorText(text);
    } else if (value[0] != '\0') {
      ImGui::Text("%s: %s", text, value);
    } else if (row.flags & PORT_PAUSE_ROW_DIM) {
      ImGui::TextDisabled("%s", text);
    } else {
      ImGui::TextUnformatted(text);
    }
  }
}

void notesSection(int) { drawRows(notesRows, true); }
void controlsSection(int) { drawRows(controlsRows, false); }
void recipeSection(int) { drawRows(recipeRows, true); }
void rosterSection(int) { drawRows(rosterRows, true); }

int hintPressed(int down, int repeat) {
  if (down && !repeat) {
    portPartnerHintPress();
  }
  return 1;
}

struct Register {
  Register() {
    portQolDeclare();
    portSettingsAddToggle(kTab, "Coins up to 9,999",
                          "Mario carries up to 9,999 coins (a fourth digit on the screen), and a battle with Money "
                          "Money pays up to 99. A copy for Dolphin or a GameCube keeps 999.",
                          kPlay | PORT_SETTING_CHANGES_SAVES, "coin_cap", "coins cap 9999 money money remake wallet");
    portSettingsAddToggle(kTab, "Use healing items when the bag is full",
                          "When the bag is full, the item you choose to leave behind is eaten instead if it heals "
                          "Mario or the partner.",
                          kPlay, "full_bag_use", "bag full discard throw away use heal item remake");
    portSettingsAddToggle(kTab, "Level-ups heal every partner",
                          "A level-up fills every partner's HP, not only the one in battle (HP Plus P counted).",
                          kPlay, "levelup_heal_party", "level up heal partner hp remake");
    portSettingsAddToggle(kTab, "Partner hint",
                          "The Partner hint key or button (H; Controls > Bindings) has the partner say where the "
                          "story goes next.",
                          0, "partner_hint", "partner hint where next help goombella remake");
    portSettingsAddToggle(kTab, "Mail without stopping",
                          "New mail is a notice in the corner and a chime: Mario walks on. The first mails still "
                          "play the scene, as they teach the Mailbox SP.",
                          0, "mail_no_stop", "mail mailbox sp notice email stop remake");
    portSettingsAddToggle(kTab, "Shop improvements",
                          "In shops, how many of the item you hold and your shop points under its name; selling, "
                          "storing and taking out go straight back to the list; selling from storage too; a storage "
                          "box at the Howz of Badges; a notice when the Rogueport shop has new stock.",
                          0, "shop_plus", "shop buy sell store storage points held count remake");
    portSettingsAddToggle(kTab, "Prompts over things to check",
                          "The \"!\" over Mario shows at every spot the A button acts on, not only the ones the "
                          "game marks, and near someone Goombella has not Tattled yet.",
                          0, "talk_prompts", "prompt balloon exclamation check talk sign remake");
    portSettingsAddToggle(kTab, "Save-file details",
                          "The file screen shows each file's partner and the date it was saved, a colour per file, "
                          "and a gold star on a finished story.",
                          0, "save_file_details", "save file screen slot partner date colour medal remake");
    portSettingsAddToggle(kTab, "Left and Right page through lists",
                          "In the Start menu's items and badges, Left and Right turn the page (past the edge of "
                          "the items' two columns).",
                          0, "list_lr_scroll", "scroll page list items badges left right remake");
    portSettingsAddToggle(kTab, "Retry in Bowser's stages",
                          "In Bowser's side-scrolling stages, Y on the pause screen plays the stage again from its "
                          "start (a save state taken as it begins).",
                          kPlay, "bowser_retry", "bowser retry restart stage intermission pause remake");
    static const char* const kJukebox[] = {"Heard on this file", "Remake: a share per Shine Sprite found",
                                           "Every track"};
    portSettingsAddChoice(kTab, "Jukebox tracks",
                          "Which tracks the Jukebox lists: those heard on this file; also a share of the list for "
                          "each Shine Sprite found (all with the last); or every track.",
                          0, "jukebox_unlock", kJukebox, 3, "jukebox sound gallery unlock all shine sprite music");
    portSettingsAddToggle(kTab, "Hints for coins",
                          "The Journal sells a hint for 10 coins: where a Star Piece or Shine Sprite is still "
                          "hidden, in this area first.",
                          kPlay, "coin_hints", "hint coins star piece shine sprite journal ian foomus remake");
    static const char* const kSkipInterludes[] = {"Off", "Seen only", "Always"};
    portSettingsAddChoice(kTab, "Skip Peach's and Bowser's interludes",
                          "The story goes straight on with Mario, with all the interlude would have left. Seen "
                          "only: an interlude seen on any file before. Always: the first time too. Not online.",
                          kPlay, "skip_interludes", kSkipInterludes, 3,
                          "skip interlude peach bowser replay story always");
    portSettingsAddToggle(kTab, "Glitz Pit and chapter 6 pace trims",
                          "After your first Glitz Pit match, no guard walks you to the ring: straight there. Every "
                          "Excess Express trip after chapter 6 goes straight to the other station. Not online.",
                          kPlay, "pace_trims", "pace glitz pit guard ring train express poshley trip faster");
    portBindingsSetHandler(PORT_BIND_PARTNER_HINT, hintPressed);
    portSettingsAddSection(kTab, "Notes", "The Trouble you took, Merluvlee's predictions and the open Troubles.", PORT_SETTING_IN_GAME,
                           notesSection, "notes trouble center job request open remake");
    portSettingsAddSection("Controls", "Controls at a glance", "What each key and button does now.", 0,
                           controlsSection, "controls keys buttons list glance help");
    portSettingsAddSection(kTab, "Recipe book", "Every dish, and a hint for the ones not cooked yet.",
                           PORT_SETTING_IN_GAME, recipeSection, "recipe book cooking zess hint dish ingredient remake");
    portSettingsAddSection(kTab, "Glitz Pit roster", "Every fighter and rank, as the lobby's board shows them.",
                           PORT_SETTING_IN_GAME, rosterSection, "glitz pit roster ranking fighters rank league remake");
    portPauseAddForm(&kRecipeForm);
    portPauseAddForm(&kRosterForm);
    portPauseAddForm(&kNotesForm);
    portPauseAddForm(&kControlsForm);
  }
} sRegister;

} // namespace

// --- Hints for coins: the Journal's rows (gameplus.cpp journalRows) ---

namespace {
char sCoinHint[128]; // the last hint bought (game encoding), in the file loaded then:
int sCoinHintFile = -1; // (portPracticeFileLoads; another file's hint isn't this one's)
constexpr int kCoinHintRow = 900;
} // namespace

int portQolJournalRows(PortPauseRow* out, int n, int max) {
  if (sCoinHintFile != portPracticeFileLoads()) {
    sCoinHint[0] = '\0';
  }
  if (!portQol("coin_hints") || n + 2 > max) {
    return n;
  }
  PortPauseRow r = portPauseRow("A hint for coins", "Where a Star Piece or a Shine Sprite is still hidden.", nullptr,
                                kCoinHintRow, PORT_PAUSE_ROW_CONFIRM);
  std::snprintf(r.value, sizeof(r.value), portTrGame("%d coins"), portCoinHintPrice());
  out[n++] = r;
  if (sCoinHint[0] != '\0') {
    PortPauseRow h = portPauseRow(nullptr, "The last hint you bought.", nullptr, -1, PORT_PAUSE_ROW_DIM);
    std::snprintf(h.text, sizeof(h.text), "%s", sCoinHint);
    fit(h.text);
    out[n++] = h;
  }
  return n;
}

const char* portQolJournalAct(int id, int dir) {
  if (id != kCoinHintRow || dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  sCoinHintFile = portPracticeFileLoads();
  switch (portCoinHintBuy(sCoinHint, sizeof(sCoinHint))) {
  case 1: return "Bought: the hint is under the row.";
  case 0: return "Not enough coins.";
  case -1: return "Every Star Piece and Shine Sprite has been found.";
  }
  return "Shown while a game is in the field.";
}
