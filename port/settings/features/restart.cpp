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
#include "port/statesync.h"
#include "port/replay.h"
#include "port/restart.h"
#include "port/resume.h"
#include "port/savestate.h" // portSaveStateNotice: a restart that didn't happen
#include "port/settings.h"
#include "port/tr.h"

#include "port/mods.h"
#include "port/offscreen.h"
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
#include <cerrno>
#include <csignal>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" int cardIsExec(void); // src/manager/cardmgr.c: a card operation is under way
extern "C" void portPause(void);  // pause_pc.c: the game clock and the sound stopped
extern "C" void portCrashQuitStarted(void); // crash_pc.c (port/platform.h, C only): ending on purpose

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
constexpr int kReplay = 3;       // sResume: a replay's recording or playback (replay.cpp puts its environment in place)
constexpr int kLauncher = 4;     // sResume: Back to the launcher (port/include/port/launcher_back.h): no disc, its launcher
int (*sWhenIdle)(void);
std::string sWhy;    // why restartNow couldn't start the program (SDL's, in English)
std::string sFailed; // the last requested restart that didn't happen, as shown (portRestartButton, a notice)

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

// The handoff: this window stays as it was (the picture still, the sound
// stopped, over the new window) until the new program has shown a frame of
// its own, so the game doesn't seem to close and open again: the new window
// opens where this one is (the saved size and place, full screen as it was)
// and takes over in place. restart_handoff=0 in settings.txt (or
// $TTYD_RESTART_PLAIN) goes back to closing this window at once.
int* sHandoffOn;

bool handoffWanted() {
  return aurora_get_window() != nullptr && SDL_getenv("TTYD_RESTART_PLAIN") == nullptr &&
         (sHandoffOn == nullptr || *sHandoffOn != 0);
}

// the file the new program makes once it has drawn (portRestartHandoffReady)
std::string handoffPath() {
  return std::string(portSettingsUserPath()) + "/restart-" + std::to_string(SDL_GetTicksNS()) + ".ready";
}

// up to 30 seconds (Preparing graphics draws as it goes: a frame comes well before), or until the
// new program has ended (it couldn't start: this one goes all the same, as before)
void waitForHandoff(SDL_Process* process, const std::string& ready) {
  SDL_Window* w = aurora_get_window();
  portPause(); // the game clock and the sound stop; the picture stays as last shown
  if (w != nullptr) {
    SDL_SetWindowAlwaysOnTop(w, true); // the new window comes up behind this one
  }
#ifdef _WIN32
  AllowSetForegroundWindow(ASFW_ANY); // and may take the focus from this one
#endif
  const Uint64 until = SDL_GetTicks() + 30000;
  while (SDL_GetTicks() < until) {
    SDL_PathInfo info;
    int code = 0;
    if (SDL_GetPathInfo(ready.c_str(), &info) || SDL_WaitProcess(process, false, &code)) {
      break;
    }
    SDL_PumpEvents(); // this window keeps answering (not "not responding") meanwhile
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    SDL_Delay(15);
  }
  SDL_RemovePath(ready.c_str());
}

