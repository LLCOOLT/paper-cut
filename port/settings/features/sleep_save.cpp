// Saves > Save before sleep (Linux: a Steam Deck above all, docs/STEAM_DECK.md).
// A Deck put to sleep with the power button can run flat in its sleep, and the
// game in memory goes with it. Before the system sleeps, Paper Cut writes
// quick resume's save (where the player is, port/include/port/resume.h) and
// the settings, so the next start comes back to the same place.
//
// How it hears of the sleep: logind's PrepareForSleep signal, read from
// `gdbus monitor` on the system bus; and so that the system waits for the
// write, a delay lock taken with `systemd-inhibit --mode=delay` (logind waits
// up to its InhibitDelayMaxSec, 5 seconds by default, for the lock to go). The
// lock's process is ended once the save is written; it is taken again on
// waking. Without gdbus or systemd-inhibit (another Linux), nothing happens.
#include "port/detach.h"
#include "port/resume.h"
#include "port/settings.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>

namespace {

#if defined(__linux__) && !defined(__ANDROID__)

int* sOn;
SDL_Process* sMonitor;
SDL_Process* sLock;
std::atomic<int> sSleeping{0}; // 1: the system is about to sleep; 2: woke up
SDL_Thread* sReader;

void takeLock() {
  if (sLock != nullptr) {
    return;
  }
  const char* args[] = {"systemd-inhibit", "--what=sleep", "--mode=delay", "--who=Paper Cut",
                        "--why=Saving where you are", "cat", nullptr};
  // (cat on a pipe from here: if Paper Cut ends, its input ends, and the lock with it)
  sLock = SDL_CreateProcess(args, true);
}

void dropLock() {
  if (sLock != nullptr) {
    SDL_KillProcess(sLock, false);
    SDL_WaitProcess(sLock, true, nullptr);
    SDL_DestroyProcess(sLock);
    sLock = nullptr;
  }
}

// gdbus's lines, on a thread of their own: "...PrepareForSleep (true,)" before sleeping, "(false,)" after
int readMonitor(void*) {
  portCrashThreadInit(); // (a stack overflow here reported too)
  SDL_IOStream* out = SDL_GetProcessOutput(sMonitor);
  char line[512];
  size_t n = 0;
  char c;
  while (out != nullptr && SDL_ReadIO(out, &c, 1) == 1) {
    if (c != '\n' && n + 1 < sizeof(line)) {
      line[n++] = c;
      continue;
    }
    line[n] = '\0';
    n = 0;
    if (std::strstr(line, "PrepareForSleep") != nullptr) {
      sSleeping = std::strstr(line, "true") != nullptr ? 1 : 2;
    }
  }
  return 0;
}

void start() {
  const char* args[] = {"gdbus", "monitor", "--system", "--dest", "org.freedesktop.login1",
                        "--object-path", "/org/freedesktop/login1", nullptr};
  sMonitor = SDL_CreateProcess(args, true);
  if (sMonitor == nullptr) {
    SDL_Log("save before sleep: no gdbus (%s)", SDL_GetError());
    return;
  }
  sReader = SDL_CreateThread(readMonitor, "sleep watch", nullptr);
  takeLock();
}

// each presented frame (the game's thread)
void tick(int) {
  static bool sStarted;
  if (!sStarted) {
    sStarted = true;
    if (*sOn) {
      start();
    }
    return;
  }
  const int state = sSleeping.exchange(0);
  if (state == 1) {
    portResumeFlush(); // where the player is
    portSettingsSave();
    SDL_Log("save before sleep: saved");
    dropLock(); // the system may sleep now
  } else if (state == 2 && *sOn) {
    takeLock();
  }
}

const bool sAdded = [] {
  sOn = portSettingsDeclare("save_before_sleep", 1, 0, 1);
  portSettingsAddToggle("Saves", "Save before sleep",
                        "Linux and the Steam Deck: before the system sleeps, where you are is saved for quick "
                        "resume, so a battery that runs out in its sleep loses nothing.",
                        PORT_SETTING_NEXT_START, "save_before_sleep",
                        "steam deck suspend sleep battery power button save resume");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

#endif

} // namespace
