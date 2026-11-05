#!/usr/bin/env python3
"""Contact sheets of a model's animations, without Blender: each animation
drawn front-on (orthographic, the game's x right and y up, the paper layers
back to front by depth) at a few frames, in a grid, as a PNG. For reviewing
motions (luigi_motions.py) and for finding what a disc animation is. Pure Python: slow, but fine for a sheet.

  char_sheet.py MODEL TEXTURES OUT.png [--anims A,B,...] [--frames 3]
                [--scale 3] [--cols 6] [--at 0,2,4] [--grid 2]
  char_sheet.py --char peach --disc DIR OUT.png [--pack FILE] ...
  char_sheet.py --char peach --disc DIR --motions peach_motions OUT.png ...

MODEL and TEXTURES are the model file and its TPL (from a disc:
luigi_anims.py extract, character_tool.py export's disc/ folder). With
--char, the model is the one the port makes from the disc's files in DIR and
that character's pack (luigi_pack.py: luigi, peach, peach_r, bowser), the
committed one or --pack's. With --motions, the motions are drawn straight
from that script (char_motions.py's format), without Blender or a pack:
the keys joined in straight lines, near enough to pose by.
Each cell is one animation at evenly spaced frames (or the frames --at
names), left to right; the names are printed in order, one row of the grid
per line. --grid draws lines every that many units (the axes darker).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402


def _mulv(m, x, y, z):
    return tuple(m[r][0] * x + m[r][1] * y + m[r][2] * z + m[r][3] for r in range(3))


def draw_pose(pose, images, anim, frame, scale, img, ox, oy, w, h, ev=None, geo=None):
    """the pose at `frame` into img (rows of RGBA lists), its origin (the feet) at ox, oy"""
    ev = ev or T.Evaluator(pose)
    geo = geo or T.Geometry(pose)
    node, vis, pos, tex = ev.at_frame(anim, frame) if anim is not None else (ev.base_node, ev.base_vis, ev.base_pos, [[t[0], t[1], t[2]] for t in ev.base_tex])
    mats = ev.world_matrices(node, vis)
    tris = []
    for gi, g in enumerate(pose.groups()):
        if g.shape < 0 or mats[gi] is None:
            continue
        m = mats[gi]
        for di, fan in geo.shape_polys(g.shape):
            dr = geo.draws[di]
            im = None
            ts = tt = 0.0
            if dr["tex_count"]:
                slot = dr["tex_data"][0]
                if 0 <= slot < len(geo.texdata):
                    b = geo.texdata[slot][0] + tex[slot][0]
                    if 0 <= b < len(geo.texbinds):
                        ti = geo.texbinds[b][0]
                        im = images[ti] if ti < len(images) else None
                    ts, tt = tex[slot][1], tex[slot][2]
            pts = []
            for pi, c, uv in fan:
                x, y, z = _mulv(m, *pos[pi])
                pts.append((ox + x * scale, oy - y * scale, z, (uv[0] + ts, uv[1] + tt) if uv else None, c))
            for k in range(1, len(pts) - 1):
                tri = (pts[0], pts[k], pts[k + 1])
                tris.append((sum(p[2] for p in tri) / 3.0, gi, tri, im))
    tris.sort(key=lambda t: (t[0], t[1]))
    for _z, _g, tri, im in tris:
        _raster(img, w, h, tri, im)


def _raster(img, w, h, tri, im):
    (x0, y0, _a, uv0, c0), (x1, y1, _b, uv1, c1), (x2, y2, _c, uv2, c2) = tri
    d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    if abs(d) < 1e-9:
        return
    minx, maxx = max(0, int(min(x0, x1, x2))), min(w - 1, int(max(x0, x1, x2)) + 1)
    miny, maxy = max(0, int(min(y0, y1, y2))), min(h - 1, int(max(y0, y1, y2)) + 1)
    for py in range(miny, maxy + 1):
        cy = py + 0.5
        row = img[py]
        for px in range(minx, maxx + 1):
            cx = px + 0.5
            l0 = ((y1 - y2) * (cx - x2) + (x2 - x1) * (cy - y2)) / d
            l1 = ((y2 - y0) * (cx - x2) + (x0 - x2) * (cy - y2)) / d
            l2 = 1.0 - l0 - l1
            if l0 < -1e-6 or l1 < -1e-6 or l2 < -1e-6:
                continue
            col = [l0 * c0[i] + l1 * c1[i] + l2 * c2[i] for i in range(4)]
            if im is not None and uv0 is not None:
                tw, th, rows = im
                u = l0 * uv0[0] + l1 * uv1[0] + l2 * uv2[0]
                v = l0 * uv0[1] + l1 * uv1[1] + l2 * uv2[1]
                tx = int(u * tw) % tw
                ty = int(v * th) % th
                t = rows[ty][tx]
                if t[3] < 128:
                    continue
                col = [t[i] * col[i] / 255.0 for i in range(3)] + [255]
            elif col[3] < 128:
                continue
            row[px] = [int(col[0]), int(col[1]), int(col[2]), 255]


def _gridlines(img, W, H, ox, oy, scale, step, x0, x1, y0, y1):
    """lines every `step` units about the origin (ox, oy), the axes darker"""
    for k in range(int(x0 // step), int(x1 // step) + 1):
        px = int(ox + k * step * scale)
        for py in range(max(0, int(oy - y1 * scale)), min(H, int(oy - y0 * scale))):
            if 0 <= px < W:
                img[py][px] = [150, 150, 165, 255] if k == 0 else [205, 205, 215, 255]
    for k in range(int(y0 // step), int(y1 // step) + 1):
        py = int(oy - k * step * scale)
        if 0 <= py < H:
            for px in range(max(0, int(ox + x0 * scale)), min(W, int(ox + x1 * scale))):
                img[py][px] = [150, 150, 165, 255] if k == 0 else [205, 205, 215, 255]


def _at(pts, f, step):
    """a track's value at frame f: straight lines between its points (or steps)"""
    if f <= pts[0][0]:
        return pts[0][1]
    for (f0, v0, *_), (f1, v1, *_) in zip(pts, pts[1:]):
        if f0 <= f <= f1:
            if step or f1 == f0:
                return v0 if f < f1 else v1
            return v0 + (v1 - v0) * (f - f0) / (f1 - f0)
    return pts[-1][1]


