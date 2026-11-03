/* The game's ROM-font lines for the frame (port/include/port/romfont_pc.h). */
#include <string.h>
#include "port/romfont_pc.h"

#define ROM_LINES 32

static PortRomText sLines[ROM_LINES];
static int sCount;
float (*gPortRomFontMeasure)(const char* utf8);

/* Latin-1 to UTF-8 (the game's ANSI text; the Japanese builds show the English
 * table on PC, romfont.c) */
static void toUtf8(const char* in, char* out, size_t size) {
    size_t n = 0;
    const unsigned char* p = (const unsigned char*)in;
    for (; *p != '\0' && n + 3 < size; p++) {
        if (*p < 0x80) {
            out[n++] = (char)*p;
        } else {
            out[n++] = (char)(0xC0 | (*p >> 6));
            out[n++] = (char)(0x80 | (*p & 0x3F));
        }
    }
    out[n] = '\0';
}

void portRomFontText(float x, float y, float scale, const unsigned char* rgba, const char* text) {
    PortRomText* t;
    if (sCount == ROM_LINES) {
        return;
    }
    t = &sLines[sCount++];
    t->x = x;
    t->y = y;
    t->scale = scale;
    memcpy(t->rgba, rgba, 4);
    toUtf8(text, t->text, sizeof(t->text));
}

int portRomFontWidth(const char* text) {
    char utf8[256];
    toUtf8(text, utf8, sizeof(utf8));
    if (gPortRomFontMeasure != NULL) {
        return (int)(gPortRomFontMeasure(utf8) + 0.5f);
    }
    return (int)strlen(text) * 12; /* the IPL font's ANSI cells are about this wide */
}

int portRomFontCount(void) {
    return sCount;
}

const PortRomText* portRomFontAt(int i) {
    return i >= 0 && i < sCount ? &sLines[i] : NULL;
}

void portRomFontClear(void) {
    sCount = 0;
}
