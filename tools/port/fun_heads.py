#!/usr/bin/env python3
"""Big heads (fun toggles, port/src/common/fun_pc.c): which groups of each model
are its head, from the disc's models, as port/src/common/fun_heads.inc.

Most of the game's paper models name their groups pPlaneN, so the head can't
be found by name at run time. Here each model is posed in its standing
animation and its head picked by shape: the group whose subtree is the
biggest lump of cards high on the model that doesn't reach the floor (the
feet), nearly alone in its height band; with it any other branch that sits
on the head (a ponytail, a hat) and doesn't reach the floor. The point it
grows about is the rig's own (a rotation centre or a joint) when that lies at
the head's foot, else the bottom of the head. OVERRIDES fixes what the shapes
get wrong. Only names and numbers come out: group names and points.

  fun_heads.py DISC [--out FILE] [--models a,b,...] [--sheet OUT.png] [--cols N] [--list]

DISC is the disc image (.iso) or an extracted disc's folder. The models are
every c_ model the game's sources name (src/ and rels/), and Mario's. --sheet draws
each model as it stands and with its head grown, for checking by eye;
a page of six rows each (OUT-1.png, ...); --list prints what was picked.
"""
import argparse
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402

GROW = 1.8          # fun_pc.c's HEAD_SCALE, for the sheet
FLOOR = 0.08        # a card reaching this share of the height above the bottom is on the floor
MIN_SHARE = 0.06    # a head holds at least this share of the model's card area

# model (a pattern) -> the head's groups, by name, the first one's pivot taken
# from the rig; "-" for no head (weapons, cannons, hands: nothing to grow)
_NONE = ["-"]
OVERRIDES = {
    "[abe]_mario*": ["Face"],  # "Head" is an empty point; the face holds the hat, eyes and nose
    # the Punies are all head, their one card reaching the floor
    "c_puni": ["locator3"], "c_pguide": ["locator3"], "c_pjoho": ["locator3"], "c_prival": ["locator3"],
    "c_pshop": ["locator3"], "c_psister": ["locator3"], "c_psoncho": ["pPlane2"],
    "c_b_hand": _NONE, "c_buki*": _NONE, "c_killtai*": _NONE, "c_mb_robo_1*": _NONE, "c_togedaruma*": _NONE,
    "c_honezuka": _NONE,
    # named only in rels/: Cortez's bones and falling swords, Dry Bones' thrown bones, the Shadow Queen's hands and hair
    "c_hone*": _NONE, "c_nagehone*": _NONE, "c_otitabuki": _NONE, "c_q_kage_f": _NONE, "c_q_kage_h": _NONE,
}

# Mario's models (src/mario names them in pieces) and the port's characters'
SETS_MARIO = ["a_mario", "a_mario_r", "a_mario_l", "a_mario_lr", "a_mario_w", "a_mario_wr", "a_mario_wl",
              "a_mario_wlr", "e_mario", "e_mario_w", "e_mario_l", "e_mario_wl", "b_mario", "b_mario_l",
              "b_mario_w", "b_mario_wl"]
EXTRA = SETS_MARIO + ["c_peach_r", "c_luigi", "c_koopa"]


def override(model):
    import fnmatch
    for pat, groups in OVERRIDES.items():
        if fnmatch.fnmatchcase(model, pat):
            return groups
    return None


def source_models():
    """{model: preferred standing animation or None} from the game's sources"""
    out = {}
    npc = os.path.join(REPO, "src", "data", "npc_data.c")
    with open(npc, encoding="utf-8") as f:
        for m in re.finditer(r'\{"[^"]*", "([a-z0-9_]+)", "([A-Za-z0-9_]+)"', f.read()):
            out.setdefault(m.group(1), m.group(2))
    # src/ and rels/ (the maps' and the battles' code: Smorg, Cortez's forms, the Shadow Queen's parts are named only there)
    for root, _dirs, files in [w for top in ("src", "rels") for w in os.walk(os.path.join(REPO, top))]:
        for fn in files:
            if fn.endswith((".c", ".h", ".inc")):
                with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                    for m in re.finditer(r'"(c_[a-z0-9_]+)"', f.read()):
                        out.setdefault(m.group(1), None)
    for m in EXTRA:
        out.setdefault(m, None)
    return out


def open_disc(path):
    import character_tool
    return character_tool.open_source(path)


