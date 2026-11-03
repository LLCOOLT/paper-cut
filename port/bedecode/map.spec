# Map files ('m/<map>/d', mapdrv.c _mapLoad / mapLoadReloc_).
# Layout: the decomp's include/driver/mapdrv.h, plus the per-kind animation
# list views in include/driver/mapfile.h.
# port/src/common/map_pc.c relocates the file (pointer words are found by the
# relocation table, made file-relative so 0 stays NULL), decodes each chunk by
# name with these descriptors, decodes meshes (MapFileJointPart.mesh: packed
# vs raw display-list union) by hand, and checks every relocated word was
# covered by a pointer field below.
# GX payloads stay big-endian in the file: vertex arrays (registered with
# portGXRegisterDiscBlob), packed display lists, tev mode bytes.
format map
header driver/mapfile.h

# "information"
struct MapFileInfo
  version string
  joint -> MapFileJoint
  mapName string
  hitName string
  date string                    # export timestamp; unused, but in the relocation table

struct MapFileJoint
  name string
  type string
  parent -> MapFileJoint
  child -> MapFileJoint
  next -> MapFileJoint
  prev -> MapFileJoint
  scale as f32[3]
  rotation as f32[3]
  translation as f32[3]
  bboxMin as f32[3]
  bboxMax as f32[3]
  drawMode -> MapJointDrawMode
  parts[partCount]

struct MapFileJointPart
  material -> MapMaterial
  mesh skip                      # map_pc.c (MapFileMesh)

struct MapJointDrawMode

struct MapMaterial
  name string
  color as u8[4]
  samplers -> MapMatSampler
  xforms as f32[56]              # MapTexXform[8], 7 floats each
  blendColor as u8[4]
  tevMode payload

struct MapMatSampler
  tex -> MapTexInfo

struct MapTexInfo
  name string
  data null                      # runtime (mapBuildTexture)

# "texture_table"
struct MapTexTable
  names[count] string

# "material_name_table"
struct MapMaterialNameTable
  entries[count]

struct MapMaterialName
  name string
  data -> MapMaterial

# "light_table"
struct MapFileLightTable
  lights[count] -> MapFileLight

struct MapFileLight
  name string
  type string
  position as f32[3]
  rotation as f32[3]
  color as u8[4]

# "fog_table"
struct MapFileFog
  color as u8[4]

# "vcd_table" (and MapFileMesh.vcdTable)
struct MapVcdTable
  positions payload
  normals payload
  colors payload
  texCoords payload

# MapFileMesh.displayLists.normalLists[] (unpacked meshes)
struct MapRawDL
  vtx[count]

struct MapVtxRec

# "animation_table"
struct MapAnimTable
  anims[count] -> MapAnimHeader

struct MapAnimHeader
  name string
  jointAnims -> MapAnimJointList
  materialAnims -> MapAnimMatList
  alphaAnims -> MapAnimAlphaList
  lightAnims -> MapAnimLightList
  lightColorAnims -> MapAnimLightParamList

struct MapAnimJointList
  items[count] -> MapAnimJoint

struct MapAnimMatList
  items[count] -> MapAnimMatData

struct MapAnimAlphaList
  items[count] -> MapAnimAlphaData

struct MapAnimLightList
  items[count] -> MapAnimLightData

struct MapAnimLightParamList
  items[count] -> MapAnimLightData2

struct MapAnimJoint
  # the joint's map object name: mapdrv.c reads it through MapAnimNm.name
  # (the decomp declares it as its 4 bytes, u8 nameRef[4])
  nameRef string
  keys[keyCount]

struct MapAnimMatData
  name string
  keys[keyCount]

struct MapAnimAlphaData
  name string
  keys[keyCount]

struct MapAnimLightData
  name string
  keys[keyCount]

struct MapAnimLightData2
  name string
  keys[keyCount]

struct MapAnimKeyCh
struct MapAnimKey
struct MapAnimKey4
struct MapAnimKey5
struct MapAnimKey9
