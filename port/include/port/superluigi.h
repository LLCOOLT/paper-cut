#ifndef PORT_SUPERLUIGI_H
#define PORT_SUPERLUIGI_H

/* Super Luigi (docs/SUPER_LUIGI.md): Luigi's story, played. Two ways in:
 *   from the inn   "Show me" after one of Luigi's tales at Rogueport's inn
 *                  (rels/gor/src/gor.c): Mario's game is kept as an
 *                  in-memory save and put back afterwards;
 *   the campaign   a game of its own from the Modes tab, saved in
 *                  <user>/superluigi_<game ID>.gci, never the player's card.
 * While a chapter runs: Play as Luigi is on, quick resume and the Gameplay+
 * retry slot are paused, Save Blocks save the campaign (or nothing, from the
 * inn), and Luigi's friends stand in Mario's partner slots.
 *
 * Files: superluigi_aurora.c (entering and leaving, the campaign, the cards,
 * the friends' slots and field points, the shared script functions),
 * superluigi_battle_aurora.c (stat overrides, target gates, forced commands,
 * prompts), sl_assets_aurora.c (our textures, and icons), and one
 * file per chapter (superluigi_ch01_aurora.c, ...) and per friend
 * (superluigi_blooey_aurora.c, ...). docs/SUPER_LUIGI.md, "The API". */

#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_SL_NONE, PORT_SL_INN, PORT_SL_CAMPAIGN };
#define PORT_SL_CHAPTERS 7

/* Super Luigi's flag bank: story switches (GSWF) the game never touches.
 * The game's GSWF table is GlobalWork.gswf[0x100], flags 0-8191, saved with
 * the file; the game's own end at 5666 (GSWF_AJI08_STAR_PIECE), and every
 * computed one stays below it (the Tattle Log 4474-4689, the trouble work
 * 5405-, badges and recipes in the low hundreds), so 7000-7799 is ours:
 * chapter N's 100 flags at 7000 + 100 N, the campaign's own at 7700. They
 * are plain GSWF: saved in the campaign's file, kept in the inn's snapshot
 * of Mario's game (and dropped with it), and usable wherever the game takes
 * a flag (a block's, a chest's, an item's).
 *   SL_FLAG(chapter, n)   the switch number, for swSet/swGet in C
 *   SL_GSWF(chapter, n)   the same as a script variable (IF_*, SET_VALUE, a
 *                         block's or chest's flag argument)
 *   SL_FLAG_CAMPAIGN(n)   the campaign's own (chapter 7)
 * n is checked at compile time (0-99), as is the chapter (0-7). */
#define SL_FLAG_BASE 7000
#define SL_FLAG_PER_CHAPTER 100
#define SL_FLAG_END (SL_FLAG_BASE + (PORT_SL_CHAPTERS + 1) * SL_FLAG_PER_CHAPTER) /* 7800 */
#define SL_FLAG_CHECK_(chapter, n)                                                                                    \
    (0 * sizeof(char[((n) >= 0 && (n) < SL_FLAG_PER_CHAPTER && (chapter) >= 0 && (chapter) <= PORT_SL_CHAPTERS) ? 1 \
                                                                                                                  : -1]))
#define SL_FLAG(chapter, n) \
    ((int)(SL_FLAG_BASE + SL_FLAG_PER_CHAPTER * (chapter) + (n) + SL_FLAG_CHECK_(chapter, n)))
#define SL_FLAG_CAMPAIGN(n) SL_FLAG(PORT_SL_CHAPTERS, n)
#define SL_GSWF(chapter, n) GSWF(SL_FLAG(chapter, n))

/* And a few story words (GSW bytes, 0-255) the same way: the game's own end
 * at 1474 of 2048; 2000-2002 are the framework's (the campaign marker, the
 * chapter, the friends who joined); chapter N's 4 at 2008 + 4 N (0-3), the
 * campaign's at 2036. SL_WORD for swByteSet/swByteGet, SL_GSW in a script. */
#define SL_WORD_BASE 2008
#define SL_WORD_PER_CHAPTER 4
#define SL_WORD_CHECK_(chapter, n)                                                                                       (0 * sizeof(char[((n) >= 0 && (n) < SL_WORD_PER_CHAPTER && (chapter) >= 0 && (chapter) <= PORT_SL_CHAPTERS) ? 1                                                                                                                   : -1]))
#define SL_WORD(chapter, n)     ((int)(SL_WORD_BASE + SL_WORD_PER_CHAPTER * (chapter) + (n) + SL_WORD_CHECK_(chapter, n)))
#define SL_GSW(chapter, n) GSW(SL_WORD(chapter, n))

