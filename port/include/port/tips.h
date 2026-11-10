#ifndef PORT_TIPS_H
#define PORT_TIPS_H
/* First-time tips (port/src/aurora/tips_aurora.c): a line or two at the bottom of the picture in the
 * game's font, as the first start's settings notice (settings_hint_aurora.c) is, each shown once per
 * install (its own key in settings.txt, tip_<name>, as settings_hint_shown is). Settings > General >
 * Tips (and the Start menu's General page) has "Show tips again", which clears them all.
 *
 * A tip asked for waits for a quiet moment: no other tip, the settings notice or the what's new window
 * up. It isn't shown online (each player's screen would differ; nothing here is the game's state
 * either way: the font's random numbers are put back), in a replay, or in tests that warp to a map
 * ($TTYD_MAP) unless $TTYD_TIPS=1 ($TTYD_TIPS=0: never). The headless build has none. */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_TIP_LENT,       /* a lent partner came out for a player: what it is */
    PORT_TIP_AUDIENCE,   /* a player's first seat in the audience: the emotes and the throws */
    PORT_TIP_ROGUEPORT,  /* the file menu offers Start in Rogueport (its window says what it gives) */
    PORT_TIP_SCENE_SKIP, /* Skip any scene's first skippable scene (its ring: scene_skip.cpp draws it) */
    PORT_TIP_NOTES,      /* the first paper note drawn in a room (notes_aurora.c): whose they are, where they go off */
    PORT_TIP_COUNT
};

/* Asks for tip (PORT_TIP_*) now: 1 when it will show (the first time on this install; marked shown), else
 * 0. The scene skip's tip is the ring's: 1 marks it, and the ring draws it (portTipsAllowed: whether it may). */
int portTipOffer(int tip);
/* tips may show now (not online, not in a replay, the tests' rule) */
int portTipsAllowed(void);
/* tip was shown on this install */
int portTipSeen(int tip);
/* "Show tips again": every tip (and the first start's settings notice) unshown */
void portTipsReset(void);
/* each game frame (makeKey, src/system.c): the tip up, drawn */
void portTipsFrame(void);
/* a tip is on the screen now */
int portTipShowing(void);

#ifdef __cplusplus
}
#endif

#endif
