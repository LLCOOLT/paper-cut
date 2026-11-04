#ifndef PORT_COSMETICS_H
#define PORT_COSMETICS_H

/* Cosmetics (port/settings/features/cosmetics.cpp): Mario's, a partner's and
 * the status bar's colours, recoloured as their textures are drawn (Aurora's
 * texture replacement, by the texels' address). Only read here: the game's
 * memory is never written, so it plays out the same. */

#ifdef __cplusplus
extern "C" {
#endif

/* One texture as the game has it in memory: its texels (GX layout,
 * big-endian), size and GX format, and a colour-indexed one's palette. */
typedef struct PortTexInfo {
    const void* data;
    int width, height;
    int format;
    const void* tlut; /* NULL: not colour-indexed */
    int tlutFormat;
    int tlutCount;
} PortTexInfo;

/* The animation driver's loaded texture file named file ("a/a_mario-"): up
 * to max of its textures; 0 when it isn't loaded (src/driver/animdrv.c). */
int portAnimTextures(const char* file, PortTexInfo* out, int max);
/* The status bar's icons' textures (part: the banners 0 HP, 1 FP, 2 Star
 * Power; the faces 3 Mario's, 4 Goombella's), each frame's, once icon.tpl and
 * icon.bin are in, else 0 (src/driver/icondrv.c). */
int portHudIconTextures(int part, PortTexInfo* out, int max);

#ifdef __cplusplus
}
#endif

#endif
