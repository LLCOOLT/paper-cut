#ifndef PORT_SOUND_SHUFFLE_H
#define PORT_SOUND_SHUFFLE_H

/* The randomizer's music and sound shuffle, and the sound editor (docs/RANDOMIZER.md,
 * "Music and sound"). The game asks here for the music (psbgmlist) and the sound effect
 * (pssfxlist) it is about to play (pmario_sound.c) and plays what comes back:
 * - the shuffle, from the randomizer's seed: the area themes swap among themselves, the
 *   battle and boss themes among themselves (the rest, fanfares and scenes' music that a
 *   script may wait on, stays); each sound effect with another of its own family (the
 *   name's first two words: SFX_ENM_..., SFX_MARIO_...), ambient loops and menus left out;
 * - the sound editor's choices (<user>/sound_editor.json, the Randomizer tab), which win.
 * Sound only: nothing in the game's state changes, so it is each player's own online.
 * port/src/common/sound_shuffle_pc.c. */

#ifdef __cplusplus
extern "C" {
#endif

/* pmario_sound.c: the tables' sizes and names ("" for an empty entry) */
int portPsndBgmCount(void);
const char* portPsndBgmName(int i);
int portPsndSfxCount(void);
const char* portPsndSfxName(int i);

/* what plays instead: a psbgmlist index; a pssfxlist id (its index in the low 13 bits, kept flags above) */
int portSoundRemapBgm(int index);
int portSoundRemapSfx(int id);

/* the shuffle from a seed (0 off), music and sounds on or off; made again at once */
void portSoundShuffleSet(unsigned long long seed, int music, int sounds);
/* the editor's choices: from -> to by name ("" to: none, as the shuffle or the game has it) */
void portSoundEditorClear(void);
void portSoundEditorSet(int sfx, const char* from, const char* to);

#ifdef __cplusplus
}
#endif

#endif
