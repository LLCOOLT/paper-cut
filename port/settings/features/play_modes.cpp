// The Modes tab's play modes (port/include/port/playmodes.h): chapter select, hardcore and the Pit of
// 100 Trials in co-op, here and in the Start menu's Settings tab (Modes, port/include/port/pause_forms.h).
#include "port/companion_menu.h"
#include "port/mod_host.h"
#include "port/overlay_place.h"
#include "port/pause_forms.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" int portWarpSettled(void); // trace_pc.c: the $TTYD_MAP warp done (or none asked)
extern "C" int portDrawFrames(void);  // port/include/port/toggles.h: game frames since the last drawn one

using portui::hint;

namespace {

constexpr const char* kTab = "Modes";

std::string sMessage;

std::string clock(unsigned long long ms) {
  char text[32];
  std::snprintf(text, sizeof(text), "%llu:%02llu:%02llu", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60);
  return text;
}

void chapterSection(int inGame) {
  static int sSeen;
  int chapter = 0;
  unsigned long long ms = 0;
  const int results = portChapterLastResult(&chapter, &ms);
  if (results != sSeen) {
    sSeen = results;
    if (chapter > 0) {
      char text[160];
      std::snprintf(text, sizeof(text), portTr("Chapter %d played again in %s."), chapter, clock(ms).c_str());
      sMessage = text;
    } else {
      sMessage = portTr("The replay was left: the file's story is as it was.");
    }
  }
  ImGui::TextWrapped("%s", portTr("Once a file has finished the game, play any chapter's story again with this file's "
                                  "Mario: his level, badges and items. When the chapter ends, the file's story is put "
                                  "back as it was and Mario returns to the room he left; the levels, items and coins won "
                                  "stay."));
  if (!inGame) {
    hint("Choose one while a file is in play.");
    return;
  }
  const int active = portChapterActive();
  if (active != 0) {
    ImGui::Text(portTr("Playing chapter %d again: %s"), active, clock(portChapterElapsed()).c_str());
    if (ImGui::Button(portTrLabel("Leave the chapter##chapters"))) {
      portChapterAbandon();
    }
    ImGui::SameLine();
    hint("The file's story is put back as it was.");
  } else if (!portChapterSelectUnlocked()) {
    hint("It opens once this file has finished the game (saved after the credits).");
  } else {
    for (int i = 1; i <= PORT_CHAPTERS; i++) {
      char label[64];
      std::snprintf(label, sizeof(label), "%s %d##chapter%d", portTr("Chapter"), i, i);
      if (ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) {
        const char* why = nullptr;
        sMessage = portChapterStart(i, &why) ? "" : portTr(why);
      }
      if (i % 4 != 0) {
        ImGui::SameLine();
      }
    }
    hint("A save made during the replay goes on with it when loaded. Leave the chapter from here to end it "
         "early.");
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// the Start menu's form
enum { kRowLeave = 100 };

int chapterRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < PORT_CHAPTERS + 1) {
    return 0;
  }
  if (const int active = portChapterActive(); active != 0) {
    PortPauseRow h = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
    std::snprintf(h.text, sizeof(h.text), "%s %d  %s", portTrGame("Chapter"), active,
                  clock(portChapterElapsed()).c_str());
    out[n++] = h;
    out[n++] = portPauseRow("Leave the chapter", "The file's story is put back as it was.", nullptr, kRowLeave,
                            PORT_PAUSE_ROW_CONFIRM);
    return n;
  }
  const bool open = portChapterSelectUnlocked() != 0;
  for (int i = 1; i <= PORT_CHAPTERS; i++) {
    PortPauseRow r = portPauseRow(nullptr,
                                  open ? "The chapter's story again with this file's Mario: it begins once the menu "
                                         "is closed."
                                       : "It opens once this file has finished the game (saved after the credits).",
                                  nullptr, i, open ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
    std::snprintf(r.text, sizeof(r.text), "%s %d", portTrGame("Chapter"), i);
    out[n++] = r;
  }
  return n;
}

const char* chapterAct(int id, int dir) {
  if (dir != 0 || id < 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  if (id == kRowLeave) {
    portChapterAbandon();
    return "The file's story is put back as it was.";
  }
  const char* why = nullptr;
  return portChapterStart(id, &why) ? "The chapter begins once the menu is closed." : why;
}

const PortPauseForm kChapterForm = {kTab, "Chapter select", chapterRows, chapterAct};

void hardcoreSection(int) {
  static std::string sHardMessage;
  portRemixDeclare();
  int* next = portSettingsFind("hardcore_next");
  ImGui::TextWrapped("%s", portTr("One life: no Retry on Game Over and no save states. A Game Over ends the file for "
                                  "good: it can't be loaded again."));
  if (portHardcoreActive()) {
    ImGui::TextUnformatted(portTr("This file is a hardcore file."));
  }
  if (next != nullptr) {
    bool on = *next != 0;
    if (ImGui::Checkbox(portTrLabel("The next new file is a hardcore file##hardcore"), &on)) {
      *next = on ? 1 : 0;
      portSettingsSave();
    }
  }
  if (ImGui::Button(portTrLabel("Restart and record a hardcore run##hardcore"))) {
    if (next != nullptr) {
      *next = 1;
      portSettingsSave(); // in the recording's settings, so its playback marks the file the same way
    }
    sHardMessage = portReplayRestartRecording("Hardcore run", 0) ? "" : portTr("Recording isn't possible here.");
  }
  hint("The game starts again at the title, recording (Practice > Replays): start a new file. The Game Over "
       "saves the run, a ghost to race in Ghost races.");
  if (!sHardMessage.empty()) {
    ImGui::TextWrapped("%s", sHardMessage.c_str());
  }
}

void pitSection(int inGame) {
  static int sSeen;
  static std::string sPitMessage;
  int floor = 0;
  unsigned long long ms = 0;
  const int results = portPitCoopResult(&floor, &ms);
  if (results != sSeen) {
    sSeen = results;
    char text[160];
    std::snprintf(text, sizeof(text), portTr("The run ended on floor %d after %s."), floor, clock(ms).c_str());
    sPitMessage = text;
  }
  ImGui::TextWrapped("%s", portTr("A run down the Pit of 100 Trials from its first floor, with a second player on the "
                                  "partner (Second player is turned on for the run, and put back after). The floor "
                                  "and the time show over the picture. The run ends when you leave the Pit."));
  if (portPitCoopActive()) {
    ImGui::Text(portTr("Floor %d, %s"), portPitCoopFloor(), clock(portPitCoopElapsed()).c_str());
    if (ImGui::Button(portTrLabel("End the run##pitcoop"))) {
      portPitCoopStop();
    }
    ImGui::SameLine();
    hint("Mario goes back to the room he started from.");
  } else {
    ImGui::BeginDisabled(!inGame);
    if (ImGui::Button(portTrLabel("Start a co-op Pit run##pitcoop"))) {
      const char* why = nullptr;
      sPitMessage = portPitCoopStart(&why) ? "" : portTr(why);
    }
    ImGui::EndDisabled();
    if (!inGame) {
      hint("Start it while a file is in play.");
    }
  }
  if (portPitCoopBest(&floor, &ms)) {
    if (ms > 0) {
      ImGui::Text(portTr("Deepest: floor %d. Best to the bottom: %s"), floor, clock(ms).c_str());
    } else {
      ImGui::Text(portTr("Deepest: floor %d"), floor);
    }
  }
  hint("A second controller plays the partner: in battle its turns and guards, in the field its walk and "
       "its ability. Online co-op: take the Pit's pipe in Rogueport together instead.");
  if (!sPitMessage.empty()) {
    ImGui::TextWrapped("%s", sPitMessage.c_str());
  }
}

void tricksSection(int inGame) {
  static int sPicked = 0;
  static char sSpot[64] = "";
  static std::string sTrickMessage;
  ImGui::TextWrapped("%s", portTr("Warps to set-ups: a room with the story where it is for a known trick or fight, "
                                  "and your own spots. A warp counts as practice (no achievements until a file is "
                                  "loaded)."));
  if (ImGui::BeginListBox("##tricks", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 8.f))) {
    for (int i = 0; i < portTrickCount(); i++) {
      char label[96];
      std::snprintf(label, sizeof(label), "%s%s##trick%d", portTrickBuiltin(i) ? "" : "* ", portTrickName(i), i);
      if (ImGui::Selectable(label, sPicked == i)) {
        sPicked = i;
      }
    }
    ImGui::EndListBox();
  }
  ImGui::BeginDisabled(!inGame);
  if (ImGui::Button(portTrLabel("Warp##tricks"))) {
    const char* why = nullptr;
    sTrickMessage = portTrickWarp(sPicked, &why) ? "" : portTr(why);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
  ImGui::InputTextWithHint(portTrLabel("##spotname"), portTr("Name"), sSpot, sizeof(sSpot));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Keep this spot##tricks"))) {
    const char* why = nullptr;
    sTrickMessage = portTrickSaveHere(sSpot, &why) ? portTr("Kept in tricks.txt (marked *).") : portTr(why);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Read tricks.txt again##tricks"))) {
    portTricksReload();
  }
  hint("tricks.txt in the user folder: a line each, name|map|entrance|story|x y z.");
  if (!sTrickMessage.empty()) {
    ImGui::TextWrapped("%s", sTrickMessage.c_str());
  }
}

// over the picture: the chapter and its time
void frame(void*) {
  // tests: $TTYD_PLAY_TEST=chapter:N[:S] (chapter N again, left S s of game time later) or trick:N (warp to
  // set-up N), $TTYD_MODE_TEST_FRAME game frames (default 700) after the $TTYD_MAP warp settled. Game frames,
  // not wall time: a loaded machine's slow run once reached its input's end before 30 s of real time.
  static bool sTested;
  static long long sLeaveIn; // game frames to the chapter's leaving (0: none)
  static int sTestFrames;
  const char* test = SDL_getenv("TTYD_PLAY_TEST");
  if (test != nullptr && !sTested && portWarpSettled()) {
    sTestFrames += portDrawFrames();
  }
  static const int sTestAt = [] {
    const char* v = SDL_getenv("TTYD_MODE_TEST_FRAME");
    return v != nullptr && std::atoi(v) > 0 ? std::atoi(v) : 700;
  }();
  if (test != nullptr && !sTested && sTestFrames >= sTestAt) {
    const char* why = nullptr;
    const std::string t(test);
    if (t.rfind("chapter:", 0) == 0) {
      sTested = portChapterStart(std::atoi(t.c_str() + 8), &why) != 0;
      const size_t colon = t.find(':', 8);
      if (sTested && colon != std::string::npos) {
        sLeaveIn = 60ll * std::strtoll(t.c_str() + colon + 1, nullptr, 10);
      }
    } else if (t.rfind("trick:", 0) == 0) {
      sTested = portTrickWarp(std::atoi(t.c_str() + 6), &why) != 0;
    }
    static const char* sLastWhy; // (tried each frame until it begins: each reason said once)
    if (!sTested && why != nullptr && why != sLastWhy) {
      std::printf("[playmodes] test not started: %s\n", why);
      std::fflush(stdout);
    }
    sLastWhy = why;
  } else if (sLeaveIn > 0 && (sLeaveIn -= portDrawFrames()) <= 0) {
    sLeaveIn = 0;
    portChapterAbandon();
  }
  // the boxes at the top right, under the status bar's rows when they are out (port/overlay_place.h)
  const float dy = portOverlayDrop480(8.0f, 4.0f);
  // a ghost race: its time, and for a few seconds after each room how the player stands against the ghost
  if (portGhostRacing()) {
    static int sSeenDelta;
    static Uint64 sDeltaUntil;
    float delta = 0.0f;
    const int n = portGhostDelta(&delta);
    if (n != sSeenDelta) {
      sSeenDelta = n;
      sDeltaUntil = SDL_GetTicks() + 4000;
    }
    char line[128];
    std::snprintf(line, sizeof(line), "%s   %s", portTr("Ghost race"),
                  clock(static_cast<unsigned long long>(portGhostRaceSeconds() * 1000.0)).c_str());
    const bool showDelta = SDL_GetTicks() < sDeltaUntil;
    char deltaText[64] = "";
    if (showDelta) {
      std::snprintf(deltaText, sizeof(deltaText), delta <= 0.0f ? portTr("%.2f s ahead of the ghost")
                                                                : portTr("%.2f s behind the ghost"),
                    delta < 0.0f ? -delta : delta);
    }
    float w = ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line).x;
    if (showDelta) {
      const float dw = ImGui::GetFont()->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, deltaText).x;
      w = dw > w ? dw : w;
    }
    w += 16.0f;
    portModHostDrawRect(632.0f - w, 36.0f + dy, w, showDelta ? 42.0f : 22.0f, 0x00000090u);
    portModHostDrawText(638.0f - w, 38.0f + dy, 14.0f, 0xC0E0FFFFu, line);
    if (showDelta) {
      portModHostDrawText(638.0f - w, 57.0f + dy, 13.0f, delta <= 0.0f ? 0x90FF90FFu : 0xFFB080FFu, deltaText);
    }
  }
  if (portPitCoopActive()) {
    char line[96];
    std::snprintf(line, sizeof(line), portTr("Pit co-op: floor %d   %s"), portPitCoopFloor(),
                  clock(portPitCoopElapsed()).c_str());
    const float w = ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line).x + 16.0f;
    portModHostDrawRect(632.0f - w, 8.0f + dy, w, 22.0f, 0x00000090u);
    portModHostDrawText(638.0f - w, 10.0f + dy, 14.0f, 0xFFFFFFFFu, line);
  }
  const int active = portChapterActive();
  if (active == 0) {
    return;
  }
  char line[96];
  std::snprintf(line, sizeof(line), "%s %d   %s", portTr("Chapter"), active, clock(portChapterElapsed()).c_str());
  const float w = ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line).x + 16.0f;
  portModHostDrawRect(632.0f - w, 8.0f + dy, w, 22.0f, 0x00000090u);
  portModHostDrawText(638.0f - w, 10.0f + dy, 14.0f, 0xFFFFFFFFu, line);
}

