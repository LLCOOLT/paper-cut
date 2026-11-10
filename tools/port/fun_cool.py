#!/usr/bin/env python3
"""Cool Mode (fun toggles, port/src/common/fun_pc.c): where each model's
sunglasses sit, from the disc's models, as port/src/common/fun_cool.inc.

Big heads' head finder (fun_heads.py) picks each model's head; here the head's
cards are drawn front-on as the model stands (its standing animation), and the
eyes found on that picture: the blobs of white (eyeballs) or of near black
(pupils, eye slits, a Shy Guy's mask holes) inside the head, not on its
outline, in a pair side by side. The glasses go over the pair, as wide as the
pair needs, tilted as the pair is. One eye (a face in profile): over it. None
(a Boo, a Bob-omb, a dragon seen as cards): across the face area, two thirds
of the way up the head. OVERRIDES fixes what the pictures get wrong.

Each row is the head group (the first of Big heads' groups) and the glasses'
quad in that group's own coordinates: its middle, and half its width and half
its height as vectors (the game's x and y as the model stands, taken into the
group's space), so the glasses follow the head through every animation, a
paper turn and Big heads. Only names and numbers come out.

Models with no glasses (a NULL group): the back views (a_mario_r..., Peach's
c_peach_r: the back of the head), the ones already wearing sunglasses (they
keep their own), and those Big heads gives no head.

  fun_cool.py DISC [--out FILE] [--models a,b,...] [--sheet OUT.png] [--cols N] [--list]

DISC is the disc image (.iso) or an extracted disc's folder. --sheet draws each
model as it stands with its glasses (the recipe's picture, art/fun/sunglasses,
cut the same way from the disc's c_guard texture 6) for checking by eye.
"""
import argparse
import math
import re
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402
import fun_heads as F  # noqa: E402

PX = 6.0  # the head picture's pixels a unit (the models are about 20 to 60 units tall)
SHADES = ("c_guard", 6)  # the Glitz Pit guard's shades, front-on (art/fun/sunglasses: the recipe's source)
SHADES_ASPECT = 32.0 / 72.0  # its height over its width
LENS_GAP = 0.5  # the lenses' middles apart, a share of the glasses' width
EYE_LENS_WIDE = 1.3  # "eye_cards": the lenses' middles apart over the eyes' (each lens then covers its eye)

# back views: the back of the head toward us (no glasses: from behind they're hidden)
BACK = {"a_mario_r", "a_mario_lr", "a_mario_wr", "a_mario_wlr", "c_peach_r", "c_koopa_r"}
# already wearing sunglasses: they keep their own
OWN = {"c_guard", "c_m_buka", "c_m_waka", "c_kiza"}

# model -> fixes, each optional: "eyes": (x, y) in the head picture's share (0..1 of its width, from the left;
# 0..1 of its height, from the bottom; below 0 or above 1 off the head's cards, where Big heads' head is a
# hat or a moustache) for the glasses' middle; "width": the glasses' width as a share of the head's;
# "tilt": degrees (anticlockwise); "scale": the width found, times this; "dy": a move up as a share of the
# head's height; "dx": a move right as a share of the glasses' width; "whole": the whole model is the face (Big heads' head is a part of it, such as a Bob-omb's
# key); "none": no glasses (not a face); "anchor": the group the glasses ride on (the search starts there,
# instead of at the card the eyes are drawn on); "eye_cards": the cards that are the two eyes (a lens over the
# middle of each, level, on the group they share)
_KMOON = {"eyes": (0.6, 0.62), "width": 0.4}
_HANNYA = {"eyes": (0.45, -0.3), "width": 0.9}
# Hooktail, Gloomtail, Bonetail: a head of cards in the round (the eyes on its sides); the glasses over the near
# eye (its ring of lids, pPlane22), before the snout's shell (the picture's front there), on the head itself;
# a little toward the back, so the near lens (not the gap between the lenses) is over the pupil
_DRAGON = {"head": "locator11", "eyes": (0.5, 0.5), "eye_card": ("pPlane22", 1.45), "dx": 0.1,
           "anchor": "locator11"}
