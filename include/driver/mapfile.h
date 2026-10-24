#pragma once
/* Typed views of map 'd' file lists for the PC port's decoder.
 *
 * The map file structs themselves are the decomp's (driver/mapdrv.h). The
 * "animation_table" lists there share one generic MapAnimList (count +
 * pointers); the decoder needs to know what each list's items are, so these
 * views name them per kind. Same layout as MapAnimList on every target.
 * Only the PC build and tools/port/gen_bedecode.py include this header. */
#include "driver/mapdrv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "animation_table" chunk */
typedef struct MapAnimTable {
    s32 count;
    MapAnimHeader* anims[];
} MapAnimTable;

/* MapAnimHeader.jointAnims */
typedef struct MapAnimJointList {
    s32 count;
    MapAnimJoint* items[];
} MapAnimJointList;

/* MapAnimHeader.materialAnims */
typedef struct MapAnimMatList {
    s32 count;
    MapAnimMatData* items[];
} MapAnimMatList;

/* MapAnimHeader.alphaAnims */
typedef struct MapAnimAlphaList {
    s32 count;
    MapAnimAlphaData* items[];
} MapAnimAlphaList;

/* MapAnimHeader.lightAnims */
typedef struct MapAnimLightList {
    s32 count;
    MapAnimLightData* items[];
} MapAnimLightList;

/* MapAnimHeader.lightColorAnims */
typedef struct MapAnimLightParamList {
    s32 count;
    MapAnimLightData2* items[];
} MapAnimLightParamList;

#ifdef __cplusplus
}
#endif
