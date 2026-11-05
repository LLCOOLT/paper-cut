# Replays

A replay is a run's controller input, recorded to a small file and played
back exactly. Practice tab > Replays.

## How it plays out the same

The game plays out the same from the same start and the same input: the
lockstep online co-op uses (port/include/port/online.h). The game clock is
virtual (a field per frame, from a base the file keeps), disc reads finish
within their frame, audio is rendered per frame. A save loaded into a game
that is already running is not the same start (scripts, effects, AI and the
random numbers drawn while loading were already different), so a recording
and its playback each begin with a fresh start of the program (Restart now's
machinery, restart.cpp), from:

- **where you are**: quick resume's last clean save (written out first, with
  quick resume off too), loaded at start-up as the launcher's Continue does
  (`TTYD_LOAD_SAVE`, warp_pc.c); or
- **the title**.

Each frame's input is taken at the pad read (src/sdk/DEMOPad.c,
`portReplayApplyPad`), after the keyboard, the view's controls and online's:
what the game itself gets. Controllers 1 and 2 are kept; 3 and 4 are "none"
in both the recording and the playback.

While recording or playing, what would change this game outside the pad's
input is off: save states, save warps, the Practice tab's edits, fast travel,
crowd control, photo mode, and taps or clicks on the game's lists (the
mouse's B and wheel go through the pad, so they stay). Fast-forward is off
while recording (the clock is virtual); a playback can fast-forward (its
frames come sooner; the game clock is unchanged). Quick resume keeps making
its snapshots during both (with the setting off too), since a snapshot reads
the clock; during a playback none is written, so the player's own stays.

## Record

Choose **From where you are** or **From the title**, a name, and **Restart
and record**. The game starts again and records from its first frame; a red
"Recording" label shows the time. **Stop and save** writes
`<user>/replays/<name> <date>.pcreplay`. A recording under way is also saved
when the game quits or restarts.

## Play

Pick one from the list (or **Open a replay file...** on desktop) and
**Play**. The game starts again with the replay's settings for that run
(`portSettingsSetForRun`, nothing saved to settings.txt), its memory card in
`<user>/replays/.playback` (the player's own card is not touched), its start
save, its clock and its input. The player can't control it; **Esc** or **B**
stops it, and the game starts again as it was before (quick resume's save).
The label shows the frame and the length.

A replay from another release's disc (its ID) is refused. One from another
Paper Cut version, or with other mods, is played with a warning, as it may
go out of step.

## Checking a playback is right

Every 600 frames the recording keeps the game's fingerprint
(`portOnlineFingerprint`: the clock and random state, the map, Mario and the
partner, the scripts, the NPCs, the flags, battle HP), taken at the pad read
before the frame plays. The playback compares each as it comes; the first
that differs is logged (`[replay] out of step at frame N`) and shown under
the label ("Out of step since frame N").

## Ghost races

A recording also keeps where Mario was every other frame: the room, his place, his facing and his animation (`port/src/aurora/ghost_aurora.c`; it only reads him, so the game is unchanged). A replay that has it shows **Race its ghost**: the game starts again from the replay's start, as for a playback (its settings, card, start save and clock), but with the player in control. The replay's Mario is drawn see-through, a pose of the game's own Mario animations, in the same room at the same time from the start; each room entered says how far ahead or behind the ghost was there (the ghost's first time in that room since the last one matched). **Stop the race** (or Shift+Esc) starts the game again as it was before. Quick resume writes nothing during a race. Local only for now.

A hardcore run recorded from the Modes tab is saved at its Game Over, so it can be raced too.

## The file

Little-endian; "bytes" is a u32 length and then them.

| what | |
| --- | --- |
| magic | `PCRPLAY1` |
| bytes ×5 | Paper Cut's version, the disc's ID (G8ME01...), the mods (names, sorted), the name, the date |
| u64 | the game clock's base |
| u8, u8 | the start (0 the title, 1 a save), the save's card slot (0-3) |
| bytes | the settings: `key=value` lines, every one but this device's own (presets.cpp's list, but the intro and PAL's 50 Hz are kept) |
| u32, then per file bytes name, bytes data | the memory card's files (.gci) |
| bytes | the start save (a .gci), or empty |
| u32 | frames |
| u32, then per run u32 frames + 2 × 16 bytes | the input, run-length: both controllers as online co-op's 16 bytes (`portOnlinePackPad`) |
| u32, then per check u32 frame + u64 hash | the fingerprints |
| the rest (optional) | the ghost track: `GHST`, u32 frames per sample (2), u32 maps + 16 bytes each, u32 animations + 32 bytes each, u32 samples + 20 bytes each (u16 map, s16 animation, u8 pose group, u8 mirrored, s16 turn in tenths of a degree, f32 x, y, z); a map of 0xFFFF is "not in the field". Older builds stop reading before it. |
| then (optional) | a leaderboard run's trailer (docs/LEADERBOARDS.md): `MODE`, u32 6, u8 the board (0 boss rush mid-game, 1 late game, 2 Pit of 100 Trials), u8 finished, u32 the run's time in frames. Read after the ghost track (whose length its counts give); builds before it take it as the ghost track's end and skip it. |

A run from a save holds the card (about 72 KB a file) and the save (72 KB);
the input itself is a few KB for minutes of play.

## Tests

- `TTYD_REPLAY_RECORD=1`: record from the start (with `TTYD_LOAD_SAVE` /
  `TTYD_LOAD_SAVE_SLOT`, from that save); `TTYD_REPLAY_NAME`, its name;
  `TTYD_REPLAY_OUT=<file>`, where it's saved; `TTYD_REPLAY_FRAMES=N`, saved
  after N frames.
- `TTYD_REPLAY_PLAY=<file>`: play it.
- `TTYD_REPLAY_EXIT`: the program ends once the recording is saved, or the
  playback is over (exit 3 if it went out of step).
- With `TTYD_FINGERPRINT=<file>` on both, the two files must be the same
  line for line over the replay's frames.

## Limits

- A setting that changes play, changed during a recording, isn't in it (the
  settings are kept at its start).
- This device's own settings (window, graphics, sound, controls...) aren't
  in the file: one that reads the game clock while playing could put a
  playback on another setup out of step. The fingerprints would say so.
- Not on phones and tablets (their apps can't start themselves again), and
  not during online co-op.
