// The randomizers' log lines: SDL's log on Aurora (the console, logcat), the
// standard output headless (no SDL there; the test runs read it).
#pragma once

#ifdef PORT_AP_NO_SDL
#include <cstdio>
#define portApLog(...) (std::printf(__VA_ARGS__), std::printf("\n"), std::fflush(stdout))
#else
#include <SDL3/SDL.h>
#define portApLog(...) SDL_Log(__VA_ARGS__)
#endif
