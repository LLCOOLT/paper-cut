#ifndef PORT_BROTHERS_H
#define PORT_BROTHERS_H

/* Brothers mode (setting "brothers_mode", off by default; docs/BROTHERS.md):
 * Luigi joins Mario as a second hero, played by the second controller.
 *
 * In the field (port/src/common/brothers_pc.c) Luigi is a walker of the
 * game's own party code in a party slot of the port's own (party.c's
 * partyPtrTbl[PORT_PARTY_SLOT_LUIGI], port/include/port/players.h), so he walks, falls and lands with the partners' collision;
 * no script, save or menu ever sees that slot. His model is Play as Luigi's
 * (c_luigi_pc, with his hammer). The second player's stick walks him, A jumps
 * (a hit from below works a block), B swings his hammer (blocks, switches);
 * left alone he follows Mario's steps, hopping where Mario jumped, and he is
 * brought back to Mario's side when he falls, gets stuck or is left far
 * behind. He steps out of sight for pipes, paper forms, Yoshi rides and the
 * like, and comes back beside Mario after them and after every room change.
 * The camera keeps both brothers in view.
 *
 * In battle (port/src/aurora/brothers_aurora.c) Luigi is a second Mario unit
 * on Mario's side, as a versus fight's Luigi is on the other side: Mario's
 * moves, badges and items (the brothers share the pouch, the badges and Star
 * Power), his own HP and FP (as high as Mario's; kept between battles, healed
 * in the field with Mario), his own turn with the second controller, his own
 * guards. The battle is lost when both brothers are down. While Luigi acts,
 * the game's "Mario" is Luigi.
 *
 * Everything follows from the settings online already sends and from the
 * second controller's input, so online lockstep stays deterministic.
 * Off, every hook returns at once and the game is as it was. */

#ifdef __cplusplus
extern "C" {
#endif

struct PartyEntry;

/* The mode is on and Luigi can be there: the setting, Mario himself (not
 * Peach or Bowser), no versus fight. */
int portBrothersOn(void);
/* The setting alone (Play as Luigi stays out of Brothers mode: luigi_pc.c). */
int portBrothersSetting(void);

/* --- the field (party.c, party_move.c, mario_cam.c) --- */

/* partyMain, once a field frame before the partners move: Luigi made, put
 * back or taken away as the mode and the scene say; the second player's
 * buttons read. */
void portBrothersFieldFrame(void);
/* partyWalkMain: Luigi's walk for this frame (party is his entry): 1 done
 * (the game's footmark walk is not run for him), 0 for any other entry. */
int portBrothersWalk(struct PartyEntry* party);
/* marioUpdateCam, after it set the camera's target from Mario: moved toward
 * Luigi (eased, at most half-way) so both stay in view. */
void portBrothersCamera(float* x, float* z);
/* party is Luigi's entry */
int portBrothersIsLuigi(const struct PartyEntry* party);
/* Luigi's entry, or NULL (the co-op tag over him) */
struct PartyEntry* portBrothersFieldLuigi(void);
/* the second player is walking him now (else he follows Mario) */
int portBrothersDriven(void);
/* seq_load.c, a file started or loaded (after its party is cleared): Luigi
 * comes in again with the next field frame, his HP and FP full. */
void portBrothersReset(void);

/* --- battle --- */

/* unit (a BattleUnit*) is Luigi's unit in this battle */
int portBrothersLuigiUnit(const void* unit);
/* brothers_aurora.c: the unit made for Luigi in this battle (-1 none) */
void portBrothersSetLuigiUnit(int unitId);
/* Luigi has the turn now (choosing, aiming, his action commands) */
int portBrothersLuigiActs(void);
/* Luigi's HP and FP between battles (max: Mario's) */
void portBrothersGetStats(int* hp, int* fp);
void portBrothersSetStats(int hp, int fp);

/* second_player_pc.c's makeKey hook, while the mode is on: the second
 * controller read as the first for Luigi's turn in battle. 1 while Luigi
 * has the turn (co-op's routing stays out of it); 0 else (co-op's routing:
 * players 3 and 4's turns with their companions' partners). */
int portBrothersRoute(int secondPad);
/* second_player_pc.c's makeKey hook, each frame before any routing (versus
 * fights too): duo moves' Bros. moves (port/include/port/duo.h: the other
 * hero, Luigi's controller), and who plays the halves: two players with the
 * second controller there, else one (A Mario, B Luigi); duo's own rule with
 * the mode off. */
void portBrothersDuoFrame(int secondPad);
/* The defender's guard pad record (a BattleWorkPad*) for a hit on unit:
 * Luigi's guards are the second controller's; *handled is 1 for Luigi's
 * unit while the mode is on (else co-op's rule: the partner's guards are
 * player 1's, or player 3's or 4's with their companion's partner). */
void* portBrothersGuardPad(const void* unit, int secondPad, int* handled);

/* mods.cpp: the battle's hooks (Luigi's unit, the game's "Mario" while he
 * acts, his HUD), registered once. */
void portBrothersRegister(void);

#ifdef __cplusplus
}
#endif

#endif
