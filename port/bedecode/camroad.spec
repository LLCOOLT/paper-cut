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
  projection payload     # Vec header data (projection->x: fov)
  curves payload if curveNum   # CamRoadCurve[curveNum], 0xb8 each
  groups payload if groupNum     # floor groups, 0x68 each
  strips payload if stripNum     # strips {s32 idxOfs, num}
  curveVtx payload if curveVtxNum     # Vec pairs (points, normals)
  groupVtx payload if groupVtxNum     # Vec vertices
  stripIdx payload if stripIdxNum    # s32 indices
  collGroups payload if collGroupNum    # curve limit groups, 0x68 each
  extra payload if extraNum    # unused by the game code: left big-endian
