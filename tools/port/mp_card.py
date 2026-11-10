#!/usr/bin/env python3
"""A test's start card, made at test time (testpass.py's user_gen): the game's card file with one save as file 1,
written to DEST (a .gci the save warp, $TTYD_LOAD_SAVE, loads) and, with --card, also where the game keeps its
card in the user folder (DEST's folder), so the game's own saves write beside it.

  mp_card.py DEST SAVE [--region us] [--card]
  mp_card.py DEST @CARD --slot N [--region us]     (file N+1 of a card already in the user folder: a save made by
                                                    an earlier scenario of the run, user_from)

SAVE is a checkpoint (tests/port/full_run/checkpoints/<region>/NN_name.sav: one slot's bytes) or a .gci; a path
is the repository's unless it starts with @ (the user folder's). The mp_ scenarios start from the full run's
checkpoints with it, so a chained multiplayer run starts where a played-through game is, not from a new file."""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import full_run  # noqa: E402  (its card layout: gci_from_slot, slot_of, card_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dest")
    ap.add_argument("save")
    ap.add_argument("--region", default="us", choices=sorted(full_run.REGIONS))
    ap.add_argument("--slot", type=int, default=None, help="the slot of a .gci to take (default: its first valid)")
    ap.add_argument("--card", action="store_true", help="also as the user folder's own card")
    a = ap.parse_args()
    user = os.path.dirname(os.path.abspath(a.dest))
    src = os.path.join(user, a.save[1:]) if a.save.startswith("@") else os.path.join(full_run.ROOT, a.save)
    data = open(src, "rb").read()
    if len(data) == full_run.SLOT:
        slot = data
    else:
        n = a.slot if a.slot is not None else 0
        slot = full_run.slot_of(data, n)
        if len(slot) != full_run.SLOT or slot[:2] == b"\x00\x01":
            sys.exit(f"{src}: slot {n} is empty or missing")
    card = full_run.gci_from_slot(slot, a.region)
    with open(a.dest, "wb") as f:
        f.write(card)
    if a.card:
        own = full_run.card_path(user, a.region)
        os.makedirs(os.path.dirname(own), exist_ok=True)
        with open(own, "wb") as f:
            f.write(card)
    print(f"{a.dest}: {os.path.basename(src)} as file 1")


if __name__ == "__main__":
    main()
