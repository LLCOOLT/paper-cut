#ifndef PORT_COSMETICS_H
#define PORT_COSMETICS_H

/* Cosmetics (port/settings/features/cosmetics.cpp): Mario's, the partners' and
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

/* The Start menu's Wardrobe (port/src/aurora/wardrobe_aurora.c): the slots
 * (0 Mario's cap and shirt, 1 his overalls, 2 Goombella's hair, 3-5 the HP,
 * FP and Star Power banners, 6 and 7 Koops's shell and hoodie, 8 Flurrie's
 * hair, 9 Bobbery's body, 10 Ms. Mowz's mask and cape, 11 Vivian's body;
 * Yoshi's colour is the game's own, pouchGetPartyColor), their labels and notes (English, for
 * portTrGame), the colour chosen (0 the game's own, then the named ones, the
 * last the slot's custom one) and stepped (dir -1 or 1, wrapping; saved),
 * a colour's name, and the slot's colour now as 0xRRGGBB (the game's own
 * an approximation). The looks: 0 the game's own, then Luigi, Wario,
 * Waluigi and Fire Mario (his cap and overalls), the one he has now (-1:
 * none of them) and putting one on (saved). */
enum { PORT_COSMETIC_CAP, PORT_COSMETIC_OVERALLS, PORT_COSMETIC_PARTNER, PORT_COSMETIC_HUD_HP, PORT_COSMETIC_HUD_FP,
       PORT_COSMETIC_HUD_SP, PORT_COSMETIC_KOOPS_SHELL, PORT_COSMETIC_KOOPS_HOODIE, PORT_COSMETIC_FLURRIE_HAIR,
       PORT_COSMETIC_BOBBERY_BODY, PORT_COSMETIC_MOWZ_MASK, PORT_COSMETIC_VIVIAN };
int portCosmeticsSlotCount(void);
const char* portCosmeticsSlotLabel(int slot);
const char* portCosmeticsSlotNote(int slot);
int portCosmeticsChoice(int slot);
void portCosmeticsStep(int slot, int dir);
const char* portCosmeticsColourName(int colour);
unsigned portCosmeticsSlotRgb(int slot);
/* the Start menu's colour picker (port/src/aurora/pause_picker_aurora.c): the slot's own colour set
 * (its _rgb key) and the slot set to it (Custom), saved */
void portCosmeticsSetRgb(int slot, unsigned rgb);
int portCosmeticsLookCount(void);
const char* portCosmeticsLookName(int look);
int portCosmeticsLook(void);
void portCosmeticsSetLook(int look);

#ifdef __cplusplus
}
#endif

#endif
