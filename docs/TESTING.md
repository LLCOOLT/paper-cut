# Testing without a window: offscreen runs and the test pass

Paper Cut's own checks (the map sweep, the settings fuzzer, the story runs, the
card scripts: see [PC_PORT.md](PC_PORT.md), Testing) mostly run the headless
build, which draws nothing. Looks need the Aurora build's pictures, and an
unattended run should never put a window on the screen or take the focus from
whoever is using the computer. This page is about the two pieces made for that:
**offscreen runs** and the **test pass runner** with its scenarios and
references.

## Offscreen runs

`TTYD_OFFSCREEN=1` on an Aurora build on a desktop:

- The game's window is created hidden and stays hidden for the whole run: it is
  never shown, raised, made full screen or focused, and on Windows it is a tool
  window (no taskbar button, should anything show it). A hidden window doesn't
  pause the game, as a minimized one would.
- The game runs and draws as usual, on the GPU: `TTYD_SHOT`, an input script's
  `shot NAME` and `TTYD_LAUNCHER_SHOT` work as in a shown window, and give the
  same pictures. (The game's captures are read back from its own frame, not
  from the screen, so nothing about them depends on the window.)
- The sound goes to SDL's dummy device: nothing is heard, and the game's sound
  runs at its usual pace.
- Everything else that would open a window of its own is left out and logged
  as `[offscreen] ... not shown`: message boxes, the file, save and folder
  pickers, the disc picker (give the disc on the command line), links and
  folders opened in another program, the second screen's window (created
  hidden) and the focus taken back after a restart.

It is for desktops: phones and the browser ignore it. On Windows it is proven
by watching the run's process: its windows (the game's, and SDL's own helper
windows) all stay invisible from start to end. On Linux under X11 a hidden
window draws the same way; under Wayland a window that is never shown gets no
surface, so offscreen runs there need X11 (Xwayland, or `SDL_VIDEODRIVER=x11`)
or a virtual display.

```
TTYD_OFFSCREEN=1 TTYD_FAST=1 TTYD_USER_DIR=/tmp/pc-user TTYD_NO_LAUNCHER=1 \
TTYD_MAP=gor_01 TTYD_EXIT_FRAMES=600 TTYD_SHOT=400 TTYD_SHOT_DIR=/tmp/shots \
  build-pc-us-aurora/ttyd-us "Paper Mario - The Thousand-Year Door.iso"
```

## The test pass runner

`tools/port/testpass.py` plays named **scenarios** and keeps what each run
leaves: its log, captures, fingerprint and exit state.

```
python3 tools/port/testpass.py list
python3 tools/port/testpass.py run baseline --run mine --versions us
python3 tools/port/testpass.py run baseline fingerprint --run mine --ref ref
python3 tools/port/testpass.py compare mine --ref ref
python3 tools/port/testpass.py sheet mine
```

- `run [SCENARIO|GROUP ...]` plays each scenario on each of its versions (US,
  JP and EU unless the scenario says otherwise, `--versions` narrows it), with
  the builds of `--tree` (default this checkout: `build-pc-<version>-aurora` or
  `-headless`) and its disc images (`orig/<game id>/*.iso`, or `--iso-dir`).
  Every run is a fresh user folder with `settings.txt` set for tests
  (`skip_intro=1`, `fullscreen=0`, no Discord status, the settings hint seen,
  then the scenario's own settings), `TTYD_FAST=1`, its own pipeline cache,
  `TTYD_WATCHDOG`, and a time limit. Aurora runs are offscreen; the runner also
  watches the game's windows on Windows, and a visible one stops the game and
  fails the run. Where a slot script is set up (`$TTYD_GAME_SLOT`, or a
  `game_slot.sh` beside the checkout's folder, which runs its arguments once a
  slot is free), every game
  waits for a free slot so a pass never runs more games than the machine is
  meant to.
- Each run lands in `local-docs/testpass/<run>/<scenario>/<version>/` (a
  git-ignored folder for the test tools' output):
  `log.txt`, `shots/`, `fingerprint.txt`, `input.txt`, `result.json` (exit
  code, time, captures, why it failed) and `report.txt` for the whole run.
- A run fails on an exit code the scenario doesn't allow, a crash, panic or
  stall, a warp or input step not reached, a stuck story, a visible window, a
  log line it must not have or lacks, or a capture it expects but didn't make.
- `--ref RUN` (or `compare RUN --ref REF`) then compares each capture with the
  reference run's (a pixel differs when a channel is more than `tolerance`
  apart; a capture fails when more than `threshold` of its pixels do) and
  writes `diff/<capture>.png` (the new capture greyed, the differing pixels
  red); and each fingerprint line by line, giving the first frame that
  differs (a run that ends a frame or two sooner or later is not a
  difference: builds exit a frame apart). A failed run on either side fails
  the comparison. An online scenario's players' captures are compared role
  by role. A scenario (or version) the reference never ran is a skip, and the
  report says how many were compared; a compare of nothing fails.
  `compare.json` per run, `compare_<ref>.txt` for the whole run.
- `sheet RUN` puts a run's captures on contact sheets (`sheet_NN.png`, with
  `sheets.txt` naming each tile), to look a pass over at a glance; `summary
  RUN` writes its `report.txt` again from its results (a run made in parts).
  A run lists its jobs in `jobs.json` before it starts them, so `summary`
  fails a job with no result (not run, cut off, or still going) or with an
  older run's result.
