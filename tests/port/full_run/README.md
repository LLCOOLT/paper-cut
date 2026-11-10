# The full run

One continuous playthrough, from a new file to the credits, replayed in fast
mode as a regression test: one save carried through the whole game, so it
catches what crosses chapters (saves, partners, items, flags, money, the
story's state). `tools/port/full_run.py` runs it.

## Segments

The run is cut into segments, `NN_name.txt` here, played in order (`NNa_` to
`NNo_` fit between `NN_` and the next number). Each is an
input script (`TTYD_INPUT`, the commands are listed at the top of
`port/src/common/input_pc.c`) that

- starts from a save: segment 00 from no save at all (a new file, from the
  title); every other one from the save the segment before it ended with,
  loaded at the title by the save warp (`TTYD_LOAD_SAVE`), so its first step
  is `wait_seq 2` (the field);
- ends with `save` (the game's own save, as a Save Block's, here and now) and
  `end`.

Its first lines say where it starts and where it must end:

    #start map=gor_00 story=3 party=1
    #end story=7 map=gor_01

`#end` is checked on every run (the story value and map of the final save).

Every segment plays with the story test hooks (`port/include/port/story_test.h`)
`TTYD_AUTO_BATTLE=1 TTYD_GOD=1 TTYD_AUTO_TEXT=1 TTYD_STORY_WATCH=120`: fights
are played and won, messages read. A segment's `#env` line adds variables or
switches one off for it (`#env TTYD_GOD=0 TTYD_AUTO_CHOICE=*:1`); the set it
played with is recorded with its expected values.
`#start` describes the start save, so one can be generated for the segment
without playing everything before it (below).

## Running

    full_run.py                      # every segment, in order, each from the last one's save
    full_run.py --from 4 --to 6      # segments 4-6; 4 from its checkpoint
    full_run.py --only 5             # one segment, from its checkpoint
    full_run.py --only 5 --script my_try.txt   # another script in its place (while writing one)
    full_run.py --record             # after a deliberate change: new expected values and checkpoints
    full_run.py --gen 5              # generate segment 5's start save from its #start line
    full_run.py --backend aurora     # off screen with screenshots; headless (default) is about 25x faster
    full_run.py --pm64               # the PM64 chain (below)
    full_run.py --pm64 --gen gaps    # generate the start saves of the segments the chain doesn't reach
    full_run.py --list [--pm64]      # the segments, numbered

Side by side, each segment from its checkpoint (the quick check, for every
test round): `full_run_parallel.py --set both --jobs 8` runs the TTYD set
(headless) and the PM64 set together and writes `summary.md` with each
segment's time and the wall time. The same segments are test pass scenarios
too, groups `fullrun` and `pm64run` (`tools/port/gen_fullrun_scenarios.py`
writes them; run it again after changing a segment).

## The PM64 chain

`--pm64` plays the same chain with Paper Mario 64's content on (the tester's
own ROM, `$PAPERCUT_PM64_ROM`, read where it is; the partners joined by beating
their bosses, the bosses in Rogueport), on the Aurora build off screen (the
PM64 bosses are Aurora's). Its segments are the TTYD ones and, between them,
`pm64/NNp_name.txt` (after `NN_`): a boss fight, the partner's join (the log,
the notice, out in the host's place, the Party tab) and their first battle.
Its expected values and recorded checkpoints are its own
(`expected_<region>_pm64.json`, `checkpoints/<region>_pm64/`); without them a
TTYD segment starts from the TTYD set's checkpoint, and `NNp_` from the next
TTYD segment's (where `NN_` ends). `pm64/9Np_*` (written by
`tools/port/gen_pm64_run_segments.py`) are the later bosses: the TTYD chain
doesn't reach them yet, so each is a `#gap` from a generated start. Such a
side trip ends elsewhere (Rogueport), so the chain goes on past it from the save
before it: `91_` after `90p_` starts where `90_` ended.

More lines a segment can have: `#setting KEY=VALUE ...`, settings.txt lines for its run (an Enhancement a
player can turn on: `#setting fast_travel=1`); `#must REGEX` / `#must_not REGEX`, what its log
must (not) say (`#must_pm64`: checked in the PM64 set's run of a TTYD segment
only); `#gap WHY`, a segment the chain doesn't reach, started from its own
checkpoint even in a chained run.

Each segment runs in a fresh user folder in fast mode (`TTYD_FAST`), off
screen, never taking the focus. The output folder (default
`build-full-run/<region>-<backend>/`) gets one folder per segment (its log,
screenshots, the fingerprint's last frames, its end save) and `summary.md`.

A segment passes when the game exits cleanly at its script's end (no crash, no
stall: `TTYD_WATCHDOG`; no wait or walk that ran out) and its final save is at
its `#end`. Against `expected_<region>.json` the frame count and the
game-state fingerprint at its end are compared too: a difference is DRIFT, the
game played differently (look at it, or `--record` after a deliberate change).

## Checkpoints

`checkpoints/<region>/NN_name.sav` is segment NN's start save, recorded from a
full run (`--record`); `NN_name.gen.sav` is one generated from its `#start`
line (`--gen`), used when there is no recorded one. Either is one card slot's
save: the player's state only, no banner, icon or anything else from the disc.

`#start` keys for `--gen`: `map` (`map` or `map,entrance`), `story`, `party`
(partner ids, the first one out), `level`, `hp`, `fp` (maximums),
`coins`, `sp`, `maxsp`, `stars` (special moves' mask), `items`, `flags`
(`ITEM_`/`GSWF_` names or numbers, comma-separated), `gsw` (`NAME:value,...`),
`badges` (`ITEM_` names, worn), `rank` (every partner's: 0 Normal, 1 Super,
2 Ultra). Put `maxsp` before `sp`: the keys are applied in order.
In `flags` and `gsw`, `@name` adds a bundle from `flag_bundles.txt`: what a
player has cleared long before (`@rogueport_open`: Gus and Zess T. out of
the way), which a generated save would not have.
