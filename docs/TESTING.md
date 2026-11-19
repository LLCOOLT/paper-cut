# Testing without a window: offscreen runs and the test pass

Paper Cut's own checks (the map sweep, the settings fuzzer, the story runs, the
card scripts: see [TECHNICAL.md](TECHNICAL.md), Testing) mostly run the headless
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
| `env` | the game's switches ([TECHNICAL.md](TECHNICAL.md)'s table); `null` removes a default, `{iso}` is the disc image, `{out}` the run's folder, `{data_root}` the extracted discs' folder (`--data-root`; the player's saves in `data/saves`) |
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
| `wide_check` | `{"shots": [...], "rows": 0.35, "diff": 28, "flat": 10, "bar": 0.015, "dark": 10}`: each capture (or those named) of a frame wider than 4:3 checked for something still drawn at 4:3: a straight edge where the 4:3 picture's side would be with a flat side to it (a bar, a fade or backdrop stopping there), or a black bar at the frame's side short of it (the black past a set's walls), down `rows` of the height; `testpass.py wide PNG...` prints its numbers |
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

**Checks every online scenario gets**, whatever it asks for: no crash, panic or
abort, no hang (the game's watchdog, every `wait_*` step's limit, the run's
timeout), clean exit codes, and no black or covered screen: each game logs
`[sync] screen: covered N frames (why)` while the field is hidden in the
field sequence (`portScreenLive`, `port/include/port/screen_live.h`: the 3D
camera off, the map not drawn, a fade out left covering it, the pipe's black,
the white-out, a fade that never ends), when the picture shows again, and
how it ends (`screen: at the end, ...`); covered for 30 seconds of game time,
or for 10 seconds when the game ends, fails (`"check": {"screen": false}`
turns it off for a scenario that ends on purpose in a fade). Error and
warning lines are counted in each game's result (`log_problems`, the first few
in `log_problem_lines`) without failing it alone. `"link": "real"` plays the
scenario on the real relay through `tools/port/relay_bridge.py`, only with
`testpass.py run --real-relay` (skipped otherwise), each with a hard budget of
the relay's requests (`--real-budget`, default 2,000).

### The online matrix

`tools/port/netmatrix.py` makes online scenarios from axes and plays them
through the runner above, so every one also gets those checks. The axes:

| Axis | Values |
|---|---|
| players | 2, 3, 4, or 2 and a watcher |
| host_as, guest_as | the host's and the others' play-as pick: Mario, Luigi, Peach, Bowser |
| pm64 | the Paper Mario 64 partners and looks off, the host's, the guests', both (`$PAPERCUT_PM64_ROM`; unset, skipped) |
| version | US, JP or EU for all; another release's disc (each pair, by relay and by address) must be refused on both |
| settings | one player's unlike the others': remake rules, restorations, difficulty, a randomizer seed, graphics |
| join | when the others come in: at once, from the title, in the field, the host mid-walk, in the Start menu, in a scene, in a battle, during a room change |
| action | what the host does then: walk, change rooms, a scripted battle, the enemy's first strike, the host's, a guest's (its own hammer), the Start menu, a save, the Pit |
| leave | stay, the host leaves, a guest leaves, a guest leaves and joins the same code again, a guest dropped without a word, a connection gone quiet and reset (reconnect by ticket), a half-open one that never closes |
| link | a room on the local relay, or by address |
| net | clean, 100 ms, 400 ms round trips with jitter, jitter alone, 1% and 10% loss, duplicates and reordering, loss bursts, a 5 s and a 30 s stall, a relay restart |
| mode | playing together (co-op) or each their own game |

The **story** tier (`--tier story`) has the host play a segment of the full run
from its checkpoint with the others along (`tests/port/full_run/`): the sewers'
pipe and goons, Blooper, Petalburg's shops, Hooktail, the Peach and Bowser
interludes, Punio and the Punies following (and battles with them), the Glitz
Pit, Yoshi, Ms. Mowz, Doopliss, Keelhaul Key and the Excess Express, over
players, picks, PM64, settings, leaving and the network.

Coverage: every pair of two axes' values is in at least one combination
(pairwise, greedy), and the risky groups in full: leaving x the network, the
two play-as picks, players x leaving, action x mode, the host's pick x the
action (story: segment x players). A combination that can't be played (a
stall or a restart by address, a segment joined mid-battle) is never made; a
pair it would have covered is covered by another. `netmatrix.py plan` lists
the combinations and the coverage (seed 51: 122 local combinations covering
all 1,807 tuples; 60 story combinations, all 700).

Every combination runs more than once, varying the start order, the join frame
and the fault seeds: `--repeat` (default 2) times, and `--repeat-risky`
(default 10) times when it has a battle, the Pit, a segment, a join in battle
or any leaving. **A pass rate under 100% is a failure**: a flake is a bug that
shows only sometimes. `netmatrix.py repeat SCENARIO -n 10` does the same for
the scenarios here (pass rates).

