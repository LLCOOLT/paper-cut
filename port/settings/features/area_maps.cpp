// The "Area map" tab (port/include/port/area_maps.h): a drawn map of an area,
// its rooms as paper cards on the area's grid (from the exits' directions),
// the ways between them, the rooms this file has been in, the exits not taken
// yet and the Star Pieces and Shine Sprites still to find. portAreaMapDraw is
// also the second screen's (companion.cpp).
#include "port/area_maps.h"
#include "port/practice.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kTab = "Area map";

struct AreaName {
  const char* area;
  const char* name;
};
// the areas' names where the game's Journal has none (English)
const AreaName kNames[] = {
    {"gor", "Rogueport"},        {"tik", "Rogueport Sewers"}, {"hei", "Petal Meadows"},
    {"nok", "Petalburg"},        {"gon", "Hooktail Castle"},  {"win", "Boggly Woods"},
    {"mri", "The Great Tree"},   {"tou", "Glitzville"},       {"usu", "Twilight Town"},
    {"gra", "Twilight Trail"},   {"jin", "Creepy Steeple"},   {"muj", "Keelhaul Key"},
    {"dou", "Pirate's Grotto"},  {"hom", "Riverside Station"}, {"eki", "Riverside Station"},
    {"rsh", "Excess Express"},   {"pik", "Poshley Heights"},  {"bom", "Fahr Outpost"},
    {"moo", "The Moon"},         {"aji", "X-Naut Fortress"},  {"las", "Palace of Shadow"},
};

const char* areaName(const char* area) {
  if (const char* game = portAreaName(area)) {
    return game; // the game's own, in its language
  }
  for (const AreaName& n : kNames) {
    if (std::strncmp(n.area, area, 3) == 0) {
      return portTr(n.name);
    }
  }
  return area;
}

// the exits' directions as steps on the grid (1 north, clockwise)
const float kDir[9][2] = {{1, 0}, {0, -1}, {0.7f, -0.7f}, {1, 0}, {0.7f, 0.7f}, {0, 1}, {-0.7f, 0.7f}, {-1, 0}, {-0.7f, -0.7f}};

void dashed(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float thick, float dash) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1.f) {
    return;
  }
  for (float t = 0.f; t < len; t += dash * 2.f) {
    const float e = std::min(t + dash, len);
    dl->AddLine(ImVec2(a.x + dx * t / len, a.y + dy * t / len), ImVec2(a.x + dx * e / len, a.y + dy * e / len), c,
                thick);
  }
}

// a Star Piece (a small diamond) and a Shine Sprite (a small sun)
void pieceIcon(ImDrawList* dl, ImVec2 c, float r) {
  dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.7f, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.7f, c.y),
                    IM_COL32(246, 196, 69, 255));
  dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.7f, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.7f, c.y),
              IM_COL32(150, 100, 20, 255), 1.f);
}

void spriteIcon(ImDrawList* dl, ImVec2 c, float r) {
  for (int k = 0; k < 8; k++) {
    const float t = k * 3.14159265f / 4.f;
    dl->AddLine(ImVec2(c.x + std::cos(t) * r * 0.5f, c.y + std::sin(t) * r * 0.5f),
                ImVec2(c.x + std::cos(t) * r, c.y + std::sin(t) * r), IM_COL32(240, 150, 30, 255), 1.5f);
  }
  dl->AddCircleFilled(c, r * 0.55f, IM_COL32(255, 200, 60, 255));
}

int sArea = -1; // the page's chosen area (-1: where Mario is)

