#ifndef PORT_DIRECTORS_CUT_H
#define PORT_DIRECTORS_CUT_H
/* Director's Cut (Modes > Director's Cut; port/src/aurora/directors_cut_aurora.c):
 * content the game's code still has but never uses, put back, opt-in.
 * The setting ("directors_cut") is part of the settings online's host sends,
 * and everything here follows from it and the game's own state, so both
 * games of a session play the same. What is restored, and why the rest
 * isn't yet: docs/DIRECTORS_CUT.md. */
#ifdef __cplusplus
extern "C" {
#endif

/* on */
int portDirectorsCut(void);
/* npcdrv's group choice (npcSetupBattleInfo): a GSWF that picks a field
 * enemy's alternate groups, taken as set (gswf: the flag's number) */
int portDirectorsCutFlag(int gswf);
/* Dazzle (evt_badgeshop.c): the restored badges put in (or, off, taken out
 * of) his stock where the player has none; returns how many there are to add
 * to his menu's size (0 off) */
int portDirectorsCutDazzle(void);
/* one of the restored badges (the item table then named as the setting says):
 * Remix's badge draft offers them only while Director's Cut is on */
int portDirectorsCutBadge(int item);
/* msgSearch: the restored content's names and texts (NULL: not one of them) */
const char* portDirectorsCutMsg(const char* tag);

/* --- an area's cut battles, given by its REL (its battle database, under
 * TARGET_PC) from its _prolog --- */

/* One of the area's own group lists (an NpcBattleSelect array) and the one
 * with the cut groups added, used while Director's Cut is on. */
typedef struct PortDirectorsCutSelect {
    void* select;
    void* restored;
} PortDirectorsCutSelect;

/* A field enemy the area's data has the battle for but no map places: added
 * to the map's enemy setup (as the disc's setup file lists its others) while
 * Director's Cut is on. Strings as the game's (Shift-JIS from the REL). */
typedef struct PortDirectorsCutEnemy {
    const char* map;   /* "gon_04" */
    const char* name;  /* the NPC's name, unique in the map */
    const char* tribe; /* its tribe (npcGetTribe) */
    const char* ai;    /* its AI (the setup file's aitype) */
    float pos[3];      /* where it starts, the middle of its territory */
    float loiter[3];   /* its territory's size, wandering */
    float homing[3];   /* its territory's size, chasing */
    void* battle;      /* its NpcSetupBattleInfo */
} PortDirectorsCutEnemy;

/* rel: the REL's name; both lists end with a NULL first field (either may
 * be NULL). */
void portDirectorsCutRel(const char* rel, const PortDirectorsCutSelect* selects,
                         const PortDirectorsCutEnemy* enemies);
/* npcSetupBattleInfo: the list to choose from (the restored one, or select) */
void* portDirectorsCutSelect(void* select);
/* setupDataBase (src/database.c), before reading the map's setup file
 * (*data, *size): the cut enemies' entries added after the file's; after
 * the enemies are made: their battles given (the file names none). */
void portDirectorsCutSetup(const char* map, void** data, unsigned int* size);
void portDirectorsCutSetupDone(const char* map);

/* The level-up's confirmation box (battle_seq_end.c): on */
int portDirectorsCutLevelUpConfirm(void);

/* The sewers' three cut pipe rooms (tik_09, tik_10, tik_14): tik_06's init
 * script, after SET_VALUE(LW(0), PTR(bero_entry_data)): its exit-only pipe
 * (dokan_3) leads down into them while Director's Cut is on. */
struct EventEntry;
int portEvtDirectorsCutBero(struct EventEntry* event, int isFirstCall);
/* Once at start (modes_aurora.c): the hooks (the rooms' MapData). */
void portDirectorsCutInit(void);

/* The Journal's pages on each restored thing, for the settings page and the
 * Start menu (port/settings/features/directors_cut.cpp): count, then each
 * one's English title and text */
int portDirectorsCutPages(void);
const char* portDirectorsCutPageTitle(int i);
const char* portDirectorsCutPageText(int i);

#ifdef __cplusplus
}
#endif
#endif
