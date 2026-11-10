#ifndef PORT_UI_SHOT_H
#define PORT_UI_SHOT_H

/* Tests: captures with the Dear ImGui layer (port/settings/launcher_shot.cpp). With $TTYD_SHOT_UI=1
 * (testpass's "ui_shots"), each named capture (portShotRequest) also writes <name>_ui.png to
 * $TTYD_SHOT_DIR: the picture as shown with the full settings page, the map editor's panel and the
 * other ImGui windows over it, drawn again on the CPU. The game's own capture leaves ImGui out. */

#ifdef __cplusplus
extern "C" {
#endif

/* $TTYD_SHOT_UI is set */
int portUiShotOn(void);
/* <name>_ui.png, from the next frames */
void portUiShotRequest(const char* name);
/* each frame, after it was drawn (aurora_end_frame): the captures under way */
void portUiShotFrame(void);
/* a <name>_ui.png asked for and not yet written (a test's end, portShotPending, waits for it too) */
int portUiShotPending(void);

#ifdef __cplusplus
}
#endif

#endif