The network: the game's own `$TTYD_NET_FAULTS` (drops of the messages that
may be lost, duplicates, reorders, delays, disconnects, offline spells) by
address; through the relay, `sync_test_relay.py`'s `--net` shapes each
connection as a real link does (`tools/port/netshape.py`: latency, jitter,
TCP-like loss that holds everything behind it for a resend, bursts, stalls,
bandwidth; never a lost, duplicated or reordered frame, which the relay's
WebSocket never makes either), `--restart` cuts every connection and forgets
every room, `--halfopen` leaves one silent for good, `--zombie` has the relay
forget one but keep its socket, and `--kill` and `--quiet` drop one as before.
Each relay logs `counts` (the frames into it by role).

```bash
python tools/port/netmatrix.py plan                       # the combinations, nothing run
python tools/port/netmatrix.py run --run nm1              # the local tier (headless, 6 games at once)
python tools/port/netmatrix.py run --tier story --run st1 # the story tier
python tools/port/netmatrix.py run --plan nm1 --only l51-042 --repeat 10   # one combination of run nm1 again
python tools/port/netmatrix.py repeat mp_online_goons_leave3 -n 10
python tools/port/netmatrix.py real                       # the real relay: its cost, nothing run
python tools/port/netmatrix.py real --smoke --yes         # 2 players, a minute
```

`local-docs/netmatrix/<run>/report.txt` ranks the failing combinations (a
crash, a hang, a stuck screen, players apart, drift, a link, a log line), each
with its pass rate, the first reasons, its logs' folder and the command that
reruns it exactly. Set `PAPERCUT_PM64_ROM` to the tester's own ROM for the
PM64 axis.

**The real relay** (`netmatrix.py real`): a few chosen combinations (two
players walking, a battle with Bowser and Peach, three players with a
guest leaving, a rejoin; about 2,600 requests in all) through `relay_bridge.py`, at 60 frames a second as
players play. The bridge carries the headless games' stand-in frames to the
relay as WebSocket messages (unsealed: the relay never looks inside), rewrites
every room to `TEST` and four random characters with `&test=netmatrix` (never
listed, never a player's room), and counts what the Workers Free plan bills: a
connection about 4 requests, every 20 messages into the relay 1. It prints the
estimate first and refuses budgets adding up to more than 5% of the day's
100,000 requests; each scenario stops hard at its own budget (`--budget`,
default 1,200), every connection cut. Run it with `--yes`; the smoke run (2
players, a minute and a half) used 124 requests.

**Party games** (versus, raid, boss rush, quiz, cards, bingo, dungeon master)
and the Multiplayer page exist in the Aurora build only
(`port/src/aurora/*_aurora.c`; `port/src/headless/platform/headless.c` stubs
them: `portVersusOn` is 0, and a script's `together` step says "refused (the
Multiplayer page is Aurora's)"; `press_until_pick` waits on the page's pick,
which headless never makes), so
`mp_party_*` and `together_party_*` stay offscreen: run them with
`netmatrix.py repeat mp_party_versus2 ... -n 10` on a tree with an Aurora
build (`--backend offscreen` is theirs already).

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

### Online fuzzing

Online play takes messages from other programs, so every handler must come
through anything a broken or hostile peer could send: no crash, no hang, no
memory written out of place, and the session going on once it stops. Two
tools send it such messages on purpose, both with the headless build and the
local stand-in relay (`tools/port/sync_test_relay.py`, whose rooms are
unsealed, so the mutations reach the game's own messages):

- **In-process** (`port/src/common/statesync_fuzz_pc.inc`, headless only):
  `TTYD_NET_FUZZ=1` in a game playing online keeps a few of every message type
  it takes from the others, then from frame `TTYD_NET_FUZZ_FROM` (300) to
  `TTYD_NET_FUZZ_TO` hands `TTYD_NET_FUZZ_RATE` (4) mutants a frame straight to
  the handlers: truncated, grown, bits flipped, counts and indexes at edge
  values, floats NaN, infinite or huge, types changed, spliced, random, replayed,
  from the wrong player or no player, to a connection whose hello hasn't come.
  `TTYD_NET_FUZZ_LAYERS` picks the handlers (default `mrdtfs`): `m` the
  unreliable messages, `r` every reliable one, `d` the connection's own (ack,
  ping, the reliable wrapper), `t` the relay's words, `f` the relay's frames,
  `s` wrong seats, `p` before the handshake; `h` hellos, `k` the key's rounds
  and `w` raw bytes into the reader restart sessions, so they're off unless
  asked for. `TTYD_NET_FUZZ_SEED` seeds it. With `TTYD_NET_FUZZ_LAST=<file>`
  each mutant is written there first; `TTYD_NET_FUZZ_REPLAY=<file>` hands that
  one message on again, once, to reproduce a crash. `TTYD_NET_FUZZ_INJECT`
  spells out messages for a scenario (`LAYER:FROM:TYPE:SIZE@OFFSET=HEX...`,
  `;` between them; layer `m` or `r`, or `f` for a relay room frame of kind
  TYPE on the link to player FROM; `t:WORD` is a relay word as it is), handed
  on once at `TTYD_NET_FUZZ_FROM` (the `online_fuzz` group's scenarios), or,
  with `TTYD_NET_FUZZ_INJECT_AT=here`, the next time the relay says the other
  is in the room after that, before that connection's key agrees.
- **Hostile peer**: `sync_test_relay.py --hostile SEED:PERCENT[:ROLES[:UNTIL[:FROM]]]` passes
  the games' real traffic on with PERCENT of the messages to ROLES (`host`,
  `guest`, `peer0`, `peer1`; default all) mutated at every level (the relay
  message, its room frames, the game messages inside, inside the relay's own
  wrapping for a framed host), or replayed, duplicated, held back behind newer
  ones, plus junk relay words, empty and too-big messages. Each is logged in the
  relay's log. UNTIL and FROM (seconds from the relay's start) bound it: from
  after the keys are agreed (a key's round mutated is a wrong code) to a while
  before the end, so the games show they play together again.

`tools/port/net_fuzz.py` runs rounds of both on sync_soak.py's kinds (walking,
battles, three and four players, playing together), each with its own seeds,
rate and layers, and fails a round on a crash, an AddressSanitizer report, a
hang, or a game no longer linked after the fuzzing stopped:

