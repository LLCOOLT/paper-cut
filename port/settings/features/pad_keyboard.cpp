// An on-screen keyboard for Paper Cut's own text fields with a controller (port/include/port/pad_keyboard.h).
//
// Dear ImGui's text fields take letters from a keyboard only, so a controller player could reach a room code's
// field, a save state's label or the search box and not type in it. Here, while a field is being typed in and the
// last press was a controller's, a keyboard is drawn over the page by the field (Dear ImGui's own text cursor
// place, the one it gives the system's IME): ten keys a row, letters (Shift: capitals) or symbols, and a row of
// Shift, symbols, Space, Delete and Done. It types through Dear ImGui's own input (characters, Backspace, the
// arrows, Enter), so every field works as with a keyboard: its filters, its length, its Enter.
//
// While it is up the controllers are its alone: their button events stop here (no Settings from Back, no page
// back from B), Dear ImGui's gamepad navigation is off for the next frame and its gamepad keys are locked (the
// page doesn't move, its tabs don't switch under the shoulders), and the game gets nothing
// (portKeyboardApplyPad). Any key or click (portMenuPadInUse) gives the field back to the keyboard.
#include "port/pad_keyboard.h"
#include "port/menu_buttons.h"
#include "port/tr.h"

#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // the active text field (InputTextState), key owners, ClearActiveID

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace {

constexpr int kRows = 4, kCols = 10;
// letters (Shift: the capitals) and symbols; each row ten keys
const char* const kLetters[kRows] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm,.'"};
const char* const kSymbols[kRows] = {"1234567890", "!?@#$%&*()", "+-=_/:;\"~^", "<>[]{}|,.'"};
// the bottom row
enum { kShift, kSym, kSpace, kDelete, kDone, kSpecials };
const char* const kSpecialText[kSpecials] = {"Shift", "#+=", "Space", "Delete", "Done"};
constexpr float kSpecialWidth[kSpecials] = {2.f, 1.5f, 3.f, 2.f, 1.5f}; // in keys, 10 in all

bool sShown;        // drawn last frame: the controllers are its
bool sShift, sSyms; // Shift held down (one letter), the symbols shown
int sRow = 1, sCol; // the key under the cursor (row kRows: the bottom one, sCol a special)
bool sDoneAsked;    // Done on a field of several lines: it lets go of the field in the frame (ClearActiveID)
int sDir = -1;      // the direction held (0 left, 1 right, 2 up, 3 down), -1 none
Uint64 sNextMove;   // its next repeat
const ImGuiID kOwner = ImHashStr("pad_keyboard");

bool logOn() {
  static const bool on = SDL_getenv("TTYD_PAD_KEYBOARD_TEST") != nullptr;
  return on;
}
void say(const char* what, const char* detail = "") {
  if (logOn()) {
    std::printf("[pad-keyboard] %s%s%s\n", what, detail[0] != '\0' ? " " : "", detail);
    std::fflush(stdout);
  }
}

// the field being typed in now (Dear ImGui's), or none
ImGuiInputTextState* activeField() {
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  if (ctx == nullptr || ctx->ActiveId == 0 || ctx->InputTextState.ID != ctx->ActiveId) {
    return nullptr;
  }
  return &ctx->InputTextState;
}

char keyChar(int row, int col) {
  const char c = (sSyms ? kSymbols : kLetters)[row][col];
  return sShift && !sSyms ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
}

void tap(ImGuiKey k) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(k, true);
  io.AddKeyEvent(k, false); // (Dear ImGui's queue trickles them: down this frame, up the next)
}

void typeChar(char c) {
  ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(static_cast<unsigned char>(c)));
  char s[2] = {c, '\0'};
  say("typed", s);
  if (sShift) {
    sShift = false; // (one capital, as a phone's Shift)
  }
}

void done() {
  const ImGuiInputTextState* f = activeField();
  if (f != nullptr && (f->Flags & ImGuiInputTextFlags_Multiline) != 0) {
    sDoneAsked = true; // (Enter there is a new line: the field let go in the frame instead)
  } else {
    tap(ImGuiKey_Enter);
  }
  say("done");
}

void press() {
  if (sRow < kRows) {
    typeChar(keyChar(sRow, sCol));
    return;
  }
  switch (sCol) {
  case kShift: sShift = !sShift; break;
  case kSym: sSyms = !sSyms; break;
  case kSpace: typeChar(' '); break;
  case kDelete: tap(ImGuiKey_Backspace); break;
  case kDone: done(); break;
  default: break;
  }
}

