// The launcher's onboarding (port/settings/launcher_onboard.h), and Settings > General > Tips (Show tips
// again: port/include/port/tips.h).
//
// - What's new: on the first launch after an update (launcher_news_seen in settings.txt below this
//   build's version; not on a first start ever, not in a dev build), a paper card over the stage with
//   0.5.0's headline features in short lines, each with a button to its page or setting. Closing it marks
//   it seen (and the game's own first-start window too: one "what's new" is enough). About reopens it.
// - Have Paper Mario 64? Link it: the Play tab's Paper Mario 64 row, while no ROM is linked, says it, and its
//   What it adds opens what linking unlocks; "Don't show again" (launcher_pm64_card_hidden) leaves the plain row.
// - Turn on all Paper Mario 64 content?: a ROM just linked (enhancement_presets.h), a small card over the
//   stage, Yes first and chosen, so the Play tab under it keeps its room ($TTYD_PM64_ASK_ANSWER=yes or no
//   answers it: tests).
// - How multiplayer works: About's button (and What's new's) opens a page of two halves, on one screen and
//   online, a line each with a picture: the game's own faces from the art cache (launcher_popup.h; none
//   before the game first ran on a disc: the players' colours instead) and the controller's icons.
#include "../enhancement_presets.h"
#include "../launcher_look.h"
#include "../launcher_onboard.h"
#include "../launcher_popup.h"
#include "../launcher_style.h"
#include "port/button_markup.h"
#include "port/menu_buttons.h"
#include "port/pause_forms.h"
#include "port/pm64.h"
#include "port/settings.h"
#include "port/tips.h"
#include "port/tr.h"
#include "port/whats_new.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // FocusItem

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void portSettingsShowTab(const char* tab);     // settings_ui.cpp
extern "C" void portSettingsShowSearch(const char* label); // (the same: one setting found)

namespace {

enum class Overlay { None, News, Help, Pm64Ask };
Overlay sUp = Overlay::None;
bool sFocus = false; // the card just opened: the focus to its first button
int sHalf = 0;       // the help page where only one half fits: 0 on one screen, 1 online

// "0.5.0" -> 500, "1.2.3" -> 10203; 0 for anything else ("dev"), as whats_new_aurora.c reads it
int versionNumber(const char* v) {
  int n[3] = {0, 0, 0}, parts = 0;
  const char* p = v;
  while (parts < 3) {
    char* end;
    const long x = std::strtol(p, &end, 10);
    if (end == p || x < 0 || x > 99) {
      return 0;
    }
    n[parts++] = static_cast<int>(x);
    if (*end == '\0') {
      break;
    }
    if (*end != '.') {
      return 0;
    }
    p = end + 1;
  }
  return n[0] * 10000 + n[1] * 100 + n[2];
}

int* newsSeen() {
  static int* sSeen = portSettingsDeclare("launcher_news_seen", 0, 0, 999999);
  return sSeen;
}

int* pm64Hidden() {
  static int* sHidden = portSettingsDeclare("launcher_pm64_card_hidden", 0, 0, 1);
  return sHidden;
}

// --- the frame every card and page here shares: the stage dimmed, a paper card in the middle ---

const ImU32 kPlayer[4] = {IM_COL32(255, 112, 96, 255), IM_COL32(108, 200, 255, 255), IM_COL32(124, 224, 108, 255),
                    IM_COL32(255, 216, 80, 255)};

// contentH: a small card's height (its content's, last frame) in the middle of the stage; 0 the stage's
void beginOverlay(const char* id, float scale, ImVec2 a, ImVec2 b, float maxW, float contentH = 0.f) {
  ImGui::SetNextWindowPos(a, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(b.x - a.x, b.y - a.y), ImGuiCond_Always);
  if (sFocus) {
    ImGui::SetNextWindowFocus();
  }
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, b, IM_COL32(16, 8, 24, 150));
  const float w = std::min(b.x - a.x - 24.f * scale, maxW * scale);
  const float full = b.y - a.y - 24.f * scale;
  const float h = contentH > 0.f ? std::min(full, contentH + 40.f * scale) : full;
  const ImVec2 at(a.x + (b.x - a.x - w) * 0.5f, a.y + 12.f * scale + (full - h) * 0.5f);
  launcherPanel(dl, at, ImVec2(at.x + w, at.y + h), scale);
  ImGui::SetCursorScreenPos(at);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f * scale, 20.f * scale));
  ImGui::BeginChild("card", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
  ImGui::PopStyleVar();
}