```
python tools/port/net_fuzz.py --exe build-pc-us-headless/ttyd-us.exe \
    --iso orig/G8ME01/game.iso --out net_fuzz --hours 2 --parallel 2
```

A failed round's folder keeps its plan (the seeds), the logs, the relay's log
and the last mutant. For memory errors, run it on the sanitizer build (below,
"Sanitizer builds") with `--asan`, which puts the sanitizer's DLL on the path.

### Online soak

`tools/port/sync_longsoak.py` plays one long session: two to four headless
games in a room on the stand-in relay walk about, go through hei_03 and
hei_02's doors, open the Start menu, meet the rooms' enemies, and the guests leave the
room and join it again about every `--rejoin-minutes` (12) of game time, under
mild network faults (`--faults`; `--relay-faults` adds the relay's quiet
drops). Every `--every` seconds it samples each game's memory and its
`[sync] status:` line (linked, sent, got, resends, reconnects, unacknowledged),
and at the end fails on a crash, a hang, memory growing more than
`--max-mb-per-hour` per game-hour, messages per status line growing past 1.5x
from the first quarter to the last, a game linked less than 80% of the time,
or unacknowledged messages that never drain:

```
python tools/port/sync_longsoak.py --exe build-pc-us-headless/ttyd-us.exe \
    --iso orig/G8ME01/game.iso --out soak --players 4 --game-hours 2
```

`<out>/samples.jsonl` has every sample and `<out>/summary.json` the verdict.
A game's exit code 6 is a test step that failed, most often a battle the auto
battler couldn't end (`[story] STUCK` in its log, after 60 commands). A fight
nothing on Mario's side can hurt (Bald Clefts' Defence against a Jump and a
Headbonk) isn't one: after 16 commands running with nothing that hurts and no
foe's HP down, the auto battler runs away (`running away (nothing hurts the
foes)`). Games still running when the host's game ends are stopped by the soak,
and their exit code then counts for nothing.
### Sanitizer builds

A crash only a phone shows is often a memory error a computer gets away with. A
headless build with AddressSanitizer (and part of UBSan) finds those on a
computer. Clang's runtime is the dynamic one (`clang_rt.asan_dynamic`), and the
link needs the headless build's LTO off:

```bash
SAN="-fsanitize=address -fsanitize=bounds,vla-bound,bool,builtin,unreachable -fno-sanitize-recover=unreachable -fno-omit-frame-pointer -gline-tables-only"
CFLAGS="$SAN" CXXFLAGS="$SAN" LDFLAGS="-fsanitize=address -fsanitize=undefined" \
  cmake -S . -B build-pc-us-headless-asan -G Ninja --toolchain cmake/toolchains/windows-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=headless -DTTYD_HEADLESS_LTO=OFF
