# A generated test texture pack (no game pixels) for tests/port/testpass/plat_texpack_mem*.json: for each dumped
# name, scale x the size, a flat tint with a checker so it is obvious on screen (and some 4096 px ones,
# to exercise Texture pack memory's size cap and budget).  usage: gen_test_texpack.py DUMPDIR OUTDIR [scale]
#   (plat_texdump_gor_01 dumps into the runner cache folder texture_dumps; OUTDIR local-docs/texpack/textures, scale 8)
import os, re, struct, sys, zlib
dump, out = sys.argv[1], sys.argv[2]
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 4
os.makedirs(out, exist_ok=True)
def png(path, w, h, c1, c2, cell):
    row_a = b"\0" + b"".join(bytes(c1 if (x // cell) % 2 == 0 else c2) for x in range(w))
    row_b = b"\0" + b"".join(bytes(c2 if (x // cell) % 2 == 0 else c1) for x in range(w))
    raw = b"".join(row_a if (y // cell) % 2 == 0 else row_b for y in range(h))
    def chunk(k, d): return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
names = sorted(f for dp, _, fs in os.walk(dump) for f in fs if f.startswith("tex1_") and f.endswith((".png", ".dds")))
total = 0
for i, f in enumerate(names):
    m = re.match(r"tex1_(\d+)x(\d+)_", f)
    w, h = int(m.group(1)), int(m.group(2))
    s = scale * (4 if i % 5 == 0 else 1)  # every fifth one much larger
    W, H = min(w * s, 4096), min(h * s, 4096)
    hue = [(255, 64, 200, 255), (64, 220, 255, 255), (255, 220, 40, 255), (90, 255, 90, 255)][i % 4]
    png(os.path.join(out, f.rsplit(".", 1)[0] + ".png"), W, H, hue, (40, 40, 40, 255), max(4, W // 8))
    total += W * H * 4
print(f"{len(names)} textures, {total / 2**20:.0f} MiB as RGBA")
