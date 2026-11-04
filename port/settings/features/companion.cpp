// The second screen (port/include/port/companion.h): its setting (Display >
// Second screen), and on the desktop its presenter, a window of its own
// ("Paper Cut companion"), on another monitor when there is one. It draws
// with a Dear ImGui context of its own through SDL_Renderer, never the
// game's renderer, in the launcher's paper style, a few times a second.
// Android's presenter is in Java (port/android, TtydCompanion.java); iOS has
// none yet.
#include "port/companion.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../launcher_style.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define COMPANION_WINDOW 1
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

extern "C" {
extern const unsigned char portFontBold[];
extern const unsigned int portFontBold_size;
extern const unsigned char portFontMedium[];
extern const unsigned int portFontMedium_size;
extern const unsigned char portIcon64[];
}
// tr.cpp: the system's Japanese merged into the font just added, for these characters
ImFont* portTrMergeJapaneseRanges(float px, const ImWchar* ranges);
#endif

namespace {

#ifdef COMPANION_WINDOW

constexpr float kLoad = 2.f; // fonts loaded at twice their size, drawn smaller
constexpr ImU32 kNight = IM_COL32(24, 28, 74, 255);
constexpr ImU32 kNight2 = IM_COL32(70, 44, 110, 255);
constexpr ImU32 kHp = IM_COL32(214, 44, 52, 255);
constexpr ImU32 kFp = IM_COL32(64, 140, 220, 255);
constexpr ImU32 kTrack = IM_COL32(222, 196, 160, 255);
constexpr ImU32 kMuted = IM_COL32(128, 98, 74, 255);

SDL_Window* sWindow;
SDL_Renderer* sRenderer;
ImGuiContext* sCtx;
bool sFailed, sCloseAsked, sFontsDirty;
Uint64 sLastDraw, sLastFonts;
float sStyleScale;
PortCompanionSnapshot sSnap;
int sPrevScene = -1;
int sUseItem = -1; // the item whose target is being asked
ImFont* sHeading;
ImFont* sBody;
ImFont* sBig;
std::set<ImWchar> sGlyphs; // the characters past Latin-1 shown so far (Japanese)

// --- Fonts: Fredoka, with the system's Japanese merged for the characters shown ---

// notes the characters of text the fonts lack (they are added at the next rebuild)
const char* noteText(const char* text) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0;) {
    unsigned cp = *p;
    int n = 1;
    if (cp >= 0xF0) {
      cp = ((cp & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F), n = 4;
    } else if (cp >= 0xE0) {
      cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F), n = 3;
    } else if (cp >= 0xC0) {
      cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F), n = 2;
    }
    for (int i = 1; i < n; i++) {
      if (p[i] == 0) {
        return text;
      }
    }
    p += n;
    if (cp > 0xFF && cp != 0x2013 && cp != 0x2026 && cp <= 0xFFFF && sGlyphs.insert(static_cast<ImWchar>(cp)).second) {
      sFontsDirty = true;
    }
  }
  return text;
}

// the port's English in the menu language
const char* T(const char* english) {
  return noteText(portTr(english));
}

ImFont* addFont(const unsigned char* data, unsigned int size, float px, const ImVector<ImWchar>& extra) {
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false; // the embedded array stays ours
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  static const ImWchar kLatin[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0};
  ImFont* f = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), px,
                                                         &cfg, kLatin);
  if (extra.Size > 1) {
    portTrMergeJapaneseRanges(px, extra.Data);
  }
  return f;
}

