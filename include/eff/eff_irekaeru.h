// eff_irekaeru.c: swap effect: effIrekaeruEntry, effIrekaeruMain and effIrekaeruDisp.
#pragma once

#include "driver/camdrv.h"
#include "driver/effdrv.h"
#include <dolphin/types.h>

EffectEntry* effIrekaeruEntry(s32 type, f32 x, f32 y, f32 z);
#ifndef TARGET_PC /* PC port: static in its source file (clang rejects a static definition after an extern declaration) */
void effIrekaeruMain(EffectEntry* effect);
void effIrekaeruDisp(CameraId cameraId, EffectEntry* effect);
#endif
