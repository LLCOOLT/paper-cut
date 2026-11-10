#ifndef PORT_PM64_MUSIC_H
#define PORT_PM64_MUSIC_H

/* The Paper Mario 64 soundtrack (docs/PM64_EXTRAS.md): Paper Cut's own player
 * for the N64 game's music, reading the SBN archive in the linked ROM's cache
 * (portPm64Segment(PM64_SEG_AUDIO)): its INIT table (songs, banks, drums), the
 * BGM sequences and the BK instrument banks (VADPCM samples, envelopes). It
 * renders stereo at 32 kHz; port/src/common/music_pc.c plays it on the game's
 * music stream channel in place of the TTYD track, so the game's own fades,
 * pauses and volume apply. Settings > Audio > Soundtrack: "Paper Mario 64's"
 * or "Mixed by area" (pm64_music 1 / 2); nothing changes without a linked ROM.
 * Formats as the PM64 decomp documents them; the code is Paper Cut's own. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pm64Song Pm64Song;

/* PM64 song songId (the decomp's SONG_ ids, 0 Toad Town .. 0x96), from its
 * first variation; NULL without a linked ROM or if it can't be read. Opened on
 * the game thread, rendered on the audio thread, closed on either. */
Pm64Song* portPm64SongOpen(int songId);
/* frames stereo s16 frames at 32 kHz into out; the frames made (fewer only
 * once the song has ended: jingles end, field and battle songs loop). */
int portPm64SongRender(Pm64Song* s, short* out, int frames);
void portPm64SongClose(Pm64Song* s);
/* "Toad Town" for a mapped song id, or "" */
const char* portPm64SongName(int songId);

/* The PM64 song for TTYD track name (BGM_STG1_NOK1, ...), or -1: the area
 * mapping listed in docs/PM64_EXTRAS.md. */
int portPm64SongForTrack(const char* track);
/* Settings "pm64_music": 0 the game's, 1 PM64's, 2 mixed by area. With a
 * linked ROM, the song to play for track in area (gp->currentMapName's first
 * three letters) or -1 (the game's own). */
int portPm64MusicPick(const char* track, const char* mapName);
/* A Paper Mario 64 boss fight's battle track: TTYD's own boss theme ("Big battle"), what plays without a ROM or with
 * "pm64_boss_music" off. (It was once "BGM_BOSS_BATTLE1", a name the game's list hasn't: no track, no stream to
 * play PM64's song on, every PM64 boss fight silent.) */
#define PM64_BOSS_TRACK "BGM_CHUBOSS_BATTLE1"
/* A Paper Mario 64 boss fight is about to start (port/src/aurora/pm64_bosses_aurora.c): with a linked ROM and
 * "pm64_boss_music" on (the default), its battle track (PM64_BOSS_TRACK) plays PM64's song songId (its own boss
 * theme, whatever "pm64_music" says) until another track follows it; -1: none. */
void portPm64MusicBossSong(int songId);
/* A battle's music has been asked for (portMusicBattleCheck): a boss song armed for a fight that never started (one
 * refused, or left) is let go, so a later fight on the same track doesn't play it. */
void portPm64MusicBattleStarted(void);

#ifdef __cplusplus
}
#endif

#endif
