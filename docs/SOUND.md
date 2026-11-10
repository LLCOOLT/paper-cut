# Music, headphone 3D audio and the DualSense's speaker

## Soundtrack

Audio > **Soundtrack** chooses what music plays. Only you hear it: online and in replays everyone keeps their own.

| Choice | What plays |
|---|---|
| The game's | the game's own tracks |
| Your music (the default) | a file in the `music` folder in the user folder plays in place of the track it is named after (`music/tracks.txt` lists the names: `BGM_STG1_NOK1.ogg` plays in Petalburg); the game's tracks everywhere else |
| The game's, shuffled | each room's music is another of the game's: field music for field music, battle music for battle music. Scenes, jingles and fanfares stay as they are |
| Yours and the game's, shuffled | the same, with the folder's files joining in: one named after a field or battle track as that kind, any other as field music (in `music/battle/`, as battle music) |

WAV files always play; OGG, MP3 and FLAC where the build has their decoders. A file loops at its LOOPSTART and LOOPLENGTH (or LOOPEND) tags, or a WAV's sampler loop, otherwise as a whole. Audio > Music folder opens the folder and looks for files again.

The choice is kept in `settings.txt` as three settings, as before: `music_shuffle` (the shuffle), `custom_music` (your music) and `music_shuffle_custom` (the shuffle picks your files). With the shuffle and your music on but `music_shuffle_custom` off (an older settings file can have that), the row shows Yours and the game's, shuffled; your files then play only in place of the tracks they are named after, and the file stays as it is until the row is changed. Code: `port/settings/features/music.cpp`, `port/src/common/music_pc.c`.


## Headphones (3D)

Audio > Speakers > **Headphones (3D)** plays the game's surround mix on headphones as if from speakers around your head: the front pair 30 degrees left and right, the rear (the game's surround and Pro Logic II rear channels, which 5.1 and 7.1 send to the rear speakers) 110 degrees behind. It takes effect from the next start, as the other speaker choices do. Code: `port/src/common/hrtf_pc.c`, from `libs/musyx/src/musyx/runtime/hw_pc.c`.

The sound reaches each ear through a small head model built into the code, with no measured data and nothing downloaded: C. P. Brown and R. O. Duda's structural model (*A structural model for binaural sound synthesis*, IEEE Transactions on Speech and Audio Processing 6(5), 1998). A sphere's head shadow (a one-pole, one-zero filter: brighter for the ear facing the sound, duller for the far one) and its time delay between the ears, plus a gentle high-frequency roll-off for sounds behind. It changes only what you hear: replays and online play are unaffected.

## Sound from the DualSense

Audio → **Sound from the DualSense** (on by default): with a DualSense connected by a USB cable, the action commands' blips and ratings, the Superguard's clink and coins also play from the controller's own speaker. Code: `port/settings/features/dualsense_speaker.cpp`.

A DualSense on a cable is also a sound device with four channels; Paper Cut opens it, tells the controller (through its output report, as for the adaptive triggers) to play the first pair from its speaker, and plays short tones made in the code (none of the game's sounds) when the game plays the matching sound. Over Bluetooth the controller has no sound device the system can use, so nothing plays there. Turning the setting off gives the controller's audio back to its headset jack.

## Output rate

Audio > **Output rate**: 48 kHz (the default) or 32 kHz, from the next start. The game mixes at 32 kHz, as the GameCube's DSP does, and most sound devices run at 48 kHz, so something resamples it. With 48 kHz Paper Cut does that itself, before the sound leaves the game: exactly 3 output frames for every 2, through a polyphase windowed-sinc filter (16 taps per phase, a Kaiser window, cut-off at 15 kHz), which keeps the highs and doesn't add the aliasing of a linear resampler. The device then takes the sound as it is. 32 kHz hands the game's own rate to the system, as before. The mix itself is the same either way, so replays, online play and recordings of the mix ($TTYD_SURROUND_WAV, still 32 kHz) don't change. Code: `resample48` in `libs/musyx/src/musyx/runtime/hw_pc.c`.