// --- the Start menu's forms: the same starts as the page's ---

// Online and in replays the page holds these (what changes play stays as the run began): the Start
// menu's rows are dimmed alike (the starts say why themselves)
constexpr const char* kHeld = "Not during online play or a replay.";
constexpr const char* kSecondScreen = "Not on the second screen: use the game's Start menu.";

bool held() { return portReplayActive(); }

enum { kRowNext = 101, kRowRecord = 102, kRowStart = 103, kRowEnd = 104 };

int hardcoreRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 3) {
    return 0;
  }
  portRemixDeclare();
  if (portHardcoreActive()) {
    out[n++] = portPauseRow("This file is a hardcore file.", nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
  }
  const int* next = portSettingsFind("hardcore_next");
  out[n++] = portPauseRow("Next new file: hardcore", "One life: no Retry on Game Over and no save "
                          "states. A Game Over ends the file for good: it can't be loaded again.",
                          next != nullptr && *next ? "On" : "Off", kRowNext, held() ? PORT_PAUSE_ROW_DIM : 0);
  out[n++] = portPauseRow("Record a hardcore run", "The game starts again at the title, recording "
                          "(kept with the replays): start a new file. The Game Over saves the run, a ghost to race in "
                          "Ghost races.", nullptr, kRowRecord, held() ? PORT_PAUSE_ROW_DIM : PORT_PAUSE_ROW_CONFIRM);
  return n;
}

