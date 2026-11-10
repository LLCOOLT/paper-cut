#!/usr/bin/env python3
"""The test pass's runner: named scenarios played on a build with no window on the screen.

A scenario (tests/port/testpass/*.json, or .toml) names a start (a map, a story row, a save),
an input script, settings, captures and what its log must or must not say. Each run of it is
a fresh user folder, TTYD_FAST=1, the game slot (game_slot.sh, when it is there), and either
the headless build or the Aurora build with TTYD_OFFSCREEN=1 (its window never shown). Its
log, captures, fingerprint and exit state land in local-docs/testpass/<run>/<scenario>/<version>/;
captures and fingerprints can then be compared with another run's (a reference).

  testpass.py list
  testpass.py run [SCENARIO|GROUP ...] --run NAME [--versions us,jp,eu] [--tree DIR] [--ref RUN]
  testpass.py compare RUN --ref REFRUN [SCENARIO ...]
  testpass.py sheet RUN          (contact sheets of its captures)
  testpass.py summary RUN        (its report again, from every result in it)
  testpass.py lint [SCENARIO ...] (each as written: parses, known fields and versions, a positive check)

No false passes: a scenario file that doesn't parse, an unknown version, a run that selects nothing, a capture
asked for and not made, and a scenario with no positive check (something that shows the game got somewhere:
log_must, expect_shots, file_must, save_matches, fingerprint, stereo_check; online, linked or progress) all
fail. A run writes <run>/jobs.json first: summary fails every listed job with no result (not run, cut off, still
going) or with an older run's, and a testpass error in a job writes its own failing result.json. Compared with a
reference, a capture the reference hasn't fails ("no reference") unless the scenario is "new": true; a scenario
(or version) the reference never ran is a skip, counted apart ("N of M compared"), and a compare of nothing fails.
A set $PAPERCUT_PM64_ROM or $TTYD_REF041_TREE that isn't there fails (unset, a skip); --no-skips fails on any skip.

Scenario fields (all optional but one start; see docs/TESTING.md):
  desc        what it shows
  groups      names it is run under ("baseline", "fingerprint", ...)
  versions    the versions it applies to (default us, jp, eu)
  backend     "offscreen" (default) or "headless"
  build       a build of its own: "touch", the Aurora build configured with -DTTYD_TOUCH_OVERLAY=ON in
              build-pc-<version>-aurora-touch (the on-screen controls; not in a desktop player's build); an
              online player's own "build" alike (a handheld's game in a desktop's room)
  env         environment for the game (TTYD_MAP, TTYD_EVENT, TTYD_EXIT_FRAMES, ...); null removes
              a house default, {iso} is the disc image, {out} the run's folder, {repo} this checkout,
              {pm64rom} the tester's own Paper Mario ROM ($PAPERCUT_PM64_ROM; a scenario that names it
              is skipped, not failed, when it is unset: docs/PM64_LINK.md; set to no file, it fails), {data_root} the extracted
              discs' folder (data/: the player's saves in data/saves; a worktree's falls back to the main
              checkout's; skipped, not failed, when no data/ has the files it names)
  disc        false: no disc on the command line (the launcher)
  user_files  files or folders copied into the fresh user folder (a memory card, saves; "SRC>DEST" to DEST in it); {version}
              is the run's version and {run} its name, and a folder ending in "/" has its contents
              copied (another run's user folder, kept with --keep-user: old data)
  user_gen    files made into the fresh user folder at test time: [{"script": "tools/port/x.py", "args": [...],
              "dest": "music/A.ogg"}], the script run as "script DEST args..." (generated, nothing committed)
  user_from   another scenario's name: its user folder from the same run (and version) instead of a
              fresh one, as a second launch finds it (its settings.txt kept: the house defaults only fill
              what it lacks); such scenarios run after the ones they start from, which a run of them
              alone brings along
  user_dir_slash  true: TTYD_USER_DIR given with a separator at its end (as a desktop install's SDL_GetPrefPath)
  tree        "ref041": played on the 0.4.1 reference checkout ($TTYD_REF041_TREE, else ttyd-port-ref041
              beside this one) instead of --tree; skipped, not failed, when that has no build (but failed when
              $TTYD_REF041_TREE is set: it was meant to run); an online player's "tree" alike
  portable    true: portable mode, as with portable.txt beside the program: the run's own folder stands for the
              program's ($TTYD_PORTABLE_BASE), no TTYD_USER_DIR or TTYD_CACHE_DIR; User and Cache must appear there
  texpack_from  a scenario of the same run that dumps textures (TTYD_DUMP_TEXTURES with TTYD_CACHE_DIR {out}/cache):
              a test pack made from its dumps' names (tools/port/gen_test_texpack.py, texpack_scale, default 8) goes
              into the user folder's textures; run after it, which a run of this one alone brings along
  library     versions whose discs are put in the launcher's library (library.txt): ["jp"]
  library_last  true: the library's first disc is also the one last played (last_disc.txt), the one "Skip the
              launcher" plays at start
  settings    settings.txt lines over the house defaults ({"widescreen": 1})
  input       an input script: a list of lines, or a file under the repo ("tests/port/input/x.txt")
  shots       frames to capture (TTYD_SHOT; the script's "shot NAME" lines capture too); on Aurora each one
              asked for (frame or script line) must be made, or the run fails
  expect_shots  capture names that must be made (any backend: launcher shots, a script's on a TTYD_MAP run)
  file_must, file_must_not  {"file under the run folder": [regular expressions]}: crash_test.txt, a dump; a
              file_must file must be there (with [], not empty; a folder, not empty)
  shots_partial  true: captures asked for a frame or two apart (a GIF's) or past where the run ends (a sweep):
              some are dropped (one asked for while another is in flight), so at least one must be made, not all
  shots_unchecked  true: the captures asked for are there to drive the renderer (a readback that is meant to
              fail, as with a full frame), so none has to be made; expect_shots still must be
  new         true: its captures have no reference yet (a compare passes them; else "no reference" fails)
  fingerprint true: TTYD_FINGERPRINT, compared line by line with the reference's
  clean_frame true: TTYD_CLEAN_FRAME=1, nothing over the picture a README shot doesn't ask for (notes, tips,
              toasts, What's new, the update notice, the timer and overlays, the scenes' black edges); `run
              --clean-frame` does it for every scenario, and `run --env K=V --set K=V` change any (media captures)
  timeout     seconds of play before the run is stopped (default 180)
  exit        the exit codes that pass (default [0]); "any" for a run stopped by its timeout
  log_must, log_must_not   regular expressions over the log (crash, panic, stall, a warp
              not reached, a stuck story and a battle with no music always fail, unless "allow_failures" lists them)
  flat_max    a share of the frame (0.2): a capture whose largest flat bright region (one colour, every
              channel 190 or more) covers more fails (a NaN polygon across the screen)
  stereo_check  {"layout": "sbs"|"sbs_half"|"tb"|"tb_half"|"red_cyan"|"green_magenta", "shots": [...],
              "depth": true, "min_moving": 0.15, "min_spread": 2, "near": [fx0, fy0, fx1, fy1], "far": [...],
              "direction": true, "hud": [[fx0, fy0, fx1, fy1]], "hud_shots": [...]}: each capture split into its two eyes and matched
              block by block (stereo_verdict): with depth, enough blocks shifted between the eyes, near and far
              apart, the near rectangle (default the bottom 15%) in front of the far one (default the top half:
              the eyes not swapped); "depth": false with "max_moving": 1.0 only proves a valid pair (the two
              eyes found where the layout puts them, matching block by block); "depth": false
              (3D depth 0) nearly none shifted; each "hud" rectangle (shares of an eye) at screen depth.
              "testpass.py stereo PNG LAYOUT" prints one capture's numbers
  tattle_check  {"min": 0.04, "max": 0.9, "count": 1}: with $TTYD_TATTLE_SHOT=1 each Tattle card is captured once
              it rests (its "[tattle] card: ... picture x0 y0 x1 y1" line); the share of its picture that isn't the
              card's paper (tattle_fill) must be in [min, max] (none: empty; all of it: garbage), and at least
              count cards must be captured
  edge_check  {"dir": "burst", "frames": 200, "rows": 8}: a burst of every frame shown (TTYD_USER_SHOT=@NAME:N with
              TTYD_USER_SHOT_INBETWEEN=1 and TTYD_USER_SHOT_FORMAT=bmp into {out}/<dir>) checked whole: at least frames
              of them, the picture centred, the bars around it the clear colour in every frame, and no frame with up
              to rows rows or columns clear inside the picture's edges that the others draw (bars that flash)
  compare     {"tolerance": 8, "threshold": 0.002}: a pixel differs when a channel is more
              than tolerance apart; a capture fails when more than threshold of its pixels do
  online      two to four games playing together (run_online below; docs/TESTING.md)
  save_matches  {"ref": "tests/port/full_run/checkpoints/{version}/02_frankly.sav", "ignore": [...]}: the run's
              last save, field by field, the same as the reference (tools/port/save_diff.py; play time and
              counters left out)
"""

import argparse
import concurrent.futures
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCENARIOS = os.path.join(REPO, "tests", "port", "testpass")
OUT_ROOT = os.path.join(REPO, "local-docs", "testpass")
# the shared game slots (a script that runs its arguments once a slot is free): $TTYD_GAME_SLOT, else a
# game_slot.sh beside this checkout's folder, else none
GAME_SLOT = os.environ.get("TTYD_GAME_SLOT", os.path.join(os.path.dirname(REPO), "game_slot.sh"))
VERSIONS = {"jp": "G8MJ01", "us": "G8ME01", "eu": "G8MP01"}
HOUSE_SETTINGS = {"skip_intro": 1, "fullscreen": 0, "discord_status": 0, "settings_hint_shown": 1}
# a fresh user folder's settings.txt stands for one this release wrote: its one-time upgrade steps' marks set
# (settings_aurora.c's portSettingsLoad: without wide_hud_050 a file reads as 0.4.x's, and wide_hud=0 in it, a
# scenario's own, would be turned on); a scenario that tests the step sets the mark to 0
HOUSE_SETTINGS_NEW = {"wide_hud_050": 1}
# lines that fail any run (map_sweep.py's signatures)
FAILURES = {
    "panic": r"^OSPanic ",
    "crash": r"\*\*\* crash",
    "stall": r"\*\*\* stall",
    "warp": r"^\[warp\] FAIL",
    "stuck": r"^\[story\] (STUCK|NO CONTROL)",
    "input": r"^\[input\] .*not reached",
    "window": r"^\[testpass\] visible window",
    # a device flow's expectation not met, or a motion not back to standing in time (port/src/common/flows_pc.c)
    "flows": r"^\[flows\] (FAIL|STUCK)",
    # a battle that started with no music playing (seq_battle.c's portMusicBattleCheck: it plays a fallback, but
    # the fight asked for a track the game hasn't, or none)
    "silent_battle": r"^\[music\] battle with no music",
}


# --- PNG (8-bit grey, RGB or RGBA, not interlaced: what the game and the launcher write) ---

