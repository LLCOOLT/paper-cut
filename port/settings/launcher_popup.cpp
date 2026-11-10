// The pop-up launcher (launcher_popup.h): the launcher's stage built like a
// pop-up book as it opens. The shapes are the launcher's own (launcher_style);
// the cast is the game's own faces, made from the player's disc by an art
// recipe (port/assets/luigi/art/recipes/launcher_cast.recipe) at the game's
// first start and read back here from the art cache, so nothing of the
// game's is in the program. Before the game first ran on a disc there is no
// cast, and the rest plays the same. With a Paper Mario 64 ROM linked (port/include/port/pm64.h), PM64's
// partners stand with them, their faces made from the player's ROM as the launcher opens.
#include "launcher_popup.h"
#include "launcher_look.h"
#include "launcher_style.h"
#include "launcher_shot.h"

#include "port/pm64.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <aurora/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// the timeline, in seconds from the launcher's start
constexpr float kCurtain0 = 0.0f, kCurtain1 = 0.9f;  // the curtains part (and gather at their tie-backs)
// the stage's layers pop up in turn behind them (LauncherStageLayer's order)
constexpr float kLayer[][2] = {{0.12f, 0.55f},  // the boards fold up from the front edge
                               {0.28f, 0.72f},  // the far clouds
                               {0.38f, 0.82f},  // the near clouds
                               {0.48f, 0.98f},  // the moon and stars drop on their strings
                               {0.8f, 1.25f}};  // the footlights and spotlight come up
constexpr float kTitle0 = 0.55f, kTitle1 = 1.05f;    // the title unfolds
constexpr float kCards0 = 0.4f, kCards1 = 1.0f;      // the cards rise
constexpr float kCast0 = 0.85f, kCastStep = 0.08f, kCastPop = 0.4f; // the cast springs up, one after another

constexpr const char* kCast[] = {"mario", "goombella", "koops", "bobbery", "yoshi", "flurrie", "vivian", "mowz"};
constexpr int kCastCount = 8;
constexpr int kPm64Max = 16; // room for every PM64 partner (pm64.h's portPm64CastCount)

int* sOn;
bool sSkipped = false;
std::string sCastDir; // <user>/art_cache/<game id>/art/launcher/, "" without
bool sCastTried = false;
struct Face {
  ImTextureID tex{};
  float w = 0.f, h = 0.f;
} sFaces[kCastCount + kPm64Max];
int sFaceCount = 0;
int sGameFaces = 0;    // of them the game's own (the rest PM64's, after them)
unsigned sPm64Gen = 0; // portPm64Generation() + 1 when PM64's were made, 0 not yet

// when the last of the cast is up: the pop-up's end
float castEnd() { return kCast0 + static_cast<float>(std::max(sFaceCount, kCastCount) - 1) * kCastStep + kCastPop; }

bool on() {
  if (sOn == nullptr) {
    sOn = portSettingsDeclare("launcher_popup", 1, 0, 1);
  }
  return *sOn != 0 && !sSkipped;
}

