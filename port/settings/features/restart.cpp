// Restart now (port/include/port/restart.h): a setting that applies from the
// next start (texture upscaling, anti-aliasing, the game's language...) gets a
// "Restart now" button on the page once it is changed, and the Start menu's
// Settings tab a "Restart now" row. The restart waits until the memory card is
// not being written (a Save Block's save, autosave), writes quick resume's
// save and the settings out, starts this program again with the same disc
// (and, with quick resume, the save warp to where the player was: as the
// launcher's Continue does), and ends this one. iOS and Android apps can't
// start themselves again: there the note says to close and reopen the app.
#include "port/aurora_pc.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/restart.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/tr.h"

#include "port/mods.h"
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach-o/dyld.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>
#endif

extern "C" int cardIsExec(void); // src/manager/cardmgr.c: a card operation is under way

namespace {

constexpr bool kCanRestart =
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
    false;
#else
    true;
#endif

// The values of the "next start" settings when this run began: taken the first
// time the page or the Start menu's tab lists the settings (before they can be
// changed), a setting declared later at its first sight.
std::map<const int*, int>& started() {
  static std::map<const int*, int> sStarted;
  return sStarted;
}

void snapshot() {
  static std::vector<PortSettingsControl> controls(512);
  const int n = portSettingsControls(controls.data(), static_cast<int>(controls.size()));
  for (int i = 0; i < n; i++) {
    if ((controls[i].flags & PORT_SETTING_NEXT_START) && controls[i].value != nullptr) {
      started().emplace(controls[i].value, *controls[i].value); // (kept when already there)
    }
  }
}

bool sRequested;
int sResume;
constexpr int kOnlineRejoin = 2; // sResume: online co-op's recovery (neither loads nor clears quick resume)
constexpr int kReplay = 3;       // sResume: a replay's recording or playback (replay.cpp puts its environment in place)
int (*sWhenIdle)(void);

// this program's own executable
std::string selfPath() {
#ifdef _WIN32
  wchar_t wide[4096];
  const DWORD len = GetModuleFileNameW(nullptr, wide, sizeof(wide) / sizeof(wide[0]));
  if (len == 0 || len >= sizeof(wide) / sizeof(wide[0])) {
    return {};
  }
  char utf8[8192];
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len), utf8, sizeof(utf8) - 1, nullptr, nullptr);
  return n > 0 ? std::string(utf8, static_cast<size_t>(n)) : std::string();
#elif defined(__APPLE__)
  char path[4096];
  uint32_t size = sizeof(path);
  return _NSGetExecutablePath(path, &size) == 0 ? std::string(path) : std::string();
#elif defined(__linux__) && !defined(__ANDROID__)
  char path[4096];
  const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  return n > 0 ? std::string(path, static_cast<size_t>(n)) : std::string();
#else
  return {};
#endif
}

std::string readSmall(const char* path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path, &size);
  std::string text = data != nullptr ? std::string(static_cast<const char*>(data), size) : std::string();
  SDL_free(data);
  return text;
}

