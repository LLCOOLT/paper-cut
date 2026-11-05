// The launcher's widgets in the game's menu style (launcher_look.cpp), for every launcher page (Play,
// Online, Settings, About, and pages added later such as the mod manager's): the paper window, the
// torn strip, the item list's rows with the pointing glove, the torn-paper buttons, the Start
// menu's coloured tabs, the page turn between pages, and a disc's region tag.
//
// Two looks draw them, the same sizes and behaviour in each:
// - paper (the default): from shapes and Paper Cut's own art (launcher_brand.cpp), no game assets;
// - game (a trial: $TTYD_LAUNCHER_STYLE=game, or the setting launcher_style=1): the game's own menu
//   art (the glove, the woven paper window, the torn strip and label), made from the player's disc
//   by art/recipes/launcher_ui.recipe into the art cache. Without that art (no disc in the list
//   yet) it draws the paper look.
#pragma once

#include <imgui.h>

enum class LauncherLook { Paper, Game };
// The look this frame.
LauncherLook launcherLook();
// The game look was asked for (whether or not its art is there).
bool launcherLookGameWanted();
// The game look's art from the art cache of a disc's version (<user>/art_cache/<gameId>/), made
// there from the disc image at discPath first if it isn't yet (discPath nullptr: only read). Called
// at start and when a disc is added; gameId nullptr: no art (the paper look).
void launcherLookArt(const char* userPath, const char* gameId, const char* discPath);

// A texture for Dear ImGui from RGBA8 (kept for the launcher's test captures too).
ImTextureID launcherTexture(int w, int h, const void* rgba);

// ---- frames and paper ----
// The Start menu's paper window from a to b (the cards behind every page).
void launcherPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale);
// The same in the paper look whatever the look (launcherPaperCard: the in-game settings page, the second screen).
void launcherPanelPaper(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale);
// A torn paper strip from a to b (headings, the selected row, notes), tinted.
void launcherStrip(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale, ImU32 tint = IM_COL32_WHITE);
// A heading on its strip, the width of the text (the page's sections).
void launcherHeading(const char* text, float scale);

// ---- items ----
enum class LauncherButton { Normal, Primary };
// A torn-paper label button (size 0: from the label). The glove points at it when hovered or
// focused. Inside BeginDisabled it is drawn faded and can't be pressed.
bool launcherButton(const char* label, ImVec2 size, float scale, LauncherButton kind = LauncherButton::Normal);
// One row of an item list, full width, height h: the selected row on a strip, the glove at the
// hovered or focused one (else the selected). Returns true when clicked or confirmed; draw the
// row's content after it on the same line (SameLine).
bool launcherRow(const char* id, bool selected, float h, float scale);
// The same look for a row of one's own (a Selectable drawn with its Header colours clear): call right
// after it. room: the glove's at its left (0: the glove points from outside the row).
void launcherRowDecorate(bool selected, float scale, float room);
// The glove points at this item now (min, max: its rectangle); fallback: only when nothing hovered
// or focused points elsewhere (a list's chosen row).
void launcherPoint(ImVec2 min, ImVec2 max, bool fallback = false);
// Once a frame, last: the glove, gliding to what it points at, its sounds.
void launcherCursorFrame(float time, float scale);

// ---- tabs and pages ----
// The Start menu's tab colours: Mario's red, Party's pink, Gear's orange, Badges' green, Journal's blue.
enum class LauncherTabColour { Red, Pink, Orange, Green, Blue };
struct LauncherTab {
  const char* label; // shown as is (translate it first)
  LauncherTabColour colour;
};
// The tabs in a row from the cursor, the current one raised; returns the one chosen (clicked,
// or moved to with the keyboard or a controller), or current.
int launcherTabs(const LauncherTab* tabs, int count, int current, float scale);
// The page shown this frame (any number that names it): when it changes, the page turns (about a
// third of a second) over the panel a..b. Call after drawing the page.
void launcherPageTurn(int page, ImVec2 a, ImVec2 b, float scale, float time);

// ---- discs ----
// A disc's region tag ("JP", "US", "EU") at `at`, h tall; returns its width.
float launcherRegionTag(ImDrawList* dl, ImVec2 at, const char* region, float h);

// ---- sounds ----
enum class LauncherSound { Move, Select, Back, Turn };
// A short paper sound (made here, not the game's), unless the launcher's sounds are off.
void launcherSound(LauncherSound s);
// The launcher's sound stream closed (the game's own sound starts after it).
void launcherSoundEnd();