def stand_anim(pose, want):
    names = [a.name for a in pose.anims if getattr(a, "model_offset", None) is None]
    for n in names:
        if n == "S_1" or n.endswith("_S_1"):
            return n  # standing still, the same for every user of the model
    if want in names:
        return want
    for pat in ("S_1", "_S_", "S_"):
        for n in names:
            if n.endswith(pat) or pat in n:
                return n
    return names[0] if names else None


def _inv(m):
    """inverse of an affine 4x4 (row lists); a flattened axis (a paper card's
    scale of 0) is taken as unit length, since nothing lies along it"""
    a = [list(r[:3]) for r in m[:3]]
    cols = [[a[r][c] for r in range(3)] for c in range(3)]
    norm = [math.sqrt(sum(v * v for v in col)) for col in cols]
    for c in range(3):
        if norm[c] < 1e-6:
            u, v = cols[(c + 1) % 3], cols[(c + 2) % 3]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            ln = math.sqrt(sum(x * x for x in n))
            n = [x / ln for x in n] if ln > 1e-9 else [1.0 if r == c else 0.0 for r in range(3)]
            for r in range(3):
                a[r][c] = n[r]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        return None
    c = [[0.0] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            r = [k for k in range(3) if k != j]
            s = [k for k in range(3) if k != i]
            minor = a[r[0]][s[0]] * a[r[1]][s[1]] - a[r[0]][s[1]] * a[r[1]][s[0]]
            c[i][j] = (-1) ** (i + j) * minor / det
    t = [m[i][3] for i in range(3)]
    ti = [-sum(c[i][k] * t[k] for k in range(3)) for i in range(3)]
    return [c[0] + [ti[0]], c[1] + [ti[1]], c[2] + [ti[2]], [0.0, 0.0, 0.0, 1.0]]


def _apply(m, p):
    return [m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3)]


class Posed:
    """a model in its standing pose: per group its cards' world points"""

    def __init__(self, pose, anim, share=0.0):
        """share: how far into the animation (0 its first frame, 0.5 halfway)"""
        self.pose = pose
        self.groups = pose.groups()
        ev, self.geo = T.Evaluator(pose), T.Geometry(pose)
        a = pose.anim(anim) if anim else None
        if a is not None:
            frame = a.start if not share else a.local_frame(a.start + share * (a.end - a.start))
            self.node, self.vis, self.pos, self.tex = ev.at_frame(a, frame)
        else:
            self.node, self.vis, self.pos = ev.base_node, ev.base_vis, ev.base_pos
            self.tex = [[t[0], t[1], t[2]] for t in ev.base_tex]
        self.mats = ev.world_matrices(self.node, self.vis)
        self.root = len(self.groups) - 1
        self.parent = {}
        self.cards = {}  # group -> [world points]
        for gi, g in enumerate(self.groups):
            if g.child != -1:
                c = g.child
                while c != -1:
                    self.parent[c] = gi
                    c = self.groups[c].brother
            if g.shape >= 0 and self.mats[gi] is not None:
                pts = [_apply(self.mats[gi], self.pos[pi]) for _di, fan in self.geo.shape_polys(g.shape)
                       for pi, _c, _uv in fan]
                if pts:
                    self.cards[gi] = pts
        # the drawn tree: from the root, as dispProc walks it
        self.drawn = []
        self._walk(self.root)

    def _walk(self, gi):
        while gi != -1 and 0 <= gi < len(self.groups):
            self.drawn.append(gi)
            self._walk(self.groups[gi].child)
            gi = self.groups[gi].brother

    def subtree(self, gi):
        out = [gi]
        c = self.groups[gi].child
        while c != -1:
            out += self.subtree(c)
            c = self.groups[c].brother
        return out


def _bbox(pts):
    return ([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)])


def _area(pts):
    lo, hi = _bbox(pts)
    return max(hi[0] - lo[0], 0.05) * max(hi[1] - lo[1], 0.05)