void endOverlay() {
  ImGui::EndChild();
  ImGui::End();
}

// the back button (a controller's own, the keyboard's Escape) or the mouse's back
bool backPressed() {
  return portMenuUiPressed(PORT_MENU_BACK) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

void closeOverlay() {
  if (sUp == Overlay::News) {
    // seen: this build's version (and the game's own window for it: the same news once)
    const int current = versionNumber(portWhatsNewBuild);
    if (current != 0) {
      *newsSeen() = current;
      if (int* game = portSettingsDeclare("whats_new_seen", 0, 0, 999999); game != nullptr && *game < current) {
        *game = current;
      }
      portSettingsSave();
    }
  }
  sUp = Overlay::None;
  launcherSound(LauncherSound::Back);
}

void muted(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// --- What's new ---

struct NewsLine {
  const char* text;     // English (translated, its {BUTTONS} as icons)
  const char* button;   // its button's label (an id after ##)
  int tab;              // the launcher tab it goes to
  const char* page;     // the Settings tab shown, or nullptr
  const char* search;   // the setting shown alone (the Settings search), or nullptr
  bool help;            // the multiplayer help page over it
};

const NewsLine kNews[] = {
    {"Online co-op on any mix of devices: computers, phones and handhelds together", "Play##news_online", 0,
     nullptr, nullptr, false},
    {"The Multiplayer page, and {PADSTART} on any controller to join", "How it works##news_page", 0, nullptr, nullptr,
     true},
    // (the help page, not Settings: the Multiplayer settings are the game's own, none in the launcher)
    {"Lent partners so every player has someone, and the audience in battle", "How it works##news_lent", 0,
     nullptr, nullptr, true},
    {"Link Paper Mario 64: its partners, bosses, enemies, badges, recipes and sounds", "Settings##news_pm64", 1,
     "Paper Mario 64", nullptr, false},
    {"Start in Rogueport: a new file begins in the plaza", "Settings##news_rogueport", 1, nullptr,
     "Start in Rogueport", false},
    {"Skip any scene: hold {START}", "Settings##news_skip", 1, nullptr, "Skip any scene", false},
    {"Presets: many settings in one choice", "Settings##news_presets", 1, "Gameplay", nullptr, false},
    {"Achievements, read from your save", "Settings##news_achievements", 1, "Achievements", nullptr, false},
    {"Cool Mode: sunglasses on everyone", "Settings##news_cool", 1, nullptr, "Cool Mode", false},
};

// one line: a gold sparkle, the text wrapped, its button at the right (under the text where it's narrow);
// true when its button was pressed
bool newsLine(const NewsLine& n, float scale, bool first) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const char* label = portTrLabel(n.button);
  const float bw = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + 44.f * scale;
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float avail = ImGui::GetContentRegionAvail().x;
  const float indent = 22.f * scale;
  const float textW = avail - indent - bw - 14.f * scale;
  const bool stack = textW < 150.f * scale;
  launcherSparkle(dl, ImVec2(start.x + 8.f * scale, start.y + ImGui::GetTextLineHeight() * 0.5f + 6.f * scale),
                  6.f * scale, kGold);
  ImGui::SetCursorScreenPos(ImVec2(start.x + indent, start.y + 4.f * scale));
  ImGui::PushTextWrapPos(stack ? 0.f : ImGui::GetCursorPosX() + textW);
  ImGui::TextUnformatted(portButtonsUi(portTr(n.text)));
  ImGui::PopTextWrapPos();
  const float textBottom = ImGui::GetItemRectMax().y;
  ImGui::SetCursorScreenPos(stack ? ImVec2(start.x + indent, textBottom + 4.f * scale)
                                  : ImVec2(start.x + avail - bw, start.y));
  const bool pressed = launcherButton(label, ImVec2(0, 0), scale);
  if (first) {
    launcherPoint(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true); // (the glove here while nothing else)
  }
  if (first && sFocus) {
    ImGui::FocusItem();
  }
  ImGui::SetCursorScreenPos(ImVec2(start.x, std::max(textBottom, ImGui::GetItemRectMax().y) + 8.f * scale));
  ImGui::Dummy(ImVec2(0, 0));
  return pressed;
}

int drawNews(float scale, ImVec2 a, ImVec2 b) {
  beginOverlay("##launcher_news", scale, a, b, 860.f);
  char head[96];
  std::snprintf(head, sizeof(head), portTr("What's new in Paper Cut %s"), "0.5.0");
  launcherHeading(head, scale);
  ImGui::Spacing();
  int go = -1;
  for (size_t i = 0; i < sizeof(kNews) / sizeof(kNews[0]); i++) {
    if (newsLine(kNews[i], scale, i == 0)) {
      go = static_cast<int>(i);
    }
  }
  ImGui::Spacing();
  muted(portTr("The full list: Settings > General > What's new. Reopen this card from About."));
  ImGui::Spacing();
  const bool close = launcherButton(portTrLabel("Close##news_close"), ImVec2(0, 0), scale, LauncherButton::Primary);
  endOverlay();
  sFocus = false;
  if (go >= 0) {
    const NewsLine& n = kNews[go];
    closeOverlay();
    launcherSound(LauncherSound::Select);
    if (n.page != nullptr) {
      portSettingsShowSearch(nullptr);
      portSettingsShowTab(n.page);
    }
    if (n.search != nullptr) {
      portSettingsShowSearch(n.search);
    }
    if (n.help) {
      launcherHelpOpen();
    }
    return n.tab;
  }
  if (close || backPressed()) {
    closeOverlay();
  }
  return -1;
}

// --- How multiplayer works ---

// a line's picture: a face of the cast (0 Mario ... 7 Ms. Mowz), the controller's button, the players' seats,
// a player's own card, or a room code
enum class Pic { Face, Button, Seats, Card, Code };
struct HelpLine {
  Pic pic;
  int face;          // Face: the cast's; Card: the player (1-3: 2P-4P)
  const char* token; // Button: its {TOKEN}
  const char* text;  // English
};

const HelpLine kCouch[] = {
    {Pic::Button, 0, "{PADSTART}", "Press {PADSTART} on a controller to join: it takes the next free seat."},
    {Pic::Seats, 0, nullptr, "1P is Mario, 2P the partner (Luigi in Brothers mode), 3P and 4P companions."},
    {Pic::Card, 1, nullptr, "Players 2-4's {START} opens their own card: character, Who's who, emotes, controls."},
    {Pic::Face, 0, nullptr, "Mario's {START} opens the full Start menu and pauses everyone."},
    {Pic::Button, 0, "{Z}", "Hold {Z} for the emote wheel: the stick points, let go to show it."},
    {Pic::Face, 1, nullptr, "Who's who swaps seats, at the next calm moment."},
    {Pic::Face, 3, nullptr, "No one of yours in a battle? You're in the audience: cheer, and throw gifts or rocks."},
    {Pic::Face, 5, nullptr, "The camera follows Mario: anyone who wanders off comes back to his side."},
};

const HelpLine kOnline[] = {
    {Pic::Code, 0, nullptr, "In the game: Start menu, Multiplayer, Online. Host a room there: you get a code to read out."},
    {Pic::Code, 0, nullptr, "Join by code: Online, then Join by code, and type your friend's code."},
    {Pic::Face, 0, nullptr, "The host's rules: everyone plays the host's adventure and settings; the host is Mario."},
    {Pic::Face, 2, nullptr, "Your game keeps a copy of the host's save: keep it as a file of your own next time."},
    {Pic::Face, 4, nullptr, "Leave the room any time: each of you goes on alone with what you have."},
};

void drawPic(const HelpLine& l, ImVec2 at, float side, float scale) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 c(at.x + side * 0.5f, at.y + side * 0.5f);
  auto face = [&](int i, ImVec2 centre, float h) {
    float aspect = 1.f;
    const ImTextureID tex = launcherCastFace(i, &aspect);
    if (tex != ImTextureID{}) {
      const float w = h * aspect;
      dl->AddImage(tex, ImVec2(centre.x - w * 0.5f, centre.y - h * 0.5f), ImVec2(centre.x + w * 0.5f, centre.y + h * 0.5f));
    } else {
      // (no art yet: the game never ran on a disc) a token in a seat's colour with the character's initial
      static const char* const kInitial[] = {"M", "G", "K", "B", "Y", "F", "V", "M"};
      dl->AddCircleFilled(centre, h * 0.42f, kPlayer[i == 0 ? 0 : 1 + (i % 3)]);
      dl->AddCircle(centre, h * 0.42f, kInk, 0, std::max(1.f, 1.5f * scale));
      ImFont* f = gLauncherFonts.heading;
      const char* letter = kInitial[i & 7];
      const ImVec2 ts = f->CalcTextSizeA(h * 0.5f, FLT_MAX, 0.f, letter);
      dl->AddText(f, h * 0.5f, ImVec2(centre.x - ts.x * 0.5f, centre.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255),
                  letter);
    }
  };
  ImFont* font = gLauncherFonts.heading;
  switch (l.pic) {
  case Pic::Face:
    face(l.face, c, side);
    break;
  case Pic::Button: {
    const char* g = portButtonsUi(l.token);
    const float fs = side * 1.1f;
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, g);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), kInk, g);
    break;
  }
  case Pic::Seats:
    for (int i = 0; i < 4; i++) {
      const float r = side * 0.2f;
      const ImVec2 p(at.x + r + (side - 2.f * r) * (static_cast<float>(i) / 3.f), c.y + (i % 2 == 0 ? -r * 0.6f : r * 0.6f));
      dl->AddCircleFilled(p, r, kPlayer[i]);
      char tag[4];
      std::snprintf(tag, sizeof(tag), "%d", i + 1);
      const ImVec2 ts = font->CalcTextSizeA(r * 1.4f, FLT_MAX, 0.f, tag);
      dl->AddText(font, r * 1.4f, ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), tag);
    }
    break;
  case Pic::Card: {
    const ImVec2 a(at.x + side * 0.12f, at.y + side * 0.1f), b(at.x + side * 0.88f, at.y + side * 0.9f);
    dl->AddRectFilled(a, b, IM_COL32(255, 247, 228, 255), 4.f * scale);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + side * 0.24f), kPlayer[l.face], 4.f * scale, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, kInk, 4.f * scale, 0, std::max(1.f, 1.5f * scale));
    const ImVec2 ts = font->CalcTextSizeA(side * 0.22f, FLT_MAX, 0.f, "2P");
    dl->AddText(font, side * 0.22f, ImVec2(a.x + 3.f * scale, a.y + (side * 0.24f - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), "2P");
    face(1, ImVec2(c.x, c.y + side * 0.1f), side * 0.42f);
    break;
  }
  case Pic::Code: {
    const ImVec2 a(at.x, c.y - side * 0.24f), b(at.x + side, c.y + side * 0.24f);
    launcherStrip(dl, a, b, scale, kGold);
    const float fs = side * 0.26f;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, "K7P2");
    dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), kInk, "K7P2");
    break;
  }
  }
}