void buildFonts() {
  static ImVector<ImWchar> sRanges; // read when the atlas is built (the next frame)
  ImGuiIO& io = ImGui::GetIO();
  if (io.Fonts->TexID != 0) {
    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
  }
  io.Fonts->Clear();
  sRanges.clear();
  ImFontGlyphRangesBuilder b;
  for (ImWchar c : sGlyphs) {
    b.AddChar(c);
  }
  b.BuildRanges(&sRanges);
  sBody = addFont(portFontMedium, portFontMedium_size, 17.f * kLoad, sRanges);
  sHeading = addFont(portFontBold, portFontBold_size, 21.f * kLoad, sRanges);
  sBig = addFont(portFontBold, portFontBold_size, 30.f * kLoad, sRanges);
  io.FontDefault = sBody;
  sFontsDirty = false;
  sLastFonts = SDL_GetTicks();
}

// --- The window ---

// the window's place and size: on another monitor when there is one, else
// beside the game's window if there is room
void placeWindow(int* x, int* y, int* w, int* h) {
  SDL_Window* game = aurora_get_window();
  const SDL_DisplayID gameDisplay = game != nullptr ? SDL_GetDisplayForWindow(game) : 0;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  SDL_DisplayID other = 0;
  for (int i = 0; displays != nullptr && i < count; i++) {
    if (displays[i] != gameDisplay) {
      other = displays[i];
      break;
    }
  }
  SDL_free(displays);
  const SDL_DisplayID d = other != 0 ? other : gameDisplay;
  float scale = d != 0 ? SDL_GetDisplayContentScale(d) : 1.f;
  scale = scale > 0.f ? scale : 1.f;
  SDL_Rect r{0, 0, 0, 0};
  if (d == 0 || !SDL_GetDisplayUsableBounds(d, &r) || r.w <= 0 || r.h <= 0) {
    *x = *y = SDL_WINDOWPOS_UNDEFINED;
    *w = static_cast<int>(480 * scale);
    *h = static_cast<int>(720 * scale);
    return;
  }
  *w = std::min(static_cast<int>(480 * scale), r.w);
  *h = std::min(static_cast<int>(760 * scale), r.h - static_cast<int>(40 * scale));
  if (other != 0) {
    *x = r.x + (r.w - *w) / 2;
    *y = r.y + (r.h - *h) / 2;
    return;
  }
  int gx = 0, gy = 0, gw = 0, gh = 0;
  if (game != nullptr && !(SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) && SDL_GetWindowPosition(game, &gx, &gy) &&
      SDL_GetWindowSize(game, &gw, &gh)) {
    const int gap = static_cast<int>(12 * scale);
    if (gx + gw + gap + *w <= r.x + r.w) {
      *x = gx + gw + gap;
      *y = std::max(r.y + static_cast<int>(30 * scale), gy);
      return;
    }
    if (gx - gap - *w >= r.x) {
      *x = gx - gap - *w;
      *y = std::max(r.y + static_cast<int>(30 * scale), gy);
      return;
    }
  }
  *x = *y = SDL_WINDOWPOS_UNDEFINED;
}

bool openWindow() {
  int x, y, w, h;
  placeWindow(&x, &y, &w, &h);
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, portTr("Paper Cut companion"));
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
  SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
  // clicks on it leave the game's window with the keyboard and the focus
  SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, false);
  sWindow = SDL_CreateWindowWithProperties(props);
  SDL_DestroyProperties(props);
  if (sWindow == nullptr) {
    std::printf("[companion] no window: %s\n", SDL_GetError());
    return false;
  }
  if (SDL_Surface* icon = SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_RGBA32, (void*)portIcon64, 64 * 4)) {
    SDL_SetWindowIcon(sWindow, icon);
    SDL_DestroySurface(icon);
  }
  sRenderer = SDL_CreateRenderer(sWindow, nullptr);
  if (sRenderer == nullptr) {
    std::printf("[companion] no renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(sWindow);
    sWindow = nullptr;
    return false;
  }
  SDL_SetRenderVSync(sRenderer, 0); // never holds the game's frame back
  ImGuiContext* prev = ImGui::GetCurrentContext();
  sCtx = ImGui::CreateContext();
  ImGui::SetCurrentContext(sCtx);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  ImGui_ImplSDL3_InitForSDLRenderer(sWindow, sRenderer);
  ImGui_ImplSDLRenderer3_Init(sRenderer);
  buildFonts();
  ImGui::SetCurrentContext(prev);
  sStyleScale = 0.f;
  sPrevScene = -1;
  portCompanionSetActive(1);
  std::printf("[companion] window open (%dx%d)\n", w, h);
  std::fflush(stdout);
  return true;
}

