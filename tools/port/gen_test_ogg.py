#!/usr/bin/env python3
"""A silent Ogg Vorbis file with any channel count (1-255), made from nothing: for the test pass
(tests/port/testpass/music_ogg_many_channels.json: a 9-16 channel file in the music folder must play
without writing past the decoder's buffer, port/src/common/music_pc.c oggRead).

  gen_test_ogg.py OUT.ogg [--channels N] [--rate HZ] [--seconds S]

The stream is valid Vorbis I: one two-entry codebook, a floor 1 with no partitions, a residue that is
never read, one mapping and one short-block mode; every audio packet marks each channel's floor unused,
so the decoder plays silence for the whole length. No audio of anyone's is in it.
"""

import argparse
import struct


class Bits:
    """Vorbis packs bits from each byte's least significant end."""

    def __init__(self):
        self.out = bytearray()
        self.bit = 0

    def put(self, value, count):
        for i in range(count):
            if self.bit == 0:
                self.out.append(0)
            if (value >> i) & 1:
                self.out[-1] |= 1 << self.bit
            self.bit = (self.bit + 1) & 7

    def bytes_(self, data):
        for b in data:
            self.put(b, 8)

    def done(self):
        return bytes(self.out)


def ident(channels, rate):
    p = b"\x01vorbis" + struct.pack("<IBIiiiB", 0, channels, rate, 0, 0, 0, (8 << 4) | 8) + b"\x01"
    assert len(p) == 30  # (stb_vorbis wants exactly this, alone on the first page)
    return p


def comment():
    vendor = b"Paper Cut test pass"
    return b"\x03vorbis" + struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 0) + b"\x01"


def setup(channels):
    b = Bits()
    b.bytes_(b"\x05vorbis")
    b.put(0, 8)  # one codebook
    b.put(0x564342, 24)
    b.put(1, 16)  # dimensions
    b.put(2, 24)  # entries
    b.put(0, 1)  # not ordered
    b.put(0, 1)  # not sparse
    b.put(0, 5)  # entry 0: length 1
    b.put(0, 5)  # entry 1: length 1
    b.put(0, 4)  # no lookup
    b.put(0, 6)  # one time domain transform
    b.put(0, 16)
    b.put(0, 6)  # one floor
    b.put(1, 16)  # type 1
    b.put(0, 5)  # no partitions
    b.put(1, 2)  # multiplier 2
    b.put(7, 4)  # range bits: 128, half the short block
    b.put(0, 6)  # one residue
    b.put(0, 16)  # type 0
    b.put(0, 24)  # begin
    b.put(0, 24)  # end
    b.put(0, 24)  # partition size 1
    b.put(0, 6)  # one classification
    b.put(0, 8)  # its class book
    b.put(0, 3)  # cascade low bits
    b.put(0, 1)  # no high bits
    b.put(0, 6)  # one mapping
    b.put(0, 16)  # type 0
    b.put(0, 1)  # one submap
    b.put(0, 1)  # no coupling
    b.put(0, 2)  # reserved
    b.put(0, 8)  # (time, unused)
    b.put(0, 8)  # floor 0
    b.put(0, 8)  # residue 0
    b.put(0, 6)  # one mode
    b.put(0, 1)  # short blocks
    b.put(0, 16)
    b.put(0, 16)
    b.put(0, 8)  # mapping 0
    b.put(1, 1)  # framing
    del channels
    return b.done()


def audio(channels):
    b = Bits()
    b.put(0, 1)  # an audio packet (one mode: no mode bits)
    for _ in range(channels):
        b.put(0, 1)  # this channel's floor unused: silence
    return b.done()


def crc32_ogg(data):
    crc = 0
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def page(packets, granule, seq, flags, serial=0x50434F47):
    lacing = bytearray()
    for p in packets:
        n = len(p)
        while n >= 255:
            lacing.append(255)
            n -= 255
        lacing.append(n)
    assert len(lacing) <= 255
    head = b"OggS" + struct.pack("<BBqIIIB", 0, flags, granule, serial, seq, 0, len(lacing)) + bytes(lacing)
    body = b"".join(packets)
    crc = crc32_ogg(head + body)
    return head[:22] + struct.pack("<I", crc) + head[26:] + body


def make(channels, rate, seconds):
    out = bytearray()
    out += page([ident(channels, rate)], 0, 0, 2)
    out += page([comment()], 0, 1, 0)
    out += page([setup(channels)], 0, 2, 0)
    blocks = max(2, int(rate * seconds) // 128 + 1)  # each short block after the first gives 128 samples
    pkt = audio(channels)
    seq, done = 3, 0
    while done < blocks:
        n = min(200, blocks - done)
        done += n
        out += page([pkt] * n, (done - 1) * 128, seq, 4 if done == blocks else 0)
        seq += 1
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--channels", type=int, default=16)
    ap.add_argument("--rate", type=int, default=32000)
    ap.add_argument("--seconds", type=float, default=10.0)
    a = ap.parse_args()
    data = make(a.channels, a.rate, a.seconds)
    with open(a.out, "wb") as f:
        f.write(data)
    print(f"{a.out}: {a.channels} channels, {len(data)} bytes")


if __name__ == "__main__":
    main()
