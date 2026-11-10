/* Binary font header swap; see port/include/port/font_pc.h. */
#include "port/font_pc.h"
#include "port/bedecode.h"
#include "manager/fontmgr.h"

extern const PortType portBE_font_BinaryFontHeader;
extern const PortType portBE_font_BinaryFontBlock;
extern const PortType portBE_font_BinaryFontInfo;
extern const PortType portBE_font_BinaryFontGlyph;
extern const PortType portBE_font_BinaryFontMap;
extern const PortType portBE_font_BinaryFontWidth;

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

int portFontSwap(void* file, uint32_t size) {
    uint8_t* base = (uint8_t*)file;
    uint32_t count, i, off;

    if (!file || size < sizeof(BinaryFontHeader)) {
        return 0;
    }
    count = portBE32(base + 0xC);
    if (!portSwapInPlace(&portBE_font_BinaryFontHeader, base, 1)) {
        return 0;
    }
    off = sizeof(BinaryFontHeader);
    for (i = 0; i < count; i++) {
        uint8_t* b = base + off;
        uint32_t magic, bsize;
        const PortType* t;
        if (off > size || size - off < sizeof(BinaryFontBlock)) {
            return 0;
        }
        magic = portBE32(b);
        bsize = portBE32(b + 4);
        if (bsize < sizeof(BinaryFontBlock) || bsize > size - off) {
            return 0;
        }
        switch (magic) {
        case FOURCC('I', 'N', 'F', '1'): t = &portBE_font_BinaryFontInfo; break;
        case FOURCC('G', 'L', 'Y', '1'): t = &portBE_font_BinaryFontGlyph; break;
        case FOURCC('M', 'A', 'P', '1'): t = &portBE_font_BinaryFontMap; break;
        case FOURCC('W', 'I', 'D', '1'): t = &portBE_font_BinaryFontWidth; break;
        default: t = &portBE_font_BinaryFontBlock; break;
        }
        if (bsize < t->gcSize) {
            t = &portBE_font_BinaryFontBlock;
        }
        if (!portSwapInPlace(t, b, 1)) {
            return 0;
        }
        if (t == &portBE_font_BinaryFontMap) {
            /* table[]: u16 entries to the end of the block (TABLE: one per
             * code, MAP: code/glyph pairs, SJIS: table[0] = base glyph). */
            portSwap16Run(b + sizeof(BinaryFontMap), (bsize - sizeof(BinaryFontMap)) / 2);
        }
        off += bsize;
    }
    return 1;
}

/* A character the font has no glyph for: logged once per code (up to 64 codes), so a crawl over the text finds
 * every letter drawn as the font's replacement. */
#include <dolphin/os.h>
#include <stdio.h>

static char sText[64]; /* the text fontmgr is drawing or measuring (its first bytes), for the log line */

const char* portFontNoteText(const char* text) {
    size_t i = 0;
    if (text != NULL) {
        for (; i + 1 < sizeof(sText) && text[i] != '\0'; i++) {
            sText[i] = text[i];
        }
    }
    sText[i] = '\0';
    return text;
}

void portFontNoGlyph(uint32_t code, uint16_t glyph) {
    static uint16_t seen[64];
    static int count;
    char shown[sizeof(sText) * 4 + 1];
    size_t o = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (seen[i] == (uint16_t)code) {
            return;
        }
    }
    if (count >= (int)(sizeof(seen) / sizeof(seen[0]))) {
        return;
    }
    seen[count++] = (uint16_t)code;
    /* the text, its bytes past ASCII as \xNN (the game's text is Shift-JIS or Latin-1, the log UTF-8) */
    for (i = 0; sText[i] != '\0' && o + 5 < sizeof(shown); i++) {
        const unsigned char c = (unsigned char)sText[i];
        if (c >= 0x20 && c < 0x7F && c != '"') {
            shown[o++] = (char)c;
        } else {
            o += (size_t)snprintf(shown + o, sizeof(shown) - o, "\\x%02X", c);
        }
    }
    shown[o] = '\0';
    OSReport("font: no glyph for 0x%04X, drawn as glyph %u (the font's replacement), in \"%s\"\n",
             (unsigned)(uint16_t)code, (unsigned)glyph, shown);
}