float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
float phase(float time, float a, float b) { return clamp01((time - a) / (b - a)); }
float easeOut(float t) { return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
float easeInOut(float t) { return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f; }
// a pop-up page's spring: past upright and back
float popUp(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.f, u = t - 1.f;
  return t <= 0.f ? 0.f : t >= 1.f ? 1.f : 1.f + c3 * u * u * u + c1 * u * u;
}

// any key, click, touch or button: the pop-up skipped
void checkSkip(float time) {
  if (sSkipped || time > castEnd()) {
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  bool pressed = false;
  for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
    pressed |= io.MouseClicked[b];
  }
  for (ImGuiKey k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !pressed; k = static_cast<ImGuiKey>(k + 1)) {
    if (k >= ImGuiKey_GamepadLStickLeft && k <= ImGuiKey_GamepadRStickDown) {
      continue; // a resting stick's drift is no press
    }
    if (k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY) {
      continue;
    }
    pressed |= ImGui::IsKeyPressed(k, false);
  }
  if (pressed) {
    sSkipped = true;
  }
}

void addFace(int w, int h, const void* rgba) {
  Face& f = sFaces[sFaceCount++];
  f.tex = launcherTexture(w, h, rgba); // (made once per picture: the faces come again at each start and link)
  f.w = static_cast<float>(w);
  f.h = static_cast<float>(h);
}

// PM64's partners after the game's cast, from the linked ROM (none without one); again after a link or unlink
void loadPm64Cast() {
  if (sGameFaces == 0) {
    return; // (with the game's cast only: they stand after it)
  }
  (void)portPm64Linked(); // (its first look at the cache, which may link one: tests' TTYD_PM64_ROM)
  if (sPm64Gen == portPm64Generation() + 1) {
    return;
  }
  sFaceCount = sGameFaces;
  sPm64Gen = portPm64Generation() + 1;
  static uint8_t rgba[PM64_CAST_FACE_MAX * PM64_CAST_FACE_MAX * 4];
  const int n = std::min(portPm64CastCount(), kPm64Max);
  for (int i = 0; i < n; i++) {
    int w = 0, h = 0;
    if (portPm64CastFace(i, rgba, &w, &h) && w > 0 && h > 0) {
      addFace(w, h, rgba);
    }
  }
}

void loadCast() {
  if (sCastTried) {
    loadPm64Cast();
    return;
  }
  sCastTried = true;
  sFaceCount = sGameFaces = 0;
  sPm64Gen = 0;
  if (sCastDir.empty()) {
    return;
  }
  for (const char* name : kCast) {
    const std::string path = sCastDir + name + ".png";
    SDL_Surface* s = SDL_LoadPNG(path.c_str());
    if (s == nullptr) {
      continue;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba == nullptr) {
      continue;
    }
    if (rgba->pitch == rgba->w * 4) {
      addFace(rgba->w, rgba->h, rgba->pixels);
    }
    SDL_DestroySurface(rgba);
  }
  sGameFaces = sFaceCount;
  loadPm64Cast();
}

float aspect(const Face& f) { return f.w / std::fmax(f.h, 1.f); }

// A row's width in face heights: its faces, the gaps within its two groups, and a face's width between them
float rowUnits(int first, int count) {
  float units = 0.f;
  for (int i = first; i < first + count; i++) {
    units += aspect(sFaces[i]);
  }
  return units + 0.15f * static_cast<float>(std::max(count - 2, 0)) + 1.f;
}

// A row of the cast: faces [first, first + count) h tall, standing on bottom, between l and r: Mario's group
// (the first half) from the left, the rest to the right. Each springs up in its turn of the whole cast.
void drawRow(ImDrawList* dl, int first, int count, float h, float bottom, float l, float r, float time, bool playing) {
  const float gap = h * 0.15f;
  const int left = (count + 1) / 2;
  float xl = l, xr = r + gap;
  for (int j = left; j < count; j++) {
    xr -= h * aspect(sFaces[first + j]) + gap;
  }
  for (int j = 0; j < count; j++) {
    const int i = first + j;
    const Face& f = sFaces[i];
    const float w = h * aspect(f);
    float& at = j < left ? xl : xr;
    const float x = at;
    at += w + gap;
    const float t0 = kCast0 + static_cast<float>(i) * kCastStep;
    const float k = playing ? popUp(phase(time, t0, t0 + kCastPop)) : 1.f;
    if (k <= 0.f) {
      continue;
    }
    // its shadow on the floor, then the face standing on it
    dl->AddEllipseFilled(ImVec2(x + w * 0.5f, bottom), ImVec2(w * 0.45f, h * 0.08f), IM_COL32(20, 10, 30, 90));
    dl->AddImage(f.tex, ImVec2(x, bottom - h * k), ImVec2(x + w, bottom));
  }
}

} // namespace

void launcherPopupStart(const char* userPath, const char* gameId) {
  sSkipped = false;
  sCastTried = false;
  sCastDir.clear();
  if (userPath != nullptr && gameId != nullptr && gameId[0] != '\0') {
    sCastDir = std::string(userPath) + "/art_cache/" + gameId + "/art/launcher/";
  }
}

void launcherPopupStage(float time, float scale) {
  (void)scale;
  checkSkip(time);
}

