#ifndef PORT_ROMFONT_PC_H
#define PORT_ROMFONT_PC_H

/* The game's ROM-font text on PC (src/romfont.c): the console's IPL font isn't
 * there, so romFontPrintGX hands its lines here and the port draws them over
 * the picture (Aurora: port/settings/settings_ui.cpp, in Fredoka) at the same
 * place, for the frame. Coordinates are romfont.c's: 1 unit is one line of a
 * 480-line picture, from its centre, y up. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortRomText {
    float x, y, scale;
    unsigned char rgba[4];
    char text[256]; /* UTF-8 */
} PortRomText;

/* A line this frame (text in the game's encoding: Latin-1). */
void portRomFontText(float x, float y, float scale, const unsigned char* rgba, const char* text);
/* Its width in the same units (for the game's centring). */
int portRomFontWidth(const char* text);
/* This frame's lines, then cleared by portRomFontClear (the drawer's). */
int portRomFontCount(void);
const PortRomText* portRomFontAt(int i);
void portRomFontClear(void);
/* The drawer's measure of a line's width, in picture lines (Aurora); NULL: estimated. */
extern float (*gPortRomFontMeasure)(const char* utf8);

#ifdef __cplusplus
}
#endif

#endif
