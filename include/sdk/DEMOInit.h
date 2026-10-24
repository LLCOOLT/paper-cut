#pragma once

#include <dolphin/gx.h>

void DEMOInit(GXRenderModeObj* mode);
void DEMOBeforeRender(void);
void DEMODoneRender(void);
GXRenderModeObj* DEMOGetRenderModeObj(void);
void* L_DEMOGetCurrentBuffer(void);
#define DEMOGetCurrentBuffer L_DEMOGetCurrentBuffer
void DEMOEnableGPHangWorkaround(u32 timeoutFrames);
#ifndef TARGET_PC /* PC: static in DEMOInit.c (static after extern: MSVC-only extension) */
void DEMOSetGPHangMetric(GXBool enable);
#endif