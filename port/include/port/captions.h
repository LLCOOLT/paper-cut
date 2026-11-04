#ifndef PORT_CAPTIONS_H
#define PORT_CAPTIONS_H

/* Captions for sound cues (Accessibility > Captions for sounds;
 * port/settings/features/accessibility.cpp): a short line low on the screen
 * for the sounds that tell the player something (an enemy noticing Mario, a
 * hidden block, a Star Piece, a guard, a door or switch out of sight), for
 * players who can't hear them. They only show: the game is left as it is.
 * Not while the second screen's menu runs (port/companion_menu.h). */

#ifdef __cplusplus
extern "C" {
#endif

/* pmario_sound.c (__psndSFXOn): a sound effect starts, by its pssfxlist name
 * ("SFX_..."), at pos (x, y, z in the world; NULL: no position). The game's
 * own cues that have no sound of their own come the same way, by a name of
 * the port's ("PORT_HIDDEN_BLOCK", "PORT_GUARD", "PORT_SUPERGUARD"). */
void portCaptionSfx(const char* name, const float* pos);

/* where a cue was heard, as port/src/aurora/captions_aurora.c tells the page */
enum {
    PORT_CAPTION_NO_POS = 0,     /* no position (a cue of the whole scene) */
    PORT_CAPTION_ON_SCREEN = 1,
    PORT_CAPTION_OFF_LEFT = 2,
    PORT_CAPTION_OFF_RIGHT = 3,
    PORT_CAPTION_OFF_SCREEN = 4, /* out of sight, above, below or behind */
};
/* accessibility.cpp: the caption table's entry for name (-1: none, or
 * captions off) and whether it is shown only out of sight; then the cue */
int portCaptionLookup(const char* name, int* offScreenOnly);
void portCaptionPush(int entry, int where);

#ifdef __cplusplus
}
#endif

#endif