void closeWindow() {
  if (sWindow == nullptr) {
    return;
  }
  ImGuiContext* prev = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(sCtx);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(sCtx);
  ImGui::SetCurrentContext(prev != sCtx ? prev : nullptr);
  sCtx = nullptr;
  SDL_DestroyRenderer(sRenderer);
  SDL_DestroyWindow(sWindow);
  sRenderer = nullptr;
  sWindow = nullptr;
  portCompanionSetActive(0);
}

// --- Drawing ---

float sScale = 1.f;
ImVec2 sCardA;
float sCardW;

float px(float v) {
  return v * sScale;
}

// a paper card around what is drawn between cardBegin and cardEnd
void cardBegin() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->ChannelsSplit(2);
  dl->ChannelsSetCurrent(1);
  sCardA = ImGui::GetCursorScreenPos();
  sCardW = ImGui::GetContentRegionAvail().x - px(6);
  ImGui::Dummy(ImVec2(0, px(4)));
  ImGui::Indent(px(14));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + sCardW - px(28));
}

void cardEnd() {
  ImGui::PopTextWrapPos();
  ImGui::Unindent(px(14));
  ImGui::Dummy(ImVec2(0, px(4)));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->ChannelsSetCurrent(0);
  launcherPaperCard(dl, sCardA, ImVec2(sCardA.x + sCardW, ImGui::GetCursorScreenPos().y), sScale * 0.6f);
  dl->ChannelsMerge();
  ImGui::Dummy(ImVec2(0, px(8)));
}

float innerWidth() {
  return sCardW - px(28);
}

void heading(const char* text) {
  ImGui::PushFont(sHeading);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

// a labelled bar: "label  value/max" over a rounded gauge
void bar(const char* label, int value, int max, ImU32 color, float width) {
  char text[64];
  std::snprintf(text, sizeof(text), "%s %d/%d", label, value, max);
  ImGui::TextUnformatted(text);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float h = px(9);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, ImVec2(a.x + width, a.y + h), kTrack, h * 0.5f);
  const float f = max > 0 ? std::clamp(static_cast<float>(value) / static_cast<float>(max), 0.f, 1.f) : 0.f;
  if (f > 0.f) {
    dl->AddRectFilled(a, ImVec2(a.x + std::max(width * f, h), a.y + h), color, h * 0.5f);
  }
  ImGui::Dummy(ImVec2(width, h + px(2)));
}