// Starts this program again and ends this one; false (and the reason in the
// log) if it couldn't start
bool restartNow() {
  const std::string exe = selfPath();
  if (exe.empty()) {
    std::fprintf(stderr, "restart: this program's path is unknown\n");
    sWhy = "this program's path is unknown";
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
  // what another version's launcher (an invite: the Multiplayer page, the invite's room) or an update's
  // restart (the What's new notice) told this start: this start's only
  const char* updated = SDL_GetEnvironmentVariable(env, "TTYD_UPDATE_APPLIED");
  const std::string updateApplied = updated != nullptr ? updated : "";
  for (const char* name : {"TTYD_OPEN_MULTIPLAYER", "TTYD_ONLINE_INVITE", "TTYD_UPDATE_APPLIED"}) {
    if (SDL_GetEnvironmentVariable(env, name) != nullptr) {
      std::printf("[restart] not passed on: %s\n", name);
      SDL_UnsetEnvironmentVariable(env, name);
    }
  }
  char path[1024], sidecar[1024];
  portResumePath(path, sizeof(path), PORT_GAME_ID, 0);
  portResumePath(sidecar, sizeof(sidecar), PORT_GAME_ID, 1);
  if (sResume == kLauncher) {
    // Back to the launcher: the new program shows its launcher, whatever "Skip the launcher" says, and no disc
    // is given to it (nor a test's or a script's start); quick resume's save written for its Continue
    for (const char* name : {"TTYD_DISC", "TTYD_NO_LAUNCHER", "TTYD_EXIT_FRAMES", "TTYD_SHOT", "TTYD_LAUNCHER_BACK_AT",
                             "TTYD_MODE_TEST", "TTYD_MODE_STOP", "TTYD_LAUNCHER_TAB"}) {
      SDL_UnsetEnvironmentVariable(env, name);
    }
    SDL_SetEnvironmentVariable(env, "TTYD_LAUNCHER_BACK", "1", true);
    portResumeFlush();
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
  // the new program's window drawn before this one goes (waitForHandoff), unless turned off
  const std::string handoff = handoffWanted() ? handoffPath() : std::string();
  SDL_UnsetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF_RAISE");
  if (!handoff.empty()) {
    SDL_RemovePath(handoff.c_str());
    SDL_SetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF", handoff.c_str(), true);
    if (SDL_GetWindowFlags(aurora_get_window()) & SDL_WINDOW_INPUT_FOCUS) {
      SDL_SetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF_RAISE", "1", true); // (the focus goes with it)
    }
  } else {
    SDL_UnsetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF");
  }
  const std::string disc = sResume == kLauncher ? std::string() : std::string(portAuroraDiscPath());
  const char* args[] = {exe.c_str(), disc.empty() ? nullptr : disc.c_str(), nullptr};
  SDL_Process* process = SDL_CreateProcess(args, false);
  if (process == nullptr) {
    std::fprintf(stderr, "restart: could not start %s: %s\n", exe.c_str(), SDL_GetError());
    sWhy = SDL_GetError();
    // (this game runs on: what was set for the new one taken back, as the launcher's Continue does)
    for (const char* name : {"TTYD_LOAD_SAVE", "TTYD_LOAD_SAVE_SLOT", "TTYD_RESTART_HANDOFF",
                             "TTYD_RESTART_HANDOFF_RAISE", "TTYD_LAUNCHER_BACK"}) {
      SDL_UnsetEnvironmentVariable(env, name);
    }
    if (!updateApplied.empty()) {
      SDL_SetEnvironmentVariable(env, "TTYD_UPDATE_APPLIED", updateApplied.c_str(), true); // (still this run's)
    }
    return false;
  }
  std::printf("[restart] %s%s%s\n", exe.c_str(),
              sResume == kLauncher          ? " (the launcher)"
              : sResume == kReplay          ? " (replay)"
              : sResume && portResumeHave() ? " (quick resume)"
                                            : "",
              handoff.empty() ? "" : ", this window kept until the new one has drawn");
  std::fflush(stdout);
  portModsStartedOk(); // a restart isn't a failed start (safe mode, port/include/port/mods.h)
  // online play left now, as this program's end would (the room told while this window is still
  // up): every way out, the handoff's or not (portEndForStarted's _Exit runs no leave of its own)
  portStateSyncLeave();
  if (!handoff.empty()) {
    waitForHandoff(process, handoff); // the wait for its window
  }
  portEndForStarted(process); // it runs on; this one ends
  return true;
}

// a requested restart once the card is idle: what had to wait for it, then the restart
void tryRestart() {
  if (!sRequested || cardIsExec()) {
    return;
  }
  sRequested = false;
  int (*whenIdle)(void) = sWhenIdle;
  sWhenIdle = nullptr;
  if (whenIdle != nullptr && !whenIdle()) {
    // (what failed is on the tool's own page: its status line)
    sFailed = portTr("Not restarted: the save couldn't be changed.");
    portSaveStateNotice(sFailed.c_str());
    return;
  }
  if (!restartNow()) { // (returns only when it couldn't)
    // the card already rewritten: the game running now still has the old save in memory
    sFailed = whenIdle != nullptr
                  ? portTrf("The save is changed, but the game couldn't start again (%s). Quit and open the game again.",
                            sWhy.c_str())
                  : portTrf("The game couldn't start again (%s). Quit and open it again yourself.", sWhy.c_str());
    portSaveStateNotice(sFailed.c_str());
  }
}

// each presented frame
void frame(int) { tryRestart(); }

const bool sAdded = [] {
  portSettingsAddOverlay(frame, nullptr);
  sHandoffOn = portSettingsDeclare("restart_handoff", 1, 0, 1);
  return true;
}();

} // namespace

// The new program's side of the handoff (vi_aurora.c, once its first frame is shown): the old
// window told it can go, and the focus taken over if the old one had it
extern "C" void portRestartHandoffReady(void) {
  SDL_Environment* env = SDL_GetEnvironment();
  const char* at = SDL_GetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF");
  if (at == nullptr || at[0] == '\0') {
    return;
  }
  const std::string ready = at;
  const bool raise = SDL_GetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF_RAISE") != nullptr;
  SDL_UnsetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF"); // (not this program's own next restart)
  SDL_UnsetEnvironmentVariable(env, "TTYD_RESTART_HANDOFF_RAISE");
  SDL_SaveFile(ready.c_str(), "ready", 5);
  if (SDL_Window* w = aurora_get_window(); w != nullptr && raise && !portOffscreen()) {
    SDL_RaiseWindow(w);
  }
}

// Linux in an AppImage or a Flatpak: the program started lives in this one's mount (the AppImage's
// runtime unmounts it once this process is gone: the new one then dies of SIGBUS at its next page
// of code, or can't load a map's module) or sandbox (ended with this process). So this process
// becomes a small waiter (the same executable, $TTYD_WAIT_FOR_PID) that holds them until the new
// one ends: everything else of this one (the GPU, the window, the memory) goes with the exec.
extern "C" void portEndForStarted(SDL_Process* process) {
  portCrashQuitStarted(); // (a fault while handing over is no crash)
  std::fflush(stdout);
  std::fflush(stderr);
  if (SDL_Window* w = aurora_get_window()) {
    SDL_HideWindow(w);
  }
  if (process != nullptr && SDL_getenv("TTYD_RESTART_WAIT") != nullptr) {
    // tests: this one stays (its window hidden) until the new one ends, and ends with its code, so the
    // test's run lasts as long as both and stopping it stops both
    int code = 0;
    SDL_WaitProcess(process, true, &code);
    SDL_DestroyProcess(process);
    std::printf("[restart] the program started ended (%d): this one ends too\n", code);
    std::fflush(stdout);
    std::_Exit(code);
  }
#if defined(__linux__) && !defined(__ANDROID__)
  if (process != nullptr && (SDL_getenv("APPIMAGE") != nullptr || access("/.flatpak-info", F_OK) == 0) &&
      SDL_getenv("TTYD_NO_WAITER") == nullptr) {
    const Sint64 pid = SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0);
    if (pid > 0) {
      char text[32];
      std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(pid));
      setenv("TTYD_WAIT_FOR_PID", text, 1);
      execl("/proc/self/exe", "papercut-waiter", static_cast<char*>(nullptr));
      unsetenv("TTYD_WAIT_FOR_PID"); // (couldn't: this one ends as everywhere else)
    }
  }