float launcherPopupLayer(LauncherStageLayer layer, float time) {
  if (!on()) {
    return 1.f;
  }
  static bool sTold; // once: the pop-up is playing
  if (!sTold) {
    sTold = true;
    std::printf("[launcher] pop-up playing\n");
  }
  const int i = static_cast<int>(layer);
  return phase(time, kLayer[i][0], kLayer[i][1]);
}

int launcherPopupMark(ImDrawList* dl) { return dl->VtxBuffer.Size; }

void launcherPopupFold(ImDrawList* dl, int mark, float baseline, float time) {
  if (!on() || time >= kTitle1) {
    return;
  }
  // upright from flat on the stage (its height from 0), springing past upright
  const float k = popUp(phase(time, kTitle0, kTitle1));
  for (int i = mark; i < dl->VtxBuffer.Size; i++) {
    ImDrawVert& v = dl->VtxBuffer[i];
    v.pos.y = baseline + (v.pos.y - baseline) * k;
  }
}

float launcherPopupRise(float height, float time) {
  if (!on() || time >= kCards1) {
    return 0.f;
  }
  return height * (1.f - easeOut(phase(time, kCards0, kCards1)));
}

void launcherPopupFront(float time, float scale) {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  const bool playing = on();
  // the cast along the stage's front edge (they stay once up)
  if (sOn == nullptr || *sOn != 0) {
    loadCast();
  }
  if (sFaceCount > 0 && (sOn == nullptr || *sOn != 0)) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float floorTop = size.y * 0.86f;
    const float side = std::fmax(36.f * scale, size.x * 0.075f);
    const float h = std::fmin((size.y - floorTop) * 0.62f, 60.f * scale);
    const float bottom = size.y - 4.f * scale;
    const float l = side + 16.f * scale, r = size.x - side - 16.f * scale;
    const int pm64 = sFaceCount - sGameFaces;
    if (pm64 <= 0) {
      drawRow(dl, 0, sFaceCount, h, bottom, l, r, time, playing);
    } else {
      // PM64's partners too: all in one row, shrunk to fit the stage's width, or (a narrow stage) theirs a
      // second row behind the game's, standing higher, between them: whichever shows the faces larger.
      // Neither reaches above the floor's front, where the cards stand (launcherBackdrop's stage).
      const float cardsBottom = floorTop + (size.y - floorTop) * 0.35f;
      const float span = std::fmax(r - l, 1.f);
      const float lift = 0.55f; // the back row's feet above the front's at least, in face heights
      const float one = std::fmin(h, span / rowUnits(0, sFaceCount));
      const float two = std::min({h, (bottom - cardsBottom) / (1.f + lift),
                                   span / std::fmax(rowUnits(0, sGameFaces), rowUnits(sGameFaces, pm64) + 1.f)});
      if (one * 1.15f >= two) {
        drawRow(dl, 0, sFaceCount, one, bottom, l, r, time, playing);
      } else {
        // (as high as the room below the cards allows, up to a whole face: the back row hidden as little as can be)
        const float raise = std::clamp((bottom - cardsBottom) / two - 1.f, lift, 1.f);
        const float inset = two * 0.5f; // (half a face in from each side: between the front row's)
        drawRow(dl, sGameFaces, pm64, two, bottom - two * raise, l + inset, r - inset, time, playing);
        drawRow(dl, 0, sGameFaces, two, bottom, l, r, time, playing);
      }
    }
  }
  if (!playing || time >= kCurtain1) {
    return;
  }
  // the curtains, closed over the whole stage at first, drawn to the sides and tied back
  launcherStageFront(easeInOut(phase(time, kCurtain0, kCurtain1)), time, scale);
}

ImTextureID launcherCastFace(int i, float* aspect) {
  loadCast();
  if (i < 0 || i >= sGameFaces || i >= kCastCount) {
    return ImTextureID{};
  }
  // (a face that didn't load leaves the rest one earlier: only the full cast is in kCast's order)
  if (sGameFaces != kCastCount) {
    return ImTextureID{};
  }
  if (aspect != nullptr) {
    *aspect = sFaces[i].w / std::fmax(sFaces[i].h, 1.f);
  }
  return sFaces[i].tex;
}