// Starts this program again and ends this one; false (and the reason in the
// log) if it couldn't start
bool restartNow() {
  const std::string exe = selfPath();
  if (exe.empty()) {
    std::fprintf(stderr, "restart: this program's path is unknown\n");
    return false;
  }
  SDL_Environment* env = SDL_GetEnvironment();
  // a test's map warp and input script were for the run that had them
  for (const char* name : {"TTYD_MAP", "TTYD_STORY", "TTYD_INPUT", "TTYD_LOAD_SAVE", "TTYD_LOAD_SAVE_SLOT",
                           "TTYD_SETTINGS_OPEN"}) {
    SDL_UnsetEnvironmentVariable(env, name);
  }
  // a recording under way: saved first (replay.cpp); a replay's own start-up is for this run only
  portReplaySaveIfRecording();
  SDL_UnsetEnvironmentVariable(env, "TTYD_REPLAY_RECORD");
  SDL_UnsetEnvironmentVariable(env, "TTYD_REPLAY_PLAY");
  SDL_UnsetEnvironmentVariable(env, "TTYD_GHOST_RACE");
  // a leaderboard run's, an upload's and a board entry's (boards.cpp): this run's only
  SDL_UnsetEnvironmentVariable(env, "TTYD_BOARD_RUN");
  SDL_UnsetEnvironmentVariable(env, "TTYD_BOARD_UPLOAD");
  SDL_UnsetEnvironmentVariable(env, "TTYD_BOARD_ENTRY");
  char path[1024], sidecar[1024];
  portResumePath(path, sizeof(path), PORT_GAME_ID, 0);
  portResumePath(sidecar, sizeof(sidecar), PORT_GAME_ID, 1);
  if (sResume == kOnlineRejoin) {
    // online co-op's recovery (portRestartForOnline): the session starts from the
    // host's save itself; this player's own quick resume is left as it is
  } else if (sResume == kReplay) {
    // a replay's recording or playback: its start save and file (replay.cpp);
    // quick resume left as it is
    portReplayRestartEnv();
  } else if (!sResume) {
    // the card was changed under the game (a save restored or copied in): the
    // launcher's Continue must not bring back the game from before it, whose
    // next save would go over the new one (the old save is a backup already)
    SDL_RemovePath(path);
    SDL_RemovePath(sidecar);
  } else if (portResumeHave()) {
    // quick resume's save, as the save file it came from (the launcher's Continue)
    portResumeFlush();
    std::string slot = readSmall(sidecar);
    slot = slot.empty() || slot[0] < '0' || slot[0] > '3' ? "3" : slot.substr(0, 1);
    SDL_SetEnvironmentVariable(env, "TTYD_LOAD_SAVE", path, true);
    SDL_SetEnvironmentVariable(env, "TTYD_LOAD_SAVE_SLOT", slot.c_str(), true);
  }
  portSettingsSave();
  portSettingsCloudPush(1);
  const std::string disc = portAuroraDiscPath();
  const char* args[] = {exe.c_str(), disc.empty() ? nullptr : disc.c_str(), nullptr};
  SDL_Process* process = SDL_CreateProcess(args, false);
  if (process == nullptr) {
    std::fprintf(stderr, "restart: could not start %s: %s\n", exe.c_str(), SDL_GetError());
    return false;
  }
  SDL_DestroyProcess(process); // it runs on; this one ends
  std::printf("[restart] %s%s\n", exe.c_str(),
              sResume == kOnlineRejoin ? " (online: to rejoin)"
              : sResume == kReplay     ? " (replay)"
              : sResume && portResumeHave() ? " (quick resume)"
                                            : "");
  std::fflush(stdout);
  // at once, as the launcher's Play of another version: Aurora's shutdown (its
  // GPU and audio threads) can hang, and everything that matters is written
  if (SDL_Window* w = aurora_get_window()) {
    SDL_HideWindow(w);
  }
  portModsStartedOk(); // a restart isn't a failed start (safe mode, port/include/port/mods.h)
  std::_Exit(0);
}

// a requested restart once the card is idle: what had to wait for it, then the restart
void tryRestart() {
  if (!sRequested || cardIsExec()) {
    return;
  }
  sRequested = false;
  int (*whenIdle)(void) = sWhenIdle;
  sWhenIdle = nullptr;
  if (whenIdle == nullptr || whenIdle()) {
    restartNow();
  }
}

// each presented frame
void frame(int) { tryRestart(); }

const bool sAdded = [] {
  portSettingsAddOverlay(frame, nullptr);
  return true;
}();

} // namespace

extern "C" int portRestartSupported(void) { return kCanRestart ? 1 : 0; }

extern "C" int portRestartPendingFor(const int* value) {
  snapshot();
  for (const auto& [at, was] : started()) {
    if ((value == nullptr || at == value) && *at != was) {
      return 1;
    }
  }
  return 0;
}

extern "C" int portRestartPending(void) { return portRestartPendingFor(nullptr); }

extern "C" void portRestartRequestWith(int resume, int (*whenIdle)(void)) {
  if (!kCanRestart || portOnlineActive()) {
    return; // online co-op: the other player's game would be left alone mid-session
  }
  sRequested = true;
  sResume = resume;
  sWhenIdle = whenIdle;
  tryRestart(); // now when the card is idle (returns only when it couldn't restart)
}

extern "C" void portRestartRequest(int resume) { portRestartRequestWith(resume, nullptr); }

// Online co-op's recovery (online.cpp's portOnlineRestartRejoin): this program
// started again at once, online or not (both players' games do it together),
// with what the environment says to rejoin; quick resume as it is.
extern "C" void portRestartForOnline(void) {
  if (!kCanRestart) {
    return;
  }
  sResume = kOnlineRejoin;
  restartNow();
}

// A replay's recording or playback (replay.cpp): as Restart now, once the card
// is idle, with the replay's environment (portReplayRestartEnv)
extern "C" void portRestartForReplay(void) { portRestartRequestWith(kReplay, nullptr); }

extern "C" int portRestartWaiting(void) { return sRequested ? 1 : 0; }

extern "C" void portRestartButton(void) {
  if (!kCanRestart) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("Close and reopen the app to apply it."));
    return;
  }
  ImGui::SameLine();
  if (portOnlineActive()) {
    ImGui::TextDisabled("%s", portTr("Applies after the online game."));
    return;
  }
  if (sRequested) {
    ImGui::TextDisabled("%s", portTr("Restarting when the game has finished saving..."));
    return;
  }
  if (ImGui::SmallButton(portTrLabel("Restart now##restart"))) {
    portRestartRequest(1);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("Starts the game again with this disc; quick resume brings you back here."));
  }
}
