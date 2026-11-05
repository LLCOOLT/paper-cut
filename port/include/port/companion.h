#ifndef PORT_COMPANION_H
#define PORT_COMPANION_H

/* The second-screen companion (docs/SECOND_SCREEN_MENU.md): the Start menu's
 * information live, the map and the battle, on a second screen.
 *
 * Two layers. The game side (port/src/common/companion_pc.c) fills a plain
 * snapshot from the game's state a few times a second while a presenter
 * shows it, and keeps a small queue of taps. A presenter draws the snapshot
 * on its platform: a second window on the desktop
 * (port/settings/features/companion.cpp), a Presentation on an Android
 * device's second display (port/android, TtydCompanion.java).
 *
 * A tap never changes the game directly: portInputApply puts the next one in
 * controller 1's read (its analogA and analogB, which the game never reads),
 * and the game acts on it from there (portCompanionRoute, from makeKey), as it
 * would on a button. So it stays the same for online co-op, where both
 * games run from the same input. Plain C: no game or SDK types. */

#include "port/area_maps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_COMPANION_NAME 48
#define PORT_COMPANION_ITEMS 20
#define PORT_COMPANION_BADGES 48
#define PORT_COMPANION_ENEMIES 8
#define PORT_COMPANION_ORDER 12
#define PORT_COMPANION_DOTS 32
#define PORT_COMPANION_TEXT 600

typedef struct PortCompanionMember {
    int id;     /* 0 Mario, 1-7 the partners (1 Goombella, 2 Koops, 3 Bobbery, 4 Yoshi, 5 Flurrie, 6 Vivian, 7 Ms. Mowz) */
    int hp, maxHp;
    int rank;   /* a partner's level (0 normal, 1 super, 2 ultra) */
    int out;    /* the partner with Mario now */
    char name[PORT_COMPANION_NAME]; /* UTF-8, the game's own */
} PortCompanionMember;

typedef struct PortCompanionItem {
    int id;
    int hp, fp;   /* what it restores in the field menu (0 for none) */
    int usable;   /* the Start menu could use it now */
    char name[PORT_COMPANION_NAME];
} PortCompanionItem;

typedef struct PortCompanionBadge {
    int id, count, bp;
    char name[PORT_COMPANION_NAME];
} PortCompanionBadge;

typedef struct PortCompanionEnemy {
    int unitId;
    int known;       /* its kind has been tattled */
    int hpShown;     /* known, or Peekaboo worn */
    int hp, maxHp, defense;
    int acting;      /* its turn now */
    char name[PORT_COMPANION_NAME];
    char text[PORT_COMPANION_TEXT]; /* the Tattle Log's description */
} PortCompanionEnemy;

typedef struct PortCompanionDot {
    float x, z;
    int kind; /* 0 Mario, 1 the partner, 2 someone to talk to, 3 an enemy */
} PortCompanionDot;

typedef struct PortCompanionSnapshot {
    unsigned serial; /* counts the fills */
    int scene;       /* 0 no game (title, file select), 1 the field, 2 a battle */
    int fieldReady;  /* a tap can act now (the field, nothing in the way) */
    /* the Start menu */
    int level, starPoints, coins, starPieces, shineSprites;
    int fp, maxFp, bp, maxBp;
    int sp, maxSp;    /* Star Power, 100 a star */
    int crystalStars; /* bit n-1: the nth Crystal Star's power */
    char crystalNames[7][PORT_COMPANION_NAME];
    /* the game's names for a coin, a Star Piece and a Shine Sprite */
    char coinName[PORT_COMPANION_NAME], pieceName[PORT_COMPANION_NAME], spriteName[PORT_COMPANION_NAME];
    int memberCount;  /* Mario first, then the partners who have joined */
    PortCompanionMember members[8];
    int itemCount;
    PortCompanionItem items[PORT_COMPANION_ITEMS];
    int badgeCount, badgeTotal; /* the kinds worn (grouped), and every one worn */
    PortCompanionBadge badges[PORT_COMPANION_BADGES];
    /* the map */
    char area[PORT_COMPANION_NAME], map[16];
    char hint[160];   /* where the story goes next */
    int pieces, piecesTotal, sprites, spritesTotal; /* this room's (found, of) */
    float boxMin[2], boxMax[2]; /* the room's extent (x, z) */
    int dotCount;
    PortCompanionDot dots[PORT_COMPANION_DOTS];
    /* the area's map (port/include/port/area_maps.h; roomCount 0: none) */
    PortAreaView areaMap;
    /* the battle */
    int turn;
    int enemyCount;
    PortCompanionEnemy enemies[PORT_COMPANION_ENEMIES];
    int orderCount;   /* who acts this phase, in order */
    char order[PORT_COMPANION_ORDER][PORT_COMPANION_NAME];
    int orderActive;  /* the one acting (-1: none) */
} PortCompanionSnapshot;

