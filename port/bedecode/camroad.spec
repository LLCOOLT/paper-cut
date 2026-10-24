# Camera road file ("1.01"; camRoadSetup in src/cam_road.c). Only the header
# is a struct graph: its offsets lead to flat tables whose element layouts are
# partly unknown (pads in CamRoadCurve/CamRoadGroup). The header is decoded to
# a native CamRoadMap with these descriptors; port/src/common/camroad_pc.c then
# swaps the tables in place (32-bit words: floats, s32 indices/offsets),
# skipping empty ones (their offset words are stale),
# keeping curve names (bytes 0..0x20 of a 0xb8-byte curve) and group pads
# (bytes 0..0x40 of a 0x68-byte group) as they are.
format camroad
header cam_road.h

struct CamRoadMap
  f_e8 payload     # Vec header data (f_e8->x: fov)
  curves payload if curveNum   # CamRoadCurve[curveNum], 0xb8 each
  f_f0 payload if f_f0Num     # curve collision groups, 0x68 each
  f_f4 payload if f_f4Num     # strips {s32 idxOfs, num}
  f_f8 payload if f_f8Num     # Vec pairs (points, normals)
  f_fc payload if f_fcNum     # Vec vertices
  f_100 payload if f_100Num    # s32 indices
  f_104 payload if f_104Num    # curve limit groups, 0x68 each
  f_108 payload if f_108Num    # unused by the game code: left big-endian
