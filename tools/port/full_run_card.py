#!/usr/bin/env python3
"""A full-run segment's start card for a test pass scenario (tests/port/testpass/fullrun_*, pm64run_*:
tools/port/gen_fullrun_scenarios.py writes them; their "user_gen" runs this): the game's card file holding the
segment's checkpoint as file 1 (tools/port/full_run.py's checkpoint and gci_from_slot), written to DEST, which
the scenario names twice: the card in the user folder (the segment's own save writes there) and the save warp's
start ($TTYD_LOAD_SAVE). Segment 00 (a new file): an empty card.

usage: full_run_card.py DEST SEGMENT [--pm64] [--region us]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import full_run  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dest")
    ap.add_argument("segment", help="its name (02_frankly) or index")
    ap.add_argument("--pm64", action="store_true")
    ap.add_argument("--region", default="us", choices=sorted(full_run.REGIONS))
    a = ap.parse_args()
    full_run.PM64 = a.pm64
    segs = [s for s in full_run.segments() if a.segment in (s[1], str(s[0]))]
    if not segs:
        sys.exit(f"full_run_card: no segment {a.segment}")
    idx, name, script = segs[0]
    slot = None
    if idx > 0:
        ck = full_run.checkpoint(a.region, name, script)
        if ck is None:
            sys.exit(f"full_run_card: {name} has no checkpoint (full_run.py --record, or --gen)")
        with open(ck, "rb") as f:
            slot = f.read()
    os.makedirs(os.path.dirname(os.path.abspath(a.dest)), exist_ok=True)
    with open(a.dest, "wb") as f:
        f.write(full_run.gci_from_slot(slot, a.region))


if __name__ == "__main__":
    main()
