#ifndef PORT_OVERLAY_PLACE_H
#define PORT_OVERLAY_PLACE_H

/* Where the port's own drawings over the game (ImGui: a toast, a notice) may sit at the top of the screen:
 * never over the HUD's rows (the game's status bar, the partner's row, Brothers mode's Luigi row under them;
 * 2 to 3 rows playing together), which reach as far down as portHudStatusRowsBottom says (port/hud.h,
 * statuswindow.c). */

#ifdef __cplusplus
extern "C" {
#endif

float portHudStatusRowsBottom(void);
void portViewEditorPicture(float w, float h, float rect[4]); /* port/view.h: the picture's place in the window */

#ifdef __cplusplus
}

/* A top-of-screen overlay's top (the window's pixels, display_w x display_h), moved down under the HUD's rows
 * when they reach it, with gap pixels between; as it was when no row is on the screen. */
inline float portOverlayTopClear(float want, float display_w, float display_h, float gap) {
  const float share = portHudStatusRowsBottom();
  if (share <= 0.f) {
    return want;
  }
  float picture[4];
  portViewEditorPicture(display_w, display_h, picture);
  const float rows = picture[1] + share * picture[3] + gap;
  return want < rows ? rows : want;
}

/* The same for a box drawn with portModHostDrawRect/Text (port/mod_host.h: the game's 640 x 480 over the
 * picture), a mode's at the top of the screen (the boss rush's clock, the Pit's round, the raid's bar, the
 * tower's floor, a chapter's or a ghost race's time): how far down to move a box whose top is top so it starts
 * gap units under the HUD's rows (the battle's status bar, the field's while it is out; the bar's own slide
 * moves it along), 0 when no row reaches it. Every box of a stack takes the same drop, so they keep their order. */
inline float portOverlayDrop480(float top, float gap) {
  const float share = portHudStatusRowsBottom();
  if (share <= 0.f) {
    return 0.f;
  }
  const float rows = share * 480.f + gap;
  return rows > top ? rows - top : 0.f;
}
#endif

#endif
