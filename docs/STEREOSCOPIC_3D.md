# Stereoscopic 3D

The game drawn for two eyes, for a 3D TV or monitor, a phone in a VR viewer, a
VR headset's virtual screen or coloured 3D glasses. **Graphics > Stereoscopic
3D**, off by default (`stereo_3d=` in `settings.txt`):

| Mode | What comes out | For |
|---|---|---|
| Side by side | each eye in its half of the picture, keeping its shape (bars above and below) | a phone in a VR viewer; a headset's virtual screen; a 3D monitor or TV in full side-by-side |
| Side by side (half) | each eye squeezed to half the width, filling the picture | 3D TVs and projectors (their "Side by side" mode) |
| Top and bottom | the left eye above, the right below, keeping their shape (bars at the sides) | 3D players and headsets with an over/under mode |
| Top and bottom (half) | each eye squeezed to half the height | 3D TVs (their "Top and bottom" mode) |
| Anaglyph (red-cyan) | both eyes in one picture, the left through red, the right through cyan (Dubois's colours) | red-cyan paper glasses |
| Anaglyph (green-magenta) | the left eye's brightness in green, the right eye in red and blue | green-magenta glasses |

The left eye is on the left (or on top). **3D depth** sets how far apart the
eyes are; **Screen distance** where the screen sits in the scene, against the
distance to what the camera follows (at 100% Mario is at the screen's depth:
nearer things come out of it, farther ones go behind).

It changes only what is drawn: the game, saves and replays are the same, and
online each player has their own (it is one of the per-player look settings).
It works wherever the game draws with Aurora: Windows, Linux, macOS, Android
and iOS.

## How it works

Each frame the game's cameras draw twice into the same frame
(`port/src/aurora/stereo_aurora.c`, `port/settings/features/stereo.cpp`):

- **The two eyes.** Each 3D (perspective) camera is moved along its own x
  axis by half the eye separation, and its projection skewed so the plane at
  the screen distance stays where it was: an off-axis frustum, so there is no
  keystone and that plane shows at screen depth. The paper characters stand
  at their places in the scene, so they get their depth from where they are.
- **The left eye first, as a redraw that changes nothing.** It is drawn as a
  frame between the game's own is for high refresh rates
  ([FRAME_INTERPOLATION.md](FRAME_INTERPOLATION.md)): poses, random numbers,
  the frame-temporary heap and what draw code keeps are put back after it. The
  per-draw counters (the HUD's blink, the message window's scroll, the paper
  physics) step in it as the game's own draw is about to, and are put back, so
  both eyes show the same moment. The game's own draw is the right eye and
  runs on the state it would without stereo.
- **Between the eyes** the frame is kept and cleared to the game's own clear
  (black, the far depth); at the frame's end the right eye is kept too, and the
  two are drawn into the frame in the chosen layout.
- **The HUD, menus, text and fades** (the 2D cameras) are not moved: they are
  identical in both eyes, at screen depth.
- **Drawing to textures.** Everything the game copies out of the frame during
  a draw (bloom's copy, the screen captures some effects draw with, the
  paper shadows) happens once an eye, from that eye's picture. The cameras
  that draw into textures to be used later (the offscreen and shadow cameras)
  are not moved: what they make is the same for both eyes. A frame whose
  draw makes the extra textures of a few effects (`extdrv`) for the first
  time is drawn once and shown flat to both eyes. A captured image of the
  screen that the game keeps for later frames (`imgdrv`'s shadows) is taken
  from the right eye. A flat background picture is moved as far as the far
  distance, so it sits behind the scene.
- **High refresh rates.** The frames drawn between the game's are drawn
  twice too, an eye each.

## Cost and limits

- The scene is drawn twice: about twice the graphics card's work. Ray-traced
  lighting is off while stereo 3D is on (twice the cost again, and its
  screen-locked noise would sit on the screen's plane); everything else stays
  as set.
- Side by side and Top and bottom halve each eye's resolution in one or both
  directions; supersampling (Graphics) gives each eye more detail back.
- The settings page, the on-screen controls and other overlays are drawn flat
  over the whole picture, not per eye: to change settings on a 3D TV, switch
  the TV's 3D mode off first.
- Screenshots and photo mode capture the picture as shown (both eyes).

## VR headsets

There is no head tracking: the game shows on a flat 3D screen in front of you.

- **A phone in a VR viewer** (Cardboard and the like): Side by side, the phone
  held landscape. The viewer's lens distortion is not corrected, which most
  simple viewers tolerate.
- **A PC headset**: show the game's window on a virtual screen with the
  headset app's 3D mode, Side by side or Top and bottom (full or half, as
  the app expects): the desktop apps that can show a window as a 3D screen
  work as they are.
- **A standalone headset running the Android build** shows apps on a flat
  panel, which can't show two eyes; use its PC link and the above instead.
