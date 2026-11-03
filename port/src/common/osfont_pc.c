/* The console's ROM font encoding: Shift-JIS on Japanese hardware, ANSI
 * elsewhere. Each PC build reports its own region's, as the game expects. */
#include <dolphin/os.h>

u16 OSGetFontEncode(void) {
#ifdef VERSION_G8ME01
    return OS_FONT_ENCODE_ANSI;
#else
    return OS_FONT_ENCODE_SJIS;
#endif
}
