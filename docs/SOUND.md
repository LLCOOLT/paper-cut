# Headphone 3D audio and the DualSense's speaker

## Headphones (3D)

Audio > Speakers > **Headphones (3D)** plays the game's surround mix on headphones as if from speakers around your head: the front pair 30 degrees left and right, the rear (the game's surround and Pro Logic II rear channels, which 5.1 and 7.1 send to the rear speakers) 110 degrees behind. It takes effect from the next start, as the other speaker choices do. Code: `port/src/common/hrtf_pc.c`, from `libs/musyx/src/musyx/runtime/hw_pc.c`.

The sound reaches each ear through a small head model built into the code, with no measured data and nothing downloaded: C. P. Brown and R. O. Duda's structural model (*A structural model for binaural sound synthesis*, IEEE Transactions on Speech and Audio Processing 6(5), 1998). A sphere's head shadow (a one-pole, one-zero filter: brighter for the ear facing the sound, duller for the far one) and its time delay between the ears, plus a gentle high-frequency roll-off for sounds behind. It changes only what you hear: replays and online play are unaffected.

## Sound from the DualSense

Controls > **Sound from the DualSense** (on by default): with a DualSense connected by a USB cable, the action commands' blips and ratings, the Superguard's clink and coins also play from the controller's own speaker. Code: `port/settings/features/dualsense_speaker.cpp`.

A DualSense on a cable is also a sound device with four channels; Paper Cut opens it, tells the controller (through its output report, as for the adaptive triggers) to play the first pair from its speaker, and plays short tones made in the code (none of the game's sounds) when the game plays the matching sound. Over Bluetooth the controller has no sound device the system can use, so nothing plays there. Turning the setting off gives the controller's audio back to its headset jack.
