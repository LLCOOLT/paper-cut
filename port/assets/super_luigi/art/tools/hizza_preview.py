"""Hizza's preview sheet: the cards assembled roughly as the rig poses them,
plus every expression of both heads. Writes ../hizza/preview.png.

Placement uses each card's pivot from parts.json (pixels, 8 texels/unit).
"""
import json
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(__file__)
DIR = os.path.join(HERE, "..", "hizza")
META = json.load(open(os.path.join(DIR, "parts.json")))["cards"]


def card(name):
    return Image.open(os.path.join(DIR, name + ".png")).convert("RGBA")


def place(canvas, name, at, angle=0.0, pivot=None):
    """Paste card `name` so its pivot lands on `at`, rotated `angle` degrees (CCW) about the pivot."""
    im = card(name)
    px, py = pivot if pivot else META[name]["pivot"]
    r = int(max(im.width, im.height) * 1.5) + 4
    big = Image.new("RGBA", (2 * r, 2 * r), (0, 0, 0, 0))
    big.paste(im, (r - px, r - py))
    if angle:
        big = big.rotate(angle, resample=Image.BICUBIC, center=(r, r))
    canvas.alpha_composite(big, (int(at[0] - r), int(at[1] - r))) if at[0] - r >= 0 and at[1] - r >= 0 else \
        _paste_clipped(canvas, big, int(at[0] - r), int(at[1] - r))


def _paste_clipped(canvas, im, x, y):
    tmp = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    tmp.paste(im, (x, y), im)
    canvas.alpha_composite(tmp)


# head-relative anchors (in head card pixels) for the jaw hinge and tongue root
JAW_AT = (128, 100)
TONGUE_AT = (112, 100)


def head_group(canvas, v, at, face="open", jaw_angle=0.0, tongue_out=0.0, stars=False, head_angle=0.0):
    hx, hy = META[f"head_{v}"]["pivot"]
    ox, oy = at[0] - hx, at[1] - hy

    def rel(pt):
        return (ox + pt[0], oy + pt[1])
    tx, ty = TONGUE_AT
    place(canvas, f"tongue_{v}", rel((tx - tongue_out, ty)), angle=jaw_angle * 0.5)
    if jaw_angle > 20:
        place(canvas, f"mouth_{v}", rel(JAW_AT))
    place(canvas, f"jaw_{v}", rel(JAW_AT), angle=jaw_angle)
    name = {"open": f"head_{v}", "blink": f"head_{v}_blink", "hurt": f"head_{v}_hurt"}[face]
    place(canvas, name, at, angle=head_angle)
    if stars:
        place(canvas, "dizzy_stars", rel((84, 4)))


def full_pose(canvas, base=(40, 40), ground=600):
    gx, gy = base
    place(canvas, "coil_back", (gx + 330, gy + ground - 30))
    # the tail rises from the back loop, its head looking left too
    place(canvas, "tail_b1", (gx + 440, gy + 520), angle=-4)
    place(canvas, "tail_b2", (gx + 448, gy + 430), angle=-2)
    place(canvas, "tail_b3", (gx + 452, gy + 346), angle=4)
    head_group(canvas, "b", (gx + 466, gy + 270), jaw_angle=8)
    place(canvas, "neck_a1", (gx + 230, gy + 540), angle=6)
    place(canvas, "neck_a2", (gx + 216, gy + 440), angle=4)
    place(canvas, "collar", (gx + 222, gy + 478), angle=6)
    place(canvas, "neck_a3", (gx + 210, gy + 346), angle=-2)
    head_group(canvas, "a", (gx + 226, gy + 262), jaw_angle=14, tongue_out=40)
    place(canvas, "coil_front", (gx + 300, gy + ground))


def main():
    W, H = 1310, 1140
    sheet = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(sheet)
    d.rectangle([0, 640, 630, 642], fill=(180, 170, 150, 255))
    full_pose(sheet, (10, 0), ground=620)
    d.text((10, 6), "Hizza (c_sl_hizza): assembled 1:1, 8 texels/unit", fill=(40, 30, 30, 255))
    d.text((10, 20), "Luigi is 24 units = 192 px (green bar)", fill=(40, 30, 30, 255))
    # Luigi-height marker
    d.rectangle([580, 640 - 192, 584, 640], fill=(60, 140, 60, 255))
    # expressions
    exprs = [("idle", dict(face="open")), ("blink", dict(face="blink")),
             ("hiss", dict(face="open", jaw_angle=16, tongue_out=46)), ("bite", dict(face="open", jaw_angle=38)),
             ("hurt / collide", dict(face="hurt", stars=True, jaw_angle=10)),
             ("defeated", dict(face="hurt", jaw_angle=26, tongue_out=30))]
    for row, v in enumerate("ab"):
        for i, (label, kw) in enumerate(exprs):
            x, y = 640 + (i % 3) * 220, 24 + (row * 2 + i // 3) * 278
            d.rectangle([x, y, x + 214, y + 272], fill=(244, 241, 234, 255), outline=(210, 200, 185, 255))
            d.rectangle([x, y, x + 214, y + 14], fill=(225, 218, 205, 255))
            d.text((x + 4, y + 2), f"head_{v}: {label}", fill=(40, 30, 30, 255))
            head_group(sheet, v, (x + 152, y + 168), **kw)
    sheet.convert("RGB").save(os.path.join(DIR, "preview.png"))
    print(os.path.abspath(os.path.join(DIR, "preview.png")))


if __name__ == "__main__":
    main()
