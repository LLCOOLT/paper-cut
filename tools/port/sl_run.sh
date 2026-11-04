#!/bin/bash
# One Super Luigi test run, by the house rules: TTYD_FAST=1, a fresh user folder (skip_intro=1,
# fullscreen=0), no focus, the disc image as the argument (without it the game waits at its launcher).
#   tools/port/sl_run.sh VERSION NAME SLTEST SCRIPT [SECONDS]    (run from the worktree's root)
# VERSION us|jp|eu; SLTEST campaign|continue|... ; captures and log.txt in $SL_RUNS/NAME (default
# build-sl-runs/NAME, git ignores build*). A fresh folder compiles its pipelines: the first few
# minutes run slowly, so allow 300 s or more. The run ends at SECONDS: the game doesn't exit by itself.
# Checkpoints (port/src/common/checkpoint_pc.c): the run saves one at every room change and at each
# `checkpoint NAME` step, into $SL_RUNS/NAME/checkpoints (room_NNN_<map>.ckpt, NAME.ckpt).
# SL_FROM starts the run from one instead of from SLTEST (which is then ignored; pass any word):
#   SL_FROM=build-sl-runs/full/checkpoints/room_007_jin_61.ckpt   one file
#   SL_FROM=full                                                 that run's newest checkpoint
#   SL_FROM=full:room_007                                        that run's checkpoint of that name
# SL_AT=LABEL: on from that label of SCRIPT instead of the checkpoint's own step (a rewritten script).
ROOT=$(pwd)
V=$1; N=$2; T=$3; I=$4; SECS=${5:-300}
RUNS="${SL_RUNS:-$ROOT/build-sl-runs}"
R="$RUNS/$N"
FROM=
if [ -n "$SL_FROM" ]; then
    case "$SL_FROM" in
        *.ckpt) FROM="$SL_FROM";;
        *:*) FROM=$(ls "$RUNS/${SL_FROM%%:*}/checkpoints/${SL_FROM#*:}"*.ckpt 2>/dev/null | head -1);;
        *) FROM=$(ls -t "$RUNS/$SL_FROM/checkpoints/"*.ckpt 2>/dev/null | head -1);;
    esac
    [ -f "$FROM" ] || { echo "sl_run: no checkpoint for SL_FROM=$SL_FROM"; exit 2; }
    # (copied out first: it may be in the folder this run clears)
    mkdir -p "$RUNS"
    cp "$FROM" "$RUNS/.from_$N.ckpt"
    FROM="$RUNS/.from_$N.ckpt"
fi
rm -rf "$R"; mkdir -p "$R/user" "$R/shots"
printf 'skip_intro=1
fullscreen=0
' > "$R/user/settings.txt"
# SL_SETTINGS: more lines for the settings file, e.g. aspect_ratio=3 (16:10) to see a wide picture
[ -n "$SL_SETTINGS" ] && printf '%s\n' "$SL_SETTINGS" >> "$R/user/settings.txt"
if [ -n "$FROM" ]; then
    mv "$FROM" "$R/from.ckpt"
    export TTYD_CHECKPOINT_LOAD="$R/from.ckpt"
    [ -n "$SL_AT" ] && export TTYD_CHECKPOINT_AT="$SL_AT"
    T=
    echo "sl_run: from $(grep -a -m1 '^name=' "$R/from.ckpt" | cut -d= -f2) ($(grep -a -m1 '^map=' "$R/from.ckpt" | cut -d= -f2))"
fi
export TTYD_CHECKPOINT_DIR="$R/checkpoints"
case $V in us) ID=G8ME01;; jp) ID=G8MJ01;; eu) ID=G8MP01;; esac
DISC=$(ls "$ROOT"/orig/$ID/*.iso 2>/dev/null | head -1)
[ -n "$DISC" ] || DISC=$(ls "$ROOT"/../ttyd-port/orig/$ID/*.iso | head -1) # a worktree with no junction to orig
case "$I" in /*|?:*) ;; *) I="$ROOT/$I";; esac
cd "${SL_BUILD:-$ROOT/build-pc-$V-aurora}" || exit 1 # SL_BUILD: another build folder
TTYD_FAST=1 TTYD_USER_DIR="$R/user" SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0 TTYD_SL_TEST="$T" TTYD_INPUT="$I"     TTYD_SHOT_DIR="$R/shots" timeout "$SECS" "./ttyd-$V.exe" "$DISC" > "$R/log.txt" 2>&1
echo "exit=$? shots=$(ls "$R/shots" | wc -l) checkpoints=$(ls "$R/checkpoints" 2>/dev/null | wc -l) log=$R/log.txt"