// the bottom row's key under a column of the rows above it, and back
int specialAt(int col) {
  float x = 0.f;
  for (int k = 0; k < kSpecials; k++) {
    x += kSpecialWidth[k];
    if (static_cast<float>(col) + 0.5f < x) {
      return k;
    }
  }
  return kSpecials - 1;
}
int columnOf(int special) {
  float x = 0.f;
  for (int k = 0; k < special; k++) {
    x += kSpecialWidth[k];
  }
  return std::min(kCols - 1, static_cast<int>(x + kSpecialWidth[special] * 0.5f));
}

void move(int dir) {
  const int width = sRow == kRows ? kSpecials : kCols;
  switch (dir) {
  case 0: sCol = (sCol + width - 1) % width; break;
  case 1: sCol = (sCol + 1) % width; break;
  case 2:
    if (sRow == kRows) {
      sCol = columnOf(sCol);
    }
    sRow = (sRow + kRows) % (kRows + 1);
    if (sRow == kRows) {
      sCol = specialAt(sCol);
    }
    break;
  case 3:
    if (sRow == kRows) {
      sCol = columnOf(sCol);
    }
    sRow = (sRow + 1) % (kRows + 1);
    if (sRow == kRows) {
      sCol = specialAt(sCol);
    }
    break;
  default: break;
  }
}

// the D-pad or a stick held, on any controller (repeats while held, as a keyboard's arrows do)
void pollMove() {
  int dir = -1;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids != nullptr && i < count && dir < 0; i++) {
    SDL_Gamepad* p = SDL_GetGamepadFromID(ids[i]);
    if (p == nullptr) {
      continue;
    }
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
      dir = 0;
    } else if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
      dir = 1;
    } else if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
      dir = 2;
    } else if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
      dir = 3;
    } else {
      const float x = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
      const float y = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
      if (std::fabs(x) >= 0.5f || std::fabs(y) >= 0.5f) {
        dir = std::fabs(x) >= std::fabs(y) ? (x < 0.f ? 0 : 1) : (y < 0.f ? 2 : 3);
      }
    }
  }
  SDL_free(ids);
  const Uint64 now = SDL_GetTicks();
  if (dir != sDir) {
    sDir = dir;
    if (dir >= 0) {
      move(dir);
      sNextMove = now + 350;
    }
  } else if (dir >= 0 && now >= sNextMove) {
    move(dir);
    sNextMove = now + 70;
  }
}

// a button's name on the controller in hand (its own labels), in the menu language
std::string buttonName(int button) {
  SDL_Gamepad* p = SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(portMenuPadId()));
  const GlyphStyle st = p != nullptr ? glyphStyleOf(SDL_GetGamepadType(p), SDL_GetGamepadVendor(p), SDL_GetGamepadProduct(p))
                                     : GlyphStyle::Xbox;
  return portTr(nativeButtonLabel(st, static_cast<uint32_t>(button), 0));
}

