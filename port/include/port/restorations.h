#ifndef PORT_RESTORATIONS_H
#define PORT_RESTORATIONS_H

/* Restorations (Enhancements > Restorations; local survey and docs:
 * docs/DELOCALIZATION.md for the text side): what the Japanese release had
 * and the US and European releases changed, put back on US and EU, each its
 * own setting, off by default. Nothing of the game's is shipped: each one is
 * the US/EU disc's own data switched back on, the decomp's Japanese script
 * path, or (the chalk outline) read from the player's own Japanese disc.
 *
 * - "rs_chalk_outline": the chalk outline, the marks on a Rogueport house's
 *   floor (room gor_01, house 4) under the flies the US kept: four of the
 *   five marks and a strip the US map dropped. Only the Japanese disc has
 *   them (the textures are on every disc): its m/gor_01/d is
 *   read once from the player's own JP disc in the launcher's library,
 *   cached in the user folder (restorations/G8MJ01/), and used in place of
 *   this disc's from the next start (port/settings/features/restorations.cpp).
 *   Without a JP disc, this disc's own room is used with them redrawn from
 *   this disc (chalk_outline.cpp): the outline traced from Mario's own
 *   picture (an art recipe, restorations.recipe), the marks of the room's own
 *   stain texture where the Japanese map puts them. The setting's section
 *   says which is in use.
 * - "rs_bunny_boo": the Pianta Parlor's Boo (and her turn in the credits) in
 *   her bunny ears, the Japanese model c_bteresa (still on the US/EU disc)
 *   instead of the cat-eared c_bteresa2.
 * - "rs_jp_flashes": the Japanese release's screen flashes the US cut down
 *   (the Great Tree's time bomb, Twilight Town's and Keelhaul Key's darkness,
 *   the Shadow Queen's rooms). A photosensitivity warning goes with it.
 *   Less flashing ("less_flashing") wins over it: with that on, these scenes
 *   take the US release's fewer flashes on every build, JP's own included.
 * - "rs_xnaut_crowd": the prologue's army of X-Nauts at its Japanese size,
 *   1000 (the US made it 400), and its three puffs of smoke a round.
 * - Listed in the group, kept where they are (postgame_extra.h): the
 *   Japanese bad ending ("pg_jp_bad_ending"), Hooktail's frogs
 *   ("pg_jp_hooktail_frogs") and the Trial Stew recipe ("pg_jp_recipes").
 *
 * Visual only (none changes play), so online and replays keep them as each
 * player chose. On the JP build the game is already this way: every call
 * answers "on", but for the flashes with Less flashing on. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_RESTORE_CHALK_OUTLINE = 0,
    PORT_RESTORE_BUNNY_BOO = 1,
    PORT_RESTORE_JP_FLASHES = 2,
    PORT_RESTORE_XNAUT_CROWD = 3,
    PORT_RESTORE_COUNT
};

/* The restoration's setting is on (always on the JP build). */
int portRestoreOn(int id);
/* Its settings key ("rs_..."). */
const char* portRestoreKey(int id);

/* The scripts' calls (USER_FUNCs):
 *   portEvtRestore(id, out): out = 1 when on, else 0.
 *   portEvtRestorePick(id, us, jp, out): out = jp when on, else us.
 *   portEvtNannpcCount(out): the crowd's size as evt_nannpc_init made it
 *     (gor_00.c's loops count to it, whatever the setting is now). */
struct EventEntry;
int portEvtRestore(struct EventEntry* event, int isFirstCall);
int portEvtRestorePick(struct EventEntry* event, int isFirstCall);
int portEvtNannpcCount(struct EventEntry* event, int isFirstCall);

/* end_00.c: a credits silhouette's model and animation, the Japanese Boo's
 * when "rs_bunny_boo" is on. */
void portRestoreCreditNpc(const char** model, const char** anim);
/* Each frame of the game (restorations_aurora.c): the Parlor Boo's tribe
 * entry follows "rs_bunny_boo" (a new room uses it). */
void portRestorationsApply(void);

/* disc_aurora.c, once this build's disc is open (as portTextLanguageLoad):
 * the chalk outline's room from the player's Japanese disc, if chosen
 * (port/settings/features/restorations.cpp). 0 if the disc at path couldn't be
 * opened again after reading the other (as portTextLanguageLoad), else 1. */
int portRestorationsDiscOpened(const char* path);

/* Each frame of the game (restorations_aurora.c): Language > Game text
 * packs' template, when asked for, written from the disc
 * (port/settings/features/message_dump.cpp). */
void portMessageDumpFrame(void);

/* gra_00.c: the answer to Doopliss's name puzzle, a text pack's
 * ("pc_doopliss_name" in its messages) when it has one: out = that text, or
 * the game's answer (in). */
int portEvtNameAnswer(struct EventEntry* event, int isFirstCall);

#ifdef __cplusplus
}
#endif

#endif
