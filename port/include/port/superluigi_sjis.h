#ifndef PORT_SUPERLUIGI_SJIS_H
#define PORT_SUPERLUIGI_SJIS_H

/* The game's Japanese names Super Luigi's scripts use (tribes, AI types,
 * NPCs), as Shift-JIS: the port's own sources aren't converted as the
 * game's are (tools/port/sjis_src.py). Each byte its own literal. */

#define SJ_GREEN_FUZZY "\x83" "\x4F" "\x83" "\x8A" "\x81" "\x5B" "\x83" "\x93" "\x83" "\x60" "\x83" "\x87" "\x83" "\x8D" "\x83" "\x7B" "\x83" "\x93" /* グリーンチョロボン */
#define SJ_FLOWER_FUZZY "\x83" "\x74" "\x83" "\x89" "\x83" "\x8F" "\x81" "\x5B" "\x83" "\x60" "\x83" "\x87" "\x83" "\x8D" "\x83" "\x7B" "\x83" "\x93" /* フラワーチョロボン */
#define SJ_FUZZY_AI "\x83" "\x60" "\x83" "\x87" "\x83" "\x8D" "\x83" "\x7B" "\x83" "\x93" /* チョロボン */
#define SJ_PUTRID_PIRANHA "\x83" "\x7C" "\x83" "\x43" "\x83" "\x59" "\x83" "\x93" "\x83" "\x70" "\x83" "\x62" "\x83" "\x4E" "\x83" "\x93" /* ポイズンパックン */
#define SJ_PIRANHA_AI "\x83" "\x70" "\x83" "\x62" "\x83" "\x4E" "\x83" "\x93" "\x83" "\x74" "\x83" "\x89" "\x83" "\x8F" "\x81" "\x5B" /* パックンフラワー */
#define SJ_EMBER "\x83" "\x6F" "\x83" "\x75" "\x83" "\x8B" /* バブル */
#define SJ_CLERK "\x93" "\x58" "\x88" "\xF5" /* 店員 */
#define SJ_INNKEEP "\x8F" "\x68" "\x93" "\x58" "\x88" "\xF5" /* 宿店員 */
#define SJ_GOOMBA "\x83" "\x4E" "\x83" "\x8A" "\x83" "\x7B" "\x81" "\x5B" /* クリボー */
#define SJ_RED_BONES "\x90" "\xD4" "\x83" "\x7A" "\x83" "\x6C" "\x83" "\x6D" "\x83" "\x52" /* 赤ホネノコ */
#define SJ_TOAD "\x83" "\x4C" "\x83" "\x6D" "\x83" "\x73" "\x83" "\x49" /* キノピオ */
#define SJ_TOAD_BLUE SJ_TOAD "\x90" "\xC2" /* キノピオ青 */
#define SJ_TOAD_GREEN SJ_TOAD "\x97" "\xCE" /* キノピオ緑 */
#define SJ_TOADETTE SJ_TOAD "\x8F" "\x97" /* キノピオ女 */
#define SJ_TOADETTE_PINK SJ_TOADETTE "\x93" "\x8D" /* キノピオ女桃 */
#define SJ_BLOOEY_TRIBE "\x83" "\x4C" "\x83" "\x62" "\x83" "\x4E" /* キック */

#endif
