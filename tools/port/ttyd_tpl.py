#!/usr/bin/env python3
"""GameCube TPL texture files, both ways, and PNG files, in pure Python
(docs/CHARACTERS.md).

- `TplFile.parse(data)`: the images (size, format, the header's wrap, filter
  and LOD fields, the palette) with their data as stored.
- `image.pixels()`: RGBA rows (every format tpl2png.py decodes).
- `image.replace(rows)`: new pixels, encoded in the image's own format (and
  its mipmaps again from them); a new size is allowed.
- `TplFile.build()`: the file. An image not replaced keeps its bytes; when
  every image keeps its size the layout is the original's, so a file with
  nothing replaced is written back byte for byte.
- `read_png(path)` / `write_png(path, rows)`.

Encoding: I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8 and CMPR exactly as the
hardware reads them (CMPR's endpoints are a simple fit: the two colours of
each 4x4 block furthest apart); C4, C8 and C14X2 by the nearest colour of the
image's own palette, which stays as it is.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tpl2png  # noqa: E402  (the decoders)

MAGIC = 0x0020AF30
FORMAT_NAMES = tpl2png.FORMATS
BLOCKS = tpl2png.BLOCKS


def data_size(w, h, fmt):
    bw, bh, bpp = BLOCKS[fmt]
    return ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * bw * bh * bpp // 8


def _levels_size(w, h, fmt, levels):
    n = 0
    for lv in range(levels):
        n += data_size(max(w >> lv, 1), max(h >> lv, 1), fmt)
    return n


# ------------------------------------------------------------------ PNG ----

def write_png(path, rows):
    """RGBA rows ([[(r, g, b, a), ...], ...]) to an 8-bit RGBA PNG"""
    h, w = len(rows), len(rows[0])
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tpl2png.write_png(path, w, h, [p for r in rows for p in r])


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    """a PNG (any colour type, 1-16 bits, not interlaced) as RGBA rows"""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos, idat, plte, trns = 8, bytearray(), None, None
    w = h = depth = ctype = interlace = None
    while pos < len(data):
        n, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _comp, _filt, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = bytes(body)
        elif kind == b"IDAT":
            idat.extend(body)
        elif kind == b"IEND":
            break
    if w is None:
        raise ValueError(f"{path}: no IHDR")
    if interlace:
        raise ValueError(f"{path}: interlaced PNGs aren't read; save it without interlacing")
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = chans * depth
    stride = (w * bits + 7) // 8
    bpp = max(1, bits // 8)
    raw = zlib.decompress(bytes(idat))
    rows, prev, p = [], bytearray(stride), 0
    for _y in range(h):
        ft = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ft == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 255
        prev = line
        # samples
        if depth == 16:
            samples = [line[i] for i in range(0, len(line), 2)]  # the high byte
        elif depth == 8:
            samples = list(line)
        else:
            samples = []
            for byte in line:
                for s in range(8 // depth):
                    samples.append((byte >> (8 - depth * (s + 1))) & ((1 << depth) - 1))
        row = []
        for x in range(w):
            s = samples[x * chans:(x + 1) * chans]
            if ctype == 3:
                idx = s[0]
                r, g, b = plte[idx]
                a = trns[idx] if trns is not None and idx < len(trns) else 255
                row.append((r, g, b, a))
                continue
            if depth < 8:
                s = [v * 255 // ((1 << depth) - 1) for v in s]
            if ctype == 0:
                row.append((s[0], s[0], s[0], 255))
            elif ctype == 4:
                row.append((s[0], s[0], s[0], s[1]))
            elif ctype == 2:
                row.append((s[0], s[1], s[2], 255))
            else:
                row.append(tuple(s))
        rows.append(row)
    return rows


# ------------------------------------------------------------- encoding ----

def _lum(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114 + 500) // 1000


def _to565(c):
    return ((c[0] * 31 + 127) // 255) << 11 | ((c[1] * 63 + 127) // 255) << 5 | ((c[2] * 31 + 127) // 255)


def _to5a3(c):
    if c[3] >= 0xE0:
        return 0x8000 | ((c[0] * 31 + 127) // 255) << 10 | ((c[1] * 31 + 127) // 255) << 5 | ((c[2] * 31 + 127) // 255)
    return ((c[3] * 7 + 127) // 255) << 12 | ((c[0] * 15 + 127) // 255) << 8 | \
        ((c[1] * 15 + 127) // 255) << 4 | ((c[2] * 15 + 127) // 255)


def _dist(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _cmpr_sub(px):
    """one 4x4 DXT1 block (16 RGBA pixels) as 8 bytes, big-endian"""
    transparent = any(p[3] < 128 for p in px)
    solid = [p for p in px if p[3] >= 128]
    if not solid:
        return struct.pack(">HHI", 0, 0xFFFF, 0xFFFFFFFF)
    best, ea, eb = -1, solid[0], solid[0]
    uniq = list(set(p[:3] for p in solid))
    for i in range(len(uniq)):
        for j in range(i + 1, len(uniq)):
            d = _dist(uniq[i], uniq[j])
            if d > best:
                best, ea, eb = d, uniq[i], uniq[j]
    c0, c1 = _to565(ea), _to565(eb)
    if transparent:
        if c0 > c1:
            c0, c1 = c1, c0
    else:
        if c0 < c1:
            c0, c1 = c1, c0
        elif c0 == c1:
            # one colour: four-colour mode needs c0 > c1
            if c0 > 0:
                c1 = c0 - 1
            else:
                c0 = 1
    p0, p1 = tpl2png.rgb565(c0)[:3], tpl2png.rgb565(c1)[:3]
    if c0 > c1:
        pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1))]
    bits = 0
    for i, p in enumerate(px):
        if p[3] < 128:
            sel = 3
        else:
            sel = min(range(len(pal)), key=lambda k: _dist(pal[k], p))
        bits |= sel << (30 - 2 * i)
    return struct.pack(">HHI", c0, c1, bits)


def encode(rows, fmt, palette=None):
    """RGBA rows in GX format fmt (palette: RGBA colours for C4/C8/C14X2)"""
    h, w = len(rows), len(rows[0])
    bw, bh, bpp = BLOCKS[fmt]

    def at(x, y):
        return rows[y][x] if x < w and y < h else (0, 0, 0, 0)

    def nearest(c):
        cache = nearest.cache
        if c not in cache:
            if c[3] < 128 and any(p[3] < 128 for p in palette):
                cache[c] = min((i for i, p in enumerate(palette) if p[3] < 128), key=lambda i: palette[i][3])
            else:
                cache[c] = min(range(len(palette)),
                               key=lambda i: _dist(palette[i], c) + (palette[i][3] - c[3]) ** 2)
        return cache[c]
    nearest.cache = {}
    out = bytearray()
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:
                for sb in range(4):
                    sx, sy = bx + (sb & 1) * 4, by + (sb >> 1) * 4
                    out.extend(_cmpr_sub([at(sx + i % 4, sy + i // 4) for i in range(16)]))
                continue
            px = [at(bx + i % bw, by + i // bw) for i in range(bw * bh)]
            if fmt == 6:
                out.extend(bytes(v for p in px for v in (p[3], p[0])))
                out.extend(bytes(v for p in px for v in (p[1], p[2])))
                continue
            if bpp == 4:
                vals = [(_lum(p) * 15 + 127) // 255 if fmt == 0 else nearest(p) for p in px]
                out.extend(bytes((vals[i] << 4) | vals[i + 1] for i in range(0, len(vals), 2)))
            elif bpp == 8:
                if fmt == 1:
                    out.extend(bytes(_lum(p) for p in px))
                elif fmt == 2:
                    out.extend(bytes(((p[3] * 15 + 127) // 255) << 4 | ((_lum(p) * 15 + 127) // 255) for p in px))
                else:
                    out.extend(bytes(nearest(p) for p in px))
            else:
                for p in px:
                    if fmt == 3:
                        v = p[3] << 8 | _lum(p)
                    elif fmt == 4:
                        v = _to565(p)
                    elif fmt == 5:
                        v = _to5a3(p)
                    else:
                        v = nearest(p)
                    out.extend(struct.pack(">H", v))
    return bytes(out)


def downsample(rows):
    """half size, a 2x2 box (for mipmaps)"""
    h, w = len(rows), len(rows[0])
    nh, nw = max(h // 2, 1), max(w // 2, 1)
    out = []
    for y in range(nh):
        r = []
        for x in range(nw):
            ps = [rows[min(2 * y + dy, h - 1)][min(2 * x + dx, w - 1)] for dy in (0, 1) for dx in (0, 1)]
            r.append(tuple((sum(p[c] for p in ps) + 2) // 4 for c in range(4)))
        out.append(r)
    return out


# ------------------------------------------------------------------ TPL ----

class TplImage:
    def __init__(self):
        self.header = bytearray(0x24)  # as stored; size and data offset rewritten by build()
        self.data = b""                 # the image's data, every mipmap level
        self.palette_header = None      # bytearray(0xC) or None
        self.palette_data = b""
        self.replaced = False

    width = property(lambda s: struct.unpack_from(">H", s.header, 2)[0])
    height = property(lambda s: struct.unpack_from(">H", s.header, 0)[0])
    format = property(lambda s: struct.unpack_from(">I", s.header, 4)[0])
    min_lod = property(lambda s: s.header[0x21])
    max_lod = property(lambda s: s.header[0x22])

    @property
    def format_name(self):
        return FORMAT_NAMES.get(self.format, "format %d" % self.format)

    def palette(self):
        if self.palette_header is None:
            return None
        count, _u, _p, pfmt = struct.unpack_from(">HBBI", self.palette_header, 0)
        return tpl2png.palette(self.palette_data, 0, count, pfmt)

    def pixels(self):
        if self.format not in BLOCKS:
            raise ValueError("texture format %d isn't supported" % self.format)
        w, h = self.width, self.height
        px = tpl2png.decode(self.data, w, h, self.format, 0, self.palette())
        return [px[y * w:(y + 1) * w] for y in range(h)]

    def levels(self):
        """mipmap levels stored"""
        return 1 + self.max_lod if self.min_lod == 0 and self.max_lod else 1

    def replace(self, rows):
        """new pixels in the image's format; mipmaps made again from them"""
        fmt = self.format
        if fmt not in BLOCKS:
            raise ValueError("texture format %d can't be written" % fmt)
        h, w = len(rows), len(rows[0])
        if w > 1024 or h > 1024:
            raise ValueError("%dx%d: the GameCube's textures are at most 1024x1024" % (w, h))
        levels = self.levels()
        if (w, h) != (self.width, self.height) and levels > 1:
            # as many levels as before, while they still have a pixel
            levels = max(1, min(levels, max(w, h).bit_length()))
            self.header[0x22] = levels - 1
        pal = self.palette()
        if fmt in (8, 9, 10) and pal is None:
            raise ValueError("a palette format without a palette")
        out, cur = bytearray(), rows
        for lv in range(levels):
            out.extend(encode(cur, fmt, pal))
            if lv + 1 < levels:
                cur = downsample(cur)
        struct.pack_into(">HH", self.header, 0, h, w)
        self.data = bytes(out)
        self.replaced = True