def motion_pose(motion, f):
    """a char_motions.py motion's pose at frame f ({group: {channel: value}}),
    its keys joined in straight lines (the game's are curves)"""
    out = {}
    keys = motion.get("keys", [])
    chans = set()
    for _f, p, *_ in keys:
        chans |= {(g, c) for g, ch in p.items() for c in ch}
    for g, c in chans:
        rest = 1.0 if c in ("sx", "sy", "sz") else None if c == "vis" else 0
        pts = [(kf, p.get(g, {}).get(c, rest)) for kf, p, *_ in keys]
        out.setdefault(g, {})[c] = _at(pts, f, c in ("tex", "vis"))
    for g, ch in motion.get("tracks", {}).items():
        for c, pts in ch.items():
            out.setdefault(g, {})[c] = _at(pts, f, c in ("tex", "vis"))
    return out


class MotionEvaluator(T.Evaluator):
    """an Evaluator whose rest pose is a motion's pose at a frame (draw_pose
    with no animation draws it): for posing without Blender"""

    def __init__(self, pose, rig, motion, frame):
        super().__init__(pose)
        geo = T.Geometry(pose)
        groups = pose.groups()
        index = {g.name: i for i, g in enumerate(groups)}
        node, vis = list(self.base_node), list(self.base_vis)
        tex = [list(t) for t in self.base_tex]
        p = motion_pose(motion, frame)
        chain = sum(p.get(g, {}).get("rz", 0.0) or 0.0 for g in rig.chain)
        for g, ch in p.items():
            grp = groups[index[g]]
            n = grp.node
            for c, v in ch.items():
                if v is None:
                    continue
                if c == "hw":  # the hammer's world angle less the hand chain's turn
                    node[n + 8] += (v - chain) / 2.0
                elif c == "vis":
                    vis[grp.vis] = int(v)
                elif c == "tex":
                    tex[geo.draws[geo.shapes[grp.shape]["draw_start"]]["tex_data"][0]][0] = int(v)
                elif c[0] == "t":
                    node[n + "xyz".index(c[1])] += v
                elif c[0] == "r":
                    node[n + 6 + "xyz".index(c[1])] += v / 2.0  # (stored halved)
                elif c[0] == "s":
                    node[n + 3 + "xyz".index(c[1])] *= v
        self.base_node, self.base_vis, self.base_tex = node, vis, tex