// the Star Power gauge: one sparkle a star, filled by the power there is
void starPower(int sp, int maxSp) {
  const int stars = maxSp / 100;
  char text[64];
  std::snprintf(text, sizeof(text), "%s %d.%02d/%d", T("Star Power"), sp / 100, sp % 100, stars);
  ImGui::TextUnformatted(text);
  if (stars <= 0) {
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float r = px(11), step = px(26);
  for (int i = 0; i < stars; i++) {
    const ImVec2 c(a.x + r + step * i, a.y + r);
    const float fill = std::clamp((sp - i * 100) / 100.f, 0.f, 1.f);
    launcherSparkle(dl, c, r, IM_COL32(190, 156, 116, 255));
    if (fill > 0.f) {
      launcherSparkle(dl, c, r * (0.35f + 0.65f * fill), fill >= 1.f ? kGold : IM_COL32(246, 196, 69, 170));
    }
  }
  ImGui::Dummy(ImVec2(step * stars, r * 2 + px(2)));
}

void crystalStars(const PortCompanionSnapshot& s) {
  static const ImU32 kColors[7] = {IM_COL32(170, 220, 255, 255), IM_COL32(60, 190, 90, 255),
                                   IM_COL32(246, 196, 69, 255),  IM_COL32(220, 40, 60, 255),
                                   IM_COL32(50, 100, 230, 255),  IM_COL32(150, 50, 160, 255),
                                   IM_COL32(255, 170, 220, 255)};
  ImGui::TextUnformatted(T("Crystal Stars"));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float r = px(12), step = px(30);
  for (int i = 0; i < 7; i++) {
    const ImVec2 c(a.x + r + step * i, a.y + r);
    const bool found = (s.crystalStars >> i) & 1;
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    ImGui::PushID(i);
    ImGui::InvisibleButton("star", ImVec2(r * 2, r * 2));
    ImGui::PopID();
    if (found) {
      launcherSparkle(dl, c, r * 1.15f, IM_COL32(255, 255, 255, 255));
      launcherSparkle(dl, c, r, kColors[i]);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", noteText(s.crystalNames[i]));
      }
    } else {
      launcherSparkle(dl, c, r * 0.7f, IM_COL32(214, 186, 146, 255));
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + r * 2 + px(4)));
  ImGui::Dummy(ImVec2(step * 7, 0));
}

// a row of small value boxes (level, Star Points, coins ...)
void counters(const PortCompanionSnapshot& s) {
  // the coins, Star Pieces and Shine Sprites by the game's own names
  const struct {
    const char* label;
    int value;
  } kItems[] = {{T("Level"), s.level},
                {T("Star Points"), s.starPoints},
                {noteText(s.coinName), s.coins},
                {noteText(s.pieceName), s.starPieces},
                {noteText(s.spriteName), s.shineSprites}};
  const int cols = innerWidth() > px(400) ? 5 : 3;
  if (ImGui::BeginTable("counters", cols)) {
    for (const auto& it : kItems) {
      ImGui::TableNextColumn();
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kMuted));
      ImGui::TextWrapped("%s", it.label);
      ImGui::PopStyleColor();
      ImGui::PushFont(sHeading);
      ImGui::Text("%d", it.value);
      ImGui::PopFont();
    }
    ImGui::EndTable();
  }
}

const PortCompanionMember* partnerOut(const PortCompanionSnapshot& s) {
  for (int i = 1; i < s.memberCount; i++) {
    if (s.members[i].out == 1) {
      return &s.members[i];
    }
  }
  return nullptr;
}

