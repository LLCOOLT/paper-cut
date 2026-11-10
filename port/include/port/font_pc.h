#ifndef PORT_FONT_PC_H
#define PORT_FONT_PC_H
/* Binary font (.bfn) on PC: JUTFontSetup reads the file into a fresh buffer and
 * calls portFontSwap once, which byte-swaps the header and every block header
 * (INF1/GLY1/MAP1/WID1, port/bedecode/font.spec) plus the MAP1 tables in place.
 * WID1 entries are bytes; the GLY1 glyph sheet (GX texels) stays big-endian.
 * Returns nonzero on success; on a malformed chain the rest is left as is. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int portFontSwap(void* file, uint32_t size);
/* JUTFont_CodeToGlyph found no glyph for code (no MAP1 block maps it, or it maps past the sheet): logs
 * "font: no glyph for 0x...." once per code, so a crawl over the text finds every letter the font lacks. */
void portFontNoGlyph(uint32_t code, uint16_t glyph);
/* fontmgr's text functions note the text they draw or measure (returned as is), for that log line */
const char* portFontNoteText(const char* text);

#ifdef __cplusplus
}
#endif
#endif
