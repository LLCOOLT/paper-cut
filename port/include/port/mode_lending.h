#ifndef PORT_MODE_LENDING_H
#define PORT_MODE_LENDING_H

/* What the modes hold that isn't the file's own (port/src/aurora/modes_aurora.c): the one check every path
 * that keeps or shares the game asks, so none can miss a mode again. The boss rush, the endless Glitz Pit and
 * the refights, the raid, the dojo, the battle puzzles, versus, and any of the modes' own maps (the Paper
 * Tower's floors, the trophy room, the exhibition rings), from the frame a mode starts to the frame Mario is
 * back where he was.
 *
 * Asked by quick resume and autosave (no snapshot), the game's own save (cardmgr.c: a Save Block saves
 * nothing), the achievements (nothing earned from a lent loadout; the modes' own counters still count) and
 * online state sync (a lent pouch isn't anyone's progress). A header of its own: the common sources that ask
 * can't include port/modes.h (its PORT_LOADOUT_COUNT is gameplus.h's macro there). Aurora builds only (the
 * modes are); elsewhere nothing is lent. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_LEND_POUCH = 1,    /* Mario's pouch is a loadout the mode lent (or the mode puts the file's back after) */
    PORT_LEND_MAP = 2,      /* a mode runs, or Mario is on one of the modes' maps or not yet back in his spot */
    PORT_LEND_WORDS = 4,    /* the story's words are the file's only once the mode gives them back (versus) */
    PORT_LEND_SETTINGS = 8, /* the battles are scaled by the mode, not the player's settings (the endless Pit) */
};

/* PORT_LEND_* of what is lent now; 0: the game is the file's own. */
int portModeLending(void);

#ifdef __cplusplus
}
#endif

#endif
