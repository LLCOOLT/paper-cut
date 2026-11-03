/* Stand-in for the asset the decomp extracts from the game (build/<version>/
 * include/assets): used only when that is missing, e.g. CI builds without a
 * disc (CMakeLists.txt). The save file then has a blank banner/icon; nothing
 * else differs. */
const unsigned char _mariost_icon_tex[0x400] __attribute__((aligned(4))) = {0};