/* PORT_SL_* while a chapter runs (weak 0 where Super Luigi isn't built) */
int portSuperLuigiMode(void);
int portSuperLuigiActive(void);
/* The chapter running (0-6), or -1 */
int portSuperLuigiChapter(void);
/* The chapter's title ("The Letter", ...) */
const char* portSuperLuigiChapterTitle(int chapter);

/* The Modes tab: the campaign (fresh: a new one; else continued from its
 * save) and one chapter on its own from Mario's game, as "Show me" does.
 * 0 and why when it can't start now. */
int portSuperLuigiStartCampaign(int fresh, const char** why);
int portSuperLuigiCampaignSaved(void);
/* A new campaign from chapter's card, with its own pouch and friends */
int portSuperLuigiStartCampaignAt(int chapter, const char** why);
int portSuperLuigiPlayChapter(int chapter, const char** why);
/* Leaves the chapter: Mario's game back (inn), or the title (campaign). */
void portSuperLuigiQuit(void);
/* Whether Luigi has told chapter's tale in this save (the inn's unlock) */
int portSuperLuigiChapterTold(int chapter);
int portSuperLuigiChapterBuilt(int chapter);

/* cardmgr.c's cardWrite: 1 when the save was taken (written to the
 * campaign's file, or dropped from the inn), the card untouched. */
int portSuperLuigiCardWrite(int slot);
/* database.c: a borrowed map whose own field enemies stay away */
int portSuperLuigiNoSetup(const char* map);
/* msgdrv.c: the chapters' own text isn't name-swapped (Luigi is Luigi) */
int portSuperLuigiOwnText(const char* tag);

/* Once at start (mods.cpp, beside the modes' maps) */
void portSuperLuigiRegister(void);

/* ======================================================================
 * Between the framework and the chapters
 * ====================================================================== */

typedef struct PortSlMap {
    const char* map;   /* the new map, in the borrowed map's area */
    void* init;        /* its init script */
    const char* files; /* the borrowed map */
    int noSetup;       /* 1: without the borrowed map's field enemies */
} PortSlMap;

/* A unit kind's numbers in one chapter, applied to its units as they enter
 * a battle while the chapter runs (the game's data is never changed). 0
 * keeps the unit's own (-1 for defense and level). */
typedef struct PortSlUnitStats {
    int kind;        /* BattleUnitType (UNIT_GOOMBA, ...); 0 ends the table */
    short hp;        /* max HP */
    short attack;    /* every damaging attack of the unit does this (before badges, guards...) */
    short attackAdd; /* or: added to each of its attacks (min 1), keeping their spread */
    short defense;   /* -1: own; else the normal defense of each part (immunities, 99, stay) */
    short level;     /* -1: own (Star Points: BtlUnit_GetExp) */
} PortSlUnitStats;

/* Luigi's friends */
enum {
    PORT_SL_FRIEND_NONE,
    PORT_SL_FRIEND_BLOOEY,
    PORT_SL_FRIEND_JERRY,
    PORT_SL_FRIEND_TORQUE,
    PORT_SL_FRIEND_HAYZEE,
    PORT_SL_FRIEND_SCREAMY,
    PORT_SL_FRIENDS
};
#define PORT_SL_BIT(friendId) (1 << (friendId))

typedef struct PortSlChapter {
    const char* title;
    const char* firstMap, *firstBero;
    const char* tale;   /* the inn tale's message (gor_luigi_talk_NNNN) */
    int friendId;       /* PORT_SL_FRIEND_* joining in it, or 0 */
    /* Luigi coming from the inn: level, HP, FP, BP, boots, hammer, coins */
    short level, hp, fp, bp, boots, hammer, coins;
    const short* items; /* 0-terminated */
    const short* badges;
    int party;                    /* PORT_SL_BIT(friend) | ...: with him coming in from the inn */
    const PortSlMap* maps;        /* its rooms, ended by {NULL} */
    const char* const* text;      /* tag, text, ..., NULL */
    const PortSlUnitStats* stats; /* its unit overrides, ended by {0} */
    void (*setup)(void);          /* once at start: its own hooks, texture sets, gates */
    const char* diary;            /* the ending's diary page (a tag), as Mario's chapters end */
} PortSlChapter;

