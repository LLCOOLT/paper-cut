# Anim models ('a/<name>' files, filemgr type 5; animdrv.c).
# Layout: the decomp's include/driver/animdrv.h.
# Decoded into a per-file arena by port/src/common/anim_pc.c:
#   - CPU-read tables -> native structs (shapes, draws, groups, textures,
#     texture-matrix base values).
#   - Arrays of anonymous Vec / AnimPoseNode floats (bufferVtxPos,
#     nrmVtxBuffer, nodeData) and AnimData.loopInfo are 32-bit word arrays:
#     'raw' here, swapped by anim_pc.c.
#   - GX arrays stay big-endian in the blob (colors, texcoords; registered with
#     portGXRegisterDiscBlob). Index tables are read by the CPU in immediate
#     mode but stay big-endian: animdrv reads them with portBE32.
#   - Each AnimData (pointers relative to the AnimData, not the file) is
#     decoded by anim_pc.c ('raw' in AnimTableEntry).
format anim
header driver/animdrv.h

struct AnimationPoseData
  bboxMin as f32[3]
  bboxMax as f32[3]
  shapes -> AnimPoseShape[shapeCount]
  drawCalls -> AnimDrawCallEntry[drawCallCount]
  bufferVtxPos raw
  posIndex payload if mBufferPosIdxNum
  nrmVtxBuffer raw
  nrmIndex payload if mBufferNrmIdxNum
  clr0VtxBuffer payload if clr0Num
  clr0Index payload if clr0IdxNum
  texIndex payload if texIdxNum   # slot i gated by texIdxNum[i]; unused slots hold stale words
  texVtxBuffer payload if texVtxNum
  texAnimEntries -> AnimTexMtx[mBufferTexAnimNum]
  texData -> AnimPoseTexEntry[texDataCount]
  texBinds -> AnimTexBind[texBindCount]
  draws -> AnimPoseShapeDraw[drawCount]
  groupVisibility payload if mBufferVisibilityNum
  nodeData raw
  groups -> AnimPoseGroup[groupCount]
  mpAnims -> AnimTableEntry[mAnimCount]

struct AnimPoseShapeVertex

struct AnimPoseShape

struct AnimDrawCallEntry

struct AnimTexMtx

struct AnimPoseTexEntry

struct AnimTexBind

struct AnimPoseShapeDraw

struct AnimPoseGroup

struct AnimTableEntry
  data raw                       # offset from the file start; anim_pc.c decodes it

# Decoded with the AnimData as the blob base (its pointers are relative to it).
struct AnimData
  loopInfo raw                   # f32[loopInfoCount] words, [0] read as s32: anim_pc.c
  keyframes -> AnimKeyframe[keyframeCount]
  vtxPosDeltas payload if vtxPosDeltaCount   # bytes only; stale word when empty
  vtxNrmDeltas payload if vtxNrmDeltaCount
  texDeltas -> AnimTexDelta[texDeltaCount]
  visDeltas payload if visDeltaCount
  nodeDeltas payload if nodeDeltaCount
  valueKeys -> AnimValueKey[valueKeyCount]
  bboxMin as f32[3]
  bboxMax as f32[3]

struct AnimKeyframe

struct AnimTexDelta

struct AnimValueKey