def sheet(pose, images, names, out, frames=3, scale=3.0, cols=6, at=None, grid=None, margin=4.0, motions=None):
    """motions: (rig, {name: motion}) to draw from the script, not the model's animations"""
    ev = T.Evaluator(pose)
    geo = T.Geometry(pose)
    lo = pose.bbox
    if at:
        frames = len(at)
    half = max(abs(lo[0]), abs(lo[3])) + margin
    bw = half * 2 * scale
    bh = (lo[4] - min(lo[1], 0) + 10) * scale
    cw, ch = int(bw * frames) + 6, int(bh * 1.4) + 6
    rows = (len(names) + cols - 1) // cols
    W, H = cw * cols, ch * rows
    img = [[[250, 250, 250, 255] if ((x // cw + y // ch) % 2) else [228, 228, 232, 255] for x in range(W)]
           for y in range(H)]
    for i, n in enumerate(names):
        m = motions[1].get(n) if motions else None  # (else the model's own)
        a = None if m else pose.anim(n)
        start, end = (0, m["end"]) if m else (a.start, a.end)
        cx, cy = (i % cols) * cw, (i // cols) * ch
        for f in range(frames):
            t = min(end, at[f]) if at else start + (end - start) * (f / max(1, frames - 1))
            ox, oy = cx + 3 + bw * (f + 0.5), cy + ch - 0.2 * bh
            if grid:
                _gridlines(img, W, H, ox, oy, scale, grid, -half, half, -0.2 * bh / scale, (ch - 0.2 * bh) / scale)
            e = MotionEvaluator(pose, motions[0], m, t) if m else ev
            draw_pose(pose, images, a, t, scale, img, ox, oy, W, H, e, geo)
    T.write_png(out, [[tuple(p) for p in r] for r in img])
    for r in range(rows):
        print(" | ".join(names[r * cols:(r + 1) * cols]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="MODEL TEXTURES OUT.png, or OUT.png with --char")
    ap.add_argument("--char", help="our model of a character (luigi_pack.py's): luigi, peach, peach_r, bowser")
    ap.add_argument("--disc", help="(--char) the folder of the disc's files")
    ap.add_argument("--pack", help="(--char) this pack, not the committed one")
    ap.add_argument("--anims")
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--at", help="the frames to draw, by number (past an animation's end: its end)")
    ap.add_argument("--scale", type=float, default=3.0)
    ap.add_argument("--cols", type=int, default=6)
    ap.add_argument("--grid", type=float)
    ap.add_argument("--margin", type=float, default=4.0, help="room beside the model's bounds in each cell")
    ap.add_argument("--motions", help="(--char) draw the motions of this module (peach_motions, bowser_motions) "
                                      "from the script, without Blender: --char's model with the hammer alone")
    a = ap.parse_args(argv)
    motions = None
    if a.char:
        import luigi_pack
        ch = luigi_pack.CHARS[a.char]
        pack = a.pack
        if a.motions:
            import importlib
            import tempfile
            mod = importlib.import_module(a.motions)
            motions = [(r, m) for r, m in mod.MODELS if r.char == a.char][0]
            pack = os.path.join(tempfile.mkdtemp(), "empty.bin")
            luigi_pack.write_pack(pack, [], T.PoseFile.parse(open(os.path.join(a.disc, ch.base), "rb").read()), ch)
        res = luigi_pack.merge_files(a.disc, ch, pack)
        if res is None:
            sys.exit("the disc's model isn't the one the pack fits")
        (data, tdata), out = res, a.files[0]
    else:
        data, tdata, out = open(a.files[0], "rb").read(), open(a.files[1], "rb").read(), a.files[2]
    pose = T.PoseFile.parse(data)
    images = [(w, h, rows) for w, h, rows, _s, _t in T.tpl_images(tdata)]
    names = (a.anims.split(",") if a.anims else list(motions[1]) if motions else [x.name for x in pose.anims])
    at = [float(x) for x in a.at.split(",")] if a.at else None
    sheet(pose, images, names, out, a.frames, a.scale, a.cols, at, a.grid, a.margin, motions)


if __name__ == "__main__":
    main()
