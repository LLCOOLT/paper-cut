// eff_indirect.c: screen-distortion effect: effIndirectEntry, effIndirectMain and effIndirectDisp.
#pragma once

#include "driver/camdrv.h"
#include "driver/effdrv.h"
#include <dolphin/types.h>

EffectEntry* effIndirectEntry(s32 type, s32 duration, f32 x, f32 y, f32 z);
#ifndef TARGET_PC /* PC port: static in its source file (clang rejects a static definition after an extern declaration) */
void effIndirectMain(EffectEntry* effect);
void effIndirectDisp(CameraId cameraId, EffectEntry* effect);
#endif