void helpHalf(const char* title, const HelpLine* lines, int n, float width, float scale) {
  if (title != nullptr) { // (one half at a time: its button says which)
    launcherHeading(portTr(title), scale);
    ImGui::Spacing();
  }
  const float side = 38.f * scale, gap = 12.f * scale;
  for (int i = 0; i < n; i++) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    drawPic(lines[i], start, side, scale);
    ImGui::SetCursorScreenPos(ImVec2(start.x + side + gap, start.y + 2.f * scale));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - side - gap);
    ImGui::TextUnformatted(portButtonsUi(portTr(lines[i].text)));
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(start.x, std::max(ImGui::GetItemRectMax().y, start.y + side) + 8.f * scale));
    ImGui::Dummy(ImVec2(0, 0));
  }
}

void drawHelp(float scale, ImVec2 a, ImVec2 b) {
  beginOverlay("##launcher_help", scale, a, b, 1100.f);
  launcherHeading(portTr("How multiplayer works"), scale);
  ImGui::Spacing();
  const float avail = ImGui::GetContentRegionAvail().x;
  const bool both = avail >= 760.f * scale; // (two halves side by side, else one at a time)
  bool close = false;
  if (!both) {
    // the halves as two buttons (the shoulder buttons too)
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
      sHalf = 1 - sHalf;
      launcherSound(LauncherSound::Turn);
    }
    if (launcherButton(portTrLabel("On one screen##help_couch"), ImVec2(0, 0), scale,
                       sHalf == 0 ? LauncherButton::Primary : LauncherButton::Normal)) {
      sHalf = 0;
    }
    launcherPoint(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
    if (sFocus) {
      ImGui::FocusItem();
    }
    ImGui::SameLine();
    if (launcherButton(portTrLabel("Online##help_online"), ImVec2(0, 0), scale,
                       sHalf == 1 ? LauncherButton::Primary : LauncherButton::Normal)) {
      sHalf = 1;
    }
    ImGui::Spacing();
    if (sHalf == 0) {
      helpHalf(nullptr, kCouch, static_cast<int>(sizeof(kCouch) / sizeof(kCouch[0])), avail, scale);
    } else {
      helpHalf(nullptr, kOnline, static_cast<int>(sizeof(kOnline) / sizeof(kOnline[0])), avail, scale);
    }
  } else {
    const float colW = (avail - 28.f * scale) * 0.5f;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    helpHalf("On one screen", kCouch, static_cast<int>(sizeof(kCouch) / sizeof(kCouch[0])), colW, scale);
    ImGui::EndGroup();
    const float leftBottom = ImGui::GetItemRectMax().y;
    ImGui::SetCursorScreenPos(ImVec2(top.x + colW + 28.f * scale, top.y));
    ImGui::BeginGroup();
    helpHalf("Online", kOnline, static_cast<int>(sizeof(kOnline) / sizeof(kOnline[0])), colW, scale);
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(top.x, std::max(leftBottom, ImGui::GetItemRectMax().y) + 6.f * scale));
    ImGui::Dummy(ImVec2(0, 0));
  }
  ImGui::Spacing();
  close = launcherButton(portTrLabel("Close##help_close"), ImVec2(0, 0), scale, LauncherButton::Primary);
  if (both) {
    launcherPoint(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
  }
  if (both && sFocus) {
    ImGui::FocusItem();
  }
  endOverlay();
  sFocus = false;
  if (close || backPressed()) {
    closeOverlay();
  }
}

