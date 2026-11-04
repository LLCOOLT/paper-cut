"""Rumblebump Volcano: lava and rock textures for Keelhaul Key's caves (muj_10..12) retinted as a
volcano (Super Luigi ch1). Map textures, not character cards: STYLE.md R10 (measured on muj_10's
own: power-of-two sizes, tileable, flat 3-5 tone fills with a stipple grout and a dark drop line on
the stones' lower edges, no outline, 1-bit alpha only on edge strips).

Run: python volcano.py -> ../volcano/*.png, preview.png
Each texture is the same size as the muj_10 texture it's meant to replace (README), so the map's
texel density is kept.
"""
import os
import random

import numpy as np
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "volcano")


def hexc(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], dtype=np.float32)


def tile_noise(w, h, cells, seed, octaves=3):
    """tileable value noise in [0, 1]"""
    rnd = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        cx, cy = cells * (2 ** o), max(1, int(cells * (2 ** o) * h / w))
        g = rnd.random((cy, cx)).astype(np.float32)
        ys = np.arange(h) * cy / h
        xs = np.arange(w) * cx / w
        y0 = np.floor(ys).astype(int)
        x0 = np.floor(xs).astype(int)
        fy = ys - y0
        fx = xs - x0
        fy = fy * fy * (3 - 2 * fy)
        fx = fx * fx * (3 - 2 * fx)
        y1, x1 = (y0 + 1) % cy, (x0 + 1) % cx
        a = g[y0][:, x0]
        b = g[y0][:, x1]
        c = g[y1][:, x0]
        d = g[y1][:, x1]
        top = a + (b - a) * fx[None, :]
        bot = c + (d - c) * fx[None, :]
        out += amp * (top + (bot - top) * fy[:, None])
        tot += amp
        amp *= 0.5
    return out / tot


def voronoi(w, h, n, seed, warp=6.0, wseed=None):
    """toroidal Voronoi with a noise warp: (d1, d2, index) per pixel"""
    rnd = np.random.default_rng(seed)
    pts = rnd.random((n, 2)) * [w, h]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    if warp:
        xx = xx + (tile_noise(w, h, 4, (wseed or seed) + 1) - 0.5) * 2 * warp
        yy = yy + (tile_noise(w, h, 4, (wseed or seed) + 2) - 0.5) * 2 * warp
    d1 = np.full((h, w), 1e9, np.float32)
    d2 = np.full((h, w), 1e9, np.float32)
    idx = np.zeros((h, w), np.int32)
    for i, (px, py) in enumerate(pts):
        dx = np.abs(xx - px)
        dx = np.minimum(dx, w - dx)
        dy = np.abs(yy - py)
        dy = np.minimum(dy, h - dy)
        d = np.sqrt(dx * dx + dy * dy)
        closer = d < d1
        d2 = np.where(closer, d1, np.minimum(d2, d))
        idx = np.where(closer, i, idx)
        d1 = np.where(closer, d, d1)
    return d1, d2, idx, pts


def paint(mask_list, h, w):
    img = np.zeros((h, w, 3), np.float32)
    for m, col in mask_list:
        img[m] = hexc(col)
    return img


def save(img, name, alpha=None):
    os.makedirs(OUT, exist_ok=True)
    arr = np.clip(img, 0, 255).astype(np.uint8)
    if alpha is None:
        Image.fromarray(arr, "RGB").save(os.path.join(OUT, name + ".png"))
    else:
        a = np.where(alpha, 255, 0).astype(np.uint8)
        rgba = np.dstack([np.where(alpha[..., None], arr, 0), a])
        Image.fromarray(rgba.astype(np.uint8), "RGBA").save(os.path.join(OUT, name + ".png"))


