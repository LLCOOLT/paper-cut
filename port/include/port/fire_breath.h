#ifndef PORT_FIRE_BREATH_H
#define PORT_FIRE_BREATH_H

/* Bowser's fire breath while he is the Play as pick (port/src/common/fire_breath_pc.c,
 * docs/PLAY_AS.md "Fire breath"):
 *
 * - In the field his hammer swing is a breath of fire: his disc's KPA_A_1A..D
 *   and the game's own flame (gonbaba_breath, the type his own stages use) in
 *   front of him, with his roar and the flame's sound. Draw and sound only:
 *   the swing's motion, reach, timing and hits are the hammer's as they were
 *   (blocks, switches, trees, first strikes, every script's hammer check), so
 *   no puzzle changes. The spin hammer stays his spin.
 * - The first strike that swing gives is his fire breath on the enemy struck
 *   (fire, the hammer's power, and it burns).
 * - In battle, "Fire Breath" in his Hammer list: an FP move on every ground
 *   enemy, fire (the element rules: no damage to the fire-immune, more to the
 *   fire-weak), the hammer's hold-left Action Command, the hammer's power by
 *   rank, and a chance to burn.
 * - His battle hammer swings (the plain Hammer, Power and Mega Smash, Ice
 *   Smash, Piercing Blow, Head Rattle, the Super and Ultra Hammer's whirl)
 *   are drawn as a breath of fire too: the wind-up his inhale, the swing his
 *   lunge and a short jet at the one struck, with his roar and the flame's
 *   sound. Draw and sound only: the moves' scripts, damage, rules and Action
 *   Commands are the hammer's as they were.
 * - Hammer Throw: he spits a fireball (his flame, the hammer rank's colours)
 *   that flies the thrown hammer's arc; the Quake Hammers and Fire Drive: a
 *   stomp (a leap and his hip drop onto the ground as the hammer would strike
 *   it). Draw and sound only, as above.
 *
 * Off unless Bowser is the pick (never in versus). Everything that changes
 * play reads the game's own state and input, so online and replays play it
 * the same everywhere (the host's pick is everyone's). */

#ifdef __cplusplus
extern "C" {
#endif

struct BattleCommand;
struct BattleWeapon;

/* Bowser is the pick, so his breath is on */
int portFireBreathOn(void);

/* --- the field (src/motion/mot_hammer.c) --- */
/* the swing began (in place of the hammer's whoosh) */
void portFireBreathFieldStart(void);
/* each frame of the swing, t = its timer (0..20): the flame and its sound */
void portFireBreathFieldFrame(int t);
/* the flame's sound stops (the swing ended or was cut short, a battle begins) */
void portFireBreathFieldStop(void);

/* --- the stand-in (port/src/common/luigi_pc.c) --- */
/* Bowser's own animation for this place (PORT_LUIGI_FIELD or _BATTLE) now,
 * instead of the one for Mario's pose `mario` (his animation's name); NULL:
 * none. *frame: the frame to show (< 0: it runs on from its start) */
const char* portFireBreathPose(int where, const char* mario, float* frame);

/* --- battle --- */
/* Fire Breath is in his Hammer list now, so the list opens even with the
 * wooden hammer alone (battle_seq.c: no single-hammer shortcut) */
int portFireBreathInHammerList(void);
/* the Hammer list (battle_seq_command.c): Fire Breath after the hammers */
void portFireBreathAddWeapon(struct BattleCommand* cmd, int* num);
/* the first strike of a field hammer hit (battle_seq.c, type 4): his
 * breath's, or NULL for Mario's hammer */
struct BattleWeapon* portFireBreathFirstAttack(void);
/* a battle begins (battle.c): nothing of the field's breath goes on */
void portFireBreathBattleStart(void);
/* where his body is drawn during a hammer move he breathes on, along x from
 * where his unit is (0 otherwise): his Fire Breath's distance from the one
 * struck (80) instead of the hammer's (35, the whirl's 30), eased in over the
 * walk up and out over the walk back. Draw only (luigi_pc.c, his shadow in
 * battle_disp.c, the swing's flame): the unit's place, so the hits, targets,
 * Action Commands and timing, stay the hammer's. */
float portFireBreathStandOff(void);
/* how high his body is drawn over his unit (0 otherwise): his stomp in the hammer's ground moves (Quake
 * Hammer, Mega Quake, Fire Drive): a leap as Mario winds up (KPA_J_1A), up there while the command runs
 * (KPA_J_1B), and down in his hip drop (KPA_A_2D) as the hammer would strike the ground. Draw only
 * (luigi_pc.c, his shadow in battle_disp.c): the unit, the quake, its hits and timing stay the badge's. */
float portFireBreathLift(void);
/* Hammer Throw's thrown hammer (Mario's part 3, battle_disp.c via luigi_pc.c) each frame: whether it
 * shows, and where its middle is. While Bowser is the pick it is drawn as a fireball he spat (his flame,
 * the hammer rank's colours) along the hammer's own flight; the hammer itself is not drawn (luigi_pc.c).
 * Draw only: the part's flight, hit and timing are the badge's. */
void portFireBreathThrown(int shown, float x, float y, float z);
/* a sound to leave out (luigi_pc.c portLuigiSfx): the hammer's thud on the
 * ground while his battle swing is a breath (his flame's sound plays) */
int portFireBreathMuteSfx(int id);
/* the move's name and help (msgSearch) */
const char* portFireBreathMsg(const char* tag);

#ifdef __cplusplus
}
#endif

#endif
