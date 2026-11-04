// One-click join (online co-op, port/include/port/online.h): invite links,
// papercut://join/<CODE> (papercut://join/K7P2-QX9M).
//
// Each system names Paper Cut as the link's handler: on Windows the game
// writes HKCU\Software\Classes\papercut when it starts, pointing at its own
// exe (the user's own keys only, nothing machine-wide); macOS and iOS have
// CFBundleURLTypes in their Info.plist, Linux the .desktop file's
// x-scheme-handler/papercut, Android an intent filter. A link reaches the game
// as an argument (Windows, Linux) or as SDL's drop event (macOS, iOS: SDL's
// open-URL handlers; Android: TtydActivity passes the intent's link). Either
// way the launcher opens on its Online tab with the code filled in, asking
// "Join?" (online.cpp): it never joins by itself.
#include "port/online.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

std::mutex sLock; // (Android's link comes on its Java thread)
std::string sPending;

bool isInvite(const char* url) { return url != nullptr && SDL_strncasecmp(url, "papercut:", 9) == 0; }

#ifdef _WIN32
// HKCU\Software\Classes\<name>: a value written only when it isn't already that
void setValue(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
  wchar_t have[2048];
  DWORD size = sizeof(have);
  if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, have, &size) == ERROR_SUCCESS &&
      value == have) {
    return;
  }
  RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                  static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

// papercut:// links open this exe. Not from test runs ($TTYD_FAST), which would
// point the player's links at a build folder; $TTYD_URL_SCHEME_TEST writes
// papercut-test instead (the code's own test).
void registerScheme() {
  const bool test = SDL_getenv("TTYD_URL_SCHEME_TEST") != nullptr;
  if (SDL_getenv("TTYD_FAST") != nullptr && !test) {
    return;
  }
  wchar_t exe[32768];
  const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
  if (n == 0 || n >= sizeof(exe) / sizeof(exe[0])) {
    return;
  }
  const std::wstring key = test ? L"Software\\Classes\\papercut-test" : L"Software\\Classes\\papercut";
  setValue(key, nullptr, L"URL:Paper Cut invite");
  setValue(key, L"URL Protocol", L"");
  setValue(key + L"\\DefaultIcon", nullptr, std::wstring(L"\"") + exe + L"\",0");
  setValue(key + L"\\shell\\open\\command", nullptr, std::wstring(L"\"") + exe + L"\" \"%1\"");
}
#endif

} // namespace

// The room's code in an invite link (papercut://join/K7P2-QX9M, any case, a
// dash or none), as the code itself (K7P2QX9M), or 0 when it isn't one.
extern "C" int portOnlineInviteCode(const char* url, char* out, size_t size) {
  if (!isInvite(url) || size < PORT_ONLINE_CODE_LENGTH + 1) {
    return 0;
  }
  const char* p = url + 9;
  while (*p == '/') {
    p++;
  }
  if (SDL_strncasecmp(p, "join/", 5) != 0) {
    return 0;
  }
  p += 5;
  std::string code;
  for (; *p != '\0' && *p != '/' && *p != '?' && *p != '#'; p++) {
    if (*p == '-') {
      continue;
    }
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    if (std::strchr(PORT_ONLINE_CODE_CHARS, c) == nullptr || code.size() >= PORT_ONLINE_CODE_LENGTH) {
      return 0;
    }
    code += c;
  }
  if (code.size() != PORT_ONLINE_CODE_LENGTH) {
    return 0;
  }
  std::snprintf(out, size, "%s", code.c_str());
  return 1;
}

// A link came: kept for the Online tab, which asks.
extern "C" void portOnlineInvite(const char* url) {
  char code[16];
  if (!portOnlineInviteCode(url, code, sizeof(code))) {
    if (isInvite(url)) {
      SDL_Log("online: a link that isn't an invite");
    }
    return;
  }
  std::lock_guard<std::mutex> hold(sLock);
  sPending = code;
  SDL_Log("online: an invite link (room %.4s)", code); // (the room's half: logs go out with crash reports)
  std::printf("[online] invite link: room %.4s-%s\n", code, "****");
  std::fflush(stdout);
}

// the link's code, once (the Online tab's question); 0 when none came
extern "C" int portOnlineTakeInvite(char* out, size_t size) {
  std::lock_guard<std::mutex> hold(sLock);
  if (sPending.empty()) {
    return 0;
  }
  std::snprintf(out, size, "%s", sPending.c_str());
  sPending.clear();
  return 1;
}

// the link's code still waiting, left there (a build of another version, started
// from the launcher, takes it: $TTYD_ONLINE_INVITE)
extern "C" int portOnlinePeekInvite(char* out, size_t size) {
  std::lock_guard<std::mutex> hold(sLock);
  if (sPending.empty()) {
    return 0;
  }
  std::snprintf(out, size, "%s", sPending.c_str());
  return 1;
}

// each SDL event (vi_aurora.c): a link given to the running game (macOS, iOS, Android)
extern "C" void portOnlineInviteEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type == SDL_EVENT_DROP_FILE && isInvite(e->drop.data)) {
    portOnlineInvite(e->drop.data);
    if (SDL_getenv("TTYD_LAUNCHER_TAB") == nullptr) {
      SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LAUNCHER_TAB", "Online", true);
    }
  }
}

// At the start (main_aurora.c): a link among the arguments (Windows, Linux) taken
// out of them (it isn't a disc), the launcher then opening on the Online tab; and
// on Windows, papercut:// links pointed at this exe.
extern "C" void portOnlineInviteArgs(int* argc, char** argv) {
  bool found = false;
  for (int i = 1; i < *argc;) {
    if (isInvite(argv[i])) {
      portOnlineInvite(argv[i]);
      for (int k = i; k + 1 < *argc; k++) {
        argv[k] = argv[k + 1];
      }
      (*argc)--;
      argv[*argc] = nullptr;
      found = true;
    } else {
      i++;
    }
  }
  if (const char* passed = SDL_getenv("TTYD_ONLINE_INVITE")) { // from the launcher of another version's build
    std::string url = std::string("papercut://join/") + passed;
    portOnlineInvite(url.c_str());
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_ONLINE_INVITE");
    found = true;
  }
  if (found && SDL_getenv("TTYD_LAUNCHER_TAB") == nullptr) {
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LAUNCHER_TAB", "Online", true);
  }
#ifdef _WIN32
  registerScheme();
#endif
}