void partyPanel(const PortCompanionSnapshot& s) {
  cardBegin();
  counters(s);
  ImGui::Spacing();
  const float w = innerWidth();
  const float half = (w - px(16)) * 0.5f;
  ImGui::BeginGroup();
  bar(T("FP"), s.fp, s.maxFp, kFp, half);
  ImGui::EndGroup();
  ImGui::SameLine(0, px(16));
  ImGui::BeginGroup();
  char bp[64];
  std::snprintf(bp, sizeof(bp), "%s %d/%d", T("BP left"), s.bp, s.maxBp);
  ImGui::TextUnformatted(bp);
  ImGui::EndGroup();
  starPower(s.sp, s.maxSp);
  crystalStars(s);
  cardEnd();

  // the party: Mario, then each partner; a tap switches to that partner
  cardBegin();
  heading(T("Party"));
  for (int i = 0; i < s.memberCount; i++) {
    const PortCompanionMember& m = s.members[i];
    const bool canSwitch = i > 0 && m.out == 0 && s.fieldReady;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float rowH = px(46);
    ImGui::PushID(i);
    ImGui::BeginDisabled(!canSwitch && i > 0 && m.out != 1);
    const bool pressed = ImGui::InvisibleButton("member", ImVec2(w, rowH)) && canSwitch;
    const bool hovered = ImGui::IsItemHovered() && canSwitch;
    ImGui::EndDisabled();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (m.out == 1 || hovered) {
      dl->AddRectFilled(a, ImVec2(a.x + w, a.y + rowH), m.out == 1 ? IM_COL32(246, 196, 69, 110) : IM_COL32(246, 196, 69, 60),
                        px(8));
    }
    const ImU32 ink = m.out == -1 ? kMuted : kInk;
    dl->AddText(sHeading, sHeading->FontSize * ImGui::GetIO().FontGlobalScale, ImVec2(a.x + px(8), a.y + px(4)), ink,
                noteText(m.name));
    for (int k = 0; k < m.rank; k++) {
      launcherSparkle(dl, ImVec2(a.x + px(14) + px(14) * k, a.y + px(35)), px(6), kGold);
    }
    const char* note = m.out == 1 ? T("With Mario") : m.out == -1 ? T("Away") : nullptr;
    if (note != nullptr) {
      dl->AddText(ImVec2(a.x + px(14) + px(14) * m.rank, a.y + px(27)), kMuted, note);
    }
    // the HP gauge on the right half
    const float gx = a.x + w * 0.45f, gw = w * 0.55f - px(8);
    char hp[32];
    std::snprintf(hp, sizeof(hp), "%s %d/%d", T("HP"), m.hp, m.maxHp);
    dl->AddText(ImVec2(gx, a.y + px(6)), ink, hp);
    const float gy = a.y + px(30), gh = px(9);
    dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), kTrack, gh * 0.5f);
    const float f = m.maxHp > 0 ? std::clamp(static_cast<float>(m.hp) / static_cast<float>(m.maxHp), 0.f, 1.f) : 0.f;
    if (f > 0.f) {
      dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + std::max(gw * f, gh), gy + gh), kHp, gh * 0.5f);
    }
    if (pressed) {
      portCompanionTap(PORT_COMPANION_TAP_PARTNER, m.id, 0);
    }
  }
  if (s.memberCount > 1) {
    ImGui::TextDisabled("%s", T("Tap a partner to switch."));
  }
  cardEnd();

  // the bag: a tap uses an item, as the Start menu would
  cardBegin();
  heading(T("Items"));
  if (s.itemCount == 0) {
    ImGui::TextDisabled("%s", T("The bag is empty."));
  }
  const PortCompanionMember* out = partnerOut(s);
  const float cell = (w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  for (int i = 0; i < s.itemCount; i++) {
    const PortCompanionItem& it = s.items[i];
    if (i % 2 == 1) {
      ImGui::SameLine();
    }
    ImGui::PushID(i);
    ImGui::BeginDisabled(!it.usable || !s.fieldReady);
    if (ImGui::Button(noteText(it.name), ImVec2(cell, 0))) {
      if (it.hp > 0 && out != nullptr) {
        sUseItem = i;
        ImGui::OpenPopup("use");
      } else {
        portCompanionTap(PORT_COMPANION_TAP_ITEM, i, 0);
      }
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("use")) {
      ImGui::TextUnformatted(T("Use on"));
      if (ImGui::Button(noteText(s.members[0].name), ImVec2(px(160), 0))) {
        portCompanionTap(PORT_COMPANION_TAP_ITEM, sUseItem, 0);
        ImGui::CloseCurrentPopup();
      }
      if (out != nullptr && ImGui::Button(noteText(out->name), ImVec2(px(160), 0))) {
        portCompanionTap(PORT_COMPANION_TAP_ITEM, sUseItem, 1);
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (s.itemCount > 0) {
    ImGui::TextDisabled("%s", s.fieldReady ? T("Tap an item to use it.") : T("Items can be used in the field."));
  }
  cardEnd();

  // the badges worn
  cardBegin();
  char title[96];
  std::snprintf(title, sizeof(title), "%s (%d)", T("Badges worn"), s.badgeTotal);
  heading(title);
  if (s.badgeCount == 0) {
    ImGui::TextDisabled("%s", T("No badges worn."));
  } else if (ImGui::BeginTable("badges", 2)) {
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("bp", ImGuiTableColumnFlags_WidthFixed, px(70));
    for (int i = 0; i < s.badgeCount; i++) {
      const PortCompanionBadge& b = s.badges[i];
      ImGui::TableNextColumn();
      if (b.count > 1) {
        ImGui::Text("%s x%d", noteText(b.name), b.count);
      } else {
        ImGui::TextUnformatted(noteText(b.name));
      }
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%d %s", b.bp * b.count, T("BP"));
    }
    ImGui::EndTable();
  }
  cardEnd();
}

void mapPanel(const PortCompanionSnapshot& s) {
  cardBegin();
  ImGui::PushFont(sBig);
  ImGui::TextUnformatted(noteText(s.area[0] != '\0' ? s.area : s.map));
  ImGui::PopFont();
  ImGui::TextDisabled("%s", s.map);
  ImGui::Spacing();
  heading(T("Where next"));
  ImGui::TextWrapped("%s", s.hint[0] != '\0' ? noteText(s.hint) : T("The story is over: go wherever you like."));
  cardEnd();

  cardBegin();
  heading(T("This room"));
  if (s.piecesTotal + s.spritesTotal == 0) {
    ImGui::TextDisabled("%s", T("No Star Pieces or Shine Sprites here."));
  } else {
    if (s.piecesTotal > 0) {
      ImGui::Text("%s: %d/%d", noteText(s.pieceName), s.pieces, s.piecesTotal);
    }
    if (s.spritesTotal > 0) {
      ImGui::Text("%s: %d/%d", noteText(s.spriteName), s.sprites, s.spritesTotal);
    }
  }
  // the room seen from above: Mario, the partner, the people and the enemies
  const float bw = s.boxMax[0] - s.boxMin[0], bh = s.boxMax[1] - s.boxMin[1];
  if (bw > 1.f && bh > 1.f && s.dotCount > 0) {
    ImGui::Spacing();
    const float w = innerWidth();
    const float h = std::clamp(w * bh / bw, px(80), px(320));
    const float k = std::min(w / bw, h / bh);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 o(a.x + (w - bw * k) * 0.5f, a.y + (h - bh * k) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + bw * k, o.y + bh * k), IM_COL32(232, 210, 172, 255), px(8));
    dl->AddRect(o, ImVec2(o.x + bw * k, o.y + bh * k), IM_COL32(190, 156, 116, 255), px(8), 0, px(1.5f));
    static const ImU32 kDot[4] = {IM_COL32(214, 44, 52, 255), IM_COL32(246, 196, 69, 255), IM_COL32(64, 140, 220, 255),
                                  IM_COL32(60, 40, 30, 255)};
    // the field's x across, z (towards the camera) down
    for (int i = 0; i < s.dotCount; i++) {
      const PortCompanionDot& d = s.dots[i];
      const ImVec2 c(o.x + (d.x - s.boxMin[0]) * k, o.y + (d.z - s.boxMin[1]) * k);
      const float r = d.kind == 0 ? px(7) : px(5);
      dl->AddCircleFilled(c, r + px(1.5f), IM_COL32(255, 255, 255, 255));
      dl->AddCircleFilled(c, r, kDot[d.kind & 3]);
    }
    ImGui::Dummy(ImVec2(w, h));
    // the legend
    const char* const kLegend[4] = {"Mario", "Partner", "People", "Enemies"};
    for (int i = 0; i < 4; i++) {
      if (i > 0) {
        ImGui::SameLine(0, px(14));
      }
      const ImVec2 c = ImGui::GetCursorScreenPos();
      dl->AddCircleFilled(ImVec2(c.x + px(5), c.y + ImGui::GetTextLineHeight() * 0.5f), px(5), kDot[i]);
      ImGui::Dummy(ImVec2(px(12), ImGui::GetTextLineHeight()));
      ImGui::SameLine(0, px(4));
      ImGui::TextUnformatted(i == 0 ? noteText(s.members[0].name) : T(kLegend[i]));
    }
  }
  cardEnd();
}

void battlePanel(const PortCompanionSnapshot& s) {
  if (s.scene != 2) {
    cardBegin();
    ImGui::TextDisabled("%s", T("No battle now."));
    cardEnd();
    return;
  }
  cardBegin();
  char turn[48];
  std::snprintf(turn, sizeof(turn), "%s %d", T("Turn"), s.turn);
  heading(turn);
  starPower(s.sp, s.maxSp);
  if (s.orderCount > 0) {
    ImGui::Spacing();
    ImGui::TextUnformatted(T("Turn order"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < s.orderCount; i++) {
      const ImVec2 a = ImGui::GetCursorScreenPos();
      if (i == s.orderActive) {
        dl->AddRectFilled(ImVec2(a.x - px(6), a.y), ImVec2(a.x + innerWidth(), a.y + ImGui::GetTextLineHeight()),
                          IM_COL32(246, 196, 69, 120), px(6));
      }
      ImGui::Text("%d. %s", i + 1, noteText(s.order[i]));
    }
  }
  cardEnd();
  for (int i = 0; i < s.enemyCount; i++) {
    const PortCompanionEnemy& e = s.enemies[i];
    cardBegin();
    if (e.acting) {
      const ImVec2 a = ImGui::GetCursorScreenPos();
      launcherSparkle(ImGui::GetWindowDrawList(), ImVec2(a.x + innerWidth() - px(10), a.y + px(10)), px(10), kGold);
    }
    heading(noteText(e.name));
    if (e.hpShown) {
      bar(T("HP"), e.hp, e.maxHp, kHp, innerWidth() * 0.6f);
    }
    if (e.known) {
      if (e.text[0] != '\0') { // the Tattle Log's text has the numbers
        ImGui::TextWrapped("%s", noteText(e.text));
      } else {
        ImGui::Text("%s %d", T("Defense"), e.defense);
      }
    } else {
      ImGui::TextDisabled("%s", T("Not tattled yet."));
    }
    cardEnd();
  }
}

void drawUi() {
  const PortCompanionSnapshot& s = sSnap;
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  bg->AddRectFilledMultiColor(ImVec2(0, 0), size, kNight, kNight, kNight2, kNight2);
  bg->AddRectFilled(ImVec2(0, 0), ImVec2(size.x, px(6)), kCurtain);
  bg->AddRectFilled(ImVec2(0, px(6)), ImVec2(size.x, px(9)), kGold);
  ImGui::SetNextWindowPos(ImVec2(0, px(9)));
  ImGui::SetNextWindowSize(ImVec2(size.x, size.y - px(9)));
  ImGui::Begin("companion", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  if (s.scene == 0) {
    cardBegin();
    heading(T("Paper Cut companion"));
    ImGui::TextWrapped("%s", T("The Start menu's information, the map and the battle show here while a game runs."));
    cardEnd();
    ImGui::End();
    return;
  }
  // a battle brings its tab forward; its end, the party's
  int select = s.scene != sPrevScene ? (s.scene == 2 ? 2 : 0) : -1;
  sPrevScene = s.scene;
  static const char* sTestTab = SDL_getenv("TTYD_COMPANION_TAB"); // tests: a tab shown (0-2), the window unclickable
  if (sTestTab != nullptr && (select >= 0 || s.scene == 1)) {
    select = std::atoi(sTestTab);
  }
  if (ImGui::BeginTabBar("tabs")) {
    const char* const kTabs[3] = {"Party", "Map", "Battle"};
    for (int t = 0; t < 3; t++) {
      if (ImGui::BeginTabItem(portTrLabel(kTabs[t]), nullptr, select == t ? ImGuiTabItemFlags_SetSelected : 0)) {
        noteText(portTr(kTabs[t]));
        ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        if (t == 0) {
          partyPanel(s);
        } else if (t == 1) {
          mapPanel(s);
        } else {
          battlePanel(s);
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void drawWindow() {
  ImGuiContext* prev = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(sCtx);
  if (sFontsDirty && SDL_GetTicks() - sLastFonts > 500) {
    buildFonts();
  }
  float scale = SDL_GetWindowDisplayScale(sWindow);
  scale = scale > 0.f ? scale : 1.f;
  if (scale != sStyleScale) {
    sStyleScale = scale;
    ImGui::GetStyle() = ImGuiStyle();
    launcherPaperStyle(ImGui::GetStyle(), scale * 0.8f);
    ImGui::GetStyle().Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(kPaper);
    ImGui::GetStyle().Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kInk);
    ImGui::GetStyle().Colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(IM_COL32(236, 214, 176, 255));
    ImGui::GetIO().FontGlobalScale = scale / kLoad;
  }
  sScale = scale;
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  drawUi();
  ImGui::Render();
  SDL_SetRenderDrawColor(sRenderer, 24, 28, 74, 255);
  SDL_RenderClear(sRenderer);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sRenderer);
  SDL_RenderPresent(sRenderer);
  ImGui::SetCurrentContext(prev);
}

#endif // COMPANION_WINDOW

const bool sAdded = [] {
  portCompanionSetting(); // declared at start, so the launcher's Settings tab has it
#ifdef COMPANION_WINDOW
  static const char* const kChoices[] = {"Off", "Window"};
  portSettingsAddChoice("Display", "Second screen",
                        "A window of its own with the Start menu's information live, the map and the battle; on "
                        "another monitor when there is one. A tap there uses an item or switches partners.",
                        0, "second_screen", kChoices, 2,
                        "companion dual screen monitor second window party items badges map battle tattle");
#elif defined(__ANDROID__)
  static const char* const kChoices[] = {"Off", "On"};
  portSettingsAddChoice("Display", "Second screen",
                        "On a device with a second display (a dual-screen handheld, a connected screen): the Start "
                        "menu's information live, the map and the battle there.",
                        0, "second_screen", kChoices, 2,
                        "companion dual screen display thor second party items badges map battle tattle");
#endif
  return true;
}();

} // namespace

extern "C" int portCompanionEvent(const void* sdlEvent) {
#ifdef COMPANION_WINDOW
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (sWindow == nullptr || SDL_GetWindowFromEvent(e) != sWindow) {
    return 0;
  }
  if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    sCloseAsked = true;
    return 1;
  }
  ImGuiContext* prev = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(sCtx);
  ImGui_ImplSDL3_ProcessEvent(e);
  ImGui::SetCurrentContext(prev);
  return 1;
#else
  (void)sdlEvent;
  return 0;
#endif
}

extern "C" void portCompanionDraw(void) {
#ifdef COMPANION_WINDOW
  if (sCloseAsked) {
    // closed with its own button: the setting goes off
    sCloseAsked = false;
    if (int* v = portSettingsFind("second_screen")) {
      *v = 0;
      portSettingsSave();
    }
  }
  if (portCompanionSetting() == 0 || aurora_get_window() == nullptr) {
    closeWindow();
    sFailed = false;
    return;
  }
  if (sWindow == nullptr) {
    if (sFailed) {
      return;
    }
    if (!openWindow()) {
      sFailed = true; // not again until the setting is turned off and on
      return;
    }
  }
  const Uint64 now = SDL_GetTicks();
  if (now - sLastDraw < 33 || (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MINIMIZED)) {
    return;
  }
  sLastDraw = now;
  portCompanionCopy(&sSnap);
  drawWindow();
#endif
}

extern "C" void portCompanionShutdown(void) {
#ifdef COMPANION_WINDOW
  closeWindow();
#endif
}