class TplFile:
    def __init__(self):
        self.images = []
        self.original = None
        self._layout = None

    @classmethod
    def parse(cls, data):
        t = cls()
        t.original = bytes(data)
        magic, n, tab = struct.unpack_from(">III", data, 0)
        if magic != MAGIC:
            raise ValueError("not a TPL file")
        starts = []
        for i in range(n):
            th, ph = struct.unpack_from(">II", data, tab + 8 * i)
            im = TplImage()
            im.header = bytearray(data[th:th + 0x24])
            off = struct.unpack_from(">I", im.header, 8)[0]
            if im.format in BLOCKS:
                size = _levels_size(im.width, im.height, im.format, im.levels())
            else:
                size = 0
            im.data = bytes(data[off:off + size])
            if ph:
                im.palette_header = bytearray(data[ph:ph + 0xC])
                count, _u, _p, _pfmt, poff = struct.unpack_from(">HBBII", im.palette_header, 0)
                im.palette_data = bytes(data[poff:poff + 2 * count])
            im.stored_size = len(im.data)
            t.images.append(im)
            starts.append((th, ph, off))
        t._layout = (tab, starts)
        return t

    def build(self):
        if self.original is not None and not any(im.replaced for im in self.images):
            return self.original
        same_size = self.original is not None and self._layout is not None and all(
            not im.replaced or len(im.data) == getattr(im, "stored_size", -1) for im in self.images)
        if same_size:
            # in place: every offset as it was
            out = bytearray(self.original)
            tab, starts = self._layout
            for im, (th, _ph, off) in zip(self.images, starts):
                if im.replaced:
                    out[off:off + len(im.data)] = im.data
                    hdr = bytearray(im.header)
                    struct.pack_into(">I", hdr, 8, off)
                    out[th:th + 0x24] = hdr
            return bytes(out)
        # laid out again: headers, then each image's palette and data, 32-aligned
        n = len(self.images)
        npal = sum(1 for im in self.images if im.palette_header is not None)
        out = bytearray(0xC + 8 * n + 0x24 * n + 0xC * npal)
        struct.pack_into(">III", out, 0, MAGIC, n, 0xC)
        pal_at = 0xC + 8 * n + 0x24 * n
        for i, im in enumerate(self.images):
            hoff = 0xC + 8 * n + 0x24 * i
            poff = 0
            if im.palette_header is not None:
                poff = pal_at
                pal_at += 0xC
                out.extend(b"\0" * (-len(out) % 32))
                ph = bytearray(im.palette_header)
                struct.pack_into(">I", ph, 8, len(out))
                out[poff:poff + 0xC] = ph
                out.extend(im.palette_data)
            struct.pack_into(">II", out, 0xC + 8 * i, hoff, poff)
            out.extend(b"\0" * (-len(out) % 32))
            hdr = bytearray(im.header)
            struct.pack_into(">I", hdr, 8, len(out))
            out[hoff:hoff + 0x24] = hdr
            out.extend(im.data)
        out.extend(b"\0" * (-len(out) % 32))
        return bytes(out)