void draw(const ImGuiInputTextState& field) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize();
  const float key = std::min((display.x - 32.f) / (kCols + 0.8f), std::max(fs * 2.1f, display.y / 15.f));
  const float gap = key * 0.08f, pad = key * 0.4f;
  const float w = key * kCols + pad * 2.f, h = key * (kRows + 1) + pad * 2.f + fs * 1.6f;
  // under the field's text cursor, else over it, else at the bottom
  const ImVec2 caret = g.PlatformImeData.InputPos;
  const float line = std::max(g.PlatformImeData.InputLineHeight, fs);
  float y = caret.y + line + 8.f;
  if (y + h > display.y - 8.f) {
    y = caret.y - 8.f - h >= 8.f ? caret.y - 8.f - h : display.y - 8.f - h;
  }
  const float x = std::floor((display.x - w) * 0.5f);
  y = std::floor(std::max(8.f, y));
  dl->AddRectFilled(ImVec2(x + 4.f, y + 5.f), ImVec2(x + w + 4.f, y + h + 5.f), IM_COL32(0, 0, 0, 90), key * 0.3f);
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(246, 238, 220, 250), key * 0.3f);
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(120, 96, 64, 255), key * 0.3f, 0, 2.f);
  auto keyBox = [&](float kx, float ky, float kw, const char* text, bool on, bool lit) {
    const ImVec2 a(kx + gap, ky + gap), b(kx + kw - gap, ky + key - gap);
    dl->AddRectFilled(a, b, on ? IM_COL32(70, 120, 210, 255) : lit ? IM_COL32(222, 206, 170, 255) : IM_COL32(255, 252, 244, 255),
                      key * 0.18f);
    dl->AddRect(a, b, on ? IM_COL32(30, 60, 140, 255) : IM_COL32(160, 136, 100, 255), key * 0.18f, 0, on ? 3.f : 1.f);
    const float size = std::min(fs * 1.25f, key * 0.5f);
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    float s = size;
    if (ts.x > kw - gap * 4.f) { // (a long translation of Delete or Space: smaller)
      s = size * (kw - gap * 4.f) / ts.x;
      ts = font->CalcTextSizeA(s, FLT_MAX, 0.f, text);
    }
    dl->AddText(font, s, ImVec2(kx + (kw - ts.x) * 0.5f, ky + (key - ts.y) * 0.5f),
                on ? IM_COL32(255, 255, 255, 255) : IM_COL32(50, 40, 30, 255), text);
  };
  for (int r = 0; r < kRows; r++) {
    for (int c = 0; c < kCols; c++) {
      const char text[2] = {keyChar(r, c), '\0'};
      keyBox(x + pad + key * c, y + pad + key * r, key, text, r == sRow && c == sCol, false);
    }
  }
  float kx = x + pad;
  for (int k = 0; k < kSpecials; k++) {
    const char* text = k == kSym ? (sSyms ? "abc" : kSpecialText[k]) : portTr(kSpecialText[k]);
    keyBox(kx, y + pad + key * kRows, key * kSpecialWidth[k], text, sRow == kRows && sCol == k,
           (k == kShift && sShift) || (k == kSym && sSyms));
    kx += key * kSpecialWidth[k];
  }
  // what the buttons do, the controller in hand's own names
  char help[256];
  std::snprintf(help, sizeof(help), portTr("%s type   %s delete   %s space   %s Shift   %s done"),
                buttonName(portMenuRoleButton(PORT_MENU_CONFIRM, 0)).c_str(),
                buttonName(portMenuRoleButton(PORT_MENU_BACK, 0)).c_str(), buttonName(SDL_GAMEPAD_BUTTON_NORTH).c_str(),
                buttonName(portMenuRoleButton(PORT_MENU_THIRD, 0)).c_str(), buttonName(SDL_GAMEPAD_BUTTON_START).c_str());
  const float hs = fs * 0.9f;
  ImVec2 ts = font->CalcTextSizeA(hs, FLT_MAX, 0.f, help);
  const float hsFit = ts.x > w - pad * 2.f ? hs * (w - pad * 2.f) / ts.x : hs;
  ts = font->CalcTextSizeA(hsFit, FLT_MAX, 0.f, help);
  dl->AddText(font, hsFit, ImVec2(x + (w - ts.x) * 0.5f, y + h - pad * 0.5f - fs * 1.2f), IM_COL32(110, 90, 66, 255), help);
  (void)field;
}

// the controllers are the keyboard's in the next frame: no gamepad navigation, its keys locked to it
void takeControllers() {
  ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
  for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown; k++) {
    ImGui::SetKeyOwner(static_cast<ImGuiKey>(k), kOwner, ImGuiInputFlags_LockUntilRelease);
  }
}

} // namespace

extern "C" int portPadKeyboardShown(void) { return sShown ? 1 : 0; }

extern "C" int portPadKeyboardEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (!sShown || ImGui::GetCurrentContext() == nullptr ||
      (e->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->type != SDL_EVENT_GAMEPAD_BUTTON_UP)) {
    return 0;
  }
  if (e->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
    return 0; // (releases go on: a button held from before lets go where it was pressed)
  }
  const int role = portMenuEventRole(sdlEvent);
  const int b = e->gbutton.button;
  if (role == PORT_MENU_CONFIRM) {
    press();
  } else if (role == PORT_MENU_BACK) {
    tap(ImGuiKey_Backspace);
    say("deleted");
  } else if (role == PORT_MENU_THIRD) {
    sShift = !sShift;
  } else if (b == SDL_GAMEPAD_BUTTON_NORTH) {
    typeChar(' ');
  } else if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
    sSyms = !sSyms;
  } else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
    tap(ImGuiKey_LeftArrow);
  } else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
    tap(ImGuiKey_RightArrow);
  } else if (b == SDL_GAMEPAD_BUTTON_START || b == SDL_GAMEPAD_BUTTON_BACK) {
    done();
  }
  return 1; // (the D-pad too: pollMove moves the cursor)
}

extern "C" void portPadKeyboardDraw(void) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGuiInputTextState* field = activeField();
  if (field != nullptr && sDoneAsked) {
    sDoneAsked = false;
    ImGui::ClearActiveID(); // (Done on a field of several lines: its text kept)
    field = nullptr;
  }
  sDoneAsked = false;
  const bool want = field != nullptr && portMenuPadInUse() != 0;
  if (want != sShown) {
    sShown = want;
    say(want ? "shown" : "hidden");
    if (want) {
      sShift = false;
      sDir = -1;
    }
  }
  if (!sShown) {
    return;
  }
  pollMove();
  draw(*field);
  takeControllers();
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::GetCurrentContext()->PlatformImeData.WantVisible = false; // (the system's keyboard stays down meanwhile)
#endif
}