void section(int inGame) {
  static PortAreaView sView; // on the page's thread only
  const char* file = portAreaMapsFile();
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Load a save file to see its maps."));
    return;
  }
  if (file[0] != '\0') {
    int total = 0;
    const int seen = portAreaMapsVisited(&total);
    ImGui::Text(portTr("%s: %d of %d rooms visited"), file, seen, total);
  }
  const char* here = portAreaHere();
  const char* current = sArea >= 0 ? portAreaAt(sArea) : here;
  char preview[96];
  std::snprintf(preview, sizeof(preview), "%s %s", current[0] != '\0' ? areaName(current) : portTr("Where Mario is"),
                sArea < 0 ? portTr("(here)") : "");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.f);
  if (ImGui::BeginCombo(portTrLabel("Area##areamap"), preview)) {
    if (ImGui::Selectable(portTr("Where Mario is"), sArea < 0)) {
      sArea = -1;
    }
    for (int i = 0; i < portAreaCount(); i++) {
      ImGui::PushID(i);
      if (ImGui::Selectable(areaName(portAreaAt(i)), sArea == i)) {
        sArea = i;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  if (!portAreaMapBuild(current, &sView)) {
    ImGui::TextDisabled("%s", portTr("Mario isn't in an area with a map (a mode's room, a cutscene)."));
    return;
  }
  ImGui::Text(portTr("%d of %d rooms visited"), sView.visited, sView.roomCount);
  if (sView.pieces + sView.sprites > 0) {
    ImGui::SameLine(0, ImGui::GetFontSize());
    ImGui::Text(portTr("Still to find: %d Star Pieces, %d Shine Sprites"), sView.pieces, sView.sprites);
  }
  const float w = ImGui::GetContentRegionAvail().x;
  portAreaMapDraw(sView, w, ImGui::GetFontSize() * 26.f, ImGui::GetFontSize() / 16.f);
}

} // namespace

void portAreaMapDraw(const PortAreaView& v, float width, float maxHeight, float unit) {
  if (v.roomCount == 0 || width < 40.f) {
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float margin = 34.f * unit;
  const float cell = std::max(24.f * unit, std::min((width - margin * 2.f) / std::max(1, v.width),
                                                    (maxHeight - margin * 2.f) / std::max(1, v.height)));
  const float card = std::min(cell * 0.62f, 70.f * unit);
  const float mapW = cell * v.width + margin * 2.f, mapH = cell * v.height + margin * 2.f;
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 o(at.x + std::max(0.f, (width - mapW) * 0.5f), at.y);
  auto centre = [&](const PortAreaViewRoom& r) {
    return ImVec2(o.x + margin + (r.x + 0.5f) * cell, o.y + margin + (r.y + 0.5f) * cell);
  };
  // the paper under it
  dl->AddRectFilled(o, ImVec2(o.x + mapW, o.y + mapH), IM_COL32(236, 222, 190, 255), 10.f * unit);
  dl->AddRect(o, ImVec2(o.x + mapW, o.y + mapH), IM_COL32(170, 140, 100, 255), 10.f * unit, 0, 1.5f * unit);
  const ImU32 kWalked = IM_COL32(120, 84, 50, 255), kNotYet = IM_COL32(150, 130, 110, 200);
  // the ways: walked ones drawn in ink, the rest dashed
  for (int i = 0; i < v.linkCount; i++) {
    const PortAreaViewLink& l = v.links[i];
    const ImVec2 a = centre(v.rooms[l.a]);
    ImVec2 b;
    if (l.b >= 0) {
      b = centre(v.rooms[l.b]);
    } else {
      const float* d = kDir[l.dir >= 0 && l.dir <= 8 ? l.dir : 0];
      b = ImVec2(a.x + d[0] * cell * 0.62f, a.y + d[1] * cell * 0.62f);
    }
    if (l.taken) {
      dl->AddLine(a, b, kWalked, 3.f * unit);
    } else {
      dashed(dl, a, b, kNotYet, 2.f * unit, 5.f * unit);
    }
    if (l.b < 0) {
      // out of the area: an arrowhead and where it goes
      const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(1.f, std::sqrt(dx * dx + dy * dy));
      const float ux = dx / len, uy = dy / len, s = 6.f * unit;
      dl->AddTriangleFilled(ImVec2(b.x + ux * s, b.y + uy * s), ImVec2(b.x - uy * s, b.y + ux * s),
                            ImVec2(b.x + uy * s, b.y - ux * s), l.taken ? kWalked : kNotYet);
      const char* name = areaName(l.to);
      const ImVec2 ts = ImGui::CalcTextSize(name);
      const float k = 0.8f;
      dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * k,
                  ImVec2(b.x + ux * s * 2.f - ts.x * k * (ux < -0.3f ? 1.f : ux > 0.3f ? 0.f : 0.5f),
                         b.y + uy * s * 2.f - ts.y * k * (uy < -0.3f ? 1.f : uy > 0.3f ? 0.f : 0.5f)),
                  IM_COL32(110, 80, 50, 255), name);
    }
  }
  // the rooms: paper cards (visited), outlines (not yet), Mario's in red
  for (int i = 0; i < v.roomCount; i++) {
    const PortAreaViewRoom& r = v.rooms[i];
    const ImVec2 c = centre(r);
    const ImVec2 a(c.x - card * 0.5f, c.y - card * 0.36f), b(c.x + card * 0.5f, c.y + card * 0.36f);
    const float round = 4.f * unit;
    if (r.visited) {
      dl->AddRectFilled(ImVec2(a.x + 2.f * unit, a.y + 3.f * unit), ImVec2(b.x + 2.f * unit, b.y + 3.f * unit),
                        IM_COL32(90, 60, 30, 60), round);
      dl->AddRectFilled(a, b, IM_COL32(255, 252, 240, 255), round);
      dl->AddRect(a, b, r.here ? IM_COL32(214, 44, 52, 255) : IM_COL32(120, 84, 50, 255), round, 0,
                  (r.here ? 3.f : 1.5f) * unit);
    } else {
      dl->AddRectFilled(a, b, IM_COL32(220, 205, 175, 255), round);
      dl->AddRect(a, b, kNotYet, round, 0, 1.f * unit);
    }
    // the room's number (gor_01: "01"; a room not visited yet: "?")
    const char* label = r.visited ? r.map + 4 : "?";
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f - (r.pieces + r.sprites > 0 ? card * 0.1f : 0.f)),
                r.visited ? IM_COL32(60, 40, 30, 255) : IM_COL32(130, 110, 90, 255), label);
    // what's still to find in it
    float x = c.x - (r.pieces + r.sprites - 1) * 5.f * unit;
    for (int k = 0; k < std::min(r.pieces + r.sprites, 6); k++) {
      const ImVec2 p(x + k * 10.f * unit, b.y - 7.f * unit);
      if (k < r.sprites) {
        spriteIcon(dl, p, 5.f * unit);
      } else {
        pieceIcon(dl, p, 5.f * unit);
      }
    }
    if (r.here) {
      dl->AddCircleFilled(ImVec2(a.x, a.y), 6.f * unit, IM_COL32(255, 255, 255, 255));
      dl->AddCircleFilled(ImVec2(a.x, a.y), 4.5f * unit, IM_COL32(214, 44, 52, 255));
    }
    // a hover's details
    if (ImGui::IsMouseHoveringRect(a, b)) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(r.map);
      ImGui::TextUnformatted(r.visited ? portTr("Visited") : portTr("Not visited yet"));
      if (r.waysUntaken > 0) {
        ImGui::Text(portTr("Exits not taken: %d"), r.waysUntaken);
      }
      if (r.pieces > 0) {
        ImGui::Text(portTr("Star Pieces to find: %d"), r.pieces);
      }
      if (r.sprites > 0) {
        ImGui::Text(portTr("Shine Sprites to find: %d"), r.sprites);
      }
      ImGui::EndTooltip();
    }
  }
  ImGui::Dummy(ImVec2(width, mapH));
  // the legend
  const float lh = ImGui::GetTextLineHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddLine(ImVec2(p.x, p.y + lh * 0.5f), ImVec2(p.x + 20.f * unit, p.y + lh * 0.5f), kWalked, 3.f * unit);
  ImGui::Dummy(ImVec2(24.f * unit, lh));
  ImGui::SameLine();
  ImGui::TextUnformatted(portTr("Walked"));
  ImGui::SameLine(0, 14.f * unit);
  p = ImGui::GetCursorScreenPos();
  dashed(dl, ImVec2(p.x, p.y + lh * 0.5f), ImVec2(p.x + 20.f * unit, p.y + lh * 0.5f), kNotYet, 2.f * unit, 5.f * unit);
  ImGui::Dummy(ImVec2(24.f * unit, lh));
  ImGui::SameLine();
  ImGui::TextUnformatted(portTr("Not taken yet"));
  ImGui::SameLine(0, 14.f * unit);
  p = ImGui::GetCursorScreenPos();
  pieceIcon(dl, ImVec2(p.x + 6.f * unit, p.y + lh * 0.5f), 5.f * unit);
  ImGui::Dummy(ImVec2(14.f * unit, lh));
  ImGui::SameLine();
  ImGui::TextUnformatted(portTr("Star Piece"));
  ImGui::SameLine(0, 14.f * unit);
  p = ImGui::GetCursorScreenPos();
  spriteIcon(dl, ImVec2(p.x + 6.f * unit, p.y + lh * 0.5f), 5.f * unit);
  ImGui::Dummy(ImVec2(14.f * unit, lh));
  ImGui::SameLine();
  ImGui::TextUnformatted(portTr("Shine Sprite"));
}

namespace {

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Area map", nullptr, PORT_SETTING_IN_GAME, section,
                           "area map rooms visited exits star pieces shine sprites tracker uncollected explore");
  }
} sRegister;

} // namespace