/* The companion is on: on Android its setting ("second_screen", on by
 * default; shown only on a dual-screen device); elsewhere only for tests and
 * development, $TTYD_COMPANION_WINDOW=1 (a window of its own, no setting). */
int portCompanionSetting(void);
/* Android (TtydCompanion.java): the device has a second display of its own
 * (built in, not an external monitor or a cast screen); the setting shows. */
void portCompanionSetDualScreen(int dual);
int portCompanionDualScreen(void);
/* A presenter is showing the companion (the snapshot is filled meanwhile). */
void portCompanionSetActive(int active);
int portCompanionShowing(void);
/* Once per game frame (vi_aurora.c): the snapshot filled every few frames
 * while a presenter is showing it. */
void portCompanionFrame(void);
/* The snapshot as last filled, copied (any thread). */
void portCompanionCopy(PortCompanionSnapshot* out);
/* The snapshot as text, one "key value" per line (Android's presenter):
 * returns its length. */
int portCompanionText(char* out, int size);

/* Taps (any thread), queued until the game reads them as input. */
enum {
    PORT_COMPANION_TAP_ITEM = 1,    /* arg: the item's place in the bag; target 0 Mario, 1 the partner out */
    PORT_COMPANION_TAP_PARTNER = 2, /* arg: the partner to switch to (1-7) */
    PORT_COMPANION_TAP_ITEM_ON = 3, /* arg: the item's place in the bag; target: 0 Mario, 1-7 that partner */
    PORT_COMPANION_TAP_BADGE = 4,   /* arg: a badge's place in the badge list (0-199), worn or taken off */
    PORT_COMPANION_TAP_SORT = 5,    /* arg: pouchSortItem's mode (the second screen's Start menu sorted) */
};
void portCompanionTap(int kind, int arg, int target);

/* The input path (port/src/common/input_pc.c, portInputApply): the next tap
 * into controller 1's read. pad: PADStatus[4]. */
struct PADStatus;
void portCompanionApplyPad(struct PADStatus* pad);
/* makeKey (src/system.c), after the read: the tap in controller 1's input
 * this frame, if any, for the game to act on. */
void portCompanionRoute(void);
/* win_main.c, when the Start menu could open: the item tapped this frame
 * used, as the Start menu would; nonzero when one was. */
int portCompanionFieldItem(void);
/* win_main.c, there too: the badge tapped this frame worn or taken off, as
 * the badge list does, or the sort tapped made; nonzero when one was. */
int portCompanionFieldBadge(void);
/* gameplus_pc.c portQuickSwapPoll: the partner tapped this frame (0: none). */
int portCompanionPartner(void);

/* The desktop's presenter (port/settings/features/companion.cpp; nothing on
 * Android and iOS): every SDL event first, nonzero for one of its window's
 * (used there, for no one else); each presented frame (vi_aurora.c), the
 * window opened, drawn or closed as the setting says; and closed at exit. */
int portCompanionEvent(const void* sdlEvent);
void portCompanionDraw(void);
void portCompanionShutdown(void);

/* The game's text (msgSearch's, Latin-1 or the JP release's Shift-JIS) as
 * UTF-8 without its tags, on one line; with lines, a new page (<p>) starts a
 * new line. */
void portCompanionUtf8(const char* game, char* out, int size, int lines);

#ifdef __cplusplus
}
#endif

#endif
