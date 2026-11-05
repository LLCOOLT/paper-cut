# Leaderboards

Boss rush and Pit of 100 Trials times on the online relay
(docs/ONLINE_RELAY.md), each kept with the replay that made it
(docs/REPLAYS.md), and checked by playing that replay back. Modes tab >
**Leaderboards**.

## The boards

| board | the run | its clock |
| --- | --- | --- |
| Boss rush (mid-game) | the boss rush (docs/MODES.md) with the mid-game loadout | from the rush's start to the last boss beaten |
| Boss rush (late game) | the same, with the late-game loadout | the same |
| Pit of 100 Trials | the story's Pit, from where you are | from entering floor 1 to reaching floor 100 |

Each board is per release (its disc's ID) and Paper Cut version, since a
replay plays out the same only on the version that made it; the EU release
played at 50 Hz has boards of its own. A board keeps its 10 fastest runs, one
per player (a faster one of yours replaces your old one). Times are the
game's frames, shown at 60 a second (50 at 50 Hz): the same run is the same
time on every computer, whatever its speed.

## Record a run

Pick the board and **Restart and record a run** (desktop, with a file in
play). The game starts again from where you are (quick resume's last save),
recording a replay as the Practice tab's Replays do:

- a boss rush starts by itself on the first frame in the field, with the
  board's loadout (level, HP, FP, BP, partners, items and Star Powers; the
  badges are your file's own);
- a Pit run's clock starts as you enter floor 1 (go down the pipe in
  Rogueport's underground).

A label under the replay's shows the board and the clock. When the run ends
(the last boss falls; floor 100) the recording is saved to the replays
folder with the run's time; a run that ends early (a Game Over, a boss rush
stopped, leaving the Pit) is saved as an ordinary replay and can't go on the
board.

## Check and upload

**Your runs** lists this version's finished runs for the board. **Check and
upload** starts the game again playing the run's replay. If the run ends on
the same frame it did when recorded, its replay (compressed, without the ghost
track) goes to the board with your name (Online tab > Your name), and the
label says where it placed. A run that doesn't play out the same here isn't
sent. Esc or B leaves the playback, as for any replay.

## Watch, and the checks

**Watch** downloads an entry's replay to `<user>/replays/boards/` and plays
it. As the run ends, this game tells the relay whether it took the same
frames here; the board shows how many games checked each run ("checked by
3"), and how many saw it differ. Each game counts once per run, and never for
its own. What this device checked is kept in `<user>/replays/checked.txt`.

The relay can't play a replay, so it can't refuse a made-up time by itself:
the checks are what tell a real run from one that isn't. A run nobody could
check, or that differed for others, is one to doubt.

## What's off

Leaderboard runs are replays, so the same holds: not during online co-op, not
on phones and tablets (they can't start themselves again; the boards can
still be looked at), and save states, warps and the Practice tab's edits are
off while recording. The run's clock only reads the game (the sequence, the
map, the Pit's floor, the boss rush's state); the boss rush's start is the one
thing a run changes, asked for at start-up in the recording and its playback
alike, so it happens on the same frame in both.

A playback's boss rush doesn't count as one of your bests on the Modes tab.

## On the relay

`port/relay/src/index.js`, the `Board` Durable Object, one per board, release
and version (`board=bossrush-mid|bossrush-late|pit100`, with `-50` at 50 Hz;
`game=`, `v=`):

    GET  /board?...                 board <N>
                                    <ID> <FRAMES> <CHECKS> <FAILS> <DATE> <NAME, percent-encoded>
    POST /board?...&frames=&name=   (body: the replay, zlib)  -> ok <ID> <RANK>
    GET  /replay?...&id=<ID>        the replay (zlib)
    POST /check?...&id=<ID>         <FRAMES its playback took> -> ok <CHECKS> <FAILS>

Limits: 1 MB a replay, 5 uploads and 50 checks an address a day. A refusal
is a status other than 200 with `<why>: <words>` (`slower`, `today`, `big`,
`gone`, `bad`).
