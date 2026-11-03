/* Texture size helpers, from libs/dolsdk2004/src/gx/GXTexture.c. */
#include <dolphin.h>
#include <dolphin/gx.h>

#define ASSERTMSGLINEV(...) ((void)0)

static void __GXGetTexTileShift(GXTexFmt fmt, u32* rowTileS, u32* colTileS) {
    switch (fmt) {
    case GX_TF_I4:
    case 0x8:
    case GX_TF_CMPR:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        *rowTileS = 3;
        *colTileS = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case 0x9:
    case GX_TF_Z8:
    case GX_CTF_RA4:
    case GX_TF_A8:
    case GX_CTF_R8:
    case GX_CTF_G8:
    case GX_CTF_B8:
    case GX_CTF_Z8M:
    case GX_CTF_Z8L:
        *rowTileS = 3;
        *colTileS = 2;
        break;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_RGBA8:
    case 0xA:
    case GX_TF_Z16:
    case GX_TF_Z24X8:
    case GX_CTF_RA8:
    case GX_CTF_RG8:
    case GX_CTF_GB8:
    case GX_CTF_Z16L:
        *rowTileS = 2;
        *colTileS = 2;
        break;
    default:
        *rowTileS = *colTileS = 0;
        ASSERTMSGLINEV(444, 0, "%s: invalid texture format", "GX");
        break;
    }
}

u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap, u8 max_lod) {
    u32 tileShiftX;
    u32 tileShiftY;
    u32 tileBytes;
    u32 bufferSize;
    u32 nx;
    u32 ny;
    u32 level;

    ASSERTMSGLINEV(460, width <= 1024, "%s: width too large", "GXGetTexBufferSize");
    ASSERTMSGLINEV(461, height <= 1024, "%s: height too large", "GXGetTexBufferSize");

    __GXGetTexTileShift(format, &tileShiftX, &tileShiftY);
    if (format == GX_TF_RGBA8 || format == GX_TF_Z24X8) {
        tileBytes = 64;
    } else {
        tileBytes = 32;
    }

    if (mipmap == GX_TRUE) {
        nx = 1 << (31 - __cntlzw(width));
        ASSERTMSGLINEV(479, width == nx, "%s: width must be a power of 2", "GXGetTexBufferSize");
        ny = 1 << (31 - __cntlzw(height));
        ASSERTMSGLINEV(482, height == ny, "%s: height must be a power of 2", "GXGetTexBufferSize");

        bufferSize = 0;
        for (level = 0; level < max_lod; level++) {
            nx = (width + (1 << tileShiftX) - 1) >> tileShiftX;
            ny = (height + (1 << tileShiftY) - 1) >> tileShiftY;
            bufferSize += tileBytes * (nx * ny);
            if (width == 1 && height == 1) {
                break;
            }
            width = (width > 1) ? width >> 1 : 1;
            height = (height > 1) ? height >> 1 : 1;
        }
    } else {
        nx = (width + (1 << tileShiftX) - 1) >> tileShiftX;
        ny = (height + (1 << tileShiftY) - 1) >> tileShiftY;
        bufferSize = nx * ny * tileBytes;
    }

    return bufferSize;
}

/* Texture objects: nothing is drawn headless, but the game reads sizes and
 * formats back (e.g. bgdrv divides by the texture width), so keep them. */
typedef struct PortTexObj {
    void* data;
    u16 width;
    u16 height;
    u32 format;
    u8 wrapS;
    u8 wrapT;
    u8 mipmap;
    u8 ci;
    u32 tlut;
} PortTexObj;
_Static_assert(sizeof(PortTexObj) <= sizeof(GXTexObj), "PortTexObj fits GXTexObj");

void GXInitTexObj(GXTexObj* obj, void* image_ptr, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrap_s,
                  GXTexWrapMode wrap_t, u8 mipmap) {
    PortTexObj* t = (PortTexObj*)obj;
    memset(obj, 0, sizeof(*obj));
    t->data = image_ptr;
    t->width = width;
    t->height = height;
    t->format = (u32)format;
    t->wrapS = (u8)wrap_s;
    t->wrapT = (u8)wrap_t;
    t->mipmap = mipmap;
}

void GXInitTexObjCI(GXTexObj* obj, void* image_ptr, u16 width, u16 height, GXCITexFmt format, GXTexWrapMode wrap_s,
                    GXTexWrapMode wrap_t, u8 mipmap, u32 tlut_name) {
    GXInitTexObj(obj, image_ptr, width, height, (GXTexFmt)format, wrap_s, wrap_t, mipmap);
    ((PortTexObj*)obj)->ci = 1;
    ((PortTexObj*)obj)->tlut = tlut_name;
}

void GXInitTexObjData(GXTexObj* obj, void* image_ptr) {
    ((PortTexObj*)obj)->data = image_ptr;
}

void* GXGetTexObjData(const GXTexObj* to) {
    return ((const PortTexObj*)to)->data;
}

u16 GXGetTexObjWidth(const GXTexObj* to) {
    return ((const PortTexObj*)to)->width;
}

u16 GXGetTexObjHeight(const GXTexObj* to) {
    return ((const PortTexObj*)to)->height;
}

GXTexFmt GXGetTexObjFmt(const GXTexObj* to) {
    return (GXTexFmt)((const PortTexObj*)to)->format;
}