const char* hardcoreAct(int id, int dir) {
  int* next = portSettingsFind("hardcore_next");
  if (held()) {
    return dir == 0 ? kHeld : nullptr;
  }
  if (id == kRowNext && next != nullptr) {
    *next = *next ? 0 : 1;
    portSettingsSave();
    return nullptr;
  }
  if (id != kRowRecord || dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  const int was = next != nullptr ? *next : 0;
  if (next != nullptr) {
    *next = 1;
    portSettingsSave(); // in the recording's settings, so its playback marks the file the same way
  }
  if (!portReplayRestartRecording("Hardcore run", 0)) {
    if (next != nullptr && *next != was) {
      *next = was; // (refused: the next new file as it was, not hardcore behind the player's back)
      portSettingsSave();
    }
    return "Recording isn't possible here.";
  }
  return nullptr;
}

const PortPauseForm kHardcoreForm = {kTab, "Hardcore", hardcoreRows, hardcoreAct};

int pitRows(PortPauseRow* out, int max) {
  int n = 0, floor = 0;
  unsigned long long ms = 0;
  if (max < 2) {
    return 0;
  }
  if (portPitCoopActive()) {
    PortPauseRow h = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
    std::snprintf(h.text, sizeof(h.text), portTrGame("Floor %d, %s"), portPitCoopFloor(),
                  clock(portPitCoopElapsed()).c_str());
    out[n++] = h;
    out[n++] = portPauseRow("End the run", "Mario goes back to the room he started from.", nullptr, kRowEnd,
                            PORT_PAUSE_ROW_CONFIRM);
    return n;
  }
  PortPauseRow r = portPauseRow("Start a co-op Pit run", "A run down the Pit of 100 Trials from its first floor, "
                                "with a second player on the partner (Second player is turned on for the run, and put "
                                "back after). The floor and the time show over the picture. The run ends when you "
                                "leave the Pit.", nullptr, kRowStart, held() ? PORT_PAUSE_ROW_DIM : PORT_PAUSE_ROW_CONFIRM);
  if (portPitCoopBest(&floor, &ms)) { // (this computer's best: only what's drawn differs between games)
    std::snprintf(r.value, sizeof(r.value), portTrGame("Deepest: floor %d"), floor);
  }
  out[n++] = r;
  return n;
}

const char* pitAct(int id, int dir) {
  if (dir != 0 || id < 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  if (id == kRowEnd) {
    portPitCoopStop();
    return "The run ends once the menu is closed.";
  }
  const char* why = nullptr;
  return portPitCoopStart(&why) ? "The run starts once the menu is closed." : why;
}

const PortPauseForm kPitForm = {kTab, "Pit of 100 Trials in co-op", pitRows, pitAct};

struct Register {
  Register() {
    portPauseAddForm(&kHardcoreForm);
    portPauseAddForm(&kPitForm);
    portSettingsAddSection(kTab, "Chapter select", nullptr, PORT_SETTING_CHANGES_PLAY, chapterSection,
                           "chapter select replay story again post game modes");
    portPauseAddForm(&kChapterForm);
    portRemixDeclare();
    portSettingsAddToggle(kTab, "Remix: enemies",
                          "The fight behind each enemy in the field is drawn from all of its area's fights (not the "
                          "bosses'), the same for a room and a seed. From the next room.",
                          PORT_SETTING_CHANGES_PLAY, "remix_enemies", "remix random enemies placement shuffle");
    portSettingsAddToggle(kTab, "Remix: badge draft",
                          "Each chapter's title card offers three badges: pick one.",
                          PORT_SETTING_CHANGES_PLAY, "remix_badge_draft", "remix badge draft choose pick");
    portSettingsAddSlider(kTab, "Remix seed", "Same seed, same remix: share it with a challenge code.",
                          PORT_SETTING_CHANGES_PLAY, "remix_seed", 0, 99999, "%d", "remix seed");
    portSettingsAddSection(kTab, "Hardcore", nullptr, PORT_SETTING_CHANGES_PLAY, hardcoreSection,
                           "hardcore one life permadeath ironman remix ghost");
    portSettingsAddSection(kTab, "Pit of 100 Trials in co-op", nullptr, PORT_SETTING_CHANGES_PLAY, pitSection,
                           "pit 100 trials co-op coop second player partner floors modes");
    portSettingsAddSection("Practice", "Trick practice", nullptr, 0, tricksSection,
                           "trick practice warp setup speedrun glitch spot position");
    portModHostAddDraw(frame, nullptr);
  }
} sRegister;

} // namespace