def png_read(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, [], 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace or ctype not in (0, 2, 4, 6):
                raise ValueError(f"{path}: PNG kind not read (depth {depth}, colour {ctype})")
            bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    out = bytearray(h * w * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        o = y * w * 4
        for x in range(w):
            px = line[x * bpp:x * bpp + bpp]
            if bpp == 1:
                rgba = (px[0], px[0], px[0], 255)
            elif bpp == 2:
                rgba = (px[0], px[0], px[0], px[1])
            elif bpp == 3:
                rgba = (px[0], px[1], px[2], 255)
            else:
                rgba = tuple(px)
            out[o + x * 4:o + x * 4 + 4] = bytes(rgba)
    return w, h, out


def png_write(path, w, h, rgba):
    rows = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def image_diff(a_path, b_path, diff_path, tolerance=8, threshold=0.002):
    """Compare two captures. The diff image: the new capture dimmed, differing pixels red."""
    aw, ah, a = png_read(a_path)
    bw, bh, b = png_read(b_path)
    if (aw, ah) != (bw, bh):
        return {"ok": False, "why": f"size {bw}x{bh}, reference {aw}x{ah}"}
    n = aw * ah
    diff = bytearray(n * 4)
    bad = 0
    worst = 0
    for i in range(0, n * 4, 4):
        d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
        if d > worst:
            worst = d
        if d > tolerance:
            bad += 1
            diff[i:i + 4] = b"\xff\x00\x00\xff"
        else:
            g = (b[i] * 3 + b[i + 1] * 6 + b[i + 2]) // 30
            diff[i:i + 4] = bytes((g, g, g, 255))
    frac = bad / n
    res = {"ok": frac <= threshold, "differing": bad, "fraction": round(frac, 6), "max_channel": worst}
    if bad:
        png_write(diff_path, aw, ah, diff)
        res["diff"] = os.path.relpath(diff_path, OUT_ROOT)
    return res


# --- stereoscopic 3D: is there depth between the two eyes? ---
#
# A capture of a stereo layout is split into its eyes (side by side: left eye on the left; top and bottom:
# left eye on top; anaglyph: red-cyan's red channel is the left eye and green+blue the right, green-magenta's
# green the left and red+blue the right; stereo.cpp's compose shader). Each eye is sampled to about 320 x 180
# and cut into blocks; a block with texture is matched along x between the eyes (normalised cross-correlation),
# its disparity d = x in the right eye - x in the left, in sampled pixels: d > 0 is behind the screen, d < 0 in
# front of it, 0 at screen depth (the HUD, menus, and with "3D depth" at 0 everything).

STEREO_LAYOUTS = {"sbs": 1, "sbs_half": 2, "tb": 3, "tb_half": 4, "red_cyan": 5, "green_magenta": 6}


def stereo_eyes(path, layout):
    """The two eyes of a stereo capture as grey planes: (left, right, width, height), sampled."""
    w, h, px = png_read(path)
    mode = STEREO_LAYOUTS[layout]
    if mode == 1:  # each eye its own shape: half the width, the middle half of the height
        rects = ((0, h // 4, w // 2, h * 3 // 4), (w // 2, h // 4, w // 2 + w // 2, h * 3 // 4))
    elif mode == 2:
        rects = ((0, 0, w // 2, h), (w // 2, 0, w // 2 + w // 2, h))
    elif mode == 3:  # half the height, the middle half of the width
        rects = ((w // 4, 0, w * 3 // 4, h // 2), (w // 4, h // 2, w * 3 // 4, h // 2 + h // 2))
    elif mode == 4:
        rects = ((0, 0, w, h // 2), (0, h // 2, w, h // 2 + h // 2))
    else:
        rects = ((0, 0, w, h), (0, 0, w, h))
    x0, y0, x1, y1 = rects[0]
    ew, eh = x1 - x0, y1 - y0
    sx = max(1, ew // 320)
    sy = max(1, eh // 180)
    gw, gh = ew // sx, eh // sy

    def plane(rect, pick):
        rx, ry = rect[0], rect[1]
        out = [0.0] * (gw * gh)
        for gy in range(gh):
            row = ((ry + gy * sy) * w + rx) * 4
            for gx in range(gw):
                o = row + gx * sx * 4
                out[gy * gw + gx] = pick(px[o], px[o + 1], px[o + 2])
        return out

    if mode == 5:
        left = plane(rects[0], lambda r, g, b: float(r))
        right = plane(rects[1], lambda r, g, b: (g + b) * 0.5)
    elif mode == 6:
        left = plane(rects[0], lambda r, g, b: float(g))
        right = plane(rects[1], lambda r, g, b: (r + b) * 0.5)
    else:
        luma = lambda r, g, b: r * 0.299 + g * 0.587 + b * 0.114
        left = plane(rects[0], luma)
        right = plane(rects[1], luma)
    return left, right, gw, gh


def stereo_disparity(path, layout, block=(16, 12), search=20, min_std=6.0, min_ncc=0.7):
    """Each textured block's disparity between the eyes: a list of (fx, fy, d, ncc), fx/fy the block's centre as
    a share of the eye's width and height."""
    left, right, gw, gh = stereo_eyes(path, layout)
    bw, bh = block
    out = []
    for by in range(0, gh - bh + 1, bh):
        for bx in range(search, gw - bw - search + 1, bw):
            lv = [left[(by + y) * gw + bx + x] for y in range(bh) for x in range(bw)]
            n = len(lv)
            lm = sum(lv) / n
            lc = [v - lm for v in lv]
            ls = sum(v * v for v in lc)
            if (ls / n) ** 0.5 < min_std:
                continue
            best, bestd = -2.0, 0
            for d in range(-search, search + 1):
                rv = [right[(by + y) * gw + bx + d + x] for y in range(bh) for x in range(bw)]
                rm = sum(rv) / n
                num = 0.0
                rs = 0.0
                for i in range(n):
                    c = rv[i] - rm
                    num += lc[i] * c
                    rs += c * c
                if rs <= 0:
                    continue
                ncc = num / (ls * rs) ** 0.5
                if ncc > best or (ncc == best and abs(d) < abs(bestd)):
                    best, bestd = ncc, d
            if best >= min_ncc:
                out.append(((bx + bw / 2) / gw, (by + bh / 2) / gh, bestd, round(best, 3)))
    return out


def stereo_verdict(path, layout, check):
    """Checks one capture against a scenario's stereo_check; returns (summary, [failures])."""
    blocks = stereo_disparity(path, layout)
    why = []
    ds = sorted(b[2] for b in blocks)
    n = len(ds)
    summary = {"blocks": n}
    if n < 8:
        return summary, [f"only {n} textured blocks matched between the eyes (not a {layout} picture?)"]
    # (a shift of one sampled pixel is the eyes' own sampling: the compose's box filter puts a flat picture at 0 or
    # 1 by column; moving means 2 or more)
    moving = sum(1 for d in ds if abs(d) >= 2) / n
    p10, p50, p90 = ds[n // 10], ds[n // 2], ds[(n * 9) // 10]
    behind = sum(1 for d in ds if d > 0)
    front = sum(1 for d in ds if d < 0)
    summary.update({"moving": round(moving, 3), "p10": p10, "p50": p50, "p90": p90, "behind": behind,
                    "front": front})
    def median_in(rect):
        ins = sorted(b[2] for b in blocks if rect[0] <= b[0] <= rect[2] and rect[1] <= b[1] <= rect[3])
        return (ins[len(ins) // 2], len(ins)) if ins else (None, 0)
    if check.get("depth", True):
        if moving < check.get("min_moving", 0.15):
            why.append(f"{moving:.0%} of blocks shifted between the eyes (no depth)")
        if p90 - p10 < check.get("min_spread", 2):
            why.append(f"disparity spread {p10}..{p90}: one plane, no depth between near and far")
        # the right way round: what is near (by default the bottom of the picture, the floor at the camera's feet)
        # nearer than what is far (the top half), so the eyes aren't swapped
        near, nn = median_in(check.get("near", [0.0, 0.85, 1.0, 1.0]))
        far, nf = median_in(check.get("far", [0.0, 0.0, 1.0, 0.5]))
        summary.update({"near": near, "far": far})
        if check.get("direction", True):
            if not nn or not nf:
                why.append(f"no textured blocks to tell near ({nn}) from far ({nf})")
            elif near + check.get("min_gap", 1) > far:
                why.append(f"near median d {near} not in front of far median d {far}: the eyes swapped, or flat")
    else:
        if moving > check.get("max_moving", 0.05):
            why.append(f"{moving:.0%} of blocks shifted with no depth asked for")
    for rect in check.get("hud", []):  # fx0, fy0, fx1, fy1 in the eye: at screen depth
        inside = sorted((b for b in blocks if rect[0] <= b[0] <= rect[2] and rect[1] <= b[1] <= rect[3]),
                        key=lambda b: b[2])
        tol = check.get("hud_tolerance", 1)  # (a sampled pixel: the eyes' box filter and a block's edges)
        off = [b for b in inside if abs(b[2]) > tol]
        mid = inside[len(inside) // 2][2] if inside else None
        summary.setdefault("hud", []).append({"rect": rect, "blocks": len(inside), "off": len(off), "median": mid})
        if not inside:
            why.append(f"no textured block in the HUD's place {rect}")
        elif len(off) > len(inside) * check.get("hud_off_share", 0.1) or abs(mid) > tol:
            why.append(f"HUD {rect}: median d {mid}, {len(off)} of {len(inside)} blocks off screen depth "
                       f"(d {sorted({b[2] for b in off})})")
    return summary, why


def bloom_verdict(off_path, on_path, check):
    """bloom_check: the glow bloom adds (the capture with it on minus the one with it off, a frame or two apart) must
    lie on what glows (the off capture's bright parts), in each half of the frame: the shift that best lines the glow
    up with its source (cross-correlation, across and then down) must be about none. A glow drawn squeezed, flipped
    or offset (widescreen's squeeze left on its full-frame draw: a see-through copy of the bright things pulled
    towards the middle) lines up best shifted, the left half's one way and the right's the other. (Bright things
    with no glow, the HUD drawn after it, only lower every shift's score alike.) Returns (summary, [failures]); the
    shifts are shares of the frame's width and height."""
    w, h, off = png_read(off_path)
    w2, h2, on = png_read(on_path)
    if (w, h) != (w2, h2):
        return {}, [f"the captures differ in size ({w}x{h}, {w2}x{h2})"]
    level = check.get("level", 150)  # what glows: the off capture's luma above this
    step = check.get("step", 4)  # sampled every step pixels
    tol = check.get("max_shift", 0.01)
    reach = check.get("search", 0.15)  # the shifts tried, up to this share of the width / height
    gw, gh = w // step, h // step
    glow, src = [0.0] * (gw * gh), [0.0] * (gw * gh)
    for gy in range(gh):
        row = gy * step * w
        for gx in range(gw):
            o = (row + gx * step) * 4
            lo = (off[o] * 299 + off[o + 1] * 587 + off[o + 2] * 114) // 1000
            ln = (on[o] * 299 + on[o + 1] * 587 + on[o + 2] * 114) // 1000
            g = ln - lo
            glow[gy * gw + gx] = g if g > 2 else 0.0  # (a level or two: the captures' own noise)
            src[gy * gw + gx] = max(lo - level, 0)

    def best(x0, x1, axis):
        """the shift (in samples) that best lines up the glow in columns x0..x1 with the source"""
        lim = max(1, int((gw if axis == 0 else gh) * reach))
        scores = {}
        for d in range(-lim, lim + 1):
            num = 0.0
            for gy in range(gh):
                sy = gy - d if axis == 1 else gy
                if not 0 <= sy < gh:
                    continue
                for gx in range(x0, x1):
                    gv = glow[gy * gw + gx]
                    if gv:
                        sx = gx - d if axis == 0 else gx
                        if 0 <= sx < gw:
                            num += gv * src[sy * gw + sx]
            scores[d] = num
        top = max(scores.values())
        return min((d for d in scores if scores[d] == top), key=abs), top

    summary, why = {}, []
    for name, (x0, x1) in (("left", (0, gw // 2)), ("right", (gw // 2, gw))):
        gsum = sum(glow[gy * gw + gx] for gy in range(gh) for gx in range(x0, x1))
        ssum = sum(src[gy * gw + gx] for gy in range(gh) for gx in range(x0, x1))
        if ssum < check.get("min_source", 500):
            summary[name] = "nothing bright"
            continue
        if gsum < ssum * check.get("min_glow", 0.02):
            why.append(f"{name}: no glow (glow {gsum:.0f} for source {ssum:.0f})")
            continue
        dxs, _ = best(x0, x1, 0)
        dys, _ = best(x0, x1, 1)
        dx, dy = dxs * step / w, dys * step / h
        summary[name] = {"dx": round(dx, 4), "dy": round(dy, 4), "glow": round(gsum), "source": round(ssum)}
        if abs(dx) > tol or abs(dy) > tol:
            why.append(f"{name}: the glow lines up with its source {dx:+.3f} wide, {dy:+.3f} high off (over {tol}): "
                       "a shifted copy, not a glow around it")
    if all(v == "nothing bright" for v in summary.values()):
        why.append("nothing bright in either half to glow")
    return summary, why


def bmp_rows(path):
    """an uncompressed BMP (a burst's, TTYD_USER_SHOT_FORMAT=bmp): (w, h, bytes a pixel, rows top first)"""
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0] // 8
    stride = (w * bpp + 3) & ~3
    n = abs(h)
    return w, n, bpp, [d[off + (y if h < 0 else n - 1 - y) * stride:][:w * bpp] for y in range(n)]


def edge_verdict(burst_dir, check):
    """edge_check: every frame shown of a burst (TTYD_USER_SHOT_INBETWEEN=1, bmp) has the same picture rectangle,
    centred, with the bars around it the clear colour (black) in every frame, and no frame with rows or columns at
    the picture's edges cleared that others draw (a bar or line there that flashes). Returns (summary, [failures])."""
    files = sorted(glob.glob(os.path.join(burst_dir, "*.bmp")))
    why = []
    if len(files) < check.get("frames", 2):
        return {"frames": len(files)}, [f"edge_check: {len(files)} frames in {burst_dir}, {check.get('frames', 2)} wanted"]
    clear = bytes(check.get("clear", [0, 0, 0]))
    depth = check.get("rows", 8)
    rects = []
    for f in files:
        w, h, bpp, rows = bmp_rows(f)
        blank = bytes(clear[::-1]) * w if bpp == 3 else None

        def row_clear(y):
            r = rows[y]
            return r == blank if blank is not None else all(r[x:x + 3] == clear[::-1] for x in range(0, len(r), bpp))

        def col_clear(x, y0, y1):
            px = clear[::-1]
            return all(rows[y][x * bpp:x * bpp + 3] == px for y in range(y0, y1 + 1, 3))

        drawn = [y for y in range(h) if not row_clear(y)]
        if not drawn:
            why.append(f"{os.path.basename(f)}: all clear")
            continue
        t, b = drawn[0], drawn[-1]
        lc = [x for x in range(w) if not col_clear(x, t, b)]
        rects.append((os.path.basename(f), w, h, t, b, lc[0], lc[-1]))
    if not rects:
        return {"frames": len(files)}, why
    shapes = {r[3:] for r in rects}
    _, w, h, *_ = rects[0]
    t = min(r[3] for r in rects)
    b = max(r[4] for r in rects)
    left = min(r[5] for r in rects)
    right = max(r[6] for r in rects)
    summary = {"frames": len(rects), "size": [w, h], "picture": [left, t, right - left + 1, b - t + 1],
               "shapes": len(shapes)}
    # the picture centred in the window (the present viewport: bars equal within a pixel)
    if abs(t - (h - 1 - b)) > 1 or abs(left - (w - 1 - right)) > 1:
        why.append(f"edge_check: the picture {summary['picture']} is not centred in {w}x{h}")
    # every frame drawn to the same edges: a frame whose picture starts lower (rows clear at its top) has a bar
    # there the others haven't
    for name, _, _, rt, rb, rl, rr in rects:
        inset = [rt - t, b - rb, rl - left, right - rr]
        if any(0 < v <= depth for v in inset):
            why.append(f"edge_check: {name}: {inset} rows/columns clear inside the picture's edges (top, bottom, "
                       f"left, right) that other frames draw")
            if len(why) > 8:
                break
    return summary, why


def flat_bright_share(path, level=190, tolerance=6, step=4):
    """The share of a capture the largest flat, bright region covers (one colour, every channel at least level,
    neighbours within tolerance): a NaN polygon drawn across the screen ("white half"). Sampled every step pixels."""
    w, h, px = png_read(path)
    gw, gh = w // step, h // step
    col = [None] * (gw * gh)
    for gy in range(gh):
        for gx in range(gw):
            o = ((gy * step) * w + gx * step) * 4
            r, g, b = px[o], px[o + 1], px[o + 2]
            if min(r, g, b) >= level:
                col[gy * gw + gx] = (r, g, b)
    seen = bytearray(gw * gh)
    best = 0
    for start in range(gw * gh):
        if col[start] is None or seen[start]:
            continue
        seen[start] = 1
        stack, n = [start], 0
        while stack:
            i = stack.pop()
            n += 1
            c = col[i]
            x = i % gw
            for j in ((i - 1) if x > 0 else -1, (i + 1) if x < gw - 1 else -1, i - gw, i + gw):
                if 0 <= j < gw * gh and not seen[j] and col[j] is not None and \
                        max(abs(col[j][0] - c[0]), abs(col[j][1] - c[1]), abs(col[j][2] - c[2])) <= tolerance:
                    seen[j] = 1
                    stack.append(j)
        best = max(best, n)
    return best / float(gw * gh)


# the Tattle card's picture frame (w/<lang>/win.tpl's 0x37, 216 x 160): its drawn border on each side, and the soft
# paper edge inside it (port/src/aurora/tattle_view_aurora.c FRAME_*; the card's eight copies of the picture, the
# smallest 0.72 of it about the middle)
TATTLE_FRAME = (15, 16, 13, 9)
TATTLE_SOFT = 0.10


def tattle_fill(path, pic, tolerance=40):
    """The share of a Tattle card's picture (pic: x0, y0, x1, y1 on the card's 608 x 480 screen, as the game's
    "[tattle] card:" line gives it) that isn't the card's paper: the enemy drawn on it. The paper's colour is the
    picture's own inside edge (its commonest colour there); a pixel more than tolerance from it (summed over the
    channels) counts. None when the picture isn't inside the capture."""
    w, h, px = png_read(path)
    x0, y0, x1, y1 = pic
    sx, sy = w / 608.0, h / 480.0
    pw, ph = x1 - x0, y1 - y0
    l, t, r, b = TATTLE_FRAME
    ix0, ix1 = x0 + l + pw * TATTLE_SOFT / 2, x1 - r - pw * TATTLE_SOFT / 2
    iy0, iy1 = y0 + t + ph * TATTLE_SOFT / 2, y1 - b - ph * TATTLE_SOFT / 2
    X0, X1, Y0, Y1 = int(ix0 * sx), int(ix1 * sx), int(iy0 * sy), int(iy1 * sy)
    if X0 < 0 or Y0 < 0 or X1 > w or Y1 > h or X1 - X0 < 8 or Y1 - Y0 < 8:
        return None

    def at(x, y):
        o = (y * w + x) * 4
        return px[o], px[o + 1], px[o + 2]

    edge = {}
    for x in range(X0, X1):
        for y in (Y0, Y1 - 1):
            c = tuple(v >> 3 for v in at(x, y))
            edge[c] = edge.get(c, 0) + 1
    for y in range(Y0, Y1):
        for x in (X0, X1 - 1):
            c = tuple(v >> 3 for v in at(x, y))
            edge[c] = edge.get(c, 0) + 1
    paper = tuple((v << 3) + 4 for v in max(edge, key=edge.get))
    n = drawn = 0
    for y in range(Y0, Y1, 2):
        for x in range(X0, X1, 2):
            c = at(x, y)
            n += 1
            if abs(c[0] - paper[0]) + abs(c[1] - paper[1]) + abs(c[2] - paper[2]) > tolerance:
                drawn += 1
    return drawn / float(n)


def tattle_verdict(s, text, out):
    """tattle_check: each Tattle card the game captured ($TTYD_TATTLE_SHOT's "[tattle] card:" lines) holds a
    picture, neither empty nor all garbage: its drawn share in [min, max]; at least count cards (default 1).
    -> ({capture: share}, [why])"""
    tc = s["tattle_check"]
    lo, hi, want = tc.get("min", 0.04), tc.get("max", 0.9), tc.get("count", 1)
    got, why = {}, []
    for m in re.finditer(r"^\[tattle\] card: kind (\d+) shot (\S+) picture (-?\d+) (-?\d+) (-?\d+) (-?\d+)", text,
                         re.M):
        shots = glob.glob(os.path.join(out, "shots", m.group(2) + ".png")) + \
            glob.glob(os.path.join(out, "shots", "*__" + m.group(2) + ".png"))
        if not shots:
            why.append(f"tattle_check: card {m.group(2)} (kind {m.group(1)}) was not captured")
            continue
        share = tattle_fill(shots[0], tuple(int(m.group(i)) for i in range(3, 7)))
        got[m.group(2)] = None if share is None else round(share, 4)
        if share is None:
            why.append(f"tattle_check: {m.group(2)}: the picture is not inside the capture")
        elif not lo <= share <= hi:
            why.append(f"tattle_check: {m.group(2)} (kind {m.group(1)}): {share:.0%} of its picture drawn, not "
                       f"{lo:.0%} to {hi:.0%} ({'empty' if share < lo else 'garbage or overflowing'})")
    if len(got) < want:
        why.append(f"tattle_check: {len(got)} Tattle card(s) captured, {want} wanted")
    return got, why


# --- scenarios ---

def load_scenario(path):
    if path.endswith(".toml"):
        import tomllib
        with open(path, "rb") as f:
            s = tomllib.load(f)
    else:
        with open(path, encoding="utf-8") as f:
            s = json.load(f)
    s.setdefault("name", os.path.splitext(os.path.basename(path))[0])
    s["path"] = path
    return s


# files that failed to parse: {path: error}. Each is a FAIL row of every run and lint (never left out quietly:
# a broken file in a group dropped from its report, and the run passed without it)
BROKEN = {}


def all_scenarios():
    paths = sorted(glob.glob(os.path.join(SCENARIOS, "*.json")) + glob.glob(os.path.join(SCENARIOS, "*.toml")))
    out = []
    for p in paths:
        # one broken file (a regex's "\(" unescaped in JSON, say) fails as itself, not the whole run
        try:
            out.append(load_scenario(p))
        except (ValueError, OSError) as e:
            BROKEN[p] = str(e)
            print(f"testpass: {os.path.basename(p)} does not parse: {e}", file=sys.stderr)
    return out


def broken_rows():
    return [{"scenario": os.path.splitext(os.path.basename(p))[0], "version": "-", "ok": False,
             "why": f"the scenario file does not parse: {e}"} for p, e in sorted(BROKEN.items())]


# --- what a scenario must have: lint (testpass.py lint) and every run's verdict ---

FIELDS = {"name", "path", "desc", "groups", "versions", "backend", "build", "env", "disc", "user_files", "user_from",
          "tree", "portable", "texpack_from", "texpack_scale", "library", "settings", "input", "shots", "ui_shots",
          "fingerprint", "timeout", "exit", "log_must", "log_must_not", "allow_failures", "flat_max", "compare",
          "online", "save_matches", "expect_shots", "file_must", "file_must_not", "new", "shots_partial",
          "shots_unchecked", "stereo_check", "user_gen", "user_dir_slash", "tattle_check", "library_last",
          "expected_fail", "clean_frame", "bloom_check", "edge_check"}
ONLINE_FIELDS = {"link", "relay_args", "faults", "seed", "pace", "code", "players", "check"}
PLAYER_FIELDS = {"role", "env", "input", "settings", "version", "backend", "tree", "link", "checked", "log_must",
                 "log_must_not", "exit", "allow_failures", "expect_shots", "user_files", "start_delay", "disc",
                 "faults", "ui_shots", "shots", "build", "user_gen"}
CHECK_FIELDS = {"linked", "drift", "progress", "names", "battle_ended", "same_hits", "same_where"}


def script_lines(script):
    """an input script's lines (a list, or a file under the repo); [] when it can't be read"""
    if script is None:
        return []
    if isinstance(script, list):
        return [str(x) for x in script]
    try:
        return open(os.path.join(REPO, script), encoding="utf-8").read().splitlines()
    except OSError:
        return []


def script_shots(script):
    """the names an input script's "shot NAME" lines capture (not "companion shot": the second screen's)"""
    return [m.group(1) for ln in script_lines(script) for m in [re.match(r"\s*shot\s+(\S+)", ln)] if m]


def positive_checks(s):
    """what in a scenario shows the game got somewhere (not only that nothing went wrong): a run with none passes
    when the game never started (a missing DLL, an instant exit with no crash line)"""
    got = []
    if "online" in s:
        o = s["online"]
        chk = dict(linked=True, progress=True, battle_ended=False)
        chk.update(o.get("check", {}))
        got += [k for k in ("linked", "progress", "battle_ended") if chk.get(k)]
        if s.get("log_must") or any(pl.get("log_must") for pl in o.get("players", [])):
            got.append("log_must")
        if any(pl.get("expect_shots") for pl in o.get("players", [])):
            got.append("expect_shots")
        return got
    for k in ("log_must", "expect_shots", "save_matches", "fingerprint", "file_must", "stereo_check", "tattle_check",
              "bloom_check", "edge_check"):
        if s.get(k):
            got.append(k)
    return got


def lint_scenario(s, by_name):
    """problems with a scenario as written: [text]"""
    bad = []
    unknown = set(s) - FIELDS
    if unknown:
        bad.append(f"unknown fields {sorted(unknown)} (a typo is a check never made)")
    for v in s.get("versions", []):
        if v not in VERSIONS:
            bad.append(f"unknown version {v!r} (one of {', '.join(VERSIONS)})")
    if not positive_checks(s):
        bad.append("no positive check: add log_must, expect_shots, file_must, save_matches or fingerprint "
                   "(online: keep linked or progress, or a player's log_must)")
    if s.get("exit") == "any" and not positive_checks(s):
        bad.append('exit "any" with no positive check passes a game that never started')
    if "expected_fail" in s:
        xf = s["expected_fail"]
        if not isinstance(xf, dict) or not xf.get("by") or not xf.get("why"):
            bad.append('expected_fail is {"by": "<the branch that fixes it>", "why": "<the bug>", "match": [regex]}')
        elif not isinstance(xf.get("match"), list) or not xf["match"]:
            # (with none, any failure was XFAIL: a crash, a timeout, a testpass error)
            bad.append('expected_fail needs "match": [regex, ...], the reasons the known bug fails it with')
        else:
            for rx in xf.get("match", []):
                try:
                    re.compile(rx)
                except re.error as e:
                    bad.append(f"expected_fail.match: /{rx}/ is no regular expression: {e}")
    for k in ("user_from", "texpack_from"):
        if s.get(k) and s[k] not in by_name:
            bad.append(f"{k} {s[k]!r} is no scenario")
    for f in s.get("shots") or []:
        if not isinstance(f, int):
            bad.append(f"shots: {f!r} is not a frame number")
    for k in ("log_must", "log_must_not"):
        for rx in s.get(k, []):
            try:
                re.compile(rx)
            except re.error as e:
                bad.append(f"{k}: /{rx}/ is no regular expression: {e}")
    for k in ("file_must", "file_must_not"):
        if k in s and not isinstance(s[k], dict):
            bad.append(f'{k} is {{"file under the run folder": [regular expressions]}}')
    if "online" in s:
        o = s["online"]
        if set(o) - ONLINE_FIELDS:
            bad.append(f"online: unknown fields {sorted(set(o) - ONLINE_FIELDS)}")
        if set(o.get("check", {})) - CHECK_FIELDS:
            bad.append(f"online.check: unknown fields {sorted(set(o['check']) - CHECK_FIELDS)}")
        for i, pl in enumerate(o.get("players", [])):
            if set(pl) - PLAYER_FIELDS:
                bad.append(f"online player {pl.get('role', i)}: unknown fields {sorted(set(pl) - PLAYER_FIELDS)}")
            if pl.get("version") and pl["version"] not in VERSIONS:
                bad.append(f"online player {pl.get('role', i)}: unknown version {pl['version']!r}")
        bad += online_timing_problems(o)
    bad += menu_at_problems(s)
    bad += headless_brothers_problems(s)
    return bad


BROTHERS_IN_BATTLE = re.compile(r"Luigi in the battle|brothers.{0,4}after the battle|Bros\.|bros_")


def headless_brothers_problems(s):
    """Brothers mode's Luigi joins a battle in the Aurora build only (port/src/aurora/brothers_aurora.c): a
    headless game with Brothers mode on fights without him. A scenario checking him in a battle (his log line,
    a Bros. move: TTYD_AUTO_DUO, a [duo] line) must run that game offscreen, else it fails on any run without
    --backend offscreen (pt1: online_brothers2_duo passed under --backend offscreen only)."""
    env0 = s.get("env") or {}
    games = []
    if "online" in s:
        for i, pl in enumerate(s["online"].get("players", [])):
            env = dict(env0, **(pl.get("env") or {}))
            games.append((f"online player {pl.get('role', i)}", env, pl.get("backend", s.get("backend", "headless")),
                          list(s.get("log_must", [])) + list(pl.get("log_must", []))))
    else:
        games.append(("the game", env0, s.get("backend", "offscreen"), list(s.get("log_must", []))))
    bad = []
    for who, env, backend, must in games:
        if backend != "headless" or str(env.get("TTYD_SET_brothers_mode", "0")) in ("0", ""):
            continue
        wants = [m for m in must if BROTHERS_IN_BATTLE.search(m) or "[duo]" in m.replace("\\", "")]
        if env.get("TTYD_AUTO_DUO") or wants:
            bad.append(f"{who}: Brothers mode headless, but it checks Luigi in a battle "
                       f"({'TTYD_AUTO_DUO' if env.get('TTYD_AUTO_DUO') else wants[0]}): headless battles have no "
                       "Luigi (brothers_aurora.c); give that game \"backend\": \"offscreen\"")
    return bad


MENU_AT_ROWS = 32  # statesync_pc.c's MENU_AT_ROWS


def menu_at_problems(s):
    """$TTYD_SYNC_MENU_AT runs its first MENU_AT_ROWS rows only (the rest logged as never run): a scenario
    listing more, checking only a later effect, would pass with a row never tried."""
    envs = [("", s.get("env") or {})]
    envs += [(f"online player {pl.get('role', i)}: ", pl.get("env") or {})
             for i, pl in enumerate((s.get("online") or {}).get("players", []))]
    bad = []
    for who, env in envs:
        v = env.get("TTYD_SYNC_MENU_AT") if isinstance(env, dict) else None
        rows = [r for r in str(v).split(",") if ":" in r] if v else []
        if len(rows) > MENU_AT_ROWS:
            bad.append(f"{who}TTYD_SYNC_MENU_AT has {len(rows)} rows: only the first {MENU_AT_ROWS} run")
    return bad


def online_timing_problems(o):
    """A shared start (battle, story) after fixed waits only, while another game's script is fixed waits only:
    both assume every game runs at the same rate. Under load one game's frames come slower and the other is
    gone (or not there yet) when it starts (t3: online_a_dm's guest left before the host's battle).
    The starter waits on the others first (wait_peer here/field), the idle ones on it (wait_peer battle)."""
    def steps(pl):
        inp = pl.get("input")
        return [str(x).split() for x in inp] if isinstance(inp, list) else None

    players = o.get("players", [])
    idle = [pl.get("role", str(i)) for i, pl in enumerate(players)
            if steps(pl) and all(st and st[0] == "wait" for st in steps(pl))]
    bad = []
    for i, pl in enumerate(players):
        for words in steps(pl) or []:
            if not words or words[0].startswith("wait_") or words[0].startswith("press_until"):
                break  # (waits on the others, or on its own game's state, before anything shared)
            if words[0] in ("battle", "story"):
                others = [r for r in idle if r != pl.get("role", str(i))]
                if others:
                    bad.append(f"online player {pl.get('role', i)}: '{' '.join(words)}' after fixed waits only, "
                               f"while {', '.join(others)} only wait fixed frames: wait_peer here/field first, "
                               "and the others wait_peer battle")
                break
    return bad


SOCKET_SEND = re.compile(r"(?<![\w.>])(?:::)?send\s*\(")


def sigpipe_rows():
    """Every socket send in port/ passes SEND_FLAGS or MSG_NOSIGNAL (port/include/port/online_sock.h): on Linux
    and Android a send to a peer that reset its end raises SIGPIPE, whose default ends the game. A plain
    send(..., 0) is allowed only as the #else of an #ifdef MSG_NOSIGNAL (Apple: SO_NOSIGPIPE instead)."""
    bad = []
    for path in glob.glob(os.path.join(REPO, "port", "**", "*"), recursive=True):
        if os.path.splitext(path)[1] not in (".c", ".cpp", ".h", ".inc", ".m", ".mm") or "relay" in path.split(os.sep):
            continue
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
        except OSError:
            continue
        for i, line in enumerate(lines):
            code = line.split("//")[0]
            m = SOCKET_SEND.search(code)
            if m is None or code.strip().startswith(("*", "/*")):
                continue
            call = " ".join(lines[i:i + 3])
            # (the socket call's four arguments: a method of that name, a declaration, isn't one)
            depth, commas, j = 0, 0, call.find("(", call.find("send"))
            while 0 <= j < len(call):
                ch = call[j]
                depth += ch in "([{"
                depth -= ch in ")]}"
                commas += ch == "," and depth == 1
                if depth == 0:
                    break
                j += 1
            if commas != 3 or re.search(r"\b(?:const\s+)?(?:void|char|uint8_t|std::string)\b[\s\*&]*\w+\s*,", call[:j]):
                continue
            before = "\n".join(lines[max(0, i - 6):i])
            if "SEND_FLAGS" in call or "MSG_NOSIGNAL" in call or ("#else" in before and "MSG_NOSIGNAL" in before):
                continue
            bad.append(f"{os.path.relpath(path, REPO)}:{i + 1}: a send without SEND_FLAGS (SIGPIPE on Linux)")
    return [{"scenario": "source: socket sends", "version": "-", "ok": not bad, **({"why": bad} if bad else {})}]


THREAD_WAIT = re.compile(r"\bSDL_WaitThread\s*\(|\bpthread_join\s*\(|[.>]join\s*\(\s*\)|\bWaitForSingleObject\s*\(")
# the waits allowed, by file: none of them on a thread that does network I/O
THREAD_WAIT_OK = {
    "port/src/aurora/photo_gallery_aurora.c": "the photo loader reads files, and stops between them",
    "port/src/common/crash_pc.c": "the crash report's helper, waited for by the crashing thread",
    "port/settings/features/crowd_control.cpp": "the chat's thread joined from a detached helper, not the game",
}


def thread_wait_rows():
    """No thread that talks to the network is waited for: a connection's thread can be stuck for tens of
    seconds or minutes in a name's lookup, a connect or a send (the OS's timeouts), and a wait for it on the
    game's thread freezes the game (Leave froze while the relay's thread was still connecting). Such threads
    are detached, with state they share by reference (online_ws_pc.c's Link, ap_client's Link,
    port/include/port/detach.h); a wait anywhere else in port/ needs its reason in THREAD_WAIT_OK."""
    bad = []
    for path in glob.glob(os.path.join(REPO, "port", "**", "*"), recursive=True):
        if os.path.splitext(path)[1] not in (".c", ".cpp", ".h", ".inc", ".m", ".mm"):
            continue
        rel = os.path.relpath(path, REPO).replace(os.sep, "/")
        if rel.startswith("port/patches/") or rel in THREAD_WAIT_OK:
            continue
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
        except OSError:
            continue
        for i, line in enumerate(lines):
            code = line.split("//")[0]
            if code.strip().startswith(("*", "/*")) or not THREAD_WAIT.search(code):
                continue
            bad.append(f"{rel}:{i + 1}: a thread waited for (detach it, or say why in THREAD_WAIT_OK)")
    for rel in THREAD_WAIT_OK:
        if not os.path.exists(os.path.join(REPO, rel)):
            bad.append(f"THREAD_WAIT_OK names {rel}, which isn't there")
    return [{"scenario": "source: thread waits", "version": "-", "ok": not bad, **({"why": bad} if bad else {})}]


# calls allowed in one file only, and why
ONE_PLACE_CALLS = {
    "psa_crypto_init": ("port/src/common/online_seal_pc.c",
                        "PSA set up once under a lock (portOnlinePsaInit): this Mbed TLS has no "
                        "MBEDTLS_THREADING_C, and the relay's thread and the game's both use it"),
    "WinHttpWebSocketClose": ("port/archipelago/websocket.cpp",
                              "waits on the server's close: only the socket's last user, never a stop "
                              "on the game's thread (interrupt() there)"),
}


def one_place_rows():
    """Calls that are safe in one place only (ONE_PLACE_CALLS): a second psa_crypto_init could race the
    relay thread's in this unthreaded Mbed TLS, and a WebSocket close on the game's thread froze it."""
    bad = []
    calls = re.compile(r"\b(" + "|".join(ONE_PLACE_CALLS) + r")\s*\(")
    for path in glob.glob(os.path.join(REPO, "port", "**", "*"), recursive=True):
        if os.path.splitext(path)[1] not in (".c", ".cpp", ".h", ".inc", ".m", ".mm"):
            continue
        rel = os.path.relpath(path, REPO).replace(os.sep, "/")
        if rel.startswith("port/patches/"):
            continue
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
        except OSError:
            continue
        for i, line in enumerate(lines):
            code = line.split("//")[0]
            m = calls.search(code)
            if m is None or code.strip().startswith(("*", "/*")) or ONE_PLACE_CALLS[m.group(1)][0] == rel:
                continue
            bad.append(f"{rel}:{i + 1}: {m.group(1)} outside {ONE_PLACE_CALLS[m.group(1)][0]} "
                       f"({ONE_PLACE_CALLS[m.group(1)][1]})")
    return [{"scenario": "source: one-place calls", "version": "-", "ok": not bad, **({"why": bad} if bad else {})}]


STATIC_DECL = re.compile(r"^(\s*)static\s+(?:(?:const|volatile|unsigned|signed|struct|enum)\s+)*[A-Za-z_][\w:<>]*"
                         r"[\s*]+(?:const\s+)?\*?\s*([A-Za-z_]\w*)\s*[\[=;,]")


def static_shadow_problems():
    """the port's C sources: a static in a function with the name of a file-level static hides it, and the
    file's one is never written (quick resume's sidecar said "map=" for every save that way): [text]"""
    bad = []
    for top in ("port/src", "port/mods", "port/settings"):
        for d, _, files in os.walk(os.path.join(REPO, top)):
            for f in files:
                if not f.endswith((".c", ".cpp")):
                    continue
                p = os.path.join(d, f)
                try:
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        lines = fh.read().splitlines()
                except OSError:
                    continue
                file_level, inner = {}, []
                for i, line in enumerate(lines, 1):
                    m = STATIC_DECL.match(line)
                    if m:
                        if m.group(1):
                            inner.append((m.group(2), i))
                        else:
                            file_level.setdefault(m.group(2), i)
                for n, i in inner:
                    if n in file_level:
                        bad.append(f"{os.path.relpath(p, REPO)}:{i}: static {n} hides the file's at :{file_level[n]}")
    return bad


EXTRA_PARTY_TEST = re.compile(r"marioGetExtraPartyId\s*\(\s*\)\s*(?:>=|<=|==|!=|<|>)")
PICKER_FILE = re.compile(r"portShowOpenFileDialog\s*\(|SDL_EVENT_DROP_FILE")
PATH_INFO_SIZE = re.compile(r"SDL_GetPathInfo\s*\(\s*[^,]+,\s*&\s*(\w+)\s*\)")


def picked_size_problems():
    """the port's sources that take files from the system's picker or a drop: a picked file's size comes from
    portfile::pickedInfo / readPicked (port/settings/file_util.h), never SDL_GetPathInfo's, which can't see the
    content:// URIs Android's picker gives (every Saves > Import was refused there). A size read from
    SDL_GetPathInfo in such a file needs "not a picked path" on its line or the one above: [text]"""
    bad = []
    for top in ("port/src", "port/mods", "port/settings"):
        for d, _, files in os.walk(os.path.join(REPO, top)):
            for f in files:
                if not f.endswith((".c", ".cpp", ".inc")) or f == "file_links.cpp":
                    continue
                p = os.path.join(d, f)
                try:
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        lines = fh.read().splitlines()
                except OSError:
                    continue
                if not any(PICKER_FILE.search(line) for line in lines):
                    continue
                for i, line in enumerate(lines):
                    m = PATH_INFO_SIZE.search(line)
                    if not m or "not a picked path" in line or (i > 0 and "not a picked path" in lines[i - 1]):
                        continue
                    size = re.compile(r"\b" + re.escape(m.group(1)) + r"\.size\b")
                    if any(size.search(near) for near in lines[i:i + 4]):
                        bad.append(f"{os.path.relpath(p, REPO)}:{i + 1}: a size from SDL_GetPathInfo in a file that "
                                   "takes picked or dropped files: portfile::pickedInfo/readPicked (Android's "
                                   "content:// URIs)")
    return bad


NOTES_CPP = os.path.join(REPO, "port", "settings", "features", "notes.cpp")
NOTES_LISTS = os.path.join(REPO, "tools", "port", "notes_lists.txt")


def notes_list(source, name):
    """notes.cpp's kPhrases or kWords, as written: its string literals in order, comments left out"""
    m = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\n\};", source, re.S)
    if not m:
        return None
    body = "\n".join(line.split("//")[0] for line in m.group(1).splitlines())
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)


def notes_list_problems():
    """paper notes' phrases and words (port/settings/features/notes.cpp): the relay keeps a note as their two
    numbers, and older games skip numbers they don't know, so the lists only ever grow at the end. They must
    start with tools/port/notes_lists.txt ("phrase N <text>" / "word N <text>" lines, the lists as released;
    add new entries there too once they ship): none reordered, renamed or removed: [text]"""
    try:
        source = open(NOTES_CPP, encoding="utf-8").read()
        snapshot = open(NOTES_LISTS, encoding="utf-8").read().splitlines()
    except OSError as e:
        return [f"paper notes' lists: {e}"]
    bad = []
    lists = {"phrase": notes_list(source, "kPhrases"), "word": notes_list(source, "kWords")}
    for kind, got in lists.items():
        if got is None:
            bad.append(f"notes.cpp: no {'kPhrases' if kind == 'phrase' else 'kWords'} list found")
            continue
        want = []
        for line in snapshot:
            if line.startswith(kind + " "):
                _, n, text = line.split(" ", 2)
                if int(n) != len(want):
                    bad.append(f"notes_lists.txt: {kind} {n} out of order (expected {len(want)})")
                want.append(text)
        if len(got) < len(want):
            bad.append(f"notes.cpp: {len(got)} {kind}s, notes_lists.txt has {len(want)}: one was removed")
        for i, (w, g) in enumerate(zip(want, got)):
            if w != g:
                bad.append(f"notes.cpp: {kind} {i} is {g!r}, was {w!r}: only ever add at the end")
    return bad


RAW_OPEN_DIALOG = re.compile(r"\bSDL_ShowOpenFileDialog\s*\(")


def raw_open_dialog_problems():
    """the port's sources: a file picker is portShowOpenFileDialog (port/include/port/offscreen.h), never
    SDL_ShowOpenFileDialog: SDL has no dialog on iOS (it answers "unsupported" at once), and the wrapper shows
    the system's document picker there instead; "Link Paper Mario 64" did nothing on an iPhone: [text]"""
    bad = []
    for d, _, files in os.walk(os.path.join(REPO, "port")):
        for f in files:
            if not f.endswith((".c", ".cpp", ".inc", ".h", ".m", ".mm")) or f == "offscreen.h":
                continue
            p = os.path.join(d, f)
            try:
                with open(p, encoding="utf-8", errors="replace") as fh:
                    lines = fh.read().splitlines()
            except OSError:
                continue
            for i, line in enumerate(lines, 1):
                if RAW_OPEN_DIALOG.search(line.split("//")[0]):
                    bad.append(f"{os.path.relpath(p, REPO)}:{i}: SDL_ShowOpenFileDialog (nothing on iOS): "
                               "portShowOpenFileDialog")
    return bad


RAW_SAVE_FOLDER_DIALOG = re.compile(r"\b(SDL_ShowSaveFileDialog|SDL_ShowOpenFolderDialog|SDL_ShowFileDialogWithProperties)"
                                    r"\s*\(")
PORT_SAVE_DIALOG = re.compile(r"\bportShowSaveFileDialog\s*\(")


def raw_save_folder_dialog_problems():
    """the port's sources: a "save as" is portfile::exportFile (port/settings/file_export.h: written first, then
    where it goes asked; iOS's export picker needs the file before it asks) and a folder picker is
    portShowOpenFolderDialog (port/include/port/offscreen.h: iOS's document picker, the folder under
    security-scoped access until portReleasePickedFolder), never SDL's own dialogs: SDL has none on iOS (it
    answers "unsupported" at once), so Saves > Export, presets and "Add a folder..." did nothing on an iPhone;
    portShowSaveFileDialog itself only in file_export.h: [text]"""
    bad = []
    for d, _, files in os.walk(os.path.join(REPO, "port")):
        for f in files:
            if not f.endswith((".c", ".cpp", ".inc", ".h", ".m", ".mm")) or f == "offscreen.h":
                continue
            p = os.path.join(d, f)
            try:
                with open(p, encoding="utf-8", errors="replace") as fh:
                    lines = fh.read().splitlines()
            except OSError:
                continue
            for i, line in enumerate(lines, 1):
                code = line.split("//")[0]
                m = RAW_SAVE_FOLDER_DIALOG.search(code)
                if m:
                    bad.append(f"{os.path.relpath(p, REPO)}:{i}: {m.group(1)} (nothing on iOS): "
                               + ("portShowOpenFolderDialog" if "Folder" in m.group(1) else "portfile::exportFile"))
                elif PORT_SAVE_DIALOG.search(code) and f != "file_export.h":
                    bad.append(f"{os.path.relpath(p, REPO)}:{i}: portShowSaveFileDialog asks for a path before the "
                               "file is written (an error on iOS): portfile::exportFile")
    return bad


def follower_test_problems():
    """the port's sources: whether a story follower walks with Mario is portCoopStoryFollowerId() (port/players.h),
    never marioGetExtraPartyId() compared: co-op's player 3 companion is out in that same party slot, and every
    mode was refused as "someone following Mario" with three players on the couch: [text]"""
    bad = []
    for top in ("port/src", "port/mods", "port/settings"):
        for d, _, files in os.walk(os.path.join(REPO, top)):
            for f in files:
                if not f.endswith((".c", ".cpp", ".inc")) or f == "second_player_pc.c":
                    continue
                p = os.path.join(d, f)
                try:
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        lines = fh.read().splitlines()
                except OSError:
                    continue
                for i, line in enumerate(lines, 1):
                    if EXTRA_PARTY_TEST.search(line.split("//")[0]):
                        bad.append(f"{os.path.relpath(p, REPO)}:{i}: a follower test on marioGetExtraPartyId() "
                                   "(player 3's companion is there too): portCoopStoryFollowerId()")
    return bad


INPUT_PC = os.path.join(REPO, "port", "src", "common", "input_pc.c")
SCRIPT_CMD = re.compile(r'strcmp\(cmd, "([a-z_]+)"\)')
SCRIPT_CODE = re.compile(r"\bs->frames = (?:[^;]*\? )?(-\d+)(?: : (-\d+))?;")
# one code on purpose, told apart by the step's own fields: -13 sfx / maxbp / companion (its name); -5/-6 wait_seq /
# wait_map and press_until_seq / _map (the keys pressed meanwhile); -16 talk and talk_all / tattle_all (the name);
# -23 walk_to and fight (fight is walk_to npc into the nearest enemy: stickX 3, the same walker)
SCRIPT_CODE_SHARED = {"-13", "-5", "-6", "-16", "-23"}


def script_code_problems():
    """port/src/common/input_pc.c: each input-script command parses to a code of its own (s->frames = -N): two
    branches merged with the same new code made one command run as the other (int6: vfx's wait_vfx, rebind's
    wait_bindings and flows' dev all took -30, mpsuite's pad and flows' dev -32): [text]"""
    try:
        with open(INPUT_PC, encoding="utf-8", errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError as e:
        return [f"input_pc.c: {e}"]
    owner, bad, cmd = {}, [], None
    for i, line in enumerate(lines, 1):
        code = line.split("//")[0]
        m = SCRIPT_CMD.search(code)
        if m:
            cmd = m.group(1)
        m = SCRIPT_CODE.search(code)
        if not m or cmd is None:
            continue
        for c in (g for g in m.groups() if g):
            if c in SCRIPT_CODE_SHARED:
                continue
            if c in owner and owner[c][0] != cmd:
                bad.append(f"input_pc.c:{i}: {cmd} takes script code {c}, which {owner[c][0]} has "
                           f"(line {owner[c][1]}): give it a code no command uses")
            owner.setdefault(c, (cmd, i))
    return bad


def lint(names=()):
    every = all_scenarios()
    by_name = {s["name"]: s for s in every}
    rows = broken_rows() + ([] if names else sigpipe_rows() + thread_wait_rows() + one_place_rows())
    for s in (pick(names) if names else every):
        bad = lint_scenario(s, by_name)
        rows.append({"scenario": s["name"], "version": "-", "ok": not bad, **({"why": bad} if bad else {})})
    if not names:
        bad = static_shadow_problems()
        rows.append({"scenario": "(source: hidden statics)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        # a macro a side library's sources test that only another target defines (CMake's PRIVATE)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import target_defines
        bad = target_defines.problems()
        rows.append({"scenario": "(source: build defines per target)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        # the online messages' layout under one PORT_SYNC_PROTOCOL (builds of one protocol play together)
        import sync_protocol
        bad = sync_protocol.problems()
        rows.append({"scenario": "(source: online protocol)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        # the game's font: no letter it lacks in the port's text drawn with it
        import font_chars
        bad = font_chars.problems()
        rows.append({"scenario": "(source: game-font characters)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        # the message tags the C code asks for: answered on both backends, in every release's languages
        import msg_tags
        bad = msg_tags.problems()
        rows.append({"scenario": "(source: message tags per build and language)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = manifest_merge_problems()
        rows.append({"scenario": "(runner: jobs.json across parts)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = follower_test_problems()
        rows.append({"scenario": "(source: follower tests)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = notes_list_problems()
        rows.append({"scenario": "(source: paper notes' lists append-only)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = picked_size_problems()
        rows.append({"scenario": "(source: picked files' sizes)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = raw_open_dialog_problems()
        rows.append({"scenario": "(source: file pickers on iOS)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = raw_save_folder_dialog_problems()
        rows.append({"scenario": "(source: save and folder pickers on iOS)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
        bad = script_code_problems()
        rows.append({"scenario": "(source: input script codes)", "version": "-", "ok": not bad,
                     **({"why": bad} if bad else {})})
    return rows


def pick(names):
    every = all_scenarios()
    if not names:
        return every
    chosen = []
    for n in names:
        if os.path.isfile(n):
            try:
                chosen.append(load_scenario(n))
            except (ValueError, OSError) as e:  # (a FAIL row of the run: broken_rows)
                BROKEN[os.path.abspath(n)] = str(e)
            continue
        hit = [s for s in every if s["name"] == n or n in s.get("groups", []) or n == "all"]
        if not hit:
            sys.exit(f"no scenario or group {n!r} (testpass.py list)")
        chosen += [s for s in hit if s not in chosen]
    return chosen


# --- the window guard (Windows): no window of the game may ever be visible ---

def visible_windows(exe):
    """Visible top-level windows of processes running this executable: [(pid, title, rect)]."""
    if os.name != "nt":
        return []
    import ctypes
    from ctypes import wintypes
    user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
    want = os.path.normcase(os.path.abspath(exe))
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        h = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return True
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        ok = kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size))
        kernel32.CloseHandle(h)
        if ok and os.path.normcase(buf.value) == want:
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title, 256)
            r = wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            found.append((pid.value, title.value, (r.left, r.top, r.right, r.bottom)))
        return True

    user32.EnumWindows(each, 0)
    return found


def kill_pid(pid):
    """the process and what it started (the timeout wrapper's game, the slot script's)"""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)], capture_output=True)
        return
    import signal
    try:  # (games are started in a session of their own: its group is the game and its children; never ours)
        group = os.getpgid(pid)
        if group == os.getpgrp():
            raise OSError("in testpass's own process group")
        os.killpg(group, signal.SIGKILL)
    except OSError:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


# --- running ---

# a scenario's "build": a build of its own beside the plain one, configured with options a player's build
# hasn't got (build-pc-<version>-aurora-<build>; BUILDS says how to make it)
BUILDS = {"touch": "-DTTYD_TOUCH_OVERLAY=ON (the on-screen touch controls)"}


def exe_of(tree, version, backend, build=None):
    d = os.path.join(tree, f"build-pc-{version}-{'headless' if backend == 'headless' else 'aurora'}"
                     + (f"-{build}" if build else ""))
    exe = os.path.join(d, f"ttyd-{version}" + (".exe" if os.name == "nt" else ""))
    return exe if os.path.exists(exe) else None


def disc_of(tree, version, iso_dir):
    for d in ([iso_dir] if iso_dir else []) + [os.path.join(tree, "orig", VERSIONS[version])]:
        isos = sorted(glob.glob(os.path.join(d, "*.iso")))
        if isos:
            return isos[0]
    return None


def bash():
    if os.name != "nt":
        return "bash"
    for p in (r"C:\Program Files\Git\usr\bin\bash.exe", r"C:\Program Files\Git\bin\bash.exe"):
        if os.path.exists(p):
            return p
    return "bash"


def apply_settings_over(user, wanted):
    # a scenario's own "settings" win over a settings.txt that user_files copied in (old data, a kept folder):
    # the copy keeps every other line, these keys take the scenario's values
    if not wanted:
        return
    path = os.path.join(user, "settings.txt")
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(path) else []
    keys = {str(k) for k in wanted}
    kept = [ln for ln in lines if ln.split("=", 1)[0].strip() not in keys]
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.writelines(f"{ln}\n" for ln in kept)
        f.writelines(f"{k}={v}\n" for k, v in wanted.items())


def missing_captures(s, env, shots, backend):
    """captures asked for and not made: expect_shots, and on Aurora every frame in "shots" (shot_<frame>.png, or
    shot_<frame>_<k>.png with $TTYD_SHOT_INBETWEEN) and every "shot NAME" of the input script (<map>__NAME.png on
    a TTYD_MAP run); the headless build captures nothing. [why]"""
    def made(name):
        return any(f == name + ".png" or f.endswith("__" + name + ".png") for f in shots)
    why = [f"no capture {name}.png" for name in dict.fromkeys(s.get("expect_shots") or []) if not made(name)]
    if backend == "headless" or str((s.get("settings") or {}).get("msaa", 0)) not in ("0", ""):
        return why  # (with MSAA, Aurora has no readback for TTYD_SHOT: the player's screenshot is the check there)
    if s.get("shots_unchecked"):
        return why
    k = str(env.get("TTYD_SHOT_INBETWEEN", "0")).strip()
    asked = [f"shot_{f}" + (f"_{k}" if k not in ("", "0") else "") for f in (s.get("shots") or [])]
    # (the launcher's TTYD_LAUNCHER_SHOT=<frame>:<name> aren't: some name a frame never reached on purpose, a
    # launcher kept up for a script; expect_shots names those that must be made)
    asked = list(dict.fromkeys(asked + script_shots(s.get("input"))))
    missing = [name for name in asked if not made(name)]
    if s.get("shots_partial"):
        # (captures a frame or two apart, a GIF's, or a sweep of frames past where the run ends: one asked for
        # while another is in flight is dropped, so only some are made; none at all is still a failure)
        if asked and len(missing) == len(asked):
            why.append(f"none of the {len(asked)} captures asked for was made")
        return why
    return why + [f"no capture {name}.png" for name in missing]


def file_checks(s, out):
    """file_must / file_must_not: {"file under the run folder": [regular expressions]}; a file_must file must be
    there (an empty list: there and not empty; a folder: not empty). [why]"""
    why = []
    for rel, rxs in (s.get("file_must") or {}).items():
        path = os.path.join(out, rel)
        if os.path.isdir(path):
            if not os.listdir(path):
                why.append(f"{rel}: an empty folder")
            continue
        if not os.path.isfile(path) or (not rxs and os.path.getsize(path) == 0):
            why.append(f"no {rel}" if not os.path.isfile(path) else f"{rel} is empty")
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        why += [f"{rel} lacks /{rx}/" for rx in rxs if not re.search(rx, text, re.M)]
    for rel, rxs in (s.get("file_must_not") or {}).items():
        path = os.path.join(out, rel)
        if os.path.isfile(path):
            text = open(path, encoding="utf-8", errors="replace").read()
            why += [f"{rel} has /{rx}/" for rx in rxs if re.search(rx, text, re.M)]
    return why


def expected_fail_verdict(s, res):
    """A scenario's "expected_fail": {"by": "<branch that fixes it>", "why": "...", "match": [regex, ...]}: a known
    bug it shows. Failing as expected (every reason matching one of "match", which it must have) it is XFAIL, not a
    failure of the run; failing otherwise, a failure as ever; passing, XPASS (the fix has landed: drop the field),
    which passes but is said. With no "match" it is never XFAIL (any failure, a crash or timeout too, would have
    hidden); nor is a crash, a timeout or a testpass error unless a pattern names it in so many words."""
    xf = s.get("expected_fail")
    if not xf or res.get("skipped"):
        return res
    res["expected_fail"] = xf.get("by")
    if res.get("ok"):
        res["xpass"] = True
        return res
    why = res.get("why")
    reasons = why if isinstance(why, list) else [why or "a capture or fingerprint differs"]
    pats = xf.get("match") or []

    def expected(r):
        r = str(r)
        m = re.match(r"(exit (?!0\b|5\b)-?\d+|testpass error|(?:%s):)" % "|".join(FAILURES), r)
        # (a crash, a timeout or another exit, a FAILURES line, testpass's own error: only when a pattern names it
        # in its own words, never a broad pattern that happens to match it too)
        lead = m.group(1).split(" ")[0].rstrip(":") if m else None
        return any(re.search(p, r) and (lead is None or lead in p) for p in pats)

    if pats and all(expected(r) for r in reasons):
        res["ok"] = res["xfail"] = True
        res["why"] = reasons
    return res


def run_one_safe(s, version, a):
    # one scenario's setup error (a missing local file, say) fails that scenario, not the whole run; either way its
    # result.json is (re)written with this run part's stamp, so summary tells it from an older run's (and never
    # finds a failure missing: run_one may have removed the folder before the error)
    try:
        res = run_one(s, version, a)
    except Exception as e:
        res = {"scenario": s["name"], "version": version, "ok": False, "why": f"testpass error: {e!r}"}
    res = expected_fail_verdict(s, res)
    res["part"] = getattr(a, "part", None)
    out = os.path.join(OUT_ROOT, a.run, s["name"], version)
    try:
        os.makedirs(out, exist_ok=True)
        json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
    except (OSError, TypeError, ValueError) as e:
        res = dict(res, ok=False, why=f"testpass error: its result.json could not be written: {e!r}")
    return res


TEXPACK_LOCK = threading.Lock()
# named reference checkouts a scenario's "tree" can ask for (old builds: the old-data scenarios). Unset, one with
# no build is skipped (the tester has none); set, it must be there: a typo or a moved checkout fails the run
REF_TREE_ENV = {"ref041": "TTYD_REF041_TREE"}
REF_TREES = {"ref041": os.environ.get("TTYD_REF041_TREE") or os.path.join(os.path.dirname(REPO), "ttyd-port-ref041")}
# host controllers kept out of test games: a real pad (asleep, unplugged, a button held) must not add input to a
# scripted run or hold it. SDL3 hints (SDL_hints.h of the SDL 3.4 build; SDL reads a hint from the environment):
# every joystick driver a real pad comes through is off (Windows: XInput, RawInput, WGI, DirectInput, GameInput,
# HIDAPI; macOS: MFi, IOKit), and SDL's virtual driver, which the bindings and one-handed tests' "Paper Cut test
# pad" uses (with real pads' VID/PIDs, so no VID/PID list would do), stays on. TTYD_PAD_UNPLUG and TTYD_P*_PRESS
# are the game's own and keep working. (Linux has no hint for its evdev driver: there, unplug pads.)
NO_HOST_PADS = {"SDL_XINPUT_ENABLED": "0", "SDL_JOYSTICK_RAWINPUT": "0", "SDL_JOYSTICK_WGI": "0",
                "SDL_JOYSTICK_DIRECTINPUT": "0", "SDL_JOYSTICK_GAMEINPUT": "0", "SDL_JOYSTICK_HIDAPI": "0",
                "SDL_JOYSTICK_MFI": "0", "SDL_JOYSTICK_IOKIT": "0"}


def ref_tree_named(name):
    """whether the tester named that reference checkout ($TTYD_REF041_TREE): then a missing build is a failure"""
    return bool(os.environ.get(REF_TREE_ENV.get(name, ""), ""))


def pm64rom_problem(text):
    """(skip, why) for a scenario whose text names {pm64rom}: unset, a skip (nothing to run it with); set but no
    file (a typo, a moved ROM, a drive not mounted), a failure; None when it can run"""
    rom = os.environ.get("PAPERCUT_PM64_ROM", "")
    if "{pm64rom}" not in text or os.path.isfile(rom):
        return None
    if not rom:
        return True, "no Paper Mario ROM ($PAPERCUT_PM64_ROM)"
    return False, f"$PAPERCUT_PM64_ROM is set to {rom!r}, which is no file"


def run_one(s, version, a):
    if "online" in s:
        return run_online(s, version, a)
    backend = a.backend or s.get("backend", "offscreen")
    out = os.path.join(OUT_ROOT, a.run, s["name"], version)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "shots"))
    tree = a.tree
    if s.get("tree"):
        tree = REF_TREES.get(s["tree"], s["tree"])
    res = {"scenario": s["name"], "version": version, "backend": backend, "tree": tree, "ok": False}
    build = s.get("build")
    exe = exe_of(tree, version, backend, build)
    iso = disc_of(tree, version, a.iso_dir)
    if s.get("tree") and (exe is None or iso is None):
        named = ref_tree_named(s["tree"])
        # (unset, not a failure: the reference checkout is the tester's own, not the repo's; set, it was meant to run)
        res["skipped"] = res["ok"] = not named
        res["why"] = (f"no {'build' if exe is None else 'disc'} for {version} ({backend}) in the {s['tree']} tree {tree} "
                      + (f"(${REF_TREE_ENV[s['tree']]} is set: it must have them)" if named else
                         "(set $TTYD_REF041_TREE to a 0.4.1 checkout with its builds)"))
        json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
        return res
    # {data_root}: --data-root, $TTYD_DATA_ROOT, this checkout's data/, else the main checkout's (a worktree has none
    # of its own: the player's saves, data/saves, are only there); the first that has every path the env names
    data_root = a.data_root or os.environ.get("TTYD_DATA_ROOT") or os.path.join(REPO, "data")
    named = [p for v in s.get("env", {}).values() for p in str(v).split(";") if "{data_root}" in p]
    if named and not a.data_root and not os.environ.get("TTYD_DATA_ROOT"):
        roots = [os.path.join(REPO, "data")]
        try:
            common = subprocess.run(["git", "-C", REPO, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                    capture_output=True, text=True).stdout.strip()
            if common:
                roots.append(os.path.join(os.path.dirname(common), "data"))
        except OSError:
            pass
        found = [r for r in roots if all(os.path.exists(p.replace("{data_root}", r)) for p in named)]
        if not found:
            res["skipped"] = res["ok"] = True  # (not a failure: the player's own files are not the repo's)
            res["why"] = (f"the files it names under {{data_root}} are in none of {roots} (the player's own saves, "
                          f"data/saves; --data-root or $TTYD_DATA_ROOT)")
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
        data_root = found[0]
    pm64rom = os.environ.get("PAPERCUT_PM64_ROM", "")
    if (rom := pm64rom_problem(" ".join(str(v) for v in s.get("env", {}).values()))):
        res["skipped"] = res["ok"] = rom[0]  # (unset, not a failure: nothing to run it with; set and wrong, one)
        res["why"] = rom[1]
        json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
        return res
    if exe is None and build:
        res["why"] = (f"no {build} build for {version} in {tree}: build-pc-{version}-aurora-{build}, the Aurora build "
                      f"configured with {BUILDS.get(build, '?')}")
        json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
        return res
    if exe is None or iso is None:
        res["why"] = f"no {'build' if exe is None else 'disc'} for {version} ({backend}) in {tree}"
        json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
        return res
    user = os.path.join(out, "user")
    if s.get("env", {}).get("TTYD_USER_DIR"):
        # (a scenario's own user folder, a path to test: the house settings and user_files go there)
        user = os.path.normpath(str(s["env"]["TTYD_USER_DIR"]).replace("{out}", out))
    portable = os.path.join(out, "portable") if s.get("portable") else None
    if portable:  # (portable mode: its own folder stands for the program's, with portable.txt; User and Cache in it)
        os.makedirs(portable)
        open(os.path.join(portable, "portable.txt"), "w").close()
        user = os.path.join(portable, "User")
    if s.get("user_from"):
        # the user folder another scenario of this run left (run first: see main), as a second launch finds it
        src = os.path.join(OUT_ROOT, a.run, s["user_from"], version)
        try:
            before = json.load(open(os.path.join(src, "result.json")))
        except (OSError, ValueError):
            before = {}
        if before.get("skipped"):
            res["skipped"] = res["ok"] = True
            res["why"] = f"{s['user_from']} was skipped: {before.get('why')}"
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
        if not before.get("ok"):  # (a second launch from a first that failed shows nothing about the second)
            res["why"] = f"{s['user_from']}, which it starts from, failed: {before.get('why', 'no result')}"
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
        if not os.path.isdir(os.path.join(src, "user")):
            res["why"] = (f"no user folder from {s['user_from']} in run {a.run}"
                          + (f" ({s['user_from']} failed: {before.get('why')})" if before else ""))
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
        shutil.copytree(os.path.join(src, "user"), user, ignore=shutil.ignore_patterns("shots"))
    else:
        os.makedirs(user)
    settings = dict(HOUSE_SETTINGS)
    if not os.path.exists(os.path.join(user, "settings.txt")):
        settings.update(HOUSE_SETTINGS_NEW)  # (a fresh folder's file is this release's; old data keeps its own)
    try:  # (a second launch keeps the settings the first one left: old data; the house defaults fill the rest)
        for line in open(os.path.join(user, "settings.txt"), encoding="utf-8", errors="replace"):
            k, eq, v = line.strip().partition("=")
            if eq:
                settings[k] = v
    except OSError:
        pass
    settings.update(s.get("settings", {}))
    with open(os.path.join(user, "settings.txt"), "w", newline="\n", encoding="utf-8") as f:  # (the game reads UTF-8)
        f.writelines(f"{k}={v}\n" for k, v in settings.items())
    library = [disc_of(a.tree, v, None) for v in s.get("library", [])]  # other versions' discs in the library
    if library:
        with open(os.path.join(user, "library.txt"), "w", newline="\n", encoding="utf-8") as f:
            f.writelines(f"{d}\n" for d in library if d)
        if s.get("library_last") and library[0]:  # (the one "Skip the launcher" plays: launcher_ui.cpp's last_disc.txt)
            with open(os.path.join(user, "last_disc.txt"), "w", newline="", encoding="utf-8") as f:
                f.write(library[0])
    if s.get("texpack_from"):
        # a test texture pack made now from another scenario's texture dumps of this run (their names only: tinted
        # checkers, no game pixels; tools/port/gen_test_texpack.py), once per run and version, into user/textures
        src = os.path.join(OUT_ROOT, a.run, s["texpack_from"], version)
        dumps = os.path.join(src, "cache", "texture_dumps")
        if not os.path.isdir(dumps) or not os.listdir(dumps):
            try:
                before = json.load(open(os.path.join(src, "result.json")))
            except (OSError, ValueError):
                before = {}
            res["why"] = (f"no texture dumps from {s['texpack_from']} in run {a.run} ({dumps})"
                          + (f": it failed: {before.get('why')}" if before and not before.get("ok") else ""))
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
        pack = os.path.join(src, "texpack")
        with TEXPACK_LOCK:
            if not os.path.isfile(os.path.join(pack, ".done")):
                shutil.rmtree(pack, ignore_errors=True)
                r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "port", "gen_test_texpack.py"), dumps, pack,
                                    str(s.get("texpack_scale", 8))], capture_output=True, text=True)
                if r.returncode != 0:
                    res["why"] = f"gen_test_texpack.py failed: {(r.stderr or r.stdout).strip()[-300:]}"
                    json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
                    return res
                open(os.path.join(pack, ".done"), "w").write(r.stdout)
        shutil.copytree(pack, os.path.join(user, "textures"), dirs_exist_ok=True, ignore=shutil.ignore_patterns(".done"))
    for src in s.get("user_files", []):  # saves, a card: copied into the fresh user folder
        # ({version} is this run's version, {run} its name; a folder ending in "/" has its contents copied, as another
        # run's kept user folder: --keep-user)
        p = os.path.join(REPO, src.replace("{version}", version).replace("{run}", a.run))
        if src.endswith("/"):
            if not os.path.isdir(p):
                res["why"] = f"no user folder {p} (make it first: its scenario with --keep-user)"
                json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
                return res
            shutil.copytree(p, user, dirs_exist_ok=True, ignore=shutil.ignore_patterns("shots"))
            continue
        copy_user_file(p, user)
    for g in s.get("user_gen", []):  # files made at test time (nothing of anyone's committed): a repo script's output
        dest = os.path.join(user, g["dest"])
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        r = subprocess.run([sys.executable, os.path.join(REPO, g["script"]), dest] +
                           [str(x).replace("{repo}", REPO).replace("{version}", version) for x in g.get("args", [])],
                           capture_output=True, text=True)
        if r.returncode != 0:
            res["why"] = f"{g['script']} failed: {(r.stderr or r.stdout).strip()[-300:]}"
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
    apply_settings_over(user, s.get("settings", {}))
    # Aurora's pipeline cache: one per build and runner thread (two games on one cache lock its database)
    slot = threading.current_thread().name.rsplit("_", 1)[-1]
    cache = os.path.join(OUT_ROOT, ".cache", f"{os.path.basename(os.path.normpath(tree))}-{version}-{backend}-{slot}")
    os.makedirs(cache, exist_ok=True)
    env = dict(os.environ)
    for k in [k for k in env if k.startswith("TTYD_") and k not in ("TTYD_GAME_SLOT",)]:
        del env[k]  # only the scenario's switches
    env.update(TTYD_FAST="1", TTYD_USER_DIR=user, TTYD_SHOT_DIR=os.path.join(out, "shots"), TTYD_CACHE_DIR=cache,
               TTYD_NO_LAUNCHER="1", TTYD_WATCHDOG="60", TTYD_WINDOW_POS="-5000,-5000",
               SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", **NO_HOST_PADS)
    if s.get("user_dir_slash"):  # the user folder named with a separator at its end, as SDL_GetPrefPath's is
        env["TTYD_USER_DIR"] = user + os.sep
    if portable:  # (no user or cache folder named: the game finds both beside "the program", TTYD_PORTABLE_BASE)
        env["TTYD_PORTABLE_BASE"] = portable
        env.pop("TTYD_USER_DIR", None)
        env.pop("TTYD_CACHE_DIR", None)
    if backend == "offscreen":
        env["TTYD_OFFSCREEN"] = "1"
    else:
        # headless builds before 0.5.0 read the extracted disc (tools/port/extract_disc.py), not the image
        for root in [a.data_root, os.environ.get("TTYD_DATA_ROOT"), os.path.join(tree, "data"),
                     os.path.join(REPO, "data")]:
            if root and os.path.isdir(os.path.join(root, VERSIONS[version], "files", "battle")):
                env["TTYD_DATA_DIR"] = os.path.join(root, VERSIONS[version])
                break
    for k, v in s.get("env", {}).items():
        if v is None:
            env.pop(k, None)
        else:
            # ({data_root}: chosen above, where the player's own files are: data/saves)
            env[k] = (str(v).replace("{iso}", iso).replace("{out}", out).replace("{repo}", REPO)
                      .replace("{pm64rom}", pm64rom).replace("{data_root}", data_root)
                      .replace("{version}", version).replace("{run}", a.run))
    script = s.get("input")
    if script is not None:
        path = os.path.join(out, "input.txt")
        if isinstance(script, list):
            open(path, "w", newline="\n", encoding="utf-8").write("\n".join(script) + "\n")  # (NPC names: UTF-8)
        elif script.replace("\\", "/").startswith("tests/port/full_run/"):
            # the full run's segments as full_run.py gives them to the game: Japanese NPC names (walk_to npc 村長)
            # in Shift-JIS, as the game's own strings are (the files are UTF-8; copied as they are, 07/08/13's
            # walk_to npc found no such NPC)
            with open(os.path.join(REPO, script), encoding="utf-8") as fh:
                text = fh.read()
            open(path, "wb").write(text.encode("cp932", errors="replace"))
        else:
            shutil.copy(os.path.join(REPO, script), path)
        env["TTYD_INPUT"] = path
    if s.get("shots"):
        env["TTYD_SHOT"] = ",".join(str(f) for f in s["shots"])
    if s.get("ui_shots") or getattr(a, "ui_shots", False):
        env["TTYD_SHOT_UI"] = "1"  # each named shot also as <name>_ui.png, with the ImGui layer (port/ui_shot.h)
    if s.get("fingerprint"):
        env["TTYD_FINGERPRINT"] = os.path.join(out, "fingerprint.txt")
    timeout = int(s.get("timeout", 180) * a.timeout_scale)
    # "disc": false starts with no disc on the command line (the launcher)
    cmd = ["timeout", "-k", "10", str(timeout), exe] + ([iso] if s.get("disc", True) else [])
    if GAME_SLOT and os.path.exists(GAME_SLOT):
        cmd = [bash(), GAME_SLOT] + cmd
    elif os.name == "nt":
        cmd = [bash(), "-c", 'exec "$@"', "_"] + cmd
    res["env"] = {k: env[k] for k in sorted(env) if k.startswith("TTYD_") and k not in ("TTYD_USER_DIR", "TTYD_SHOT_DIR",
                                                                                      "TTYD_CACHE_DIR")}
    started = time.time()
    windows = []
    with open(os.path.join(out, "log.txt"), "w", encoding="utf-8", errors="replace") as log:
        p = subprocess.Popen(cmd, cwd=os.path.dirname(exe), env=env, stdout=log, stderr=subprocess.STDOUT,
                             start_new_session=os.name != "nt")
        # the guard: a visible window of this build is a failure, and the game is stopped at once
        while p.poll() is None:
            if backend == "offscreen":
                seen = visible_windows(exe)
                if seen:
                    windows += seen
                    for pid, _, _ in seen:
                        kill_pid(pid)
            time.sleep(0.25)
    res["seconds"] = round(time.time() - started, 1)
    res["exit"] = p.returncode
    text = open(os.path.join(out, "log.txt"), encoding="utf-8", errors="replace").read()
    if windows:
        with open(os.path.join(out, "log.txt"), "a") as log:
            for w in windows:
                log.write(f"[testpass] visible window pid {w[0]} {w[1]!r} at {w[2]}\n")
        text += "".join(f"[testpass] visible window pid {w[0]}\n" for w in windows)
    res["shots"] = sorted(os.path.basename(f) for f in glob.glob(os.path.join(out, "shots", "*.png")))
    fp = os.path.join(out, "fingerprint.txt")
    if os.path.exists(fp):
        res["fingerprint_frames"] = sum(1 for _ in open(fp))
    # the verdict
    why = []
    ok_exit = s.get("exit", [0])
    if ok_exit != "any" and res["exit"] not in ([ok_exit] if isinstance(ok_exit, int) else ok_exit):
        why.append(f"exit {res['exit']}" + (" (timeout)" if res["exit"] == 124 else ""))
    allowed = set(s.get("allow_failures", []))
    for name, rx in FAILURES.items():
        if name not in allowed and (m := re.search(rx, text, re.M)):
            why.append(f"{name}: {text[m.start():text.find(chr(10), m.start())][:160]}")
    for rx in s.get("log_must", []):
        if not re.search(rx, text, re.M):
            why.append(f"log lacks /{rx}/")
    for rx in s.get("log_must_not", []):
        if (m := re.search(rx, text, re.M)):
            why.append(f"log has /{rx}/: {text[m.start():text.find(chr(10), m.start())][:160]}")
    why += missing_captures(s, env, res["shots"], backend)
    why += file_checks(s, out)
    if not positive_checks(s):  # (testpass.py lint: a run with nothing to show it got anywhere is no pass)
        why.append("no positive check (log_must, expect_shots, file_must, save_matches or fingerprint)")
    if s.get("fingerprint") and not res.get("fingerprint_frames"):
        why.append("no fingerprint written")
    if s.get("save_matches"):  # the run's last save, field by field against a reference (tools/port/save_diff.py)
        sm = s["save_matches"]
        ref = os.path.join(REPO, sm["ref"].replace("{version}", version))
        r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "port", "save_diff.py"), user, ref, "--ignore"]
                           + sm.get("ignore", []), capture_output=True, text=True)
        open(os.path.join(out, "save_diff.txt"), "w").write(r.stdout + r.stderr)
        if r.returncode != 0:
            why.append("save differs from " + sm["ref"] + ": " + "; ".join(r.stdout.splitlines()[:4]))
    if portable:  # the caches went to Cache beside "the program" (the settings: User, written above)
        if not os.path.isdir(os.path.join(portable, "Cache")) or not os.listdir(os.path.join(portable, "Cache")):
            why.append("portable: nothing in Cache beside the program")
    if s.get("flat_max") is not None:  # no capture with a flat bright region over that share (a NaN polygon)
        res["flat"] = {}
        for f in res["shots"]:
            share = flat_bright_share(os.path.join(out, "shots", f))
            res["flat"][f] = round(share, 4)
            if share > s["flat_max"]:
                why.append(f"{f}: a flat bright region over {share:.0%} of the frame")
    if s.get("stereo_check"):  # depth between the two eyes in each named capture (stereo_verdict)
        sc = s["stereo_check"]
        res["stereo"] = {}
        names = sc.get("shots")
        for f in res["shots"]:
            if names and not any(f == n + ".png" or f.endswith("__" + n + ".png") for n in names):
                continue
            this = sc
            if sc.get("hud_shots") and not any(f == n + ".png" or f.endswith("__" + n + ".png")
                                               for n in sc["hud_shots"]):
                this = dict(sc, hud=[])  # (the HUD's places hold in those captures only)
            summary, bad = stereo_verdict(os.path.join(out, "shots", f), sc["layout"], this)
            res["stereo"][f] = summary
            why += [f"{f}: stereo: {b}" for b in bad]
        if not res["stereo"]:
            why.append("stereo_check: no capture to check")
    if s.get("bloom_check"):  # the glow centred on what glows, each pair of captures off / on (bloom_verdict)
        res["bloom"] = {}
        for pair in s["bloom_check"].get("pairs", []):
            def find(n):
                return next((f for f in res["shots"] if f == n + ".png" or f.endswith("__" + n + ".png")), None)
            fo, fn = find(pair[0]), find(pair[1])
            if fo is None or fn is None:
                why.append(f"bloom_check: no capture {pair[0] if fo is None else pair[1]}")
                continue
            # (a pair's third value: what glows, the preset's own threshold; Dreamy takes in more than Soft)
            this = dict(s["bloom_check"], level=pair[2]) if len(pair) > 2 else s["bloom_check"]
            summary, bad = bloom_verdict(os.path.join(out, "shots", fo), os.path.join(out, "shots", fn), this)
            res["bloom"][pair[1]] = summary
            why += [f"bloom_check {pair[1]}: {b}" for b in bad]
    if s.get("edge_check"):  # every frame shown: the same centred picture, clear bars, nothing flashing at its edges
        res["edges"], bad = edge_verdict(os.path.join(out, s["edge_check"].get("dir", "burst")), s["edge_check"])
        why += bad
    if s.get("tattle_check"):  # each Tattle card's picture neither empty nor garbage (tattle_verdict)
        res["tattle"], bad = tattle_verdict(s, text, out)
        why += bad
    res["ok"] = not why
    if why:
        res["why"] = why
    if not a.keep_user and s["name"] not in getattr(a, "user_sources", ()):  # (one a later run starts from: kept)
        shutil.rmtree(user, ignore_errors=True)
    json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
    return res


# --- online scenarios: two to four games playing together (docs/STATE_SYNC.md) ---
#
# "online": {"link": "address" | "relay", "relay_args": [...], "faults": "...", "pace": 300,
#            "players": [{"role": "host", "env": {...}, "input": [...], "settings": {...},
#                         "version": "jp", "backend": "offscreen", "tree": DIR, "link": false,
#                         "checked": false, "log_must": [...], "log_must_not": [...], "exit": [...]}, ...],
#            "check": {"linked": true, "drift": true, "progress": true, "names": true,
#                      "battle_ended": false, "same_hits": false}}
# same_hits: every checked game's "[story] damage:" lines (TTYD_SYNC_BATTLE_LOG) the same, in the same order:
# a shared battle's hits and the HP they met alike on every screen
# Every game is a fresh user folder, TTYD_FAST=1, paced (TTYD_SYNC_PACE) and named; the first is the
# host (by address on the IPv6 loopback, or a room on tools/port/sync_test_relay.py), the rest join.
# A group holds one game slot per game for as long as it plays (taken one group at a time, so two
# groups never each hold part of what they need).

SLOT_LOCK = threading.Lock()
PORT_LOCK = threading.Lock()
PORT_NEXT = [47600]
SYNC_PROGRESS = re.compile(r"progress ([0-9a-f]{16}/[0-9a-f]{16})")
SYNC_NAME = re.compile(r"peer: player (\d)'s name: \"")


def hold_slots(n):
    """n shared game slots, held until release_slots: [holder processes] (none without a slot script)."""
    if not (GAME_SLOT and os.path.exists(GAME_SLOT)):
        return []
    import random
    with SLOT_LOCK:
        while True:
            held, ok = [], True
            deadline = time.time() + 900
            for _ in range(n):
                p = subprocess.Popen([bash(), GAME_SLOT, sys.executable, "-c",
                                      "import sys; print('go', flush=True); sys.stdin.read()"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
                held.append(p)
                got = []
                t = threading.Thread(target=lambda: got.append(p.stdout.readline()), daemon=True)
                t.start()
                t.join(max(1, deadline - time.time()))
                if got and got[0].strip() != "go":
                    # (the slot script ended at once, an EOF, or said something else: no slot is held by it, and
                    # running anyway would bypass the cap on games at once; the scenario fails instead)
                    release_slots(held)
                    raise RuntimeError(f"the game slot script {GAME_SLOT} gave no slot: {got[0]!r}")
                if not got:
                    ok = False
                    break
            if ok:
                return held
            # (part of what's needed for 15 minutes: let it go, so two passes never hold each other up for good)
            release_slots(held)
            time.sleep(random.uniform(5, 30))


def release_slots(held):
    for p in held:
        try:
            p.stdin.close()
            p.wait(timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            kill_pid(p.pid)


def port_free(port):
    import socket
    for fam, addr in ((socket.AF_INET6, "::"), (socket.AF_INET, "0.0.0.0")):
        s = socket.socket(fam, socket.SOCK_STREAM)
        try:
            if os.name == "nt":
                s.setsockopt(socket.SOL_SOCKET, getattr(socket, "SO_EXCLUSIVEADDRUSE", -5), 1)
            s.bind((addr, port))
        except OSError:
            return False
        finally:
            s.close()
    return True


def next_ports():
    """A pair of ports nobody listens on: the host's and the relay stand-in's. Each runner process starts
    at its own place in 47600-48599 (two passes at once never share a port), and skips ports in use."""
    import random
    with PORT_LOCK:
        if PORT_NEXT[0] == 47600:
            PORT_NEXT[0] = 47600 + 2 * random.Random(os.getpid()).randrange(500)
        for _ in range(500):
            port = PORT_NEXT[0]
            PORT_NEXT[0] = 47600 + (port - 47600 + 2) % 1000
            if port_free(port) and port_free(port + 1):
                return port, port + 1
    return port, port + 1


def copy_user_file(p, user):
    """A user_files entry copied into the user folder: by its own name, or "SRC>DEST" to DEST there (a card
    put where the game reads it: "tests/port/upgrade/0.4.1/card1.gci>USA/Card A/01-G8ME-mariost_save_file.gci")."""
    dest = os.path.join(user, os.path.basename(p))
    if ">" in p:
        p, rel = p.split(">", 1)
        dest = os.path.join(user, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
    (shutil.copytree if os.path.isdir(p) else shutil.copy)(p, dest)


MP_WHERE = re.compile(r"^\[mp\] where: map '(?P<map>\w*)' story (?P<story>-?\d+) seq (?P<seq>\d+) battle (?P<battle>\d) "
                      r".*? partner (?P<partner>\d+) \| P1 Mario (?P<x>-?\d+) (?P<y>-?\d+) (?P<z>-?\d+) .*\| "
                      r"(?P<verdict>settled|NOT SETTLED: .*)$")


def same_where_problems(wheres, tolerance):
    """online check "same_where" (true, or Mario's distance allowed between games, default 80): every checked game's
    last "where coop" line ([mp] where:, port/players.h) in the same map, story and partner, Mario at the same place,
    and each one settled (nobody mid-move, no menu up, control back): [problem]"""
    tol = 80.0 if tolerance is True else float(tolerance)
    bad = [f"{r}: no '[mp] where:' line (its script needs a 'where coop' at the end)" for r, w in wheres.items()
           if w is None]
    got = {r: w for r, w in wheres.items() if w is not None}
    for r, w in got.items():
        if w["verdict"] != "settled":
            bad.append(f"{r}: {w['verdict']}")
    if len(got) > 1:
        first, w0 = next(iter(got.items()))
        for r, w in got.items():
            for k in ("map", "story", "partner", "battle"):
                if w[k] != w0[k]:
                    bad.append(f"{r}: {k} {w[k]}, {first}'s {w0[k]}")
            d = sum((float(w[k]) - float(w0[k])) ** 2 for k in "xyz") ** 0.5
            if w["map"] == w0["map"] and d > tol:
                bad.append(f"{r}: Mario {d:.0f} units from where {first}'s game has him (over {tol:.0f})")
    return bad


def run_online(s, version, a):
    o = s["online"]
    out = os.path.join(OUT_ROOT, a.run, s["name"], version)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    res = {"scenario": s["name"], "version": version, "online": True, "tree": a.tree, "ok": False, "games": {}}
    pm64rom = os.environ.get("PAPERCUT_PM64_ROM", "")
    if (rom := pm64rom_problem(json.dumps(o) + json.dumps(s.get("env", {})))):
        res["skipped"] = res["ok"] = rom[0]  # (unset, not a failure: nothing to run it with; set and wrong, one)
        res["why"] = rom[1]
        json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
        return res
    port, rport = next_ports()
    relay = o.get("link", "address") == "relay"
    code = o.get("code", "2TST-RXYM")
    timeout = int(s.get("timeout", 300) * a.timeout_scale)
    games = []
    for i, pl in enumerate(o["players"]):
        role = pl.get("role", "host" if i == 0 else "guest%d" % i)
        v = pl.get("version", version)
        backend = pl.get("backend", a.backend or s.get("backend", "headless"))
        tree = os.path.abspath(REF_TREES.get(pl.get("tree"), pl.get("tree", a.tree)))  # a REF_TREES name, a path, else --tree
        build = pl.get("build", s.get("build"))  # (a player's own: "touch", a handheld's game beside a desktop's)
        exe, iso = exe_of(tree, v, backend, build), disc_of(tree, v, a.iso_dir)
        if exe is None or iso is None:
            res["why"] = f"{role}: no {'build' if exe is None else 'disc'} for {v} ({backend}" + \
                (f", build-pc-{v}-aurora-{build}: {BUILDS.get(build, build)}" if build else "") + f") in {tree}"
            if pl.get("tree") in REF_TREES and not ref_tree_named(pl["tree"]):
                # (as a single game's "tree": a reference checkout the tester hasn't is a skip; one named, a failure)
                res["skipped"] = res["ok"] = True
                res["why"] += f" (set ${REF_TREE_ENV[pl['tree']]} to a checkout with its builds)"
            json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
            return res
        d = os.path.join(out, role)
        user = os.path.join(d, "user")
        os.makedirs(os.path.join(d, "shots"))
        os.makedirs(user)
        settings = dict(HOUSE_SETTINGS, update_check=0)
        settings.update(s.get("settings", {}))
        settings.update(pl.get("settings", {}))
        with open(os.path.join(user, "settings.txt"), "w", newline="\n", encoding="utf-8") as f:
            f.writelines(f"{k}={v2}\n" for k, v2 in settings.items())
        for src in s.get("user_files", []) + pl.get("user_files", []):
            copy_user_file(os.path.join(REPO, src), user)
        for gen in s.get("user_gen", []) + pl.get("user_gen", []):  # (as a single game's: made at test time)
            dest = os.path.join(user, gen["dest"])
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            r = subprocess.run([sys.executable, os.path.join(REPO, gen["script"]), dest] +
                               [str(x).replace("{repo}", REPO).replace("{version}", v) for x in gen.get("args", [])],
                               capture_output=True, text=True)
            if r.returncode != 0:
                res["why"] = f"{role}: {gen['script']} failed: {(r.stderr or r.stdout).strip()[-300:]}"
                json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
                return res
        apply_settings_over(user, {**s.get("settings", {}), **pl.get("settings", {})})
        slot = threading.current_thread().name.rsplit("_", 1)[-1]
        cache = os.path.join(OUT_ROOT, ".cache", f"online-{os.path.basename(tree)}-{v}-{backend}-{slot}-{i}")
        os.makedirs(cache, exist_ok=True)
        env = {k: x for k, x in os.environ.items() if not k.startswith("TTYD_")}
        env.update(TTYD_FAST="1", TTYD_USER_DIR=user, TTYD_SHOT_DIR=os.path.join(d, "shots"), TTYD_CACHE_DIR=cache,
                   TTYD_NO_LAUNCHER="1", TTYD_WATCHDOG="60", TTYD_WINDOW_POS="-5000,-5000",
                   SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", TTYD_WARP_LIMIT="120", **NO_HOST_PADS,
                   TTYD_SYNC_PACE=str(o.get("pace", 300)), TTYD_SYNC_LOG="60",
                   TTYD_SYNC_NAME="Test " + role.capitalize())
        if backend == "offscreen":
            env["TTYD_OFFSCREEN"] = "1"
        if pl.get("link", True):
            if relay:
                env["TTYD_SYNC_TEST_RELAY"] = f"127.0.0.1:{rport}"
                env["TTYD_SYNC_HOST_ROOM" if i == 0 else "TTYD_SYNC_JOIN_ROOM"] = code
            elif i == 0:
                env["TTYD_SYNC_HOST"] = str(port)
            else:
                env["TTYD_SYNC_JOIN"] = f"[::1]:{port}"
        faults = pl.get("faults", o.get("faults"))
        if faults:
            env["TTYD_NET_FAULTS"] = faults
            env["TTYD_NET_FAULTS_SEED"] = str(o.get("seed", 1234) + i)
        for k, x in list(s.get("env", {}).items()) + list(pl.get("env", {}).items()):
            if x is None:
                env.pop(k, None)
            else:
                env[k] = str(x).replace("{iso}", iso).replace("{out}", d).replace("{port}", str(port)) \
                    .replace("{relay}", str(rport)).replace("{repo}", REPO).replace("{pm64rom}", pm64rom)
        if pl.get("shots"):
            env["TTYD_SHOT"] = ",".join(str(f) for f in pl["shots"])
        if s.get("ui_shots") or pl.get("ui_shots") or getattr(a, "ui_shots", False):
            env["TTYD_SHOT_UI"] = "1"  # (as one game's: each shot also as <name>_ui.png, with the ImGui layer)
        script = pl.get("input")
        if script is not None:
            path = os.path.join(d, "input.txt")
            if isinstance(script, list):
                lines = list(script) + ([] if script and script[-1].strip() == "end" else ["end"])
                open(path, "w", newline="\n", encoding="utf-8").write("\n".join(lines) + "\n")
            else:
                shutil.copy(os.path.join(REPO, script), path)
            env["TTYD_INPUT"] = path
        games.append(dict(role=role, pl=pl, exe=exe, iso=iso, env=env, dir=d, user=user, backend=backend))
        res["games"][role] = {"version": v, "backend": backend, "tree": tree,
                              "env": {k: env[k] for k in sorted(env) if k.startswith("TTYD_") and k not in
                                      ("TTYD_USER_DIR", "TTYD_SHOT_DIR", "TTYD_CACHE_DIR")}}
    held = hold_slots(len(games))
    procs, logs, windows, timed_out, unkilled = [], [], [], [], []
    relay_p = None
    started = time.time()
    try:
        if relay:
            relay_p = subprocess.Popen([sys.executable, os.path.join(REPO, "tools", "port", "sync_test_relay.py"),
                                        str(rport), "--log", os.path.join(out, "relay.txt"), "--seconds",
                                        str(timeout + 30)] + o.get("relay_args", []))
            time.sleep(1.0)
        for g in games:
            # a copy of the program per game (a build may link the original meanwhile), beside it for its data
            run_exe = os.path.join(os.path.dirname(g["exe"]), "%s-tp-%d-%s%s" % (
                os.path.splitext(os.path.basename(g["exe"]))[0], port, g["role"], ".exe" if os.name == "nt" else ""))
            shutil.copy2(g["exe"], run_exe)
            g["run_exe"] = run_exe
            log = open(os.path.join(g["dir"], "log.txt"), "w", encoding="utf-8", errors="replace")
            logs.append(log)
            cmd = [run_exe] + ([g["iso"]] if g["pl"].get("disc", True) else [])
            procs.append(subprocess.Popen(cmd, cwd=os.path.dirname(g["exe"]), env=g["env"], stdout=log,
                                          stderr=subprocess.STDOUT, start_new_session=os.name != "nt"))
            time.sleep(g["pl"].get("start_delay", 0.3))
        while any(p.poll() is None for p in procs) and time.time() - started < timeout:
            for g in games:
                if g["backend"] == "offscreen":
                    seen = visible_windows(g["run_exe"])
                    if seen:
                        windows += [(g["role"],) + w for w in seen]
                        for pid, _, _ in seen:
                            kill_pid(pid)
            time.sleep(0.25)
        timed_out = [g["role"] for g, p in zip(games, procs) if p.poll() is None]
    finally:
        for p in procs:
            if p.poll() is None:
                kill_pid(p.pid)
                try:
                    p.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    p.kill()  # (the last resort; a game still there holds its port and slot: said in the verdict)
                    try:
                        p.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        unkilled.append(p.pid)
        for log in logs:
            log.close()
        if relay_p is not None:
            relay_p.kill()
            relay_p.wait()
        release_slots(held)
        for g in games:
            if g.get("run_exe"):
                for _ in range(20):
                    try:
                        os.unlink(g["run_exe"])
                        break
                    except OSError:
                        time.sleep(0.5)
    res["seconds"] = round(time.time() - started, 1)
    # the verdict: each game's own (exit, crash, its log) and the session's (linked, no drift, the same progress)
    why = []
    chk = dict(linked=True, drift=True, progress=True, names=True, battle_ended=False, same_hits=False)
    chk.update(o.get("check", {}))
    hashes = {}
    hits = {}
    wheres = {}
    checked =[g for g in games if g["pl"].get("checked", True)]
    for g, p in zip(games, procs):
        role, pl = g["role"], g["pl"]
        gres = res["games"][role]
        gres["exit"] = p.returncode
        text = open(os.path.join(g["dir"], "log.txt"), encoding="utf-8", errors="replace").read()
        sync = [l for l in text.splitlines() if l.startswith("[sync]")]
        if pl.get("checked", True):
            hits[role] = [l for l in text.splitlines() if l.startswith("[story] damage: ")]
        gres["sync_lines"] = len(sync)
        gres["shots"] = sorted(os.path.basename(f) for f in glob.glob(os.path.join(g["dir"], "shots", "*.png")))
        ok_exit = pl.get("exit", s.get("exit", [0]))
        if role in timed_out:
            why.append(f"{role}: ran too long (killed)")
        elif ok_exit != "any" and p.returncode not in ([ok_exit] if isinstance(ok_exit, int) else ok_exit):
            why.append(f"{role}: exit {p.returncode}")
        allowed = set(s.get("allow_failures", []) + pl.get("allow_failures", []))
        for name, rx in FAILURES.items():
            if name not in allowed and (m := re.search(rx, text, re.M)):
                why.append(f"{role}: {name}: {text[m.start():text.find(chr(10), m.start())][:160]}")
        for rx in s.get("log_must", []) + pl.get("log_must", []):
            if not re.search(rx, text, re.M):
                why.append(f"{role}: log lacks /{rx}/")
        for rx in s.get("log_must_not", []) + pl.get("log_must_not", []):
            if (m := re.search(rx, text, re.M)):
                why.append(f"{role}: log has /{rx}/: {text[m.start():text.find(chr(10), m.start())][:160]}")
        # (the scenario's settings under the player's, as the game got them: a scenario-wide msaa counts)
        seen = dict(pl, settings={**(s.get("settings") or {}), **(pl.get("settings") or {})})
        why += [f"{role}: {w}" for w in missing_captures(seen, g["env"], gres["shots"], g["backend"])]
        if not pl.get("checked", True):
            continue  # (a game outside the session's checks: an older release, a refused one)
        if chk["drift"] and (d := sum(1 for l in sync if "DRIFT" in l)):
            why.append(f"{role}: {d} DRIFT lines")
        if chk["linked"] and not any("net: session with player" in l for l in sync):
            why.append(f"{role}: never linked")
        if chk["names"]:
            named = {m.group(1) for l in sync for m in [SYNC_NAME.search(l)] if m}
            if len(named) < len(checked) - 1:
                why.append(f"{role}: learned {len(named)} other players' names of {len(checked) - 1}")
        found = [m.group(1) for l in sync for m in [SYNC_PROGRESS.search(l)] if m]
        hashes[role] = found[-1] if found else None
        if chk.get("same_where"):
            wh = [m for l in text.splitlines() for m in [MP_WHERE.match(l)] if m]
            wheres[role] = wh[-1] if wh else None
        if chk["battle_ended"] and g is checked[0] and \
                not any("the shared battle ends (back in the field)" in l for l in sync):
            why.append(f"{role}: the shared battle never ended back in the field")
    res["progress"] = hashes
    if chk.get("same_where"):
        why += same_where_problems(wheres, chk["same_where"])
    if chk["same_hits"] and hits:
        first_role, want = next(iter(hits.items()))
        if not want:
            why.append(f"{first_role}: no [story] damage lines (same_hits needs TTYD_SYNC_BATTLE_LOG)")
        for role, got in hits.items():
            if got != want:
                at = next((i for i, (x, y) in enumerate(zip(want, got)) if x != y), min(len(want), len(got)))
                why.append(f"{role}: hits differ from {first_role}'s at #{at + 1}: "
                           f"{got[at] if at < len(got) else 'none'} | {want[at] if at < len(want) else 'none'}")
    if chk["progress"] and hashes:
        if len(set(hashes.values())) != 1 or None in hashes.values() or \
                next(iter(hashes.values())) == "0000000000000000/0000000000000000":
            why.append("progress differs: " + ", ".join(f"{r} {h}" for r, h in hashes.items()))
    for w in windows:
        why.append(f"{w[0]}: visible window pid {w[1]}")
    why += [f"pid {pid} could not be stopped (still running)" for pid in unkilled]
    if not positive_checks(s):  # (testpass.py lint)
        why.append("no positive check (linked, progress, or a log_must)")
    res["ok"] = not why
    if why:
        res["why"] = why
    if not a.keep_user:
        for g in games:
            shutil.rmtree(g["user"], ignore_errors=True)
    json.dump(res, open(os.path.join(out, "result.json"), "w"), indent=1)
    return res


def compare_one(s, version, run, ref):
    """This run's captures and fingerprint against the reference run's."""
    out = os.path.join(OUT_ROOT, run, s["name"], version)
    base = os.path.join(OUT_ROOT, ref, s["name"], version)
    res = {"scenario": s["name"], "version": version, "ok": True, "shots": {}}
    if not os.path.isdir(out):
        return dict(res, ok=False, why="not run")
    if not os.path.isdir(base):
        # the reference never ran it (a scenario newer than the reference, or a version it wasn't run on): nothing
        # to compare with, said as a skip and counted apart from those compared, never as a pass or a failure. A
        # reference folder with no result or a capture missing still fails (below)
        why = "no reference: " + ("a new scenario" if s.get("new") else
                                  "the reference never ran it" if not os.path.isdir(os.path.dirname(base)) else
                                  f"the reference never ran it on {version}")
        return dict(res, skipped=True, noref=True, why=why)
    results = {}
    for side, d in (("reference", base), ("this", out)):
        try:
            r = json.load(open(os.path.join(d, "result.json")))
        except (OSError, ValueError):
            r = {"ok": False, "why": "no result.json"}
        if not r.get("ok"):
            return dict(res, ok=False, why=f"the {side} run failed: {r.get('why')}")
        results[side] = r
    if results["this"].get("skipped"):  # (nothing to compare: said as a skip, never as a pass)
        return dict(res, skipped=True, why=f"this run was skipped: {results['this'].get('why')}")
    if results["reference"].get("skipped"):
        return dict(res, ok=False, why=f"the reference was skipped: {results['reference'].get('why')}")
    if s.get("fingerprint") and not os.path.exists(os.path.join(base, "fingerprint.txt")):
        return dict(res, ok=False, why="no reference fingerprint")
    opts = s.get("compare", {})
    os.makedirs(os.path.join(out, "diff"), exist_ok=True)
    # the captures: a game's in shots/, an online scenario's players' in <role>/shots/ (each compared with the same
    # role's), named by their path under the run folder ("guest/shots/x.png")
    dirs = ["shots"] if "online" not in s else sorted(
        {os.path.join(os.path.basename(os.path.dirname(d)), "shots") for root in (base, out)
         for d in glob.glob(os.path.join(root, "*", "shots")) if os.path.isdir(d)})

    def shots_in(root):
        return {os.path.relpath(f, root).replace(os.sep, "/") for d in dirs
                for f in glob.glob(os.path.join(root, d, "*.png"))}
    theirs, mine_all = shots_in(base), shots_in(out)

    def key(name):  # (a game's own captures by their file name, as before; a player's as role/x.png)
        return name[len("shots/"):] if name.startswith("shots/") else name.replace("/shots/", "/")
    for name in sorted(theirs):
        mine = os.path.join(out, name)
        if name not in mine_all:
            res["shots"][key(name)] = {"ok": False, "why": "missing"}
        else:
            res["shots"][key(name)] = image_diff(os.path.join(base, name), mine,
                                                 os.path.join(out, "diff", key(name).replace("/", "__")),
                                                 opts.get("tolerance", 8), opts.get("threshold", 0.002))
        res["ok"] &= res["shots"][key(name)]["ok"]
    for name in sorted(mine_all - theirs):
        # a capture the reference hasn't: compared with nothing, so no pass, unless the scenario is marked
        # "new": true (its first reference is being made) (a reference whose captures broke passed everything)
        res["shots"][key(name)] = {"ok": bool(s.get("new")), "why": "no reference" +
                                   (" (a new scenario)" if s.get("new") else "")}
        res["ok"] &= bool(s.get("new"))
    if s.get("fingerprint") and not os.path.exists(os.path.join(out, "fingerprint.txt")):
        res["ok"] = False
        res["fingerprint"] = {"ok": False, "why": "missing"}
    fa, fb = os.path.join(base, "fingerprint.txt"), os.path.join(out, "fingerprint.txt")
    if os.path.exists(fa):
        if not os.path.exists(fb):
            res["fingerprint"] = {"ok": False, "why": "missing"}
        else:
            la, lb = open(fa).read().splitlines(), open(fb).read().splitlines()
            first = next((i for i, (x, y) in enumerate(zip(la, lb)) if x.split()[:2] != y.split()[:2]), None)
            # a run that ends a frame or two sooner or later (how a build exits) is not a difference in play
            if first is None and abs(len(la) - len(lb)) > opts.get("fingerprint_slack", 2):
                first = min(len(la), len(lb))
            res["fingerprint"] = {"ok": first is None, "frames": len(lb), "reference_frames": len(la)}
            if first is not None:
                res["fingerprint"]["first_difference"] = {"line": first + 1,
                                                          "reference": la[first] if first < len(la) else None,
                                                          "this": lb[first] if first < len(lb) else None}
        res["ok"] &= res["fingerprint"]["ok"]
    json.dump(res, open(os.path.join(out, "compare.json"), "w"), indent=1)
    return res


def contact_sheet(run, cols=4, scale=2):
    """Every capture of a run, halved, in rows of cols (sheet_NN.png, 16 a sheet; sheets.txt says which is which)."""
    shots = sorted(glob.glob(os.path.join(OUT_ROOT, run, "*", "*", "shots", "*.png")))
    tw, th = 640 // scale, 480 // scale
    per = cols * 4
    index = []
    for n in range(0, len(shots), per):
        part = shots[n:n + per]
        rows = (len(part) + cols - 1) // cols
        W, H = cols * tw, rows * th
        img = bytearray(W * H * 4)
        for k, path in enumerate(part):
            w, h, px = png_read(path)
            ox, oy = (k % cols) * tw, (k // cols) * th
            sx = max(1, w // tw)
            sy = max(1, h // th)
            for y in range(min(th, h // sy)):
                src = (y * sy) * w * 4
                dst = ((oy + y) * W + ox) * 4
                for x in range(min(tw, w // sx)):
                    s = src + x * sx * 4
                    img[dst + x * 4:dst + x * 4 + 4] = px[s:s + 4]
            index.append(f"sheet_{n // per:02d} #{k}: {os.path.relpath(path, os.path.join(OUT_ROOT, run))}")
        png_write(os.path.join(OUT_ROOT, run, f"sheet_{n // per:02d}.png"), W, H, img)
    open(os.path.join(OUT_ROOT, run, "sheets.txt"), "w").write("\n".join(index) + "\n")
    print("\n".join(index))


def wanted_versions(a):
    """--versions, checked: a name that is no version (US, pal) gave no runs and a pass of 0 of 0"""
    want = [v.strip() for v in a.versions.split(",") if v.strip()] if a.versions else list(VERSIONS)
    bad = [v for v in want if v not in VERSIONS]
    if bad or not want:
        sys.exit(f"testpass: --versions {a.versions!r}: {', '.join(bad) or 'none'} is no version "
                 f"(one or more of {','.join(VERSIONS)})")
    return want


def versions_of(s, a):
    return [v for v in wanted_versions(a) if v in s.get("versions", list(VERSIONS))]


def compare_notes(cmp):
    """the compare's own count: how many were compared with a reference at all (a skip for "no reference" is no
    comparison), and a failure when none was: a compare of nothing is no pass. (notes, failed)"""
    noref = sum(1 for r in cmp if r.get("noref"))
    compared = sum(1 for r in cmp if not r.get("skipped"))
    notes = [f"{compared} of {len(cmp)} compared with the reference; {noref} the reference never ran (skipped)"]
    if cmp and compared == 0:
        notes.append("nothing was compared: a failure")
    return notes, bool(cmp) and compared == 0


def manifest_path(run):
    return os.path.join(OUT_ROOT, run, "jobs.json")


def manifest_merge(old, jobs, rows, chosen, existing, part):
    """jobs.json's jobs and failed_unrun after this part: the old ones, with this part's jobs (its stamp) and rows.
    The broken-file rows are a whole-tree check made again by every part, so only this part's stand (a file fixed
    since is no longer a failure); an old unknown-version row stays only for a scenario this part didn't select
    (one it did is judged again); a job or row of a scenario no file names any more (renamed, deleted) goes."""
    listed = {(j["scenario"], j["version"]): j for j in old.get("jobs", []) if j["scenario"] in existing}
    for name, v in jobs:
        listed[(name, v)] = {"scenario": name, "version": v, "part": part}
    fixed = {(r["scenario"], r["version"]): r for r in old.get("failed_unrun", [])
             if r["version"] != "-" and r["scenario"] in existing and r["scenario"] not in chosen}
    fixed.update({(r["scenario"], r["version"]): r for r in rows})
    return sorted(listed.values(), key=lambda j: (j["scenario"], j["version"])), list(fixed.values())


def manifest_merge_problems():
    """testpass.py lint's check of manifest_merge on made-up parts: [] when right"""
    old = {"jobs": [{"scenario": "a", "version": "us", "part": "p1"}, {"scenario": "gone", "version": "us",
                                                                        "part": "p1"}],
           "failed_unrun": [{"scenario": "b", "version": "-", "ok": False, "why": "does not parse"},
                            {"scenario": "c", "version": "xx", "ok": False, "why": "unknown version 'xx'"},
                            {"scenario": "d", "version": "yy", "ok": False, "why": "unknown version 'yy'"}]}
    jobs, failed = manifest_merge(old, [("b", "us"), ("c", "us")], [], {"b", "c"}, {"a", "b", "c", "d"}, "p2")
    problems = []
    if [(j["scenario"], j["part"]) for j in jobs] != [("a", "p1"), ("b", "p2"), ("c", "p2")]:
        problems.append(f"manifest_merge: jobs {jobs}")
    if [(r["scenario"], r["version"]) for r in failed] != [("d", "yy")]:
        problems.append(f"manifest_merge: failed_unrun {failed} (a fixed file or version must clear its row)")
    return problems


def write_manifest(a, jobs, rows, chosen=(), existing=None):
    """<run>/jobs.json, before any job starts: every scenario and version this run part selected, each with the
    part's stamp (its result.json gets the same one), and the rows that fail without running (broken files,
    unknown versions). A run made in parts (the same --run again) adds to it: a job rerun takes the new stamp
    (manifest_merge says what an earlier part's entries become)."""
    path = manifest_path(a.run)
    try:
        m = json.load(open(path, encoding="utf-8"))
    except (OSError, ValueError):
        m = {}
    if existing is None:
        existing = {j["scenario"] for j in m.get("jobs", [])} | {r["scenario"] for r in m.get("failed_unrun", [])}
    listed, fixed = manifest_merge(m, [(s["name"], v) for s, v in jobs], rows, set(chosen), set(existing), a.part)
    m = {"jobs": listed, "failed_unrun": fixed, "parts": m.get("parts", []) + [a.part]}
    tmp = path + ".tmp"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(m, f, indent=1)
    os.replace(tmp, path)


def summary_rows(run):
    """the run's rows from its results, as its jobs.json lists them: a job with no result.json (not run, the runner
    killed, or still going) and one whose result is an older run's (another stamp) are failures; a result that no
    listed job made is left out, and said. Without a jobs.json (a run older than it), every result.json, and a note
    that jobs not run can't be told. (rows, notes)"""
    found = {}
    for p in sorted(glob.glob(os.path.join(OUT_ROOT, run, "*", "*", "result.json"))):
        try:
            r = json.load(open(p))
        except (OSError, ValueError) as e:
            parts = os.path.normpath(p).split(os.sep)
            r = {"scenario": parts[-3], "version": parts[-2], "ok": False, "why": f"result.json unreadable: {e!r}"}
        found[(r.get("scenario"), r.get("version"))] = r
    try:
        m = json.load(open(manifest_path(run), encoding="utf-8"))
    except OSError:
        return list(found.values()), ["(no jobs.json: a run made before it; a job that never ran or was cut off "
                                      "is missing here, not failed)"]
    except ValueError as e:
        return [{"scenario": "jobs.json", "version": "-", "ok": False, "why": f"unreadable: {e!r}"}], []
    rows = list(m.get("failed_unrun", []))
    for j in m.get("jobs", []):
        r = found.pop((j["scenario"], j["version"]), None)
        if r is None:
            rows.append(dict(j, ok=False, why="no result (not run, or the run was cut off or is still going)"))
        elif r.get("part") != j.get("part"):
            rows.append(dict(j, ok=False, why=f"stale result: from an earlier run ({r.get('part')}); this one "
                                              f"never finished it"))
        else:
            rows.append(r)
    notes = [f"({len(found)} result(s) no job of this run made, left out: "
             + ", ".join(f"{k[0]} {k[1]}" for k in sorted(found, key=str)[:10])
             + (" ..." if len(found) > 10 else "") + ")"] if found else []
    return rows, notes


def report(title, rows, path, notes=()):
    lines = [f"# {title}", ""]
    bad = [r for r in rows if not r["ok"]]
    skipped = [r for r in rows if r["ok"] and r.get("skipped")]
    xfail = [r for r in rows if r["ok"] and r.get("xfail")]
    xpass = [r for r in rows if r["ok"] and r.get("xpass")]
    lines.append(f"{len(rows) - len(bad) - len(skipped) - len(xfail)} of {len(rows)} passed"
                 + (f", {len(skipped)} skipped" if skipped else "")
                 + (f", {len(xfail)} failed as expected" if xfail else "")
                 + (f", {len(xpass)} expected to fail passed (drop their expected_fail)" if xpass else "")
                 + (f", {len(bad)} failed" if bad else "")
                 + ("" if rows else " (nothing was run: a failure)"))
    lines += list(notes)
    for r in rows:
        mark = ("SKIP" if r.get("skipped") else "XFAIL" if r.get("xfail") else "XPASS" if r.get("xpass")
                else "PASS" if r["ok"] else "FAIL")
        extra = f" - {r['why']}" if r.get("skipped") else ""
        if r.get("xfail"):
            extra = f" - expected until {r.get('expected_fail')}: " + "; ".join(str(w) for w in r.get("why") or [])
        elif r.get("xpass"):
            extra = f" - passes now: {r.get('expected_fail')}'s fix is in; drop its expected_fail"
        if not r["ok"]:
            why = r.get("why")
            if why is None:
                parts = [f"{n}: {v.get('why') or str(v.get('fraction', ''))}" for n, v in r.get("shots", {}).items()
                         if not v["ok"]]
                if r.get("fingerprint") and not r["fingerprint"]["ok"]:
                    parts.append(f"fingerprint at line {r['fingerprint'].get('first_difference', {}).get('line')}")
                why = parts
            extra = " - " + ("; ".join(why) if isinstance(why, list) else why)
        lines.append(f"{mark} {r['scenario']} {r['version']}{extra}")
    text = "\n".join(lines) + "\n"
    open(path, "w").write(text)
    print(text, end="")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    r = sub.add_parser("run")
    r.add_argument("names", nargs="*", help="scenarios or groups (default: every scenario)")
    r.add_argument("--run", required=True, help="the run's name: local-docs/testpass/<run>/")
    r.add_argument("--tree", default=REPO, help="the checkout whose builds are run (default: this one)")
    r.add_argument("--versions", help="us,jp,eu (default: each scenario's)")
    r.add_argument("--backend", choices=["offscreen", "headless"], help="over each scenario's own")
    r.add_argument("--iso-dir", help="where the disc images are (default: <tree>/orig/<game id>/)")
    r.add_argument("--data-root", help="extracted discs as <root>/<game id>/files (older headless builds; "
                                       "default $TTYD_DATA_ROOT, <tree>/data, this checkout's data/)")
    r.add_argument("--jobs", type=int, default=4, help="runs at once (the game slot caps them too)")
    r.add_argument("--ref", help="then compare with this run")
    r.add_argument("--keep-user", action="store_true", help="keep each run's user folder")
    r.add_argument("--timeout-scale", type=float, default=1.0)
    r.add_argument("--env", action="append", metavar="KEY=VALUE", help="game environment over the scenarios' own")
    r.add_argument("--set", action="append", metavar="KEY=VALUE", help="a setting over the scenarios' own")
    r.add_argument("--clean-frame", action="store_true",
                   help="TTYD_CLEAN_FRAME=1: nothing drawn over the picture that a README shot doesn't ask for")
    r.add_argument("--no-skips", action="store_true",
                   help="a skipped scenario fails the run (a full pre-release round: no ROM, reference or data left out)")
    r.add_argument("--ui-shots", action="store_true",
                   help="each named shot also with the ImGui layer: <name>_ui.png (the full settings page, the map "
                        "editor's panel); a scenario's \"ui_shots\": true does it for that one")
    c = sub.add_parser("compare")
    c.add_argument("run")
    c.add_argument("names", nargs="*")
    c.add_argument("--ref", required=True)
    c.add_argument("--versions")
    sh = sub.add_parser("sheet", help="contact sheets of a run's captures, for looking them over")
    sh.add_argument("run")
    sm = sub.add_parser("summary", help="report.txt again from every result in a run (runs made in parts)")
    sm.add_argument("run")
    sm.add_argument("--no-skips", action="store_true", help="a skipped scenario fails it too")
    st = sub.add_parser("stereo", help="one stereo capture's disparity numbers (stereo_check's)")
    st.add_argument("png")
    st.add_argument("layout", choices=sorted(STEREO_LAYOUTS))
    li = sub.add_parser("lint", help="every scenario (or those named) as written: it parses, its fields and versions "
                                     "are known, and it has a positive check")
    li.add_argument("names", nargs="*")
    a = ap.parse_args()

    if a.cmd == "stereo":
        summary, bad = stereo_verdict(a.png, a.layout, {})
        print(json.dumps(summary))
        for b in bad:
            print("  " + b)
        return 1 if bad else 0

    if a.cmd == "sheet":
        contact_sheet(a.run)
        return 0
    if a.cmd == "summary":
        rows, notes = summary_rows(a.run)
        report(f"run {a.run} (every scenario and version in it)", rows, os.path.join(OUT_ROOT, a.run, "report.txt"),
               notes)
        return 1 if not rows or any(not r["ok"] for r in rows) or (a.no_skips and any(r.get("skipped") for r in rows)) \
            else 0

    if a.cmd == "lint":
        rows = lint(a.names)
        bad = [r for r in rows if not r["ok"]]
        for r in bad:
            print(f"FAIL {r['scenario']} - {'; '.join(r['why']) if isinstance(r['why'], list) else r['why']}")
        print(f"testpass lint: {len(rows) - len(bad)} of {len(rows)} scenarios pass")
        return 1 if bad or not rows else 0

    if a.cmd == "list":
        for s in all_scenarios():
            print(f"{s['name']:28} [{','.join(s.get('groups', []))}] {','.join(s.get('versions', VERSIONS))}"
                  f" {s.get('backend', 'offscreen')}: {s.get('desc', '')}")
        return 0
    chosen = pick(a.names)
    if a.cmd == "run":
        # a scenario that starts from another's user folder (user_from) or texture dumps (texpack_from) brings that
        # one along, and its own
        by_name = {s["name"]: s for s in all_scenarios()}
        for s in chosen:
            dep = s.get("user_from") or s.get("texpack_from")
            if dep and dep in by_name and by_name[dep] not in chosen:
                chosen.append(by_name[dep])  # (appended: the loop reaches it and its own user_from too)
        a.tree = os.path.abspath(a.tree)
        # this run's own switches over the chosen scenarios' (media captures: tools/port/media/capture.py): --env,
        # --set, --clean-frame; a scenario's "clean_frame": true is the same as the last
        over_env = dict(kv.split("=", 1) for kv in (a.env or []))
        over_set = dict(kv.split("=", 1) for kv in (a.set or []))
        for s in chosen:
            if s.get("clean_frame") or a.clean_frame:
                s["env"] = {**s.get("env", {}), "TTYD_CLEAN_FRAME": "1"}
            if over_env:
                s["env"] = {**s.get("env", {}), **over_env}
            if over_set:
                s["settings"] = {**s.get("settings", {}), **over_set}
            if over_env.get("TTYD_USER_SHOT"):
                s.pop("shots", None)  # (a capture in flight would hold up the burst's frames)
        # (a scenario naming a version that is none, or a file that doesn't parse, is a failure, never left out)
        rows = broken_rows() + [{"scenario": s["name"], "version": v, "ok": False, "why": f"unknown version {v!r}"}
                                for s in chosen for v in s.get("versions", []) if v not in VERSIONS]
        jobs = [(s, v) for s in chosen for v in versions_of(s, a)]
        if not jobs:  # (a pass of "0 of 0" is no pass)
            print(f"testpass: nothing to run: no scenario of {a.names or 'all'} is for --versions {a.versions}",
                  file=sys.stderr)
            for r in rows:
                print(f"FAIL {r['scenario']} {r['version']} - {r['why']}")
            return 1
        os.makedirs(os.path.join(OUT_ROOT, a.run), exist_ok=True)
        # (this part's stamp, in jobs.json and every result.json it writes: summary tells its results from older ones)
        a.part = f"{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
        existing = set(by_name) | {r["scenario"] for r in broken_rows()}
        write_manifest(a, jobs, rows, {s["name"] for s in chosen}, existing)
        lock = threading.Lock()
        # (those that start from another's user folder, user_from, after the one they start from: a chain is played
        # in as many phases as it is long)
        a.user_sources = {j[0]["user_from"] for j in jobs if j[0].get("user_from")}
        names = {s["name"]: s for s in chosen}

        def depth(s, seen=()):
            dep = s.get("user_from") or s.get("texpack_from")
            if not dep or dep not in names or dep in seen:
                return 0
            return 1 + depth(names[dep], seen + (s["name"],))
        phases = {}
        for j in jobs:
            phases.setdefault(depth(j[0]), []).append(j)
        for phase in (phases[d] for d in sorted(phases)):
            with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
                for res in pool.map(lambda j: run_one_safe(j[0], j[1], a), phase):
                    with lock:
                        mark = "FAIL" if not res["ok"] else "SKIP" if res.get("skipped") else "PASS"
                        print(f"{mark} {res['scenario']} {res['version']} "
                              f"({res.get('seconds', '-')} s){'' if res['ok'] else ' - ' + str(res.get('why'))}",
                              flush=True)
                    rows.append(res)
        report(f"run {a.run}", rows, os.path.join(OUT_ROOT, a.run, "report.txt"))
        failed = any(not r["ok"] for r in rows) or (a.no_skips and any(r.get("skipped") for r in rows))
        if a.ref:
            cmp = [compare_one(s, v, a.run, a.ref) for s, v in jobs]
            notes, none = compare_notes(cmp)
            report(f"{a.run} against {a.ref}", cmp,
                   os.path.join(OUT_ROOT, a.run, f"compare_{os.path.basename(os.path.normpath(a.ref))}.txt"), notes)
            failed |= none or any(not r["ok"] for r in cmp)
        return 1 if failed else 0
    if a.cmd == "compare":
        a.backend = None
        # (every scenario and version asked for: one this run hasn't is a "not run" row, not left out)
        jobs = [(s, v) for s in chosen for v in versions_of(s, a)]
        compared = [compare_one(s, v, a.run, a.ref) for s, v in jobs]
        notes, none = compare_notes(compared)
        cmp = broken_rows() + compared
        report(f"{a.run} against {a.ref}", cmp,
               os.path.join(OUT_ROOT, a.run, f"compare_{os.path.basename(os.path.normpath(a.ref))}.txt"), notes)
        return 1 if not cmp or none or any(not r["ok"] for r in cmp) else 0


if __name__ == "__main__":
    sys.exit(main())