def selftest():
    """encode/decode of every writable format, PNG both ways, a TPL rebuilt"""
    import tempfile
    rows = [[((x * 37) & 255, (y * 53) & 255, ((x + y) * 11) & 255, 255 if (x + y) % 5 else 0)
             for x in range(16)] for y in range(8)]
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "t.png")
        write_png(p, rows)
        assert read_png(p) == rows, "PNG round trip"
    for fmt in (0, 1, 2, 3, 4, 5, 6, 14):
        enc = encode(rows, fmt)
        assert len(enc) == data_size(16, 8, fmt), fmt
        dec = tpl2png.decode(enc, 16, 8, fmt, 0)
        again = encode([dec[y * 16:(y + 1) * 16] for y in range(8)], fmt)
        assert again == enc or fmt == 14, "format %d: decoding and encoding again changes it" % fmt
        if fmt == 6:
            assert [dec[y * 16:(y + 1) * 16] for y in range(8)] == rows
    # a two-image TPL: unchanged, replaced in place, replaced with a new size
    im_a, im_b = encode(rows, 14), encode(rows, 5)
    n = 2
    data = bytearray(0xC + 8 * n + 0x24 * n)
    struct.pack_into(">III", data, 0, MAGIC, n, 0xC)
    for i, (blob, fmt) in enumerate(((im_a, 14), (im_b, 5))):
        hoff = 0xC + 8 * n + 0x24 * i
        struct.pack_into(">II", data, 0xC + 8 * i, hoff, 0)
        data.extend(b"\0" * (-len(data) % 32))
        struct.pack_into(">HHII", data, hoff, 8, 16, fmt, len(data))
        data.extend(blob)
    data = bytes(data)
    t = TplFile.parse(data)
    assert t.build() == data
    t.images[1].replace(t.images[1].pixels())
    assert t.build() == data, "the same pixels again: the same bytes"
    big = [[(255, 0, 0, 255)] * 32 for _ in range(32)]
    t.images[0].replace(big)
    t2 = TplFile.parse(t.build())
    assert (t2.images[0].width, t2.images[0].height) == (32, 32)
    assert t2.images[0].pixels()[5][5] == (255, 0, 0, 255)
    assert t2.images[1].data == im_b
    return True


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        selftest()
        print("ttyd_tpl: ok")
    else:
        print(__doc__)