_MENU_YOSHI = {"eyes": (0.36, 0.72), "width": 0.36}  # the Party tab's Yoshis: the eye above the snout
_BRISTLE = {"whole": True, "eyes": (0.5, 0.66), "width": 0.32}  # the eyes above the mouth
# Mario: a lens over each of his eye cards (L_Eye, R_Eye, which blink and swap with the animations), on Face,
# which carries them (a share of the whole head put the far lens over his hair)
_MARIO = {"eye_cards": ("L_Eye", "R_Eye"), "anchor": "Face"}
OVERRIDES = {
    "c_bwhite": {"eyes": (0.5, -0.35), "width": 0.7},
    "c_gans": {"eyes": (0.45, -0.35), "width": 0.8},
    "c_powan": {"eyes": (0.45, -0.35), "width": 0.8},
    "c_pub_m": {"eyes": (0.5, 0.3), "width": 0.7},
    "c_papab": {"eyes": (0.5, -0.1), "width": 0.8},
    "c_sebaschan": {"whole": True, "eyes": (0.5, 0.55), "width": 0.5},
    "c_koopa": {"eyes": (0.35, 0.62), "width": 0.55},
    "c_luigi": {"eye_cards": ("pPlane7", "pPlane8")},  # his eye cards (in group1), as Mario's
    "c_giant": {"whole": True}, "c_heavy": {"whole": True}, "c_bom_deb": {"whole": True},
    "c_jugem_c": {"eyes": (0.66, 0.36), "width": 0.45},  # one eye beside the camera he looks through
    "c_hotdog": {"eyes": (0.5, 0.75), "width": 0.45}, "c_kuri_h": {"width": 0.7},
    "c_mario": _MARIO,
    "c_cook": {"width": 0.42, "dy": 0.08},
    "c_kilr_g": {"eyes": (0.3, 0.73), "width": 0.32}, "c_kilr": {"scale": 0.7},
    "c_kmoon": _KMOON, "c_kmoon_b": _KMOON, "c_kmoon_g": _KMOON, "c_kmoon_wb": _KMOON,
    "c_hannya": _HANNYA, "c_hannya_n": {"eyes": (0.45, -0.6), "width": 0.9}, "c_hannya_t": _HANNYA,
    "c_pakflwr": {"eyes": (0.5, 0.78), "width": 0.6}, "c_pakflwr_a": {"eyes": (0.5, 0.78), "width": 0.6},
    "c_pakflwr_t": {"eyes": (0.5, 0.78), "width": 0.6}, "c_pakflwr_p": {"eyes": (0.5, 0.78), "width": 0.6},
    "c_unten": {"dy": -0.25},
    "c_gonbaba": _DRAGON, "c_gonbaba_b": _DRAGON, "c_gonbaba_z": _DRAGON,
    "c_bomt_n": {"eyes": (0.4, 0.4), "width": 0.5}, "c_vivian": {"eyes": (0.45, 0.42), "width": 0.5},
    "c_nokorin": {"eyes": (0.3, 0.78), "width": 0.42},  # Koopie Koo: over the big eyes
    "c_nokonoko": {"dy": 0.03},  # Koopa Troopa: the eye cards reach below the eyes
    # the Start menu's Party tab
    "EFF_m_bomb": {"eyes": (0.4, 0.4), "width": 0.5}, "EFF_m_kuriko": {"eyes": (0.42, 0.42), "width": 0.5},
    "EFF_m_noko": {"dy": -0.12},
    "c_q_kage": {"eyes": (0.48, 0.02), "width": 0.45}, "c_q_kage_m": {"eyes": (0.8, 0.0), "width": 0.3},
    # the whole model is the face (Big heads gives them none)
    # Magnus: on Lord Crump in the dome, at his depth (his face cards), not on the dome's front: the robot is
    # seen turned, and on the glass they sat beside his face
    "c_mb_robo_1b": {"whole": True, "eyes": (0.5, 0.8), "width": 0.14, "z": 8.5},
    "c_korutesu1": {"eyes": (0.34, 0.36), "width": 0.35},  # Cortez: over his eye, below the hat
    "c_togedaruma": _BRISTLE, "c_togedaruma_b": _BRISTLE,
    # named only in rels/
    "c_korutesu2": {"eyes": (0.09, 0.25), "width": 0.09},  # the whole snake is Big heads' head: his skull's eye
    "c_gonbaba2": _DRAGON,
    "c_n_moa_a": {"whole": True, "eyes": (0.46, 0.5), "width": 0.45},
    "c_n_moa_b": {"whole": True, "eyes": (0.46, 0.5), "width": 0.45},  # the Smorg's moas (the train)
    # not a face: no glasses
    "c_s_hand": {"none": True},
    "c_gonbaba4": {"none": True},  # the Toads Hooktail ate
    "c_q_kage_b": {"none": True}, "c_q_kage_h": {"none": True},  # the Shadow Queen's body and hair (her face: c_b_peach_b)
}
# The placement pass: each checked close up (fun_cool_check.py), where the eye finder took a mouth, a hat or a
# shell for the eyes. Shares of the head picture (fun_cool.py --grid draws it with tenths); "whole" where Big
# heads' head is only a hat or a shell.
_WHOLE = {"whole": True}
OVERRIDES.update({
    "c_atmic_trs": {"eyes": (0.32, 0.67), "width": 0.4},
    "c_bakudan": {"eyes": (0.5, 0.43), "width": 0.45},
    "c_baria": {"eyes": (0.5, 0.74), "width": 0.95}, "c_baria_c": {"like": "c_baria"}, "c_baria_z": {"like": "c_baria"},
    "c_babyyoshi": {"eyes": (0.55, 0.56), "width": 0.4, "tilt": 0.0},  # the Yoshi kid: on the eye (white, pupil to the back), level
    "c_babyyoshi2": {"like": "c_babyyoshi"}, "c_babyyoshi6": {"like": "c_babyyoshi"},
    "c_babyyoshi3": {"eyes": (0.55, 0.56), "width": 0.4, "tilt": 0.0}, "c_babyyoshi4": {"eyes": (0.55, 0.56), "width": 0.4, "tilt": 0.0},
    "c_babyyoshi5": {"eyes": (0.55, 0.56), "width": 0.4, "tilt": 0.0}, "c_babyyoshi7": {"eyes": (0.55, 0.56), "width": 0.4, "tilt": 0.0},
    "c_yoshi": {"eyes": (0.36, 0.82), "width": 0.4},
    "c_lp_met": {"eyes": (0.28, 0.18), "width": 0.4},  # the face under the shell, not the shell
    "c_sinnosuke": {"eyes": (0.5, 0.56)}, "c_sinnosuke_h": {"eyes": (0.5, 0.56)},
    "c_bubusan": {"eyes": (0.2, 0.78), "width": 0.2},
    "c_burosu_b": {"eyes": (0.33, 0.6), "width": 0.35},
    "c_burosu_f": {"like": "c_burosu_b"}, "c_burosu_h": {"like": "c_burosu_b"},
    "c_cyuru": {"eyes": (0.5, 0.48), "width": 0.45},
    "c_f_oba": {"eyes": (0.44, 0.53), "width": 0.45},
    "c_gara": {"eyes": (0.18, 0.55), "width": 0.35},
    "c_gazigazi": {"eyes": (0.56, 0.63), "width": 0.25},
    "c_ghost_k": {"eyes": (0.38, 0.49), "width": 0.3},
    "c_jpiko_p": {"eyes": (0.48, 0.43), "width": 0.35},
    "c_jugemu": {"eyes": (0.27, 0.5), "width": 0.55}, "c_jugemu_b": {"like": "c_jugemu"},
    "c_karon_h": {"eyes": (0.33, 0.74), "width": 0.4}, "c_karon_r_h": {"eyes": (0.33, 0.74), "width": 0.4},
    "c_kinodebu": {"eyes": (0.49, 0.6), "width": 0.3},
    "c_kuri_h": {"eyes": (0.47, 0.55), "width": 0.65},
    "c_kuri_kn": {"eyes": (0.48, 0.42), "width": 0.6},
    "c_lp_pansy": {"eyes": (0.5, 0.5), "width": 0.35},
    "c_m_boss": {"eyes": (0.38, 0.74), "width": 0.3},
    "c_m_ojo": {"eyes": (0.4, 0.6), "width": 0.4},
    "c_maho_w": {"eyes": (0.42, 0.6), "width": 0.35},
    "c_majinai": {"eyes": (0.5, 0.25), "width": 0.35},
    "c_marco": {"eyes": (0.33, 0.31), "width": 0.25},
    "c_maririn": {"eyes": (0.5, 0.27), "width": 0.4},
    "c_mokomoko": {"eyes": (0.42, 0.63), "width": 0.25},
    "c_monban": {"eyes": (0.42, 0.6), "width": 0.6},
    "c_monban_t": {"eyes": (0.47, 0.5), "width": 0.55},
    "c_nokoteki": {"eyes": (0.58, 0.93), "width": 0.35},
    "c_peach_f": {"eyes": (0.38, 0.55), "width": 0.4},
    "c_pshop": {"eyes": (0.4, 0.43), "width": 0.45},
    "c_pub_m": {"eyes": (0.44, 0.62), "width": 0.6},  # over his own round glasses
    "c_sebaschan": {"whole": True, "eyes": (0.35, 0.55), "width": 0.45},
    "c_shuryo": {"eyes": (0.47, 0.43), "width": 0.3},
    "c_tchibi": {"eyes": (0.56, 0.7), "width": 0.7},
    "c_teresa": {"eyes": (0.33, 0.78), "width": 0.35},
    "c_teresa_p": {"eyes": (0.33, 0.66), "width": 0.3},
    "c_togenoko": {"eyes": (0.3, 0.68), "width": 0.3},
    "c_togenoko_b": {"eyes": (0.25, 0.68), "width": 0.3},
    "c_tyusan": {"eyes": (0.38, 0.68), "width": 0.3},
    "c_tyutyu_t": {"width": 0.25},
    "c_wanwan": {"width": 0.9}, "c_wanwan_a": {"like": "c_wanwan"},
    "c_zako_e": {"eyes": (0.5, 0.55), "width": 0.55},
    "c_kmoon": {"eyes": (0.52, 0.62), "width": 0.75}, "c_kmoon_b": {"like": "c_kmoon"},
    "c_kmoon_g": {"like": "c_kmoon"}, "c_kmoon_wb": {"like": "c_kmoon"},
    "c_majyorin": {"whole": True, "eyes": (0.23, 0.6), "width": 0.2},  # Beldam: the face under the hat
    # only a hat or a shell as Big heads' head: placed on the whole model
    "c_bteresa2": {"whole": True, "eyes": (0.45, 0.5), "width": 0.38},
    "c_bwhite": {"whole": True, "eyes": (0.4, 0.55), "width": 0.5},
    "c_gans": {"whole": True, "eyes": (0.38, 0.73), "width": 0.3},
    "c_geso_kn": {"whole": True, "eyes": (0.48, 0.6), "width": 0.4},
    "c_gesso": {"whole": True, "eyes": (0.48, 0.48), "width": 0.42, "tilt": 7.0},  # the Blooper boss: both eyes (the right one higher)
    "c_joho_a": {"whole": True, "eyes": (0.4, 0.64), "width": 0.45},
    "c_korutesu3": {"whole": True, "eyes": (0.18, 0.46), "width": 0.2},
    "c_m_gansu": {"whole": True, "eyes": (0.23, 0.67), "width": 0.25},
    "c_met": {"whole": True, "eyes": (0.42, 0.3), "width": 0.5}, "c_met_h": {"like": "c_met"},
    "c_papab": {"whole": True, "eyes": (0.27, 0.45), "width": 0.3},
    "c_powan": {"whole": True, "eyes": (0.42, 0.72), "width": 0.25},
    "c_peach": {"eye_cards": ("pPlane13", "pPlane14")},  # her eye cards (the eye finder took one, and the glasses grew past her face)
    "c_puku_c": {"whole": True, "eyes": (0.35, 0.6), "width": 0.3},
    "c_resaresa": {"whole": True, "eyes": (0.38, 0.6), "width": 0.4},
    "c_sinemon": {"eyes": (0.48, -0.08), "width": 0.6},  # the eyes below the spikes Big heads takes for the head
    "c_sinemon_a": {"like": "c_sinemon"}, "c_sinemon_h": {"like": "c_sinemon"},
    "c_sinemon_m": {"like": "c_sinemon"}, "c_sinemon_w": {"like": "c_sinemon"},
    "c_star_bom": {"whole": True, "eyes": (0.45, 0.6), "width": 0.5},
    "c_togezo": {"whole": True, "eyes": (0.2, 0.52), "width": 0.25}, "c_togezo_b": {"like": "c_togezo"},
    "c_uranai": {"whole": True, "eyes": (0.48, 0.6), "width": 0.3},
    "c_zako_km": {"whole": True, "eyes": (0.49, 0.83), "width": 0.45}, "c_zako_m": {"like": "c_zako_km"},
    # not faces, or wearing their own
    "c_gesso2": {"none": True}, "c_gesso3": {"none": True}, "c_gesso4": {"none": True},  # Blooper's tentacles
    "c_koura": {"none": True}, "c_mb_robo_c": {"none": True}, "c_shuryo_p": {"none": True},
    "c_tamago": {"none": True},
})
OWN |= {"c_haiyu", "c_megane"}


