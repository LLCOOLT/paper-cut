#ifndef PORT_MUSIC_H
#define PORT_MUSIC_H

/* Music shuffle and the player's own music (Audio settings;
 * port/src/common/music_pc.c, port/settings/features/music.cpp). Both are
 * only what is heard: the game's own track still plays on its stream channel
 * (silenced while the player's file plays in its place), psbgm keeps the
 * track the game asked for, and the shuffle's choices come from a random
 * source of its own, never ttyd_rand, so two games in lockstep stay the same
 * whatever each player hears.
 *
 * Music shuffle ("music_shuffle"): a room's field music is played as another
 * field track, its battle music as another battle track, chosen once per room;
 * event music, jingles and fanfares are left alone.
 * Your music ("custom_music", on by default): a file in the music folder in
 * the user folder named after a track (BGM_STG1_NOK1.ogg; music/tracks.txt
 * lists the names, written there the first time) plays in place of it, looped
 * (at its LOOPSTART/LOOPLENGTH or LOOPEND tags, or a WAV's sampler loop, else
 * the whole file), at the music volume. WAV always; OGG, MP3 and FLAC when
 * their decoders are in libs/audio_decoders (stb_vorbis.c, dr_mp3.h,
 * dr_flac.h; music_pc.c uses each one that is there).
 * "Shuffle includes your music" ("music_shuffle_custom"): the shuffle also
 * picks the music folder's files: ones named after a field or battle track
 * join that track's kind, any others the field tracks (in music/battle/, the
 * battle tracks). */
#ifdef __cplusplus
extern "C" {
#endif

/* pmario_sound.c psndBGMOn_f_d: BGM id is about to start on BGM channel ch;
 * returns the disc track to stream in its place (id itself unless Music
 * shuffle picked another). */
int portMusicPick(int id, int ch);
/* ... and, just before its SoundSSPlayCh, the track it streams: the next
 * stream to start plays the player's file for it when there is one. */
void portMusicNext(int id);
/* sound.c: stream channel chan starts (after any stop of what played there)
 * in mix category cat (port/audio_mix.h); returns the category for its voices:
 * PORT_MIX_SILENT when the player's file plays in its place. */
int portMusicStreamStart(int chan, int cat);
/* sound.c: stream channel chan stopped. */
void portMusicStreamStop(int chan);
/* sound.c, each frame: stream channel chan's volume (0-1) while it plays,
 * or -1 while it is paused (the file waits at its place). */
void portMusicStreamVolume(int chan, float volume);
/* The mixer (portAudioMixExtra, audio thread): the player's files on top of
 * frames stereo frames at 32 kHz. */
void portMusicMix(short* out, unsigned frames);

/* The settings page: the music folder, and the files in it again (count). */
const char* portMusicFolder(void);
int portMusicRescan(void);
/* The track a file plays for now on a stream channel (its file name), or NULL. */
const char* portMusicPlaying(void);
/* Which of OGG, MP3, FLAC this build reads (bits 1, 2, 4; WAV always). */
int portMusicFormats(void);

#ifdef __cplusplus
}
#endif

#endif