- A `$PAPERCUT_PM64_ROM` or `$TTYD_REF041_TREE` that is set but isn't there
  fails the scenarios that need it (unset, they are skipped); `--no-skips`
  makes any skip fail the run.
- Test games keep the computer's own controllers out (SDL's joystick drivers
  off; its virtual test pads still work), so a pad that sleeps or is
  unplugged can't add input or hold a scripted run.
- Older headless builds read the extracted disc rather than the image:
  `--data-root` (or `$TTYD_DATA_ROOT`) names the folder that holds
  `<game id>/files`.

### Scenarios

Small JSON (or TOML) files in `tests/port/testpass/`, named by their file:

```json
{
 "desc": "Petal Meadows: a battle with the nearest Goomba, fought with jumps",
 "groups": ["baseline", "battles"],
 "env": {"TTYD_MAP": "hei_01,w_bero", "TTYD_STORY": "25", "TTYD_EXIT_FRAMES": 3000},
 "input": "tests/port/input/petal_battle.txt",
 "expect_shots": ["battle_2_start", "battle_3_turn"],
 "timeout": 180
}
```

| Field | Meaning |
|---|---|
| `desc`, `groups` | what it shows; the names it is run under (`baseline`, `maps`, `battles`, `menus`, `scenes`, `fingerprint`) |
| `versions` | the versions it applies to (default `us`, `jp`, `eu`) |
| `backend` | `offscreen` (Aurora, default) or `headless` |
| `env` | the game's switches ([PC_PORT.md](PC_PORT.md)'s table); `null` removes a default, `{iso}` is the disc image, `{out}` the run's folder, `{data_root}` the extracted discs' folder (`--data-root`; the player's saves in `data/saves`) |
| `settings` | `settings.txt` lines over the test defaults (`{"widescreen": 1}`) |
| `input` | an input script: a list of lines, or a file in the repository |
| `shots` | frames to capture (`TTYD_SHOT`); an input script's `shot NAME` captures too (`<map>__NAME.png` on a `TTYD_MAP` run) |
| `expect_shots` | captures the run must make |
| `ui_shots` | `true` (or `run --ui-shots`): each named capture is also written as `<name>_ui.png` with the Dear ImGui layer over the picture as shown (the full settings page, the map editor's panel), drawn again on the CPU (`TTYD_SHOT_UI=1`); `TTYD_SETTINGS_WALK=1` with the page open (`TTYD_TEST_F1_AT`) captures each of its tabs, each screenful |
| `fingerprint` | `true`: `TTYD_FINGERPRINT`, compared with the reference's (`compare`'s `fingerprint_slack`: frames more or fewer at the end that are not a difference, default 2) |
| `disc` | `false`: no disc on the command line (the launcher) |
| `user_files` | files or folders copied into the fresh user folder (a memory card, saves); `{version}` is the run's version and `{run}` its name; a folder ending in `/` has its contents copied (another run's user folder, kept with `--keep-user`: old data) |
| `user_from` | another scenario's name: the user folder it left in the same run (same version) instead of a fresh one, as a second launch finds it (a linked ROM's cache, a save; its settings kept); these run after the one they start from (a chain in as many phases as it is long; running one alone brings its chain along), and are skipped when it was |
| `tree` | `"ref041"`: played on the 0.4.1 reference checkout (`$TTYD_REF041_TREE`, else `ttyd-port-ref041` beside this one): the old-data scenarios; skipped when it has no build |
| `portable` | `true`: portable mode, the run's own `portable/` folder standing for the program's (`$TTYD_PORTABLE_BASE`), with `portable.txt` and no user or cache folder named; Cache must fill there |
| `texpack_from` | a texture-dumping scenario of the same run (its `TTYD_CACHE_DIR` `{out}/cache`): a test pack made from its dumps' names (`tools/port/gen_test_texpack.py`) goes into the user folder |
| `library` | versions whose disc images are put in the launcher's library (`library.txt`), for a run that reads another version's disc (`["jp"]`) |
| `library_last` | `true`: the library's first disc is also the one last played (`last_disc.txt`), the one "Skip the launcher" plays at start |
| `timeout`, `exit` | seconds before it is stopped (default 180); the exit codes that pass (default `[0]`, or `"any"`) |
| `log_must`, `log_must_not` | regular expressions over the log |
| `file_must`, `file_must_not` | `{"file under the run folder": [regular expressions]}` (a crash report, a dump); a `file_must` file must be there (with `[]`, not empty; a folder, not empty) |
| `shots_partial` | `true`: captures a frame or two apart (a GIF's) or past where the run ends: some are dropped, so at least one must be made, not every one |
| `new` | `true`: its captures have no reference yet; a `compare` otherwise fails a capture the reference hasn't ("no reference"; a scenario the reference never ran at all is a skip either way) |
| `allow_failures` | failure kinds that don't fail it (`panic`, `crash`, `stall`, `warp`, `stuck`, `input`, `window`) |
| `flat_max` | a share of the frame (`0.3`): a capture whose largest flat, bright region (one colour, every channel 190 or more) covers more fails; a polygon with a NaN or infinite corner drawn across the screen (`bug_white_half_*`) |
| `file_must`, `file_must_not` | `{"speech.txt": [regex, ...]}`: files the game wrote into the run's folder (`{out}`: TTYD_SPEECH_LOG, TTYD_CRASH_REPORT_TEST, TTYD_SETTINGS_DUMP, TTYD_TEXT_FIT) must have, or must not have, each |
| `stereo_check` | `{"layout": "sbs", "shots": [...], "hud": [[fx0, fy0, fx1, fy1]], "hud_shots": [...]}`: each stereoscopic 3D capture split into its two eyes and matched block by block: real depth (blocks shifted, near and far apart, the near rectangle in front of the far one, so the eyes aren't swapped), `"depth": false` for 3D depth 0, the HUD rectangles at screen depth. Layouts `sbs`, `sbs_half`, `tb`, `tb_half`, `red_cyan`, `green_magenta`; `testpass.py stereo PNG LAYOUT` prints one capture's numbers (`stereo_*`) |
| `bloom_check` | `{"pairs": [["off", "on", level], ...], "max_shift": 0.02}`: each pair a capture with bloom off and one with it on a few frames later (the script's `setting bloom N` between): the glow (on minus off) lined up with what glows (the off capture's luma above `level`, the preset's own threshold) in each half of the frame, the best shift (cross-correlation across, then down) within `max_shift` of the width and height; a glow drawn squeezed, flipped or offset fails (`bloom_centred_*`) |
| `compare` | `{"tolerance": 8, "threshold": 0.002}` |
| `online` | two to four games playing together (below) |

**No false passes.** A scenario must have a positive check: `log_must`, `expect_shots`, `file_must`, `save_matches` or `fingerprint` (online: `linked` or `progress` left on, or a player's `log_must`). Without one, a run fails, and so does `testpass.py lint`, which also checks that every file parses and its fields and versions are known. On Aurora every capture asked for (`shots` frames, a script's `shot NAME`) must be made (with MSAA, which has no readback, the player's screenshot is the check). A file that doesn't parse, an unknown `--versions`, a run that selects nothing, and a scenario whose `user_from` failed all fail.

**Online scenarios.** `"online": {"players": [...], "link": "address" or
"relay", "faults": "...", "relay_args": [...], "check": {...}}` plays every
player's game at once ([STATE_SYNC.md](STATE_SYNC.md)): the first hosts (on
the IPv6 loopback, or a room on `tools/port/sync_test_relay.py`), the others
join. Each player has its own `role`, `env`, `input`, `settings`,
`log_must`, `exit` and `expect_shots`, and may name another `version` (a JP
game joining a US one), `backend` or `tree` (an older release's build), or
`"link": false` and `"checked": false` (a game outside the session, joined
its own way). A run fails as a single game's does, and also when a game
logged `DRIFT`, never linked, didn't learn every other player's name, or
ended with progress unlike the others' (`check` turns these off one by one;
`battle_ended` asks for the host's shared battle to end in the field). Each
game is in `<run>/<scenario>/<version>/<role>/`, the relay's log beside them.
A group holds one game slot per game while it plays. `online_soak_long`
(its own group, `online_soak`, not in `online`) plays two games in a relay
room for 30 minutes of game time under internet-like faults, with
disconnects and shared battles: run it before a release.

The scenarios here are the regression baseline: the title, the title menu and
the file select, the launcher, the hub of every chapter and a few more maps,
widescreen, battles in five areas, the Start menu's tabs, the harbour's arrival
scene with its text (`baseline`), and same-input fingerprints of a walk, a
battle, a scene and the Start menu, with Console-accurate mode off and on
(`fingerprint`, offscreen: the headless build of older releases writes no
fingerprint). A release's references are a run of them on the
release before it: everything that should look and play as it did is then one
`--ref` away.

The on-screen touch controls can be looked at on a desktop: an Aurora build
configured with `-DTTYD_TOUCH_OVERLAY=ON` and
`TTYD_TOUCH_DEVICE=<w>x<h>@<dpi>[,<left>,<top>,<right>,<bottom>]` fakes a
phone's screen (its size in pixels, its pixels an inch and its safe area's
insets; the window is that size, halved until it fits 1600x1000), so the
controls come out at the size they have on that phone. With it,
`TTYD_TOUCH_IOS=<model>@<scale>` (`iPhone18,1@3`) takes the pixels an inch
from the iOS build's own model table instead, and `TTYD_TOUCH_UNIT_MM=<mm>`
sets the size unit (another release's, to compare). `TTYD_TOUCH_SHOT`
captures them (the window capture can't see them), `tools/port/touch_shot.py`
puts a capture together, and the `touchsize` scenarios run an AYN Odin, an
iPhone, a tablet and a Steam Deck, sideways and upright (`touchedit`: the
layout editor). They say `"build": "touch"`: the runner takes
`build-pc-<version>-aurora-touch`, that build configured beside the plain one,
and fails them with that name when it isn't there. An online player can say
`"build"` too (a handheld's game in a desktop's room).

**Device flows** (`flows`): realistic sessions as a device plays them, not as
the input script's controller 1. An input script's `dev` steps
(`port/src/common/flows_pc.c`) plug in virtual SDL controllers and press them
(the same SDL events as a real one, so Aurora's ports, the join rules and the
touch controls' take-over see a controller), push keyboard keys through SDL's
queue, and put fingers on the touch controls by name; meanwhile controller 1 is
left to those devices. `dev expect` checks the seats (`pad N port P`,
`keyboard port P`, `players N`), the touch controls (`shown`, `hidden`), the
Start menu, the partner out, and `settled`; `dev watch N` logs Mario's and each
party member's motion and says `[flows] STUCK` when a jump, hop or hammer isn't
back to standing or walking within N frames. Any `[flows] FAIL` or `STUCK` line
fails the run (failure kind `flows`). Four profiles: `desktop` (keyboard and a
controller), `deck` (one controller, no touch), `handheld` (an AYN Odin
sideways: touch controls and a built-in controller in port 1) and `phone` (touch
controls, a Bluetooth controller connecting mid-session), each with a field
flow from the title, a battle on its own input, co-op (a second controller
joining with its Start, walking and hopping the partner, a seat swap and back,
leaving), the seven partners played by player 2, and online through the local
test relay (desktop and handheld: join by code, walk, a battle together, leave,
join again, the same progress). The Windows build has no phone or tablet seat
rule (`second_player.cpp`'s, Android and iOS only), so the touch profiles there
exercise the desktop's.

**Dead ends** (`dead`, written by `tools/port/crawl_gen.py`): every screen has a
way back with every input. Each run enters a chain of screens and leaves each one
with one way out alone, pressed until the screen is gone: a controller's B
(`dev pad 1 b`), Esc (`dev key esc`), a right click (`dev back`) or the touch
controls' B (`dev touch b`, the touch build). The Start menu with its port tabs,
categories and pages must reach `menu closed`, the settings page `page closed`,
the touch layout editor `editor closed` (B and Esc cancel it, Start is Done), and
the title's Multiplayer pages the title's buttons. About a dozen runs; run the
generator again after adding a screen to its lists.

### Known limits

- Captures need MSAA off (the test settings leave it at its default, off): with
  MSAA on, Aurora's readback of the frame fails, and `TTYD_SHOT`, an input
  script's `shot` and a player's screenshot capture nothing (`[shot] readback
  unavailable on this GPU`).
- `TTYD_EXIT_FRAMES` ends a run with no warp (no `TTYD_MAP`, `TTYD_EVENT`,
  `TTYD_LOAD_SAVE` or input script) that many frames from the start; a run with
  an input script ends at its `end`.

### What still needs a person

Offscreen captures show what the game drew, not what a player sees on the
screen: the window's own scaling and the system's colour handling are left
out, as are full screen, HDR output, how the game feels with a real controller,
and anything a display, a phone or a handheld does on its own. Sound is not
heard at all (the headless build's `TTYD_AUDIO_WAV` writes it out). Those stay
for a person on real hardware.