def overrides(model):
    if model in OVERRIDES:
        return OVERRIDES[model]
    if model.startswith("EFF_m_yoshi"):
        return _MENU_YOSHI
    if model[:8] in ("a_mario", "b_mario", "e_mario") or model[:7] in ("a_mario", "b_mario", "e_mario"):
        return _MARIO  # Mario's eyes are blue: no white or near-black to find
    return {}


def _sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def _lin(inv, v):
    """a direction into a group's space (the inverse's 3x3)"""
    return [inv[r][0] * v[0] + inv[r][1] * v[1] + inv[r][2] * v[2] for r in range(3)]


def head_picture(P, heads, images):
    """the head's cards front-on: (rows of RGBA or None, z per pixel, x0, y1 (world at the top left), w, h)"""
    cards = []
    for h, _local in heads:
        cards += [c for c in P.subtree(h) if c in P.cards]
    pts = [p for c in cards for p in P.cards[c]]
    if not pts:
        return None
    lo, hi = F._bbox(pts)
    w = max(4, int(math.ceil((hi[0] - lo[0]) * PX)) + 2)
    h = max(4, int(math.ceil((hi[1] - lo[1]) * PX)) + 2)
    if w * h > 1000 * 1000:
        return None
    x0, y1 = lo[0] - 1.0 / PX, hi[1] + 1.0 / PX
    img = [[None] * w for _ in range(h)]
    zb = [[-1e30] * w for _ in range(h)]
    tris = []
    for gi in cards:
        g = P.groups[gi]
        m = P.mats[gi]
        for di, fan in P.geo.shape_polys(g.shape):
            dr = P.geo.draws[di]
            im, ts, tt = None, 0.0, 0.0
            if dr["tex_count"]:
                slot = dr["tex_data"][0]
                if 0 <= slot < len(P.geo.texdata):
                    b = P.geo.texdata[slot][0] + P.tex[slot][0]
                    if 0 <= b < len(P.geo.texbinds):
                        ti = P.geo.texbinds[b][0]
                        im = images[ti] if ti < len(images) else None
                    ts, tt = P.tex[slot][1], P.tex[slot][2]
            q = []
            for pi, col, uv in fan:
                x, y, z = F._apply(m, P.pos[pi])
                q.append(((x - x0) * PX, (y1 - y) * PX, z, (uv[0] + ts, uv[1] + tt) if uv else None, col))
            for k in range(1, len(q) - 1):
                tri = (q[0], q[k], q[k + 1])
                tris.append((sum(p[2] for p in tri) / 3.0, gi, tri, im))
    tris.sort(key=lambda t: (t[0], t[1]))
    gid = [[-1] * w for _ in range(h)]
    for _z, g, tri, im in tris:
        _raster(img, zb, w, h, tri, im, gid, g)
    head_picture.gid = gid  # which card each pixel is (the last picture drawn)
    return img, zb, x0, y1, w, h