// --- Turn on all Paper Mario 64 content? ---

float sAskH = 0.f; // the card's content height, last frame

void drawPm64Ask(float scale, ImVec2 a, ImVec2 b) {
  beginOverlay("##launcher_pm64_ask", scale, a, b, 640.f, sAskH > 0.f ? sAskH : 260.f * scale);
  const float top = ImGui::GetCursorScreenPos().y;
  launcherHeading(portTr("Paper Mario 64"), scale);
  ImGui::Spacing();
  ImGui::TextWrapped("%s", portTr(kPresetPm64Ask));
  muted(portTr(kPresetPm64AskNote));
  ImGui::Spacing();
  static const char* sTestAnswer = SDL_getenv("TTYD_PM64_ASK_ANSWER");
  int answer = -1;
  if (launcherButton(portTrLabel("Yes##pm64_ask_yes"), ImVec2(0, 0), scale, LauncherButton::Primary) ||
      (sTestAnswer != nullptr && std::strcmp(sTestAnswer, "yes") == 0)) {
    answer = 1;
  }
  launcherPoint(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
  if (sFocus) {
    ImGui::FocusItem(); // (Yes highlighted)
  }
  ImGui::SameLine(0, 12.f * scale);
  if (launcherButton(portTrLabel("No##pm64_ask_no"), ImVec2(0, 0), scale) ||
      (sTestAnswer != nullptr && std::strcmp(sTestAnswer, "no") == 0)) {
    answer = 0;
  }
  sAskH = ImGui::GetCursorScreenPos().y - top;
  endOverlay();
  sFocus = false;
  if (answer < 0 && backPressed()) {
    answer = 0;
  }
  if (answer >= 0) {
    portPresetPm64Answer(answer == 1);
    sUp = Overlay::None;
    launcherSound(answer == 1 ? LauncherSound::Select : LauncherSound::Back);
  }
}

// --- Settings > General > Tips ---

const char* sTipsSaid = nullptr;
constexpr const char kTipsNote[] = "The first-time tips (a borrowed partner, the audience, Skip any scene, Start in Rogueport, where the settings are) show again, each once.";

void tipsSection(int inGame) {
  (void)inGame;
  if (ImGui::Button(portTrLabel("Show tips again##tips_again"))) {
    portTipsReset();
    sTipsSaid = "The tips show again, each once.";
  }
  if (sTipsSaid != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr(sTipsSaid));
  }
}

int tipsRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("Show tips again", kTipsNote, nullptr, 1);
  return 1;
}

const char* tipsAct(int id, int dir) {
  if (id == 1 && dir == 0) {
    portTipsReset();
    return "The tips show again, each once.";
  }
  return nullptr;
}

const PortPauseForm kTipsForm = {"General", "Tips", tipsRows, tipsAct};

} // namespace

bool launcherNewsDue() {
  static int sDue = -1;
  if (sDue < 0) {
    const char* force = SDL_getenv("TTYD_LAUNCHER_NEWS");
    const int current = versionNumber(portWhatsNewBuild);
    if (force != nullptr) {
      sDue = std::atoi(force) != 0;
    } else if (current == 0 || SDL_getenv("TTYD_LAUNCHER_SHOT") != nullptr || SDL_getenv("TTYD_FAST") != nullptr) {
      sDue = 0; // (a dev build, or tests)
    } else if (*newsSeen() >= current) {
      sDue = 0;
    } else if (!portSettingsFileFound()) {
      // the first start ever: nothing was updated
      *newsSeen() = current;
      portSettingsSave();
      sDue = 0;
    } else {
      sDue = 1;
    }
  }
  return sDue != 0;
}

void launcherNewsOpen() {
  sUp = Overlay::News;
  sFocus = true;
  launcherSound(LauncherSound::Turn);
}

