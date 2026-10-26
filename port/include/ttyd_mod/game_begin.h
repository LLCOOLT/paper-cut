/* Before game headers in a C++ mod (no include guard: used around every
 * group of them): C linkage, and the game's field named delete as delete_
 * (MSVC's library allows that keyword macro with _ALLOW_KEYWORD_MACROS).
 * Pair with ttyd_mod/game_end.h. */
#ifdef __cplusplus
#ifndef _ALLOW_KEYWORD_MACROS
#define _ALLOW_KEYWORD_MACROS 1
#endif
#define delete delete_
extern "C" {
#endif