#endif
  SDL_DestroyProcess(process);
  // at once: Aurora's shutdown (its GPU and audio threads) can hang, and everything that matters is written
  std::_Exit(0);
}

#if defined(__linux__) && !defined(__ANDROID__)
static volatile pid_t sWaitedFor;
static void passOn(int sig) {
  const int saved = errno;
  kill(sWaitedFor, sig);
  errno = saved;
}
#endif

// The waiter's side (main_aurora.c, first thing): with $TTYD_WAIT_FOR_PID, waits for that child and
// ends with its status. Sockets and devices (the display's, the GPU's) the exec left open are
// closed; pipes and files stay (an AppImage's runtime watches a pipe to know when to unmount).
extern "C" void portWaitForStartedIfAsked(void) {
#if defined(__linux__) && !defined(__ANDROID__)
  const char* text = getenv("TTYD_WAIT_FOR_PID");
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  const pid_t pid = static_cast<pid_t>(std::atoll(text));
  unsetenv("TTYD_WAIT_FOR_PID");
  if (DIR* dir = opendir("/proc/self/fd")) {
    std::vector<int> shut;
    const int own = dirfd(dir);
    while (dirent* e = readdir(dir)) {
      const int fd = std::atoi(e->d_name);
      struct stat st;
      if (fd > 2 && fd != own && fstat(fd, &st) == 0 && (S_ISSOCK(st.st_mode) || S_ISCHR(st.st_mode))) {
        shut.push_back(fd);
      }
    }
    closedir(dir);
    for (int fd : shut) {
      close(fd);
    }
  }
  signal(SIGCHLD, SIG_DFL); // (an ignored SIGCHLD stays ignored across exec: children reaped unseen)
  // Asked to end (Steam's Exit game, a logout, Ctrl+C: #27): passed on to the game, which quits as it
  // always does, and waited for, as this one holds its mount until then
  sWaitedFor = pid;
  struct sigaction pass {};
  pass.sa_handler = passOn;
  sigemptyset(&pass.sa_mask);
  for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
    struct sigaction was {};
    if (sigaction(sig, nullptr, &was) == 0 && was.sa_handler != SIG_IGN) {
      sigaction(sig, &pass, nullptr);
    }
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    // not this process's child after all: until it's gone
    while (kill(pid, 0) == 0 || errno == EPERM) {
      usleep(500 * 1000);
    }
    std::_Exit(0);
  }
  std::_Exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
#endif
}

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
  if (!kCanRestart) {
    return;
  }
  sRequested = true;
  sResume = resume;
  sWhenIdle = whenIdle;
  sFailed.clear();
  tryRestart(); // now when the card is idle (returns only when it couldn't restart)
}

extern "C" void portRestartRequest(int resume) { portRestartRequestWith(resume, nullptr); }

// Back to the launcher (launcher_back_aurora.c), at the title with the card idle: this program again, on
// its launcher; 0 when it couldn't start (this one plays on)
extern "C" int portRestartToLauncher(void) {
  if (!kCanRestart) {
    return 0;
  }
  sRequested = false;
  sWhenIdle = nullptr;
  sResume = kLauncher;
  const bool started = restartNow(); // (returns only when it couldn't)
  sResume = 0;
  return started ? 1 : 0;
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
  if (sRequested) {
    ImGui::TextDisabled("%s", portTr("Restarting when the game has finished saving..."));
    return;
  }
  if (ImGui::SmallButton(portTrLabel("Restart now##restart"))) {
    sFailed.clear();
    portRestartRequest(1);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("Starts the game again with this disc; quick resume brings you back here."));
  }
  if (!sFailed.empty()) {
    ImGui::TextWrapped("%s", sFailed.c_str());
  }
}