def frame_picture(P, images, world, width):
    """the model's cards about the glasses (the sheet's view), as head_picture draws them"""
    keep = [c for c, pts in P.cards.items()
            if abs(sum(p[0] for p in pts) / len(pts) - world[0]) < 2.5 * width + 4
            and -3.0 * width - 6 < sum(p[1] for p in pts) / len(pts) - world[1] < 2.0 * width + 4]

    class _Only:
        groups, cards, mats, geo, pos, tex = P.groups, {c: P.cards[c] for c in keep}, P.mats, P.geo, P.pos, P.tex

        @staticmethod
        def subtree(_g):
            return keep
    return head_picture(_Only, [(P.root, None)], images) if keep else None


def _raster(img, zb, w, h, tri, im, gid=None, g=-1):
    (xa, ya, za, uva, ca), (xb, yb, zb_, uvb, cb), (xc, yc, zc, uvc, cc) = tri
    d = (yb - yc) * (xa - xc) + (xc - xb) * (ya - yc)
    if abs(d) < 1e-9:
        return
    for py in range(max(0, int(min(ya, yb, yc))), min(h - 1, int(max(ya, yb, yc)) + 1) + 1):
        cy = py + 0.5
        for px in range(max(0, int(min(xa, xb, xc))), min(w - 1, int(max(xa, xb, xc)) + 1) + 1):
            cx = px + 0.5
            l0 = ((yb - yc) * (cx - xc) + (xc - xb) * (cy - yc)) / d
            l1 = ((yc - ya) * (cx - xc) + (xa - xc) * (cy - yc)) / d
            l2 = 1.0 - l0 - l1
            if l0 < -1e-6 or l1 < -1e-6 or l2 < -1e-6:
                continue
            col = [l0 * ca[i] + l1 * cb[i] + l2 * cc[i] for i in range(4)]
            if im is not None and uva is not None:
                tw, th, rows = im
                u = l0 * uva[0] + l1 * uvb[0] + l2 * uvc[0]
                v = l0 * uva[1] + l1 * uvb[1] + l2 * uvc[1]
                t = rows[int(v * th) % th][int(u * tw) % tw]
                if t[3] < 128:
                    continue
                col = [t[i] * col[i] / 255.0 for i in range(3)]
            elif col[3] < 128:
                continue
            img[py][px] = (int(col[0]), int(col[1]), int(col[2]))
            if gid is not None:
                gid[py][px] = g
            zb[py][px] = max(zb[py][px], l0 * za + l1 * zb_ + l2 * zc)


def _hsl(c):
    r, g, b = c
    mx, mn = max(c), min(c)
    s = 0.0 if mx == 0 else (mx - mn) / mx
    return 0.299 * r + 0.587 * g + 0.114 * b, s