/* One per chapter, in its own file: portSlChapter0() ... portSlChapter6(),
 * each returning its table. A chapter not built yet isn't defined (the
 * framework's weak ones return NULL). */
const PortSlChapter* portSlChapter0(void);
const PortSlChapter* portSlChapter1(void);
const PortSlChapter* portSlChapter2(void);
const PortSlChapter* portSlChapter3(void);
const PortSlChapter* portSlChapter4(void);
const PortSlChapter* portSlChapter5(void);
const PortSlChapter* portSlChapter6(void);
/* The chapter table: NULL where it isn't built */
const PortSlChapter* portSlGetChapter(int chapter);

/* ----------------------------------------------------------------------
 * A friend of Luigi's as a partner (one file each: superluigi_<name>_aurora.c)
 * ---------------------------------------------------------------------- */

struct PartyEntry;
struct BattleUnitKind;
struct BattleWeapon;

typedef struct PortSlFriend {
    int id;                  /* PORT_SL_FRIEND_* */
    int partyId;             /* the Mario partner slot he takes: 1 Goombella, 2 Koops, 3 Bobbery, 5 Flurrie, 6 Vivian */
    const char* name;        /* "Blooey": the slot's name texts while he's in it */
    const char* model;       /* his field and party-menu model */
    const char* menuText;    /* the party menu's line about him */
    char* const* fieldPoses; /* 15 pose names, partyPoseTbl's columns */
    void (*fieldUse)(struct PartyEntry*);  /* his field ability (portSlFieldUse does the game's part) */
    void (*fieldPost)(struct PartyEntry*); /* the slot's post function while he's in it (NULL: none) */
    const char* menuPose;    /* the party menu's pose ("S_1") */
    struct BattleUnitKind* (*battleUnit)(void); /* his battle unit, made when he's swapped in */
    struct BattleWeapon** menuMoves;            /* the party menu's move list (as winPartyDt's waza) */
    const char* iconMenu;    /* asset path of his face (PNG): the menus and battle */
    const char* iconHud;     /* the HUD's (NULL: iconMenu) */
    short hp, rank;          /* as he joins: max HP, rank (0 normal, 1 Super, 2 Ultra) */
    const char* const* text; /* his own text: tag, text, ..., NULL */
    void (*swapped)(int on); /* optional: after he's put in (1) or taken out of (0) the slot */
} PortSlFriend;

/* One per friend, in his own file, returning his table (weak NULL ones in the framework) */
const PortSlFriend* portSlFriendBlooey(void);
const PortSlFriend* portSlFriendJerry(void);
const PortSlFriend* portSlFriendTorque(void);
const PortSlFriend* portSlFriendHayzee(void);
const PortSlFriend* portSlFriendScreamy(void);
const PortSlFriend* portSlGetFriend(int friendId);
/* The friend in partner slot partyId now, or NULL */
const PortSlFriend* portSlFriendInSlot(int partyId);

/* ----------------------------------------------------------------------
 * Field abilities: points a room marks, near which an ability acts
 * ---------------------------------------------------------------------- */

enum {
    PORT_SL_ABILITY_SWIM,  /* Blooey: two shores */
    PORT_SL_ABILITY_BLAST, /* Jerry */
    PORT_SL_ABILITY_TUNE,  /* Torque */
    PORT_SL_ABILITY_SING,  /* Hayzee: value = the tune (0 any) */
    PORT_SL_ABILITY_FLOAT, /* Screamy: two edges */
    PORT_SL_ABILITY_LOOK,  /* Screamy: value = a map group to reveal */
    PORT_SL_ABILITIES
};
#define PORT_SL_POINTS 8 /* per ability, per room */

typedef struct PortSlPoint {
    int on;
    float x, y, z;    /* where it is */
    float x2, y2, z2; /* its other end (two shores, two edges), or as x, y, z */
    float radius;     /* how near the friend (or Luigi) must be */
    long long value;  /* the ability's own (a tune, a group's name) */
    void* script;     /* run when the ability acts on it (LW(0) its id), or NULL */
    int used;         /* set by portSlPointRun; cleared with the room */
} PortSlPoint;

/* The nearest point of ability within its radius of (x, z) (either end),
 * or NULL; *end (if not NULL) 0 near its first end, 1 near its second.
 * Points are cleared on every map change: a room's init sets its own. */
PortSlPoint* portSlPointNear(int ability, float x, float z, int* end);
PortSlPoint* portSlPointGet(int ability, int id);
/* Runs the point's script (LW(0) = its id) and marks it used; the event, or NULL */
void* portSlPointRun(int ability, PortSlPoint* point);

