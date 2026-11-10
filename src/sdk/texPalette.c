// TPL texture palette helpers (SDK charPipeline): unpack a .tpl and build GXTexObjs from it.
#include <charPipeline/texPalette.h>
#include <dolphin/gx/GXTexture.h>
#include <dolphin/os.h>
#include <macros.h>

#ifdef TARGET_PC
#include "port/bedecode.h"
#include "port/tpl_pc.h"
#endif
#define PALETTE_VERSION 0x0020AF30

/**
 * Turns a TPL loaded in memory into usable form:
 * checks the version and converts the descriptor, texture and CLUT offsets to pointers (only once).
 * Used by the effect and background drivers after loading a .tpl.
 */
void UnpackTexPalette(TEXPalettePtr pal) {
#ifdef TARGET_PC
    /* PC port: disc TPLs are big-endian with 32-bit offsets; the native root
     * replaces the header in place. No size is passed, so bounds are loose. */
    portTplDecode(pal, PORT_DECODE_SIZE_UNKNOWN, __FILE__);
#else
    u16 i;

    if (pal->versionNumber != PALETTE_VERSION) {
        OSPanic(__FILE__, 0x24, "invalid version number for texture palette\0");
    }

    if ((u32)pal->descriptorArray <= 0x80000000) {
        pal->descriptorArray = (TEXDescriptorPtr)((Ptr)pal->descriptorArray + (u32)pal);
        for (i = 0; i < pal->numDescriptors; i++) {
            if (pal->descriptorArray[i].textureHeader) {
                pal->descriptorArray[i].textureHeader =
                    (TEXHeaderPtr)((Ptr)pal + (u32)pal->descriptorArray[i].textureHeader);
                if (!pal->descriptorArray[i].textureHeader->unpacked) {
                    pal->descriptorArray[i].textureHeader->data =
                        (Ptr)pal + (u32)pal->descriptorArray[i].textureHeader->data;
                    pal->descriptorArray[i].textureHeader->unpacked = TRUE;
                }
            }
            if (pal->descriptorArray[i].CLUTHeader) {
                pal->descriptorArray[i].CLUTHeader =
                    (CLUTHeaderPtr)((u8*)pal + (u32)pal->descriptorArray[i].CLUTHeader);
                if (!pal->descriptorArray[i].CLUTHeader->unpacked) {
                    pal->descriptorArray[i].CLUTHeader->data = (Ptr)pal + (u32)pal->descriptorArray[i].CLUTHeader->data;
                    pal->descriptorArray[i].CLUTHeader->unpacked = TRUE;
                }
            }
        }
    }
#endif
}

#ifdef TARGET_PC
#include <stdio.h>
#include "port/platform.h"

/* PC port: a lookup in a TPL that isn't loaded (NULL, never decoded, or freed: the registry in
 * tpl_pc.c) or of a texture number past its end gets this blank instead of reading freed or
 * unmapped memory: a 4x4 transparent RGBA8 texture, so the draw shows nothing and the game goes on.
 * Each call site says so once ("[tex] draw skipped"); the partner move sweep fails on that line. */
static u8 sBlankTexels[64] ATTRIBUTE_ALIGN(32);
static TEXHeader sBlankHeader = {4, 4, GX_TF_RGBA8, (Ptr)sBlankTexels, GX_CLAMP, GX_CLAMP, GX_NEAR, GX_NEAR, 0.0f, 0, 0, 0, 1};
static TEXDescriptor sBlankDesc = {&sBlankHeader, NULL};

static void texSkipped(TEXPalettePtr pal, u32 id, const void* caller, const char* why) {
    static const void* sSaid[64];
    static int sSaidCount;
    char name[160];
    int i;
    for (i = 0; i < sSaidCount; i++) {
        if (sSaid[i] == caller) {
            return;
        }
    }
    if (sSaidCount < 64) {
        sSaid[sSaidCount++] = caller;
    }
    printf("[tex] draw skipped: TPL not loaded at %s (palette %p, texture %u: %s)\n",
           portSymbolName(caller, name, sizeof(name)), (void*)pal, (unsigned)id, why);
    fflush(stdout);
}

/* the descriptor of texture id, or the blank when pal isn't a live TPL or id is past its end
 * (needHeader: also when the texture has no header) */
static TEXDescriptorPtr texLookup(TEXPalettePtr pal, u32 id, int needHeader, const void* caller) {
    if (!portTplIsLive(pal)) {
        texSkipped(pal, id, caller, pal == NULL ? "none" : "not loaded or freed");
        return &sBlankDesc;
    }
    if (id >= pal->numDescriptors) {
        texSkipped(pal, id, caller, "past the end");
        return &sBlankDesc;
    }
    if (needHeader && pal->descriptorArray[id].textureHeader == NULL) {
        texSkipped(pal, id, caller, "no texture");
        return &sBlankDesc;
    }
    return &pal->descriptorArray[id];
}
#endif

/** Returns the descriptor for texture id (the range check is a debug-only assert). */
TEXDescriptorPtr TEXGet(TEXPalettePtr pal, u32 id) {
#ifdef TARGET_PC
    return texLookup(pal, id, 0, __builtin_return_address(0));
#else
    ASSERTMSGLINE(0x90, id < pal->numDescriptors, "GetTexture():  Texture Not Found ");
    return &pal->descriptorArray[id];
#endif
}

/**
 * Fills a GXTexObj from texture id of a TPL: size, format, wrap,
 * filters and LOD range (mipmapped when the LODs differ).
 */
void TEXGetGXTexObjFromPalette(TEXPalettePtr pal, GXTexObj* to, u32 id) {
    TEXDescriptorPtr tdp;
    GXBool mipMap;

#ifdef TARGET_PC
    tdp = texLookup(pal, id, 1, __builtin_return_address(0));
#else
    tdp = TEXGet(pal, id);
#endif
    if (tdp->textureHeader->minLOD == tdp->textureHeader->maxLOD) {
        mipMap = GX_FALSE;
    } else {
        mipMap = GX_TRUE;
    }
    GXInitTexObj(to, tdp->textureHeader->data, tdp->textureHeader->width, tdp->textureHeader->height,
                 tdp->textureHeader->format, tdp->textureHeader->wrapS, tdp->textureHeader->wrapT, mipMap);
    GXInitTexObjLOD(to, tdp->textureHeader->minFilter, tdp->textureHeader->magFilter, tdp->textureHeader->minLOD,
                    tdp->textureHeader->maxLOD, tdp->textureHeader->LODBias, GX_DISABLE,
                    tdp->textureHeader->edgeLODEnable, GX_ANISO_1);
}
