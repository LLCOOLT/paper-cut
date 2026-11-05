#!/usr/bin/env python3
"""Put a $TTYD_TOUCH_SHOT capture together (port/touch/touch_overlay.cpp):
the game's picture (touch_<frame>.png) in its place in the window, and the
on-screen controls' Dear ImGui triangles (touch_<frame>.txt, the font atlas
in touch_atlas.bin) drawn over it, as the window shows them. Window captures
on Windows can't see the WebGPU swapchain, so this is how a test run's
controls are looked at.

  touch_shot.py SHOTDIR [FRAME ...] [--scale S]   writes touch_<frame>_win.png

Needs numpy and Pillow."""
import os
import sys

import numpy as np
from PIL import Image


def load_atlas(d):
    path = os.path.join(d, "touch_atlas.bin")
    if not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    head, _, rest = data.partition(b"\n")
    w, h, wx, wy = head.split()
    w, h = int(w), int(h)
    return np.frombuffer(rest[: w * h], np.uint8).reshape(h, w).astype(np.float32) / 255.0


def compose(d, frame, scale):
    lines = open(os.path.join(d, f"touch_{frame}.txt")).read().split("\n")
    ww, wh, px, py, pw, ph = (float(v) for v in lines[0].split())
    ww, wh = int(ww), int(wh)
    nv = int(lines[1])
    verts = np.array([[float(v) for v in l.split()] for l in lines[2 : 2 + nv]]).reshape(-1, 5)
    ni = int(lines[2 + nv])
    idx = np.array([int(l) for l in lines[3 + nv : 3 + nv + ni]], np.int64).reshape(-1, 3)
    out = np.zeros((wh, ww, 3), np.float32)
    shot = os.path.join(d, f"touch_{frame}.png")
    if os.path.exists(shot) and pw > 0 and ph > 0:
        pic = Image.open(shot).convert("RGB").resize((int(round(pw)), int(round(ph))))
        x0, y0 = int(round(px)), int(round(py))
        a = np.asarray(pic, np.float32) / 255.0
        h = min(a.shape[0], wh - y0)
        w = min(a.shape[1], ww - x0)
        out[y0 : y0 + h, x0 : x0 + w] = a[:h, :w]
    atlas = load_atlas(d)
    col = verts[:, 4].astype(np.uint64)
    rgba = np.stack([(col >> s) & 0xFF for s in (0, 8, 16, 24)], 1).astype(np.float32) / 255.0
    for tri in idx:
        p = verts[tri, :2]
        x0, y0 = np.floor(p.min(0)).astype(int)
        x1, y1 = np.ceil(p.max(0)).astype(int)
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, ww - 1), min(y1, wh - 1)
        if x1 < x0 or y1 < y0:
            continue
        (ax, ay), (bx, by), (cx, cy) = p
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den
        l1 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den
        l2 = 1.0 - l0 - l1
        inside = (l0 >= -1e-4) & (l1 >= -1e-4) & (l2 >= -1e-4)
        if not inside.any():
            continue
        c = rgba[tri]
        colr = l0[..., None] * c[0] + l1[..., None] * c[1] + l2[..., None] * c[2]
        alpha = colr[..., 3]
        if atlas is not None:
            uv = verts[tri, 2:4]
            u = l0 * uv[0, 0] + l1 * uv[1, 0] + l2 * uv[2, 0]
            v = l0 * uv[0, 1] + l1 * uv[1, 1] + l2 * uv[2, 1]
            tx = np.clip((u * atlas.shape[1]).astype(int), 0, atlas.shape[1] - 1)
            ty = np.clip((v * atlas.shape[0]).astype(int), 0, atlas.shape[0] - 1)
            alpha = alpha * atlas[ty, tx]
        alpha = np.where(inside, alpha, 0.0)[..., None]
        region = out[y0 : y1 + 1, x0 : x1 + 1]
        region[:] = region * (1 - alpha) + colr[..., :3] * alpha
    img = Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8))
    if scale != 1.0:
        img = img.resize((int(ww * scale), int(wh * scale)), Image.LANCZOS)
    path = os.path.join(d, f"touch_{frame}_win.png")
    img.save(path)
    return path


def main():
    args = sys.argv[1:]
    scale = 1.0
    if "--scale" in args:
        i = args.index("--scale")
        scale = float(args[i + 1])
        del args[i : i + 2]
    d = args[0]
    frames = args[1:] or sorted(
        f[6:-4] for f in os.listdir(d) if f.startswith("touch_") and f.endswith(".txt")
    )
    for f in frames:
        print(compose(d, f, scale))


if __name__ == "__main__":
    main()