/* The game's Party Use for a friend: call from his fieldUse. The first
 * frame starts script (his ability's field event, which ends by calling
 * evt_sl_ability_done) with the field held; then it waits for that and
 * hands the partner back to the player, as Goombella's and the others' do. */
void portSlFieldUse(struct PartyEntry* party, void* script);

/* A chapter's texture set (its setup): every tex1_ PNG of folder (an asset
 * folder, "textures/ch1_lava/") applies while the map matches pattern
 * (port/include/port/mods.h, portModsAddMapTexture) */
void portSlMapTextures(const char* pattern, const char* folder);

/* ----------------------------------------------------------------------
 * Battles (superluigi_battle_aurora.c)
 * ---------------------------------------------------------------------- */

/* A part that only some moves reach, and only at some times: the statue's
 * gem. Registered once (a chapter's setup); applies to every battle. */
typedef struct PortSlTargetGate {
    const struct BattleUnitKind* kind;
    int part;                              /* the part's id (BattleUnitKindPart.index) */
    const struct BattleWeapon* weapons[4]; /* only these reach it (none listed: any move) */
    int openWork, openValue;               /* open while the unit's work[openWork] == openValue (-1: always) */
    int only;                              /* 1: those moves reach nothing else of that unit */
} PortSlTargetGate;
void portSlAddTargetGate(const PortSlTargetGate* gate);

/* The game's battle command types (its act menu), for portSlForceCommand */
enum {
    PORT_SL_ACT_JUMP = 0,
    PORT_SL_ACT_HAMMER = 1,
    PORT_SL_ACT_ITEM = 2,
    PORT_SL_ACT_SPECIAL = 4,
    PORT_SL_ACT_TACTICS = 5,
    PORT_SL_ACT_PARTNER = 6
};
/* The finishing blow: on actor's turns (0 Luigi, 1 the partner) only act is
 * offered, and in it only weapon (NULL: any of it), with prompt (a message
 * tag) shown over the battle the way the game shows its announcements.
 * act -1 clears; a battle's end clears both. */
void portSlForceCommand(int actor, int act, const struct BattleWeapon* weapon, const char* prompt);
/* A prompt (a message tag) over the battle or the field for ms milliseconds; NULL or 0 hides it */
void portSlPrompt(const char* tag, int ms);

/* ----------------------------------------------------------------------
 * Our own art (sl_assets_aurora.c): port/assets/super_luigi's textures/ and
 * art/'s PNGs, built into the executable
 * ---------------------------------------------------------------------- */

/* An asset by its path under port/assets/super_luigi ("art/titles/ch1_volcano.png"), or NULL */
const void* portSlAsset(const char* path, unsigned int* size);
/* The i-th asset under a folder ("textures/ch1_lava/"), or NULL past the last */
const char* portSlAssetIn(const char* folder, int i);
/* A PNG asset as a GX texture (a GXTexObj*; RGBA8, decoded once and kept): 1, or 0 */
int portSlAssetTexture(const char* path, void* texObj, int* width, int* height);
/* icondrv.c: icon iconId drawn from our own texture (a GXTexObj*) while a
 * friend's face stands in for a partner's: 1, or 0 for the game's own */
int portIconTexOverride(unsigned int iconId, void* texObj);
/* A friend's faces for a while (chapter 6's sooty Blooey: "art/icons/blooey/blooey_soot_menu.png", ..._hud.png); NULL: his own. Script: evt_sl_friend_icons(id, menu, hud) */
void portSlFriendIcons(int friendId, const char* menu, const char* hud);

/* --- the game's hooks into the framework (weak no-ops without it) --- */

struct BattleWorkWeaponTargets;
struct BattleWork;
/* battle_detect.c: a weapon's n targets, less what a target gate shuts; the new count */
int portBtlTargetFilter(struct BattleWorkWeaponTargets* d, int n);
/* battle_seq_command.c: the act menu just made / a weapon list's entries just checked */
void portBtlActFilter(struct BattleWork* wp);
void portBtlWeaponFilter(struct BattleWork* wp, int type);
/* rels/sys/src/sys.c: where the chapter card goes on to (*map, *bero) while
 * a chapter of Luigi's runs: 1, or 0 for the game's own */
int portSlTitleNext(const char** map, const char** bero);
/* sys.c: the card's cut-outs, drawn each frame it shows (the 3D effect camera) */

#ifdef __cplusplus
}
#endif

#endif
