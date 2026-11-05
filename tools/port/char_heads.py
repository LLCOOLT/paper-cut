#!/usr/bin/env python3
"""Art recipe steps for a character's head (port/assets/luigi/art/RECIPES.md), from the
model's own numbers: the named groups of a pose (or every group reaching above -YMIN),
each card cut from its texture as the model maps it (a texture atlas too), turned and
flipped as the pose shows it, sized at the disc's 8 texels a unit, and pasted back to
front. Prints `steps PREFIX_cards` and `steps PREFIX_head`; the recipe's images then
frame the head as an icon (peach_icons.recipe, bowser_icons.recipe).

  char_heads.py MODEL ANIM PREFIX GROUP,GROUP,...|-YMIN DIR

DIR holds the model file and its textures (MODEL and MODEL-), from the player's disc
(luigi_anims.py extract, character_tool.py export). Only numbers come out: positions,
sizes, texture indices. Needs plain-transform models whose cards are axis-aligned in
the pose (Peach's and Bowser's are).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ttyd_anim as T  # noqa: E402

if len(sys.argv) != 6:
    sys.exit(__doc__)
model, animname, prefix, sel, disc = sys.argv[1:6]
pose = T.PoseFile.parse(open(os.path.join(disc, model), "rb").read())
ims = T.tpl_images(open(os.path.join(disc, model + "-"), "rb").read())
ev, geo = T.Evaluator(pose), T.Geometry(pose)
a = pose.anim(animname)
node, vis, pos, tex = ev.at_frame(a, a.start)
mats = ev.world_matrices(node, vis)
names = None if sel.startswith("-") else sel.split(",")
ymin = float(sel[1:]) if names is None else None
parts = []
for gi, g in enumerate(pose.groups()):
    if g.shape < 0 or mats[gi] is None: continue
    if names is not None and g.name not in names: continue
    m = mats[gi]
    for di, fan in geo.shape_polys(g.shape):
        dr = geo.draws[di]
        if not dr["tex_count"] or len(fan) != 4: continue
        slot = dr["tex_data"][0]; ti = geo.texbinds[geo.texdata[slot][0] + tex[slot][0]][0]
        ts, tt = tex[slot][1], tex[slot][2]
        ws = [[m[r][0]*pos[p][0]+m[r][1]*pos[p][1]+m[r][2]*pos[p][2]+m[r][3] for r in range(3)] for p,_c,_uv in fan]
        uv = [(u + ts, v + tt) for _p,_c,(u, v) in fan]
        xs=[w[0] for w in ws]; ys=[w[1] for w in ws]
        if max(xs)-min(xs) < 0.05 or max(ys)-min(ys) < 0.05: continue
        if ymin is not None and max(ys) <= ymin: continue
        z = sum(w[2] for w in ws)/4
        # world x and y directions in texture space: the edges from corner 0
        def tdir(axis):
            for k in (1, 3):
                dx, dy = ws[k][0]-ws[0][0], ws[k][1]-ws[0][1]
                d = dx if axis == 0 else dy
                if abs(d) > 1e-3 and abs(d) > abs(dy if axis == 0 else dx):
                    du, dv = uv[k][0]-uv[0][0], uv[k][1]-uv[0][1]
                    return ("u", 1 if du*d > 0 else -1) if abs(du) > abs(dv) else ("v", 1 if dv*d > 0 else -1)
            raise ValueError(g.name)
        W, H = ims[ti][0], ims[ti][1]
        us=[c[0] for c in uv]; vs=[c[1] for c in uv]
        cx, cy = round(min(us)*W), round(min(vs)*H)
        cw, ch = round(max(us)*W) - cx, round(max(vs)*H) - cy
        parts.append((z, gi, g.name, ti, (cx, cy, cw, ch), (W, H), tdir(0), tdir(1), min(xs), max(xs), min(ys), max(ys)))
parts.sort(key=lambda p: (p[0], p[1]))
X0 = min(p[8] for p in parts); X1 = max(p[9] for p in parts)
Y0 = min(p[10] for p in parts) if ymin is None else ymin
Y1 = max(p[11] for p in parts)
print(f"steps {prefix}_cards")
for i, (z, gi, name, ti, (cx, cy, cw, ch), (W, H), xd, yd, x0, x1, y0, y1) in enumerate(parts):
    p = f"p{i}"
    print(f"  piece {p} model {model} {ti}    # {name}")
    if (cx, cy, cw, ch) != (0, 0, W, H):
        print(f"  @{p} crop {cx} {cy} {cw} {ch}")
    # rows down the image are world -y; columns world +x
    if xd[0] == "v":  # turned a quarter: transpose (rotate 90, flip x) on a square
        s = max(cw, ch)
        print(f"  @{p} crop 0 0 {s} {s}")
        print(f"  @{p} rotate 90")
        print(f"  @{p} crop {s - ch} 0 {ch} {cw}")
        print(f"  @{p} flip x")
        fx, fy = xd[1] < 0, yd[1] > 0   # x along +v; y down along +u
    else:
        fx, fy = xd[1] < 0, yd[1] > 0
    if fx: print(f"  @{p} flip x")
    if fy: print(f"  @{p} flip y")
    pw, ph = round((x1-x0)*8), round((y1-y0)*8)
    tw, th = (ch, cw) if xd[0] == "v" else (cw, ch)
    if (pw, ph) != (tw, th):
        print(f"  @{p} scale {pw} {ph} texel")
print("end\n")
CW, CH = round((X1-X0)*8), round((Y1-Y0)*8)
print(f"steps {prefix}_head")
print(f"  piece head canvas {CW} {CH}")
for i, pp in enumerate(parts):
    print(f"  @head paste p{i} {round((pp[8]-X0)*8)} {round((Y1-pp[11])*8)}")
print("end")
print(f"# head {CW}x{CH}", file=sys.stderr)
