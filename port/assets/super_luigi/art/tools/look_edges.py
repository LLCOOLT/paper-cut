"""Look's glowing edge outlines for the Room of Nothing (Super Luigi ch5, tik_52).

Run: python look_edges.py -> ../look_edges/*.png, preview.png
ch5.md, New assets 6: "thin glowing strip props, straight segments in a few lengths, to lay along the
room's edges"; the reveal group: "glowing outlines (thin blue-white strips, as props) along every floor
edge, every drop and every wall; ... visible for 8 seconds after a Look, then fade".
The room is pure white, so a white glow would vanish: the strip is a white-hot core in a saturated
blue halo, which reads on white and on dark alike. These are effect textures (STYLE.md R10: effects
use IA4 / soft alpha), drawn straight in PIL:
  edge_glow      128x16, tiles along its length (u): stretch the UVs over a segment of any length
  edge_glow_end  16x16, a rounded cap for a segment's ends and corners
  edge_glow_pulse 128x16, the core brighter and the halo wider, to swap in as the Look lands
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "look_edges")
CORE = (250, 253, 255)
HALO = (70, 130, 240)


def strip_px(d, wide=1.0):
    """colour and alpha at distance d (texels) from the strip's centre line"""
    core = max(0.0, 1 - d / (1.6 * wide))
    halo = max(0.0, 1 - d / (7.5 * wide)) ** 1.6
    c = tuple(int(HALO[i] + (CORE[i] - HALO[i]) * min(1.0, core * 1.4)) for i in range(3))
    return c + (int(255 * min(1.0, halo * 0.9 + core)),)


def strip(name, wide=1.0):
    w, h = 128, 16
    im = Image.new("RGBA", (w, h))
    for y in range(h):
        d = abs(y + 0.5 - h / 2)
        for x in range(w):
            im.putpixel((x, y), strip_px(d, wide))
    im.save(os.path.join(OUT, name + ".png"))


def end():
    im = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            d = math.hypot(x + 0.5 - 8, y + 0.5 - 8)
            im.putpixel((x, y), strip_px(d))
    im.save(os.path.join(OUT, "edge_glow_end.png"))


def preview():
    W, H = 520, 260
    c = Image.new("RGBA", (W, H), (250, 250, 250, 255))
    d = ImageDraw.Draw(c)
    d.text((8, 6), "On the Room of Nothing's white: a floor edge, a drop and a block, outlined", fill=(80, 80, 90, 255))
    seg = Image.open(os.path.join(OUT, "edge_glow.png")).convert("RGBA")
    cap = Image.open(os.path.join(OUT, "edge_glow_end.png")).convert("RGBA")

    def line(x0, y0, x1, y1):
        L = int(math.hypot(x1 - x0, y1 - y0))
        s = Image.new("RGBA", (L, 16))
        for k in range(0, L, 128):
            s.alpha_composite(seg.crop((0, 0, min(128, L - k), 16)), (k, 0))
        a = math.degrees(math.atan2(y1 - y0, x1 - x0))
        r = s.rotate(-a, expand=True, resample=Image.BICUBIC)
        c.alpha_composite(r, (int((x0 + x1) / 2 - r.width / 2), int((y0 + y1) / 2 - r.height / 2)))
        for x, y in ((x0, y0), (x1, y1)):
            c.alpha_composite(cap, (int(x - 8), int(y - 8)))
    line(20, 150, 230, 150)       # a floor edge
    line(230, 150, 230, 230)      # the drop
    line(300, 120, 380, 120)      # a hidden block's outline
    line(380, 120, 380, 190)
    line(380, 190, 300, 190)
    line(300, 190, 300, 120)
    pulse = Image.open(os.path.join(OUT, "edge_glow_pulse.png")).convert("RGBA")
    c.alpha_composite(pulse, (20, 60))
    d.text((20, 44), "edge_glow_pulse (the Look landing)", fill=(80, 80, 90, 255))
    return c


def build():
    os.makedirs(OUT, exist_ok=True)
    clean(OUT)
    strip("edge_glow")
    strip("edge_glow_pulse", wide=1.35)
    end()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
