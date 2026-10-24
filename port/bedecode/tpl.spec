# TPL texture palette (.tpl files; filemgr type 4 and UnpackTexPalette).
# Header graph only: texel and CLUT data stay big-endian in the blob, Aurora
# decodes GameCube texture/TLUT formats from big-endian data.
format tpl
header sdk/texPalette.h

struct TEXPalette
  descriptorArray -> TEXDescriptor[numDescriptors]

struct TEXDescriptor
  textureHeader -> TEXHeader     # 0 = no texture
  CLUTHeader -> CLUTHeader       # 0 = no palette

struct TEXHeader
  data payload
  unpacked = 1                   # data is a pointer now (GC sets this in UnpackTexPalette)

struct CLUTHeader
  data payload
  unpacked = 1
