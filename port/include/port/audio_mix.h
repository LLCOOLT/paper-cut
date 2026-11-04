#ifndef PORT_AUDIO_MIX_H
#define PORT_AUDIO_MIX_H

/* The settings page's music, sound effect and ambience volumes: the game
 * names the category of each sound as it starts it (pmario_sound.c, and
 * sound.c for streams, which start later); each of the sound's voices is
 * scaled by that category's volume in the software mixer
 * (libs/musyx/src/musyx/runtime/hw_pcmixer.c). Anything else is a sound
 * effect. */
enum { PORT_MIX_MUSIC, PORT_MIX_SFX, PORT_MIX_AMBIENCE };

/* Sets the current category (cat >= 0; -1 only reads it) and returns the one before. */
int salPcCategory(int cat);
/* gain: 0-1 */
void salPcSetCategoryGain(int cat, float gain);
/* A volume slider's 0-100 as a gain, on an audio taper (squared): 50% sounds
 * about half as loud, where a straight line barely changes until the bottom
 * of the slider (audio_mix_pc.c). 100% is 1, the game's own level. */
float portVolumeGain(int percent);

#endif
