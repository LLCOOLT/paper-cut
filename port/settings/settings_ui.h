// Shared by the settings page and the launcher (port/settings/*.cpp).
#pragma once

// The settings' tabs, inside the current ImGui window. inGame: the running
// game's own tabs too (save states, save warps).
void portSettingsTabs(bool inGame);

// The launcher is on screen (launcher_ui.cpp): touches drive its ImGui
// windows, as they do the settings page's (settings_ui.cpp, touchToMouse).
extern bool gPortLauncherShown;

// The launcher or the settings page gone: ImGui lets go of the keyboard and
// controller (no focused window, keyboard/controller navigation off), or
// Aurora keeps giving it the keys and the game gets none.
void portUiReleaseInput();

namespace portui {
// A note in the page's grey, wrapped to the window: English (translated here)
// or text already translated.
void hint(const char* text);
// SameLine() for the next control when it still fits the row (width: its
// width), else a new row: buttons chained on one line stay on the page at
// phone width and with long translations. label: a button's text (its width
// worked out with the frame padding), or use the width form.
void sameLineOr(float width);
void sameLineOrButton(const char* label);
void sameLineOrSmallButton(const char* label);
} // namespace portui

// The last crash report sent with a click (port/settings/features/crash_report.cpp):
// Report on GitHub (a filled-in issue opened in the browser) and Save as a zip...,
// in the current ImGui window; the text with the player's name taken out of paths.
// Nothing is sent without a click. Test: portCrashReportTest ($TTYD_CRASH_REPORT_TEST).
#include <string>
void portCrashReportButtons();
void portCrashReportTest();
std::string portCrashScrub(std::string text);

// The flight record (port/include/port/flight.h, port/settings/features/safe_graphics.cpp): Copy the last
// one (Settings > General > Crash reports), and the launcher's offer after a run that stopped on screen with
// no clean exit (Safe graphics, the other graphics API on Android, the record), over the launcher once.
void portFlightRecordButtons();
void portFlightOfferDraw(float scale);

// The recovery screen at the next start after a crash (crash_report.cpp), once per crash
// (portCrashReportPending): the launcher's (launcher_ui.cpp, every system), or the game's own over
// its title when the launcher was left out (settings_ui.cpp: a disc given at start, $TTYD_NO_LAUNCHER).
// What its Continue loads: quick resume's save of the build that crashed; or one of its crash-safe snapshots
// (port/include/port/soft_crash.h), a few minutes earlier.
#include <utility>
#include <vector>
struct PortRecoverySnap {
  std::string path;      // crashsafe_<id>_<n>.gci
  int slot = 3;          // the save file it came from (0-3)
  std::string map, area; // where it was made, "" unknown
  long long made = 0;    // when (Unix seconds)
  long long before = -1; // seconds between it and the crash, -1 unknown
};
struct PortRecoveryResume {
  bool have = false;
  std::string gameId;    // "G8ME01"
  std::string path;      // resume_<id>.gci
  int slot = 3;          // the save file it came from (0-3)
  std::string map;       // where it was made ("gor_01"), "" unknown
  std::string area;      // the area's name there (the game's text, UTF-8), "" unknown
  long long before = -1; // seconds between it and the crash, -1 unknown
  std::vector<PortRecoverySnap> snaps; // the crash-safe snapshots, newest first
  std::string paused;    // why none were made lately ("online", ...: soft_crash.cpp), "" none
};
// The crashed build's game ID, from the report's "version: US (G8ME01)" line ("" unknown).
std::string portCrashReportGameId();
// gameId's quick resume save, timed against the last crash's report.
PortRecoveryResume portRecoveryResume(const char* gameId);
// The screen, centred over the display (modal: a popup nothing under it takes input from; else a
// window, its text scaled by scale; inside, if given, is called in it). 0 while it's up, 1 Continue,
// 2 Dismiss ("Load your save as usual"), 3 + n the crash-safe snapshot n.
int portRecoveryDraw(const PortRecoveryResume& resume, float scale, bool modal, void (*inside)() = nullptr);

// crash_report.cpp's parts, for Report a problem
std::string portCrashSystemText();   // the version, package, game, system and graphics adapter (not scrubbed)
std::string portCrashSettingsText(); // settings.txt's number values only
std::string portCrashReportText();   // last_crash.txt, scrubbed ("" none)
std::string portUrlEncode(const std::string& text);
std::string portZipStored(const std::vector<std::pair<std::string, std::string>>& files);

// After a crash, or a run that stopped on screen (port/include/port/soft_crash.h, soft_crash.cpp):
// when the last run ended (the crash report's time, else the flight record's last write; -1 unknown), and
// whether it stopped on screen with no crash report (the flight record's verdict, or the memory guard's)
long long portLastRunEndedAt();
bool portLastRunStopped();
// gameId's crash-safe snapshots made before endedAt (within 2 hours), newest first; and why none were lately
// ("online", "replay", ... or "")
std::vector<PortRecoverySnap> portCrashSafeSnaps(const char* gameId, long long endedAt);
std::string portCrashSafePaused(const char* gameId, long long endedAt);
// the snapshots' rows inside the current window (focus: the first one's button takes the first focus): the
// one picked (0 newest), -1 none; and the line saying why there were none lately
int portCrashSafeRows(const PortRecoveryResume& r, bool focus);
// "Paper Cut closed after you changed <setting>. Undo it?" with Undo and Keep inside the current window, when
// the last run ended within minutes of a change (nothing otherwise); true while it asks
bool portSettingsUndoRows();
// a Continue asked for by a window that isn't the recovery screen (the Safe graphics offer, the window below):
// taken once by the launcher (or the game over its title)
void portSoftCrashRequestContinue(const std::string& gameId, const std::string& path, int slot);
bool portSoftCrashTakeContinue(std::string* gameId, std::string* path, int* slot);
// After a run that stopped on screen with no crash report, when neither the recovery screen nor the Safe
// graphics offer shows: a window of its own with the undo question and the snapshots, once (gameId's, or
// nullptr: the newest of any version's). modal: a pop-up (the launcher); else a window (the game's, whose
// caller holds the game's input meanwhile). -1 not shown (or done), 0 up, 1 closed this frame.
int portSoftCrashWindowDraw(float scale, const char* gameId, bool modal, void (*inside)() = nullptr);
// the Safe graphics offer's part: the undo question and the snapshots inside it; true when one was picked
bool portSoftCrashOfferRows(const char* gameId);
// the Safe graphics offer shows this start (safe_graphics.cpp): the undo question and snapshots go in it
bool portFlightOfferShows();

// Report a problem (report_problem.cpp): its button and, once pressed, what goes in the zip, then the zip made
// in the user folder, GitHub's new-issue page and the zip's folder (a phone: Save a copy...). where: an ID of
// the place it is drawn ("settings", "about", "recovery").
void portReportProblemSection(const char* where);
// the text with the player's names, room codes' secret halves, IP addresses and the account's name taken out
std::string portReportScrub(std::string text);
// the paper notes' device code (notes.cpp) taken out of text wherever it stands, as "<notes-code>"
void portNotesScrubCode(std::string& text);
// ($TTYD_REPORT_PROBLEM_TEST) the code, when this device has one, put in a line of the log's tail (not printed), so
// the test sees the zip take it out
void portNotesCodeTestLine();
// a setting's name on the page by its key (English, for portTr), nullptr when it has no control of its own
const char* portSettingsLabelOf(const char* key);
