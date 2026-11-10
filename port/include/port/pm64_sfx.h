#ifndef PORT_PM64_SFX_H
#define PORT_PM64_SFX_H

/* Paper Mario 64's sound effects (docs/PM64_LINK.md; port/src/common/pm64_sfx_pc.c): with the player's own
 * PM64 ROM linked and the setting "pm64_sfx" on (the default), the PM64 partners, the PM64 enemies, the Star
 * Spirits and the Star Haven corner sound as they do in Paper Mario; with "pm64_sfx_general" on (off by
 * default), Mario's jump and hammer, the pick-ups and the menus too. A player of our own for the N64 game's
 * sound effect scripts (the SEF file in the ROM's SBN audio archive, its BK instrument banks) renders them at
 * 32 kHz on top of the game's mix.
 *
 * How a game sound becomes PM64's: pmario_sound.c asks portPm64SfxArm for each sound it queues on an effect
 * slot. A table of our own (TTYD sound name + whose it is: a PM64 partner on its host, the partner out or
 * acting, a PM64 enemy in the battle) picks the PM64 sound. Where it has one, the game's own sound still runs
 * on its slot (its timing, fades, 3D volume and pan, stops) but silently (PORT_MIX_SILENT), and the PM64 sound
 * follows that slot's volume and pan. Where it has none, the game's sound stays. No ROM, or the setting off:
 * nothing changes. Only the sound changes: online and replays play the same. */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Calling PM64's sounds by name (for the PM64 partners', enemies' and bosses' scripts) ----
 *
 * 1. In any of the game's sound calls (evt_snd_sfxon, btlevtcmd_snd_se, psndSFXOn, psndSFXOn_3D, an NPC's
 *    step/jump/land sound...), name the PM64 sound and the TTYD stand-in together:
 *        CALL(evt_snd_sfxon, STRING("SOUND_BOW_VANISH|SFX_AUDIENCE_TELESA_INVISIBLE1"), 0)
 *    With a ROM linked and "pm64_sfx" on, PM64's SOUND_BOW_VANISH plays, with the stand-in's timing, 3D
 *    placement, volume, fades and stop (it runs silently underneath). Otherwise the stand-in plays, as before.
 *    The names are the PM64 decomp's (include/enums.h, enum SoundIDs), listed in port/include/port/pm64_sound_ids.h;
 *    SOUND_LOOP_X plays SOUND_LRAW_X, which loops until the game's sound is stopped (psndSFXOff).
 * 2. From C, with no stand-in (centred, volume 0-127): portPm64SfxPlayName("SOUND_BOW_SPOOK", 100), or
 *    portPm64SfxPlay(PM64_SOUND_BOW_SPOOK, 100) with port/pm64_sound_ids.h's constants. */

/* name's PM64 sound id ("SOUND_BOW_SMACK", "PM64_SOUND_BOW_SMACK" or "BOW_SMACK"), 0 if the decomp has none */
unsigned portPm64SfxId(const char* name);
/* id's name without "SOUND_", or NULL */
const char* portPm64SfxName(unsigned id);
/* plays name's PM64 sound centred at volume (0-127); 1 if it plays (ROM linked, "pm64_sfx" on, name known) */
int portPm64SfxPlayName(const char* name, int volume);

/* "pm64_sfx" is on and a ROM is linked. */
int portPm64SfxActive(void);
/* "pm64_sfx_general" (general actions: jump, hammer, pick-ups, menus) is on and a ROM is linked. */
int portPm64SfxGeneralActive(void);
/* pmario_sound.c's searchPSSFXList: name "SOUND_X|SFX_Y" gives back "SFX_Y" (the stand-in to look up) and X's id in
 * *pm64 (0 for any other name); then portPm64SfxNamed with the list index found, which the next armed slot of
 * that index plays as X. */
const char* portPm64SfxSplit(const char* name, unsigned* pm64);
void portPm64SfxNamed(int index, unsigned pm64);
/* Game thread, pmario_sound.c. A sound named name (list index) was queued on effect slot (0..39) with serial: arms
 * the slot with its PM64 sound, if a "SOUND_X|" name or the table has one now. */
void portPm64SfxArm(int slot, unsigned serial, const char* name, int index);
/* As the slot's sound starts: the category to start it in (cat, or PORT_MIX_SILENT when armed). */
int portPm64SfxCategory(int slot, unsigned serial, int cat);
/* Each frame, for each live slot after the game set its volume (0-127) and pan (0-127, 64 centre). */
void portPm64SfxSlot(int slot, unsigned serial, int volume, int pan);
/* The slot's sound was stopped (psndSFXOff): its PM64 sound stops too. */
void portPm64SfxOff(int slot);
/* Once a frame, after the slots: PM64 sounds whose slot ended are let finish (looping ones stopped). */
void portPm64SfxFrame(void);
/* A PM64 sound (the decomp's SOUND_ ids, raw: no looping or alternating ids) played on its own, centred, at
 * volume (0-127): the corner's partners, the Star Spirits. */
void portPm64SfxPlay(unsigned soundId, int volume);
/* The Star Haven corner: partner p (1..7) greets Mario with a sound of its own as he talks to it. */
void portPm64SfxGreet(int pm64Partner);
/* A Star Spirit appears over a Special move. */
void portPm64SfxStarSpirit(void);
/* Audio thread: adds the PM64 sounds to frames stereo 32 kHz frames. */
void portPm64SfxMix(short* out, unsigned frames);

/* Tests and tools ($TTYD_PM64_SFX_TEST): the table's PM64 sound for name under partner p (1..7) out and
 * acting, or 0. */
unsigned portPm64SfxLookup(const char* name, int pm64Partner);

#ifdef __cplusplus
}
#endif

#endif