def pick(P, override=None):
    """[(group index, local pivot)] of the head, or [] (none found)"""
    allpts = [p for pts in P.cards.values() for p in pts]
    if not allpts:
        return []
    lo, hi = _bbox(allpts)
    bottom, height = lo[1], max(hi[1] - lo[1], 1e-3)
    area = {gi: _area(pts) for gi, pts in P.cards.items()}
    total = sum(area.values())
    floor = {gi for gi, pts in P.cards.items() if min(p[1] for p in pts) < bottom + FLOOR * height}
    drawn = set(P.drawn)
    names = {g.name: gi for gi, g in enumerate(P.groups)}

    def sub_cards(gi):
        return [c for c in P.subtree(gi) if c in P.cards]

    if override is not None:
        if override == ["-"]:
            return []
        heads = [names[n] for n in override if n in names]
        if not heads:
            return []
        main, extra = heads[0], heads[1:]
    else:
        best, main = 0.0, None
        for gi in P.drawn:
            if gi == P.root:
                continue
            cs = sub_cards(gi)
            if not cs or any(c in floor for c in cs):
                continue
            a = sum(area[c] for c in cs)
            if a < MIN_SHARE * total:
                continue
            pts = [p for c in cs for p in P.cards[c]]
            blo, bhi = _bbox(pts)
            band = blo[1] + 0.25 * (bhi[1] - blo[1])
            inband = sum(area[c] for c in P.cards
                         if (min(p[1] for p in P.cards[c]) + max(p[1] for p in P.cards[c])) / 2 > band)
            purity = a / inband if inband else 0.0
            rise = (blo[1] - bottom) / height
            s = a * purity * math.sqrt(max(rise, 0.0))
            # a parent with exactly the same cards: the outermost one (its pivot is the rig's)
            if s > best * 1.0001 or (main is not None and abs(s - best) <= best * 1e-4 and main in P.subtree(gi)):
                best, main = s, gi
        if main is None:
            return []
        # other branches sitting on the head
        hc = sub_cards(main)
        hlo, hhi = _bbox([p for c in hc for p in P.cards[c]])
        hcy = (hlo[1] + hhi[1]) / 2
        inside = set(P.subtree(main))
        ancestors = set()
        g = main
        while g in P.parent:
            g = P.parent[g]
            ancestors.add(g)
        extra = []
        for gi in P.drawn:
            if gi in inside or gi in ancestors or gi == P.root:
                continue
            if any(gi in P.subtree(e) for e in extra):
                continue
            cs = sub_cards(gi)
            if not cs or any(c in floor for c in cs):
                continue
            pts = [p for c in cs for p in P.cards[c]]
            blo, bhi = _bbox(pts)
            cx, cy = (blo[0] + bhi[0]) / 2, (blo[1] + bhi[1]) / 2
            if hlo[0] <= cx <= hhi[0] and hlo[1] <= cy <= hhi[1] and cy >= hcy - 0.1 * (hhi[1] - hlo[1]):
                # whole branches only: not a part of one already taken
                extra = [e for e in extra if e not in P.subtree(gi)]
                extra.append(gi)
                inside |= set(P.subtree(gi))
    heads = [main] + extra
    # the pivot: the rig's for the main group when it lies at the head's foot
    hc = [c for h in heads for c in sub_cards(h)]
    if not hc:
        return []
    hlo, hhi = _bbox([p for c in hc for p in P.cards[c]])
    hh = hhi[1] - hlo[1]
    foot = [(hlo[0] + hhi[0]) / 2, hlo[1], (hlo[2] + hhi[2]) / 2]
    m = P.mats[main]
    g = P.groups[main]
    nv = P.node[g.node:g.node + T.NODE_SIZE]
    rig = _apply(m, nv[12:15]) if not g.ssc and any(abs(v) > 1e-4 for v in nv[12:15]) else _apply(m, (0, 0, 0))
    ok = (hlo[0] - 0.25 * (hhi[0] - hlo[0]) <= rig[0] <= hhi[0] + 0.25 * (hhi[0] - hlo[0])
          and hlo[1] - 0.15 * hh <= rig[1] <= hlo[1] + 0.6 * hh)
    world = rig if ok else foot
    out = []
    for h in heads:
        inv = _inv(P.mats[h])
        if inv is None:
            continue
        out.append((h, _apply(inv, world)))
    return out


# ------------------------------------------------------------ the sheet ----

def draw(P, images, heads, img, W, H, ox, oy, scale):
    import char_sheet as CS
    grow = {}
    for h, local in heads:
        w = _apply(P.mats[h], local)
        for c in P.subtree(h):
            grow[c] = w
    tris = []
    for gi in P.cards:
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
            pts = []
            for pi, col, uv in fan:
                x, y, z = _apply(m, P.pos[pi])
                if gi in grow:
                    c = grow[gi]
                    x, y, z = c[0] + GROW * (x - c[0]), c[1] + GROW * (y - c[1]), c[2] + GROW * (z - c[2])
                pts.append((ox + x * scale, oy - y * scale, z, (uv[0] + ts, uv[1] + tt) if uv else None, col))
            for k in range(1, len(pts) - 1):
                tri = (pts[0], pts[k], pts[k + 1])
                tris.append((sum(p[2] for p in tri) / 3.0, gi, tri, im))
    tris.sort(key=lambda t: (t[0], t[1]))
    for _z, _g, tri, im in tris:
        CS._raster(img, W, H, tri, im)