def stipple(h, w, step=8):
    yy, xx = np.mgrid[0:h, 0:w]
    off = (yy // step) % 2 * (step // 2)
    return (((xx + off) % step) < 2) & ((yy % step) < 2)


def shift_down(m, k=2):
    return np.roll(m, -k, axis=0)


# --------------------------------------------------------------- textures --
ROCK = dict(base="#a8583a", light="#cc7c52", dark="#7a3622", line="#4a1a10", grout="#5c2618", dot="#3c140c",
            glow="#f07222", core="#ffc848")


def rock_wall(size=256, seed=3):
    w = h = size
    d1, d2, idx, _ = voronoi(w, h, 18, seed, warp=11)
    gapn = tile_noise(w, h, 6, seed + 5)
    stone = (d2 - d1) > (6 + gapn * 12)
    # light patches inside each stone, toward its upper left
    light = stone & (tile_noise(w, h, 8, seed + 7) > 0.55)
    dark = stone & (tile_noise(w, h, 8, seed + 9) < 0.28)
    # a dark line under each stone's lower edge (the stone is above grout two texels down)
    line = stone & ~shift_down(stone, 2)
    grout = ~stone
    # lava glowing in some of the grout channels
    hot = grout & (tile_noise(w, h, 3, seed + 11) > 0.66) & ((d2 - d1) < 4.0)
    core = hot & ((d2 - d1) < 1.6)
    img = paint([(grout, ROCK["grout"]), (grout & stipple(h, w), ROCK["dot"]), (hot, ROCK["glow"]),
                 (core, ROCK["core"]), (stone, ROCK["base"]), (light, ROCK["light"]), (dark, ROCK["dark"]),
                 (line, ROCK["line"])], h, w)
    save(img, "volcano_rock_wall")


def rock_edge(seed=4):
    w, h = 256, 128
    d1, d2, idx, _ = voronoi(w, h, 14, seed, warp=7)
    xs = np.arange(w)
    edge = 70 + 22 * np.sin(xs * 2 * np.pi / w * 3 + 1.0) + 12 * np.sin(xs * 2 * np.pi / w * 7)
    edge = edge + (tile_noise(w, 1, 16, seed)[0] - 0.5) * 18
    yy = np.mgrid[0:h, 0:w][0]
    body = yy < edge[None, :]
    stone = body & ((d2 - d1) > 3.5)
    light = stone & (tile_noise(w, h, 8, seed + 7) > 0.55)
    line = stone & ~shift_down(stone, 2)
    img = paint([(body, ROCK["grout"]), (body & stipple(h, w), ROCK["dot"]), (stone, ROCK["base"]),
                 (light, ROCK["light"]), (line, ROCK["line"])], h, w)
    save(img, "volcano_rock_edge", alpha=body)


def cave_streaks(seed=5):
    w = h = 256
    xs = np.arange(w)
    rnd = random.Random(seed)

    def jag(base, amp, k):
        n = tile_noise(w, 1, k, rnd.randint(0, 999), octaves=2)[0]
        hi = tile_noise(w, 1, 48, rnd.randint(0, 999), octaves=1)[0]
        return base + (n - 0.5) * amp + (hi - 0.5) * amp * 0.9
    b1, b2, b3, b4 = jag(46, 50, 6), jag(104, 60, 7), jag(166, 60, 8), jag(214, 40, 9)
    yy = np.mgrid[0:h, 0:w][0]
    cols = ["#3e1a12", "#5e2818", "#84391f", "#a8502a", "#e0702a"]
    img = paint([(yy >= 0, cols[0]), (yy >= b1[None, :], cols[1]), (yy >= b2[None, :], cols[2]),
                 (yy >= b3[None, :], cols[3]), (yy >= b4[None, :], cols[4])], h, w)
    # drip highlights down the bands
    for x in range(0, w, 1):
        if rnd.random() < 0.06:
            y0 = int(b2[x]) - rnd.randint(4, 30)
            img[max(0, y0):int(b2[x]), x] = hexc("#9a4626")
    save(img, "volcano_cave_streaks")


def ash_ground(seed=6):
    w = h = 128
    d1, d2, idx, pts = voronoi(w, h, 46, seed, warp=3)
    rnd = np.random.default_rng(seed)
    tone = rnd.integers(0, 3, len(pts))
    ember = rnd.random(len(pts)) < 0.05
    pebble = (d2 - d1) > 1.6
    img = paint([(~pebble, "#3a2626")], h, w)
    for t, col in enumerate(("#5c4240", "#6e5250", "#86686a")):
        img[pebble & (tone[idx] == t)] = hexc(col)
    img[pebble & ember[idx]] = hexc("#c8501e")
    img[pebble & ember[idx] & (d1 < 2.2)] = hexc("#ffb040")
    lit = pebble & ~np.roll(pebble, 1, axis=0) & ~ember[idx]
    img[lit] = hexc("#9e8284")
    line = pebble & ~shift_down(pebble, 1)
    img[line] = hexc("#2a1a1a")
    save(img, "volcano_ash_ground")


def lava(frame, seed=7):
    """molten lava with floating crust plates; frame 2 drifts the crust 6, 3 texels and turns the glow"""
    w = h = 256
    d1, d2, idx, _ = voronoi(w, h, 12, seed, warp=12)
    gap = d2 - d1
    crust = gap > (11 + tile_noise(w, h, 5, seed + 3) * 12)
    light = crust & (tile_noise(w, h, 8, seed + 5) > 0.6)
    if frame == 2:
        crust, light = np.roll(crust, (3, 6), (0, 1)), np.roll(light, (3, 6), (0, 1))
    line = crust & ~shift_down(crust, 2)
    molten = ~crust
    heat = tile_noise(w, h, 4, seed + 20, octaves=2)
    if frame == 2:
        heat = np.roll(heat, (9, -5), (0, 1))
    img = paint([(molten, "#e04a1a"), (molten & (heat > 0.48), "#ff7a1e"), (molten & (heat > 0.62), "#ffb43a"),
                 (molten & (heat > 0.74), "#ffe680"), (crust, "#5c2418"), (light, "#7e3a22"),
                 (crust & stipple(h, w, 10), "#4a1a10"), (line, "#2e0c06")], h, w)
    # a bright rim where the molten meets a crust plate
    rim = molten & (np.roll(crust, 2, 0) | np.roll(crust, -2, 0) | np.roll(crust, 2, 1) | np.roll(crust, -2, 1))
    img[rim] = hexc("#ffc848")
    save(img, f"volcano_lava_{frame}")


def lava_edge(seed=8):
    w, h = 256, 64
    xs = np.arange(w)
    top = 18 + 8 * np.sin(xs * 2 * np.pi / w * 4) + (tile_noise(w, 1, 24, seed)[0] - 0.5) * 12
    yy = np.mgrid[0:h, 0:w][0]
    body = yy >= top[None, :]
    rim = body & (yy < top[None, :] + 6)
    img = paint([(body, "#ff7a1e"), (body & (yy > 30), "#ffb43a"), (body & (yy > 46), "#ffe680"),
                 (rim, "#5c2418"), (rim & (yy < top[None, :] + 2), "#7e3a22"),
                 (body & ~np.roll(body, 1, axis=0), "#2e0c06")], h, w)
    save(img, "volcano_lava_edge", alpha=body)


def preview():
    names = ["volcano_rock_wall", "volcano_lava_1", "volcano_lava_2", "volcano_cave_streaks",
             "volcano_ash_ground", "volcano_rock_edge", "volcano_lava_edge"]
    W, H = 1400, 1150
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    x, y = 10, 26
    for n in names:
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        # 2x2 tiles to show the wrap
        reps = (2, 2) if n not in ("volcano_cave_streaks", "volcano_rock_edge", "volcano_lava_edge") else (2, 1)
        t = Image.new("RGBA", (im.width * reps[0], im.height * reps[1]), (0, 0, 0, 0))
        for i in range(reps[0]):
            for j in range(reps[1]):
                t.paste(im, (i * im.width, j * im.height))
        if x + t.width > W - 10:
            x, y = 10, y + 540
        c.alpha_composite(t, (x, y))
        d.text((x, y - 14), f"{n} {im.width}x{im.height}, tiled x{reps[0]}" + ("" if reps[1] == 1 else "x2"),
               fill=(40, 30, 30, 255))
        x += t.width + 12
    return c


def build():
    clean(OUT)
    rock_wall()
    rock_edge()
    cave_streaks()
    ash_ground()
    lava(1)
    lava(2)
    lava_edge()


if __name__ == "__main__":
    build()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))
    print(sorted(os.listdir(OUT)))
