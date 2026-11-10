# The damaged and empty-entry mod zips of tests/port/testpass/mods_damaged_zip.json:
#   python make_zips.py tests/port/testpass/mods/damaged/mods
import os, struct, sys, zipfile, io

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

# 1. only an end record, its directory claiming nearly 4 GiB (a truncated download's stray PK 5 6)
eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 1, 1, 0xFFFFFFF0, 0, 0)
open(os.path.join(out, "damaged-eocd.ttydmod"), "wb").write(b"junk before" + eocd)

# 2. a real zip whose directory says its mod.json unpacks to ~4 GiB
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("mod.json", '{"id": "test.damaged.size", "name": "Damaged sizes"}')
data = bytearray(buf.getvalue())
cd = data.rfind(b"PK\x01\x02")
struct.pack_into("<I", data, cd + 24, 0xFFFFFF00)  # the central header's uncompressed size
open(os.path.join(out, "damaged-size.ttydmod"), "wb").write(bytes(data))

# 3. a good zip, cut short in the middle (its end record gone)
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("mod.json", '{"id": "test.damaged.cut", "name": "Cut short"}')
    z.writestr("overlay/readme.txt", "x" * 4000)
whole = buf.getvalue()
open(os.path.join(out, "damaged-cut.ttydmod"), "wb").write(whole[: len(whole) // 2])

# 4. a good mod whose Lua module is an empty deflated file (Python writes empty files as method 8)
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("mod.json", '{"id": "test.zip.empty", "name": "Empty file in a zip"}')
    z.writestr("lua/main.lua", 'local e = require("empty")\nprint("zip-empty: the empty module loaded: " .. tostring(e))\n')
    z.writestr(zipfile.ZipInfo("lua/empty.lua"), b"", compress_type=zipfile.ZIP_DEFLATED)
data = buf.getvalue()
open(os.path.join(out, "empty-entry.ttydmod"), "wb").write(data)
with zipfile.ZipFile(io.BytesIO(data)) as z:
    print([(i.filename, i.compress_type, i.file_size) for i in z.infolist()])
