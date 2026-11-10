#ifndef PORT_PLAY_AS_VOICE_H
#define PORT_PLAY_AS_VOICE_H

/* Play as: the pick's voice (port/src/common/play_as_voice_pc.c, docs/PLAY_AS.md "Voices"). Mario's voice
 * clips (SFX_VOICE_MARIO_*) play as the pick's: Peach's and Bowser's own clips from the disc, the closest
 * of theirs for each moment, or nothing where they have none that fits; Luigi's are Mario's own, pitched up
 * and a little quieter. The other way round too: while Peach or Bowser is the pick, Mario plays their part
 * (their interludes, Bowser's boss fights), so their own clips there are Mario's. Sounds only: nothing a
 * script waits on, no random numbers taken, the same play online and in replays. With Mario as the pick
 * nothing changes. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortVoice {
    int pitch;  /* added to the effect's pitch bend (0x2000 = MusyX's full bend range up) */
    int volume; /* the list volume's share, of 256 */
    int cut;    /* frames the clip plays before it fades out (0: the whole clip) */
} PortVoice;

/* __psndSFXOn: the sound id to play (as searchPSSFXList gives it) for id, with *v set for it; -1: nothing
 * plays. v is { 0, 256, 0 } for every sound that isn't a voice of Mario's or the pick's. */
int portPlayAsVoice(int id, PortVoice* v);

/* BtlUnit_snd_se_pos: the sounds started until the next call (NULL) are the battle unit's own: a versus
 * battle's Luigi and Brothers mode's Luigi speak as Luigi, a versus battle's Mario as Mario. */
void portPlayAsVoiceUnit(const void* unit);

/* __psndSFXOn, once an effect has its slot: with TTYD_VOICE_TRACE set, a log line for every voice clip that
 * starts ("[voice] SFX_VOICE_... for SFX_VOICE_..." with the pick, pitch and cut), the scenarios' check. */
void portPlayAsVoiceStarted(int asked, int id, const PortVoice* v);

#ifdef __cplusplus
}
#endif

#endif
