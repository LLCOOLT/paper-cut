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
/* The same for the driver's texture file slot slot, whatever its name: 0
 * when the slot is empty, -1 past the last slot (the partners' textures are
 * looked for in every file loaded). */
int portAnimTexturesAt(int slot, PortTexInfo* out, int max);
/* The status bar's and menus' icons' textures (part: the banners 0 HP, 1 FP,
 * 2 Star Power; the faces 3 Mario's, 4 Goombella's, 5 Koops's, 6 Bobbery's,
 * 7 Flurrie's, 8 Vivian's, 9 Ms. Mowz's; the menus' partner pictures 10
 * Goombella's, 11 Koops's, 12 Bobbery's, 13 Flurrie's, 14 Vivian's, 15 Ms.
 * Mowz's), each frame's, once icon.tpl and icon.bin are in, else 0
 * (src/driver/icondrv.c). */
int portHudIconTextures(int part, PortTexInfo* out, int max);
/* The Play as pick chr's faces (Luigi's, Peach's, Bowser's: the status bar's
 * face, the menus' head and emblem; port/src/aurora/luigi_faces_aurora.c),
 * made once asked for: up to max, 0 for Mario or none made. */
int portPlayAsFaceTextures(int chr, PortTexInfo* out, int max);

/* The Start menu's Wardrobe (port/src/aurora/wardrobe_aurora.c): the slots
 * (0 Mario's cap and shirt, 1 his overalls, 2 Goombella's hair, 3-5 the HP,
 * FP and Star Power banners, 6 and 7 Koops's shell and hoodie, 8 Flurrie's
 * hair, 9 Bobbery's body, 10 Ms. Mowz's mask, 11 Vivian's body, 12
 * Goombella's helmet, 13 Flurrie's dress, 14 Bobbery's fuse, 15 Vivian's
 * hat, 16 her hair, 17 Ms. Mowz's cape, 18 the menus (the Start menu's pages);
 * Yoshi's colour is the game's own, pouchGetPartyColor), their labels and notes (English, for
 * portTrGame), the colour chosen (0 the game's own, then the named ones, the
 * last the slot's custom one) and stepped (dir -1 or 1, wrapping; saved),
 * a colour's name, and the slot's colour now as 0xRRGGBB (the game's own
 * an approximation). The looks: 0 the game's own, then Luigi, Wario,
 * Waluigi and Fire Mario (his cap and overalls), the one he has now (-1:
 * none of them) and putting one on (saved). */
enum { PORT_COSMETIC_CAP, PORT_COSMETIC_OVERALLS, PORT_COSMETIC_PARTNER, PORT_COSMETIC_HUD_HP, PORT_COSMETIC_HUD_FP,
       PORT_COSMETIC_HUD_SP, PORT_COSMETIC_KOOPS_SHELL, PORT_COSMETIC_KOOPS_HOODIE, PORT_COSMETIC_FLURRIE_HAIR,
       PORT_COSMETIC_BOBBERY_BODY, PORT_COSMETIC_MOWZ_MASK, PORT_COSMETIC_VIVIAN, PORT_COSMETIC_GOOMBELLA_HELMET,
       PORT_COSMETIC_FLURRIE_DRESS, PORT_COSMETIC_BOBBERY_FUSE, PORT_COSMETIC_VIVIAN_HAT, PORT_COSMETIC_VIVIAN_HAIR,
       PORT_COSMETIC_MOWZ_CAPE, PORT_COSMETIC_HUD_MENU,
       /* who Play as picks (docs/PLAY_AS.md), each their own, saved apart from
        * Mario's: Luigi's cap and shirt and overalls; Peach's dress, crown,
        * hair, gloves, earrings and shoes; Bowser's shell, hair, skin, horns
        * and spikes, and bracelets */
       PORT_COSMETIC_LUIGI_CAP, PORT_COSMETIC_LUIGI_OVERALLS, PORT_COSMETIC_PEACH_DRESS, PORT_COSMETIC_PEACH_CROWN,
       PORT_COSMETIC_PEACH_HAIR, PORT_COSMETIC_PEACH_GLOVES, PORT_COSMETIC_PEACH_EARRINGS, PORT_COSMETIC_PEACH_SHOES,
       PORT_COSMETIC_BOWSER_SHELL, PORT_COSMETIC_BOWSER_HAIR, PORT_COSMETIC_BOWSER_SKIN, PORT_COSMETIC_BOWSER_SPIKES,
       PORT_COSMETIC_BOWSER_BRACELETS };
int portCosmeticsSlotCount(void);
/* whose a slot is: the Play as pick (0 Mario .. 3 Bowser) it dresses, or -1
 * (a partner's, the status bar's, the menus') */
int portCosmeticsSlotPick(int slot);
const char* portCosmeticsSlotLabel(int slot);
const char* portCosmeticsSlotNote(int slot);
int portCosmeticsChoice(int slot);
void portCosmeticsStep(int slot, int dir);
const char* portCosmeticsColourName(int colour);
unsigned portCosmeticsSlotRgb(int slot);
/* the Start menu's colour picker (port/src/aurora/pause_picker_aurora.c): the slot's own colour set
 * (its _rgb key) and the slot set to it (Custom), saved */
void portCosmeticsSetRgb(int slot, unsigned rgb);
/* (the looks are the Play as pick's: Mario's, or Luigi's as his brother,
 * Wario, Waluigi and Fire Luigi; none for Peach and Bowser) */
int portCosmeticsLookCount(void);
const char* portCosmeticsLookName(int look);
int portCosmeticsLook(void);
void portCosmeticsSetLook(int look);
/* The status bar's and menus' themes (0 the game's own, then Night, Forest,
 * Ocean, Candy and High contrast): how many, a name (English, for
 * portTrGame), the one on, stepping it (saved), and whether a slot shows its
 * theme's colour (its own is the game's). A slot of the status bar or the
 * menus with no colour of its own takes the theme's. */
int portCosmeticsThemeCount(void);
const char* portCosmeticsThemeName(int theme);
int portCosmeticsTheme(void);
void portCosmeticsStepTheme(int dir);
int portCosmeticsSlotThemed(int slot);
/* The Start menu pages' three shades (deep, middle, light; 0xRRGGBB) in the
 * menus' colour, or 0: the game's own (src/window/win_root.c's winBgGX). */
int portCosmeticsMenuShades(unsigned* rgb);

#ifdef __cplusplus
}
#endif

#endif