def find_eyes(img, w, h):
    """[(cx, cy, bw, bh, area, kind)] eye blobs, in pixels (y down)"""
    opaque = [[img[y][x] is not None for x in range(w)] for y in range(h)]
    # inside: two pixels from any clear one (the outline is dark too)
    inside = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if not opaque[y][x]:
                continue
            ok = True
            for dy in (-2, -1, 0, 1, 2):
                for dx in (-2, -1, 0, 1, 2):
                    yy, xx = y + dy, x + dx
                    if not (0 <= yy < h and 0 <= xx < w) or not opaque[yy][xx]:
                        ok = False
                        break
                if not ok:
                    break
            inside[y][x] = ok
    total = sum(1 for y in range(h) for x in range(w) if opaque[y][x])
    out = []
    for kind in ("white", "dark"):
        sel = [[False] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                if inside[y][x]:
                    l, s = _hsl(img[y][x])
                    sel[y][x] = (l >= 190 and s < 0.28) if kind == "white" else (l <= 62)
        seen = [[False] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                if not sel[y][x] or seen[y][x]:
                    continue
                stack, px = [(x, y)], []
                seen[y][x] = True
                while stack:
                    a, b = stack.pop()
                    px.append((a, b))
                    for na, nb in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                        if 0 <= na < w and 0 <= nb < h and sel[nb][na] and not seen[nb][na]:
                            seen[nb][na] = True
                            stack.append((na, nb))
                n = len(px)
                xs, ys = [p[0] for p in px], [p[1] for p in px]
                bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
                if n < 3 or n < 0.0015 * total or n > 0.12 * total:
                    continue
                if n < 0.35 * bw * bh or max(bw, bh) > 4.0 * min(bw, bh):
                    continue  # a line (a mouth, a hat's edge), not an eye
                if bh > 0.45 * h or bw > 0.45 * w:
                    continue
                out.append((sum(xs) / n + 0.5, sum(ys) / n + 0.5, bw, bh, n, kind))
    return out, total


def pick_eyes(blobs, w, h, total):
    """the pair (or one, or none): (cx, cy, width, tilt degrees, how)"""
    # an eye: a white with a dark pupil in it, or a dark eye with a white glint (teeth, a mouth, a hat's
    # band have none)
    def nested(b):
        return any(o is not b and o[5] != b[5] and o[4] < b[4] and abs(o[0] - b[0]) < b[2] / 2
                   and abs(o[1] - b[1]) < b[3] / 2 for o in blobs)
    eyeish = {id(b) for b in blobs if nested(b)}
    # a pupil or a glint inside a bigger blob's box is part of that eye
    blobs = [b for b in blobs if not any(o is not b and o[4] > b[4] and abs(o[0] - b[0]) < o[2] / 2
                                         and abs(o[1] - b[1]) < o[3] / 2 for o in blobs)]
    blobs = [b for b in blobs if b[4] >= 0.004 * total]
    best, pair = 0.0, None
    for i in range(len(blobs)):
        for j in range(i + 1, len(blobs)):
            a, b = blobs[i], blobs[j]
            if a[0] > b[0]:
                a, b = b, a
            dx, dy = b[0] - a[0], b[1] - a[1]
            if dx < 0.08 * w or dx > 0.7 * w or abs(dy) > 0.14 * h or abs(dy) > 0.5 * dx:
                continue
            if max(a[4], b[4]) > 3.0 * min(a[4], b[4]) or dx < 1.2 * (a[2] + b[2]) / 2:
                continue
            cy = (a[1] + b[1]) / 2
            if cy < 0.08 * h or cy > 0.85 * h:
                continue
            # alike, big, and high in the middle third
            s = math.sqrt(a[4] * b[4]) * (1.0 - abs(dy) / (0.14 * h) * 0.5)
            s *= 1.0 + (0.4 if a[5] == b[5] else 0.0)
            s *= 1.0 - 0.5 * abs((a[0] + b[0]) / 2 - w / 2) / w
            s *= 1.0 + 1.5 * ((id(a) in eyeish) + (id(b) in eyeish))
            s *= 1.0 + 0.6 * (1.0 - cy / h)  # the eyes are above the mouth
            if s > best:
                best, pair = s, (a, b)
    if pair is not None:
        a, b = pair
        dx = math.hypot(b[0] - a[0], b[1] - a[1])
        span = max(dx / LENS_GAP, (dx + (a[2] + b[2]) / 2) * 1.15)
        tilt = math.degrees(math.atan2(-(b[1] - a[1]), b[0] - a[0]))
        pick_eyes.at = [(a[0], a[1]), (b[0], b[1])]
        return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, span, max(-20.0, min(20.0, tilt)), "pair")
    singles = [b for b in blobs if 0.1 * h < b[1] < 0.8 * h]
    if singles:
        b = max(singles, key=lambda e: e[4] * (1.0 + 1.5 * (id(e) in eyeish)) * (1.6 - e[1] / h))
        pick_eyes.at = [(b[0], b[1])]
        return (b[0], b[1], b[2] * 2.6, 0.0, "one")
    return None


_EYE_NAME = re.compile(r"(eye|hitomi|sirome|shirome|kurome|klurome|medama|mabuta|(^|_|hidari|migi)me\d*$|^me(_|\d|$))")


def named_eyes(P, gid, w, h, img=None, names=None):
    """the box (head picture pixels: x0, y0, x1, y1) of what shows of the cards the model's rig names as its
    eyes (names: by hand, those cards), and their front z; or None. named_eyes.mid: the middle of what is
    eye-coloured on them (the whites and the pupils, not the lids round them), or None; named_eyes.each:
    per eye card that shows, the middle of what shows of it, left to right"""
    eyes = set()
    for gi in P.cards:
        n = P.groups[gi].name.split("|")[-1]
        if names is not None:
            if n in names:
                eyes.add(gi)
        elif _EYE_NAME.search(n.lower()) and "arme" not in n.lower():
            eyes.add(gi)
    px = [(x, y) for y in range(h) for x in range(w) if gid[y][x] in eyes]
    if len(px) < 4:
        return None
    z = max(p[2] for gi in eyes for p in P.cards[gi])
    named_eyes.cards = sorted({gid[y][x] for x, y in px})  # the eye cards that show
    named_eyes.each = []
    for gi in named_eyes.cards:
        mine = [(x, y) for x, y in px if gid[y][x] == gi]
        named_eyes.each.append(((min(p[0] for p in mine) + max(p[0] for p in mine) + 1) / 2,
                                (min(p[1] for p in mine) + max(p[1] for p in mine) + 1) / 2))
    named_eyes.each.sort()
    named_eyes.mid = None
    if img is not None:
        eyeish = [(x, y) for x, y in px if img[y][x] is not None and (_hsl(img[y][x])[0] >= 190 or _hsl(img[y][x])[0] <= 62)]
        if len(eyeish) >= 4:
            named_eyes.mid = (sum(p[0] for p in eyeish) / len(eyeish) + 0.5, sum(p[1] for p in eyeish) / len(eyeish) + 0.5)
    return (min(p[0] for p in px), min(p[1] for p in px), max(p[0] for p in px) + 1, max(p[1] for p in px) + 1, z)


def _ancestors(P, g):
    out = [g]
    while g in P.parent:
        g = P.parent[g]
        out.append(g)
    return out


def card_at(gid, w, h, points):
    """the card most seen (frontmost) about the given picture points, or -1"""
    count = {}
    for ex, ey in points:
        for y in range(max(0, int(ey) - 2), min(h, int(ey) + 3)):
            for x in range(max(0, int(ex) - 2), min(w, int(ex) + 3)):
                if gid[y][x] >= 0:
                    count[gid[y][x]] = count.get(gid[y][x], 0) + 1
    return max(count, key=lambda g: count[g]) if count else -1


def eye_start(P, cards, head_area):
    """where the anchor search starts: the group the eyes are drawn on. A face card (the eyes in its
    texture) is that card; the eyes' own cards (they blink, look round and swap for crosses) give their
    nearest common parent"""
    cards = [c for c in cards if c >= 0]
    if not cards:
        return None
    if len(cards) > 1:
        common = _ancestors(P, cards[0])
        for c in cards[1:]:
            mine = set(_ancestors(P, c))
            common = [g for g in common if g in mine]
        return common[0] if common else None
    c = cards[0]
    lo, hi = F._bbox(P.cards[c])
    small = (hi[0] - lo[0]) * (hi[1] - lo[1]) < 0.2 * head_area
    name = P.groups[c].name.split("|")[-1].lower()
    if (small or (_EYE_NAME.search(name) and "arme" not in name)) and c in P.parent:
        return P.parent[c]
    return c


def place(P, heads, images, model, anchor=None):
    """(group name, middle, half width, half height (each in the group's space), how, picture) or None"""
    pic = head_picture(P, heads, images)
    if pic is None:
        return None
    img, zb, x0, y1, w, h = pic
    opaque = [(x, y) for y in range(h) for x in range(w) if img[y][x] is not None]
    if not opaque:
        return None
    ox0, ox1 = min(p[0] for p in opaque), max(p[0] for p in opaque) + 1
    oy0, oy1 = min(p[1] for p in opaque), max(p[1] for p in opaque) + 1
    hw, hh = ox1 - ox0, oy1 - oy0
    ov = overrides(model)
    gid = head_picture.gid
    blobs, total = find_eyes(img, w, h)
    if "eye_cards" in ov:  # by hand: the cards that are the eyes (a rig that doesn't name them, or as well as)
        named = named_eyes(P, gid, w, h, img, names=set(ov["eye_cards"]))
    else:
        named = named_eyes(P, gid, w, h, img) if "eyes" not in ov and not ov.get("unnamed") else None
    found = pick_eyes(blobs, w, h, total) if "eyes" not in ov and named is None else None
    eye_z = None
    if named is not None and "eye_cards" in ov and len(named_eyes.each) == 2:
        # an eye card each side: the glasses' middle between the eyes', level, the lenses a little wider apart
        # than the eyes (EYE_LENS_WIDE) so each lens covers its eye whole, tall eyes too
        (lx, ly), (rx, ry) = named_eyes.each
        eye_z = named[4]
        cx, cy = (lx + rx) / 2, (ly + ry) / 2
        width, tilt, how = (rx - lx) / LENS_GAP * EYE_LENS_WIDE, 0.0, "eyes"
    elif named is not None:
        px0, py0, px1, py1, eye_z = named
        cx, cy = named_eyes.mid if named_eyes.mid is not None else ((px0 + px1) / 2, (py0 + py1) / 2)
        width, tilt, how = max((px1 - px0) * 1.4, (py1 - py0) * 1.9), 0.0, "named"
        width = max(0.3 * hw, min(width, 1.05 * hw))
    elif found is not None:
        cx, cy, width, tilt, how = found
        width = max(0.35 * hw, min(width, 1.05 * hw))
    else:
        ex, ey = ov.get("eyes", (0.5, 0.62))
        cx, cy = ox0 + ex * hw, oy1 - ey * hh
        width, tilt, how = 0.72 * hw, 0.0, "face" if "eyes" not in ov else "set"
    if "eyes" in ov and found is None:
        width = 0.72 * hw
    if "width" in ov:
        width = ov["width"] * hw
    if "eye_card" in ov:  # by hand: the card that is the (near) eye: over its middle, its width times the share
        ec = [P.cards[c] for c in P.cards if P.groups[c].name == ov["eye_card"][0]]
        if ec:
            lo, hi = F._bbox(ec[0])
            cx, cy = ((lo[0] + hi[0]) / 2 - x0) * PX, (y1 - (lo[1] + hi[1]) / 2) * PX
            width, how = ov["eye_card"][1] * (hi[0] - lo[0]) * PX, "set"
    width *= ov.get("scale", 1.0)
    tilt = ov.get("tilt", tilt)
    cy -= ov.get("dy", 0.0) * hh
    cx += ov.get("dx", 0.0) * width
    # the front of the head about the glasses' middle (the nearest card there)
    zs = [zb[y][x] for y in range(max(0, int(cy) - 3), min(h, int(cy) + 4))
          for x in range(max(0, int(cx - width / 2)), min(w, int(cx + width / 2) + 1)) if zb[y][x] > -1e29]
    z = max(zs) if zs else max(max(r) for r in zb)
    # at the eyes' own depth (not a snout's or a hat's before them): turned, the glasses stay on the eyes
    if eye_z is not None:
        z = eye_z
    elif found is not None:
        ez = [zb[y][x] for ex, ey in pick_eyes.at for y in range(max(0, int(ey) - 1), min(h, int(ey) + 2))
              for x in range(max(0, int(ex) - 1), min(w, int(ex) + 2)) if zb[y][x] > -1e29]
        if ez:
            z = max(ez)
    z = ov.get("z", z)  # by hand: the depth (a face behind glass: the glasses on it, not on the glass)
    world = [x0 + cx / PX, y1 - cy / PX, z + 0.05]
    # seen from behind (a paper character facing the other way: its cards turned half round), the head's
    # cards come in the other order: the glasses go before the hindmost of them instead
    back_z = min(pt[2] for hg, _l in heads for c in P.subtree(hg) if c in P.cards for pt in P.cards[c])
    back = [world[0], world[1], back_z - 0.05]
    t = math.radians(tilt)
    half_w = width / PX / 2
    half_h = half_w * SHADES_ASPECT
    ax = [math.cos(t) * half_w, math.sin(t) * half_w, 0.0]
    ay = [-math.sin(t) * half_h, math.cos(t) * half_h, 0.0]
    # the group the eyes are drawn on: the glasses go with it through the animations
    if named is not None:
        eye_cards = named_eyes.cards
    else:
        pts = pick_eyes.at if found is not None else [(cx, cy)]
        eye_cards = sorted({card_at(gid, w, h, [p]) for p in pts})
    place.start = eye_start(P, eye_cards, hw * hh / (PX * PX))
    if "anchor" in ov:  # by hand: the group named
        place.start = next((i for i, g in enumerate(P.groups) if g.name == ov["anchor"]), place.start)
    if anchor is not None and callable(anchor):
        anchor = anchor(place.start)
    main = heads[0][0] if anchor is None else anchor
    inv = F._inv(P.mats[main])
    if inv is None:
        return None
    return (P.groups[main].name, F._apply(inv, world), _lin(inv, ax), _lin(inv, ay), F._apply(inv, back), how,
            (world, width / PX, tilt))


COMMON = ("W_1", "W_2", "R_1"), ("T_1", "T_2"), ("D_1", "D_2", "H_1")  # walking, talking, hurt


def common_anims(pose):
    names = [a.name for a in pose.anims if getattr(a, "model_offset", None) is None]
    out = []
    for sufs in COMMON:
        n = next((n for suf in sufs for n in names if n == suf or n.endswith("_" + suf)), None)
        if n:
            out.append(n)
    return out


SAMPLES = (0.0, 0.25, 0.5, 0.75)  # how far into each animation the anchor is checked


def steady_anchor(pose, P, head, start=None):
    """the group the glasses ride on: from the group the eyes are drawn on (start; else the head group) up,
    the first one shown, and not squashed to nothing, all through standing, walking and talking (sampled
    through each), so the glasses follow the eyes and never vanish or flatten mid-step. Hurt too where a
    group no higher than the head does it (a Bob-omb's or a Pokey's eyes swap for crosses when hurt: the
    glasses stay on); else hurt is not asked, and a face hidden when hurt takes the glasses with it."""
    names = [a.name for a in pose.anims if getattr(a, "model_offset", None) is None]
    hurt = re.compile(r"(^|_)(D|H)_\d")

    def sampled(anims):
        out = []
        for an in anims:
            if an not in names:
                continue
            for share in SAMPLES:
                try:
                    out.append(F.Posed(pose, an, share))
                except Exception:  # noqa: BLE001
                    pass
        return out
    common = [F.stand_anim(pose, None)] + [n for n in common_anims(pose) if not hurt.search(n)]
    poses = sampled(common)
    poses_hurt = poses + sampled([n for n in common_anims(pose) if hurt.search(n)])

    def scale(m):
        return (math.sqrt(sum(m[r][0] ** 2 for r in range(3))), math.sqrt(sum(m[r][1] ** 2 for r in range(3))))

    def shown(Q, g):  # drawn, and not flattened away (a pose that hides a card by scaling it to nothing)
        m = Q.mats[g]
        if m is None or g not in Q.drawn or P.mats[g] is None:
            return False
        (sx, sy), (rx, ry) = scale(m), scale(P.mats[g])
        return sx > max(1e-3, 0.35 * rx) and sy > max(1e-3, 0.35 * ry)

    chain = []
    g = start if start is not None else head
    while g is not None:
        chain.append(g)
        g = P.parent.get(g)
    upto = (chain[:chain.index(head) + 1] if head in chain else [])[:3]  # never far up just to show when hurt
    for g in upto:
        if all(shown(Q, g) for Q in poses_hurt):
            return g
    for g in chain:
        if all(shown(Q, g) for Q in poses):
            return g
    return head


# ------------------------------------------------------------ the sheet ----

def shades_image(src):
    """the recipe's picture, cut the same way (the source texture's opaque box)"""
    pose = T.PoseFile.parse(src.read("a/" + SHADES[0]))
    w, h, rows, _s, _t = T.tpl_images(src.read("a/" + pose.texture_name + "-"))[SHADES[1]]
    return w, h, rows


def sheet(entries, shades, out, cell=96, cols=10):
    """each model whole as it stands, its glasses on (entries: name, Posed, images, (world, width, tilt), how)"""
    rows = (len(entries) + cols - 1) // cols
    W, H = cell * cols, (cell + 12) * rows
    img = [[[245, 245, 245, 255] if ((x // cell + y // (cell + 12)) % 2) else [222, 222, 230, 255]
            for x in range(W)] for y in range(H)]
    sw, sh, srows = shades
    for i, (name, P, images, (world, width, tilt), how) in enumerate(entries):
        pic = frame_picture(P, images, world, width)
        if pic is None:
            continue
        pimg, _zb, x0, y1, w, h = pic
        k = min((cell - 4) / w, (cell - 4) / h, cell / 38.0)
        bx, by = (i % cols) * cell + (cell - w * k) / 2, (i // cols) * (cell + 12) + (cell - h * k) / 2
        for y in range(int(h * k)):
            for x in range(int(w * k)):
                c = pimg[int(y / k)][int(x / k)]
                if c is not None:
                    img[int(by + y)][int(bx + x)] = [c[0], c[1], c[2], 255]
        # the glasses: the shades' texels over their box, turned by the tilt
        t = math.radians(tilt)
        gw, gh = width * PX * k, width * PX * k * SHADES_ASPECT
        gx, gy = bx + (world[0] - x0) * PX * k, by + (y1 - world[1]) * PX * k
        r = int(max(gw, gh)) + 2
        for y in range(int(gy) - r, int(gy) + r):
            for x in range(int(gx) - r, int(gx) + r):
                dx, dy = x + 0.5 - gx, y + 0.5 - gy
                u = (dx * math.cos(t) - dy * math.sin(t)) / gw + 0.5
                v = (dx * math.sin(t) + dy * math.cos(t)) / gh + 0.5
                if 0 <= u < 1 and 0 <= v < 1 and (i // cols) * (cell + 12) <= y < (i // cols + 1) * (cell + 12)                         and (i % cols) * cell <= x < (i % cols + 1) * cell:
                    p = srows[int(v * sh)][int(u * sw)]
                    if p[3] >= 128:
                        img[y][x] = [p[0], p[1], p[2], 255]
        # how: a mark under the cell (green a pair, yellow one eye, red none found, blue set by hand)
        col = {"pair": [40, 170, 60, 255], "one": [220, 180, 20, 255], "face": [210, 40, 40, 255]}.get(
            how, [40, 90, 220, 255])
        yy = (i // cols) * (cell + 12) + cell + 3
        for y in range(yy, yy + 6):
            for x in range((i % cols) * cell + 4, (i % cols) * cell + cell - 4):
                img[y][x] = col
    T.write_png(out, [[tuple(p) for p in r] for r in img])
    for r in range(rows):
        print(" | ".join(e[0] for e in entries[r * cols:(r + 1) * cols]))


def grid_sheet(grids, out, cell=200, cols=5):
    """each head picture (its opaque box) with lines at every tenth, the half ones darker, the glasses' middle red"""
    import fun_cool_check as K
    rows = (len(grids) + cols - 1) // cols
    img = [[(255, 255, 255)] * (cols * (cell + 6)) for _ in range(rows * (cell + 18))]
    for i, (model, pic, (world, width, _tilt)) in enumerate(grids):
        bx, by = (i % cols) * (cell + 6), (i // cols) * (cell + 18)
        K.text(img, bx + 2, by + 2, model, (0, 0, 0))
        pimg, _zb, x0, y1, w, h = pic
        op = [(x, y) for y in range(h) for x in range(w) if pimg[y][x] is not None]
        ox0, ox1 = min(p[0] for p in op), max(p[0] for p in op) + 1
        oy0, oy1 = min(p[1] for p in op), max(p[1] for p in op) + 1
        k = min(cell / (ox1 - ox0), cell / (oy1 - oy0))
        for y in range(cell):
            for x in range(cell):
                sx, sy = ox0 + x / k, oy0 + y / k
                col = (230, 230, 236)
                if sx < ox1 and sy < oy1:
                    c = pimg[int(sy)][int(sx)]
                    if c is not None:
                        col = c
                    fx, fy = (sx - ox0) / (ox1 - ox0), (oy1 - sy) / (oy1 - oy0)
                    for f in (fx, fy):
                        t = f * 10
                        if abs(t - round(t)) * (ox1 - ox0 if f is fx else oy1 - oy0) * k / 10 < 0.5:
                            col = (0, 0, 255) if round(t) == 5 else (90, 160, 255)
                img[by + 14 + y][bx + x] = col
        gx, gy = (world[0] - x0) * PX, (y1 - world[1]) * PX
        cx, cy = int((gx - ox0) * k), int((gy - oy0) * k)
        for d in range(-4, 5):
            for (xx, yy) in ((cx + d, cy), (cx, cy + d)):
                if 0 <= xx < cell and 0 <= yy < cell:
                    img[by + 14 + yy][bx + xx] = (255, 0, 0)
    T.write_png(out, [[tuple(p) + (255,) for p in r] for r in img])


# ---------------------------------------------------------------- output ----

def write_inc(path, table):
    lines = ["/* Generated by tools/port/fun_cool.py from the disc's models: do not edit.",
             " * Per model, Cool Mode's sunglasses (fun_pc.c): the head group they sit on and their",
             " * quad in its coordinates (the middle, half the width, half the height, and the middle seen",
             " * from behind); NULL: none (a back view, a model with its own shades, or no head). */"]
    for model in sorted(table):
        r = table[model]
        if r is None:
            lines.append('{"%s", NULL, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, '
                         '{0.0f, 0.0f, 0.0f}},' % model)
            continue
        g, c, ax, ay, cb = r
        lines.append('{"%s", "%s", {%.3ff, %.3ff, %.3ff}, {%.3ff, %.3ff, %.3ff}, {%.3ff, %.3ff, %.3ff}, '
                     '{%.3ff, %.3ff, %.3ff}},' % ((model, g) + tuple(c) + tuple(ax) + tuple(ay) + tuple(cb)))
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def base_name(model):
    return model


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("--out", default=os.path.join(REPO, "port", "src", "common", "fun_cool.inc"))
    ap.add_argument("--models")
    ap.add_argument("--sheet")
    ap.add_argument("--cols", type=int, default=10)
    ap.add_argument("--cell", type=int, default=96)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--grid", help="a sheet of each head picture with a tenths grid (for OVERRIDES' shares)")
    a = ap.parse_args(argv)
    a.out_given = argv is not None and "--out" in argv or argv is None and "--out" in sys.argv
    src = F.open_disc(a.disc)
    have = set(src.models())
    want = F.source_models()
    for m in BACK:
        want.setdefault(m, None)
    for m in have:
        if m.startswith("EFF_m_"):
            want.setdefault(m, None)  # the partners as the Start menu's Party tab draws them
    if a.models:
        want = {m: want.get(m) for m in a.models.split(",")}
    table, entries, counts, grids = {}, [], {}, []
    for model in sorted(want):
        if model not in have and model not in BACK:
            continue
        if overrides(model).get("like") in table:  # a recoloured copy of another model's rig: its glasses
            table[model] = table[overrides(model)["like"]]
            counts["like"] = counts.get("like", 0) + 1
            continue
        if model in BACK or model in OWN or overrides(model).get("none"):
            table[model] = None
            counts["back" if model in BACK else "own"] = counts.get("back" if model in BACK else "own", 0) + 1
            continue
        try:
            pose = T.PoseFile.parse(src.read("a/" + model))
        except Exception:  # noqa: BLE001 (a file the tool can't read: left out)
            continue
        P = F.Posed(pose, F.stand_anim(pose, want[model]))
        heads = F.pick(P, F.override(model))
        if overrides(model).get("whole"):
            heads = []
        if overrides(model).get("head"):  # by hand: the group that is the head (its cards are the picture)
            heads = [(i, None) for i, g in enumerate(P.groups) if g.name == overrides(model)["head"]][:1] or heads
        if not heads:
            if F.override(model) == F._NONE and not overrides(model).get("whole"):
                table[model] = None
                continue
            heads = [(P.root, None)]  # all head (a Boo, a Bob-omb): the whole model is the face
            if not any(c in P.cards for c in P.subtree(P.root)):  # the last group isn't the drawn root (a Bristle)
                heads = [(g, None) for g in range(len(P.groups))
                         if g not in P.parent and any(c in P.cards for c in P.subtree(g))]
        images = []
        try:
            images = [(w, h, rows) for w, h, rows, _s, _t in T.tpl_images(src.read("a/" + pose.texture_name + "-"))]
        except Exception:  # noqa: BLE001
            pass
        if not heads:  # nothing drawn to place them on
            continue
        r = place(P, heads, images, model, lambda start: steady_anchor(pose, P, heads[0][0], start))
        if r is None:
            continue
        g, c, ax, ay, cb, how, at = r
        table[model] = (g, c, ax, ay, cb)
        if a.grid:
            grids.append((model, head_picture(P, heads, images), at))
        counts[how] = counts.get(how, 0) + 1
        if a.list:
            print(model, g, how, ["%.2f" % v for v in c])
        if a.sheet:
            entries.append((model, P, images, at, how))
    if a.sheet:
        per = a.cols * 8
        shades = shades_image(src)
        for i in range(0, len(entries), per):
            out = a.sheet if len(entries) <= per else "%s-%d.png" % (a.sheet[:-4], i // per + 1)
            print(out)
            sheet(entries[i:i + per], shades, out, cell=a.cell, cols=a.cols)
    if a.grid:
        grid_sheet(grids, a.grid)
    if not a.models or a.out_given:
        write_inc(a.out, table)
    print("%d models with glasses, %d without; %s" % (sum(1 for v in table.values() if v),
                                                      sum(1 for v in table.values() if not v), counts))


if __name__ == "__main__":
    main()
