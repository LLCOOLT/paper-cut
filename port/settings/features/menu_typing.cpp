// Typing on the keyboard into the Start menu's on-screen keyboards (the text
// page, port/src/aurora/pause_pages_aurora.c, and the Online tab's,
// pause_online_aurora.c), desktop only: while one is open, the letters typed
// go into it, Backspace rubs one out, Enter is OK and Esc leaves, and the
// keyboard's keys are not the game's buttons or Paper Cut's actions (so
// typing an E or a P neither goes back nor opens photo mode). A controller
// and the mouse work on the page as before. The page asks each frame it is
// open (portMenuTypingFrame); a page gone for a frame or two lets go.
#include "port/menu_typing.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/pad.h>

#include <string>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace {

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr bool kDesktop = false; // (a phone's own keyboard would cover the page's)
#else
constexpr bool kDesktop = true;
#endif

bool sOn;       // typing goes to a page
Uint64 sAsked;  // when the page last asked (SDL_GetTicks)
std::string sQueue; // typed since the page last read: letters, '\b', '\n', 0x1B

void setOn(bool on) {
  if (on == sOn) {
    return;
  }
  sOn = on;
  sQueue.clear();
  SDL_Window* w = aurora_get_window();
  if (on) {
    if (w != nullptr) {
      SDL_StartTextInput(w);
    }
    PADSetKeyboardActive(0, FALSE); // the keys are letters now, not the game's buttons
  } else {
    if (w != nullptr) {
      SDL_StopTextInput(w);
    }
    PADSetKeyboardActive(0, TRUE);
  }
}

// each frame drawn (the settings page's overlays): a page that stopped asking
// (closed, or the game paused) lets go; by time, as frames drawn in between
// the game's own don't ask
void tick(int) {
  if (sOn && SDL_GetTicks() - sAsked > 250) {
    setOn(false);
  }
}

} // namespace

extern "C" void portMenuTypingFrame(void) {
  if (!kDesktop) {
    return;
  }
  sAsked = SDL_GetTicks();
  setOn(true);
}

extern "C" int portMenuTyping(void) {
  return sOn ? 1 : 0;
}

extern "C" int portMenuTyped(char* out, int size) {
  int n = 0;
  if (size <= 0) {
    return 0;
  }
  for (char c : sQueue) {
    if (n >= size - 1) {
      break;
    }
    out[n++] = c;
  }
  out[n] = '\0';
  sQueue.clear();
  return n;
}

extern "C" int portMenuTypingEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (!sOn) {
    return 0;
  }
  switch (e->type) {
  case SDL_EVENT_TEXT_INPUT:
    for (const char* p = e->text.text; p != nullptr && *p != '\0'; p++) {
      if (static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) < 0x7F && sQueue.size() < 64) {
        sQueue += *p; // (the pages' keys are plain ASCII)
      }
    }
    return 1;
  case SDL_EVENT_KEY_DOWN:
    if (e->key.key == SDLK_BACKSPACE) {
      sQueue += '\b';
    } else if ((e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) && !e->key.repeat) {
      sQueue += '\n';
    } else if (e->key.key == SDLK_ESCAPE && !e->key.repeat) {
      sQueue += '\x1B';
    } else if (e->key.key == SDLK_V && (e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 && !e->key.repeat) {
      if (char* clip = SDL_GetClipboardText()) {
        for (const char* p = clip; *p != '\0' && sQueue.size() < 64; p++) {
          if (static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) < 0x7F) {
            sQueue += *p;
          }
        }
        SDL_free(clip);
      }
    } else if (e->key.key >= SDLK_F1 && e->key.key <= SDLK_F12) {
      return 0; // F1 (Settings), F11 (full screen)...: still Paper Cut's
    }
    return 1;
  case SDL_EVENT_KEY_UP:
    return e->key.key >= SDLK_F1 && e->key.key <= SDLK_F12 ? 0 : 1;
  default:
    return 0;
  }
}

namespace {
const bool sAdded = [] {
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();
} // namespace
