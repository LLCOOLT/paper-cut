# Binary font (<root>/f/<name>.bfn, src/manager/fontmgr.c JUTFontSetup).
# Pointer-free: header then a chain of {magic, size} blocks (INF1, GLY1, MAP1,
# WID1). port/src/common/font_pc.c swaps the block headers in place with
# portSwapInPlace (native layout == disc layout, asserted); MAP1 tables are
# u16 runs, WID1 entries are byte pairs, the GLY1 sheet (GX I4 texels) stays
# big-endian for Aurora.
format font
header manager/fontmgr.h

struct BinaryFontHeader

struct BinaryFontBlock

struct BinaryFontInfo

struct BinaryFontGlyph

struct BinaryFontMap
  table skip                 # u16[] up to the block size: swapped as a run

struct BinaryFontWidth
