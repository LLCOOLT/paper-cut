#ifndef PORT_COMPANION_MENU_H
#define PORT_COMPANION_MENU_H

/* The second screen's Start menu (docs/SECOND_SCREEN_MENU.md): the game's
 * own pause menu (src/window/win_*.c), run a second time on a WindowWork of
 * its own while the game plays on, drawn into a picture for the second
 * screen's presenter (port/settings/features/companion.cpp, Android's
 * TtydCompanion.java).
 *
 * It must leave the game exactly as it found it (online co-op compares two
 * games run from the same input, and only one player has a second screen).
 * Each of its frames (port/src/aurora/companion_menu_aurora.c) runs between
 * two of the game's, with the flag below set:
 * - the game's data and bss are copied before and put back after, but for
 *   the menu's own statics, which are swapped for the second screen's copy,
 *   and the decoders' (port/companion_section.h);
 * - its allocations, files, effects and animation poses are its own, in host
 *   memory (the hooks below, from memory.c, filemgr.c, effdrv.c, animdrv.c);
 * - its sounds do nothing (pmario_sound.c), its random numbers are put back;
 * - what would change the game (an item used, a partner chosen) becomes a
 *   tap of the second screen (port/companion.h), which the game acts on as
 *   input; the rest (equipping badges, sorting, mail) is left out.
 * Plain C. */

#ifdef __cplusplus
extern "C" {
#endif

/* Set while the second screen's menu runs (its frame); 0 otherwise. */
extern int gPortCompanionMenu;

/* --- the hooks (port/src/common/companion_menu_pc.c), while the flag is set --- */
struct smartEntry;
struct FileEntry;
struct EffectEntry;
/* __memAlloc / __memFree / _mapAlloc (zeroed host memory) */
void* portCompanionAlloc(unsigned int size);
void portCompanionFree(void* p);
/* smartAlloc / smartFree; type 3 (this frame's) is let go at its end */
struct smartEntry* portCompanionSmartAlloc(unsigned int size, unsigned char type);
void portCompanionSmartFree(struct smartEntry* entry);
/* fileAlloc / fileAsync (loaded at once, from the disc, kept for the run) / fileFree */
struct FileEntry* portCompanionFileAlloc(const char* name, int type);
void portCompanionFileFree(struct FileEntry* entry);
/* effEntry / effDelete: entries of its own (its effects never reach effMain) */
struct EffectEntry* portCompanionEffEntry(void);
void portCompanionEffDelete(struct EffectEntry* entry);
/* The end of its frame: this frame's blocks let go. */
void portCompanionFrameEnd(void);
/* portMenuPoint (menu_tap_pc.c) during its frame: the second screen's tap,
 * in the menu's units (608 x 480 from the centre, y up); 1 a tap, 0 none. */
int portCompanionMenuPoint(float* gx, float* gy);
void portCompanionMenuSetPoint(int kind, float gx, float gy); /* its frame: this frame's */
/* keyGet* (system.c) during its frame: 0 button, 1 its new presses, 2 its
 * repeats, 3 the stick's direction, 4 new, 5 repeats, 6 stick x, 7 stick y,
 * 8 the C stick (none). */
unsigned portCompanionMenuKey(int which);
void portCompanionMenuSetKeys(const unsigned* keys); /* its frame: this frame's, 8 */

/* --- the frame (port/src/aurora/companion_menu_aurora.c) --- */
/* After the game's frame is out (vi_aurora.c, the next one begun): the
 * second screen's menu's frame, when it shows and the game lets it. */
void portCompanionMenuFrame(void);
/* The picture as last drawn (any thread): RGBA, PORT_COMPANION_MENU_W x
 * PORT_COMPANION_MENU_H; returns its serial (0: none yet), the pixels copied
 * only when it differs from *seen (then *seen updated). */
#define PORT_COMPANION_MENU_W 640
#define PORT_COMPANION_MENU_H 480
unsigned portCompanionMenuPicture(unsigned char* rgba, unsigned* seen);
/* The menu can be shown (a game runs, readback works); 0: the presenter shows
 * its own panels instead. */
int portCompanionMenuReady(void);
/* Input from the presenter (any thread): a tap at (nx, ny), 0-1 across the
 * picture; a button. */
void portCompanionMenuTap(float nx, float ny);
enum {
    PORT_COMPANION_BUTTON_A = 1,
    PORT_COMPANION_BUTTON_B,
    PORT_COMPANION_BUTTON_UP,
    PORT_COMPANION_BUTTON_DOWN,
    PORT_COMPANION_BUTTON_LEFT,
    PORT_COMPANION_BUTTON_RIGHT,
    PORT_COMPANION_BUTTON_L, /* a page back */
    PORT_COMPANION_BUTTON_R, /* a page on */
    PORT_COMPANION_BUTTON_X, /* a partner's moves */
    PORT_COMPANION_BUTTON_Y, /* the Journal's hint */
};
void portCompanionMenuButton(int button);
/* Tests: the next picture written as NAME.png in $TTYD_SHOT_DIR (Aurora). */
void portCompanionMenuShot(const char* name);

#ifdef __cplusplus
}
#endif

#endif