ninja -C build-pc-us-headless-asan
```

In such a build the game's own heaps are checked too (`port/include/port/asan_heap.h`).
The OS heaps, the map heap and its battle area, and the smart heap all carve
their blocks out of one host allocation, the arena. Their free space is poisoned,
and each block is unpoisoned as it is handed out. So a touch past a game block,
or of one already freed, stops the run with a report. Nothing changes in any
other build. To run the test pass on it: `TESTPASS_HEADLESS_SUFFIX=-asan`
(testpass then runs `build-pc-<version>-headless-asan`), with Clang's
`lib/clang/<n>/lib/windows` on `PATH` and `ASAN_OPTIONS=halt_on_error=1`.
Scenarios can add `"ERROR: AddressSanitizer"` and `"runtime error:"` to their
`log_must_not`.

Two test hooks reproduce how a phone runs:

- `TTYD_TEST_UNDRAWN=1` (headless) runs every other game frame undrawn, as the
  battery saver does. `TTYD_TEST_UNDRAWN=N` runs about one frame in N undrawn,
  never two in a row, as a phone's keep_game_speed catch-up does online. The
  rules match Aurora's: never in a shared battle, and a frame that brings a new
  draw pass is always drawn.
- `TTYD_STACK_REPORT=1` (Windows) prints the main thread's deepest stack use at
  a test's exit (`[stack] main thread: N KiB of 1024 KiB`). A phone gives the
  game about 1 MiB.

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

## The second screen on Android, in the emulator

`tools/port/android_dualscreen_test.py` checks the second screen's Start menu
([SECOND_SCREEN_MENU.md](SECOND_SCREEN_MENU.md)) on Android without a
dual-screen handheld: the Android emulator, headless (`-no-window`), with a
second display added and removed through its console (`adb emu multidisplay`).

```bash
python tools/port/android_dualscreen_test.py            # the US APK from port/android's build
python tools/port/android_dualscreen_test.py --apk app-us-release.apk --keep
```

- The SDK is `$ANDROID_HOME`, else `~/Android/sdk`; the emulator and an
  Android 14 Google APIs image (x86_64 on a PC, arm64 on Apple silicon) are
  installed with its `sdkmanager` when missing (only that needs a JDK; with
  `adb`, the emulator and the image already in the SDK it needs none). The AVD
  `papercut_thor` is written by the script itself (or again with
  `--recreate`): landscape 1920x1080 at 420 dpi, as the Thor's main screen; the
  second display is 1240x1080 at 320 dpi. The AVD grows to several GB once
  booted (its user data, the disc's copy); delete `~/.android/avd/papercut_thor.avd`
  and `papercut_thor.ini` when done and the script makes them again.
- The US APK (`--apk`, default `assembleUsRelease`'s, then
  `assembleUsDebug`'s) is installed and the disc (`--disc`, default
  `orig/G8ME01`'s image) copied once into the app's own files folder on the
  emulator, never anywhere else. `env.txt` there starts a new game on
  `gor_01` (`TTYD_MAP`, with `TTYD_EXIT_FRAMES` and `TTYD_WARP_LIMIT` raised so
  the warp neither exits after its 600 frames nor gives up during the
  emulator's slower boot) with `TTYD_FAST=1`, a fresh `TTYD_USER_DIR`,
  `TTYD_COMPANION_DUMP=1` and `TTYD_COMPANION_ANY_DISPLAY=1` (the emulator's
  display is not built in, which the app otherwise requires).
- Checks, one `PASS`/`FAIL` line each, exit 0 only when all pass:
  `presentation` (the second screen on the second display), `menu` (its
  picture is the Start menu's: the A button where the page draws it), `touch`
  (taps on its R and L buttons change the menu's tab, from the log's
  `[companion] menu input` lines), `game` (the main display's picture keeps
  changing), `rotation` (the main display upside down and back, through
  `cmd window fixed-to-user-rotation`, since the app is `sensorLandscape` and
  the emulator's sensor doesn't turn it: the app alive, the menu still there),
  `display2-gone` and `display2-back` (the second display removed: no crash;
  added back: the menu there again), `background` (Home and back: the second
  screen and the game again). Both displays' pictures and the log go to
  `--out` (`build-android-dualscreen-test/`): the main one by `screencap`, the
  added one by the emulator console's `screenrecord screenshot --display 1`
  (`screencap -d` can't capture a virtual display). The emulator puts a
  navigation bar under the added display's page; the button positions are
  taken from the page's own height.
- Run on Apple silicon (arm64 image, the game natively): all checks pass.
- The emulator needs CPU virtualization: on Windows, virtualization turned on
  in the firmware and the Windows Hypervisor Platform feature or the Android
  Emulator hypervisor driver. Without it the script stops at once (the
  emulator's `-accel-check`); software emulation stalls at boot.
- The emulator's GPU is the host's (gfxstream): speed, drivers and how the
  picture looks on a real panel say nothing about a device. What stays for a
  real dual-screen handheld: that its second display is reported as built in
  (`Display.getType()`), how its own launcher places the game on either
  screen, touch on its panel, and its performance.