def sheet(entries, out, cell=110, cols=8):
    rows = (len(entries) + cols - 1) // cols
    W, H = cell * 2 * cols, cell * rows
    img = [[[250, 250, 250, 255] if ((x // (cell * 2) + y // cell) % 2) else [226, 226, 232, 255]
            for x in range(W)] for y in range(H)]
    for i, (name, P, images, heads) in enumerate(entries):
        allpts = [p for pts in P.cards.values() for p in pts]
        if not allpts:
            continue
        lo, hi = _bbox(allpts)
        span = max(hi[0] - lo[0], hi[1] - lo[1], 1.0)
        scale = (cell * 0.55) / span
        cx, cy = (i % cols) * cell * 2, (i // cols) * cell
        ox = (lo[0] + hi[0]) / 2
        for k, hs in enumerate(([], heads)):
            draw(P, images, hs, img, W, H, cx + cell * k + cell / 2 - ox * scale, cy + cell - 6 + lo[1] * scale, scale)
        # the pivot: a red dot
        for h, local in heads[:1]:
            w = _apply(P.mats[h], local)
            px, py = int(cx + cell + cell / 2 + (w[0] - ox) * scale), int(cy + cell - 6 - (w[1] - lo[1]) * scale)
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    if 0 <= py + dy < H and 0 <= px + dx < W:
                        img[py + dy][px + dx] = [230, 30, 30, 255]
    T.write_png(out, [[tuple(p) for p in r] for r in img])
    for r in range(rows):
        print(" | ".join(e[0] for e in entries[r * cols:(r + 1) * cols]))


# ---------------------------------------------------------------- output ----

def write_inc(path, table):
    lines = ["/* Generated by tools/port/fun_heads.py from the disc's models: do not edit.",
             " * Per model, the groups that are its head (big heads, fun_pc.c) and the point",
             " * each grows about (the neck, in the group's own coordinates); NULL: no head. */"]
    for model in sorted(table):
        if not table[model]:
            lines.append('{"%s", NULL, {0.0f, 0.0f, 0.0f}},' % model)
        for gname, p in table[model]:
            lines.append('{"%s", "%s", {%.3ff, %.3ff, %.3ff}},' % (model, gname, p[0], p[1], p[2]))
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("--out", default=os.path.join(REPO, "port", "src", "common", "fun_heads.inc"))
    ap.add_argument("--models")
    ap.add_argument("--sheet")
    ap.add_argument("--cols", type=int, default=8)
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args(argv)
    src = open_disc(a.disc)
    have = set(src.models())
    want = source_models()
    if a.models:
        want = {m: want.get(m) for m in a.models.split(",")}
    table, entries, skipped = {}, [], []
    for model in sorted(want):
        if model not in have:
            skipped.append(model)
            continue
        try:
            pose = T.PoseFile.parse(src.read("a/" + model))
        except Exception as e:  # noqa: BLE001 (a file the tool can't read: left out, named)
            skipped.append("%s (%s)" % (model, e))
            continue
        anim = stand_anim(pose, want[model])
        P = Posed(pose, anim)
        heads = pick(P, override(model))
        if heads:
            table[model] = [(P.groups[h].name, p) for h, p in heads]
        elif override(model) == _NONE:
            table[model] = []  # no head: and none found by name either
        if a.list:
            print(model, anim, [P.groups[h].name for h, _p in heads])
        if a.sheet:
            images = []
            try:
                images = [(w, h, rows) for w, h, rows, _s, _t in T.tpl_images(src.read("a/" + pose.texture_name + "-"))]
            except Exception:  # noqa: BLE001 (no textures: drawn in its colours)
                pass
            entries.append((model, P, images, heads))
    if a.sheet:
        per = a.cols * 6  # a page: six rows
        for i in range(0, len(entries), per):
            out = a.sheet if len(entries) <= per else "%s-%d.png" % (a.sheet[:-4], i // per + 1)
            print(out)
            sheet(entries[i:i + per], out, cols=a.cols)
    elif not a.models:
        write_inc(a.out, table)
        print("%d models with a head, %d without; %d named but not on the disc" %
              (sum(1 for v in table.values() if v), sum(1 for v in table.values() if not v), len(skipped)))


if __name__ == "__main__":
    main()