void launcherHelpOpen() {
  sUp = Overlay::Help;
  sFocus = true;
  launcherSound(LauncherSound::Turn);
}

bool launcherHelpButton(float scale) {
  if (launcherButton(portTrLabel("?##mp_help"), ImVec2(0, 0), scale)) {
    launcherHelpOpen();
    return true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("How multiplayer works"));
  }
  return false;
}

bool launcherOverlayShowing() {
  return sUp != Overlay::None;
}

int launcherOverlayDraw(float scale, ImVec2 stageMin, ImVec2 stageMax, bool ready) {
  static bool sAsked;
  if (!sAsked && ready) {
    sAsked = true;
    // $TTYD_LAUNCHER_HELP=1 (tests, screenshots): the help page at start
    if (SDL_getenv("TTYD_LAUNCHER_HELP") != nullptr) {
      launcherHelpOpen();
    } else if (launcherNewsDue()) {
      launcherNewsOpen();
    }
  }
  if (sUp == Overlay::None && ready && portPresetPm64AskPending()) {
    sUp = Overlay::Pm64Ask; // (a ROM just linked: asked over the stage)
    sFocus = true;
    sAskH = 0.f;
    launcherSound(LauncherSound::Turn);
  } else if (sUp == Overlay::Pm64Ask && !portPresetPm64AskPending()) {
    sUp = Overlay::None; // (answered elsewhere: the Settings page's)
  }
  switch (sUp) {
  case Overlay::Pm64Ask:
    drawPm64Ask(scale, stageMin, stageMax);
    return -1;
  case Overlay::News:
    return drawNews(scale, stageMin, stageMax);
  case Overlay::Help:
    drawHelp(scale, stageMin, stageMax);
    return -1;
  default:
    return -1;
  }
}

bool launcherPm64CardWanted() {
  return portPm64Linked() == PM64_NONE && *pm64Hidden() == 0;
}

void launcherPm64CardHide() {
  *pm64Hidden() = 1;
  portSettingsSave();
}

void launcherPm64CardBody(float scale, bool shortCard, bool heading) {
  if (heading) {
    ImGui::TextUnformatted(portTr("Have Paper Mario 64? Link it"));
  }
  if (shortCard) {
    muted(portTr("Its partners, bosses, enemies, badges, recipes and sounds join this game."));
    return;
  }
  static const char* const kLines[] = {
      "Its partners join yours, with their own moves",
      "Its bosses and enemies to fight",
      "Its badges and recipes",
      "Its sounds and music",
  };
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (const char* line : kLines) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    launcherSparkle(dl, ImVec2(at.x + 6.f * scale, at.y + ImGui::GetTextLineHeight() * 0.5f), 5.f * scale, kGold);
    ImGui::SetCursorScreenPos(ImVec2(at.x + 18.f * scale, at.y));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(portTr(line));
    ImGui::PopTextWrapPos();
  }
  muted(portTr("Your own ROM, read on this device only; the ROM itself isn't kept."));
}

const char* launcherRogueportNote() {
  return portTr("Rogueport: Goombella with you in the plaza, the prologue done and Lord Crump's Star Points. Items "
                "and coins as any new file's.");
}

namespace {

struct TipsSettings {
  TipsSettings() {
    portSettingsAddSection("General", "Tips",
                           "The first-time tips (a borrowed partner, the audience, Skip any scene, Start in "
                           "Rogueport, where the settings are) show once each. Show tips again brings them back.",
                           0, tipsSection, "tips hints help first time again show reset notice");
    portPauseAddForm(&kTipsForm);
  }
} sTips;

} // namespace
