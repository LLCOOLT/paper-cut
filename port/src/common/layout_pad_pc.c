/* Tests of online play between different programs (docs/STATE_SYNC.md, "Every game the same"): a build
 * configured with CFLAGS=-DTTYD_LAYOUT_PAD (environment) lays the game's data out at other places from the
 * code (a quarter of a megabyte more data before the game's tables), as another compiler or platform
 * would. Two games, one of each, playing a shared battle show that nothing crossing depends on where
 * the program's data is. Nothing in any other build. */
#ifdef TTYD_LAYOUT_PAD
unsigned char portLayoutPad[1 << 18] = {1};
const unsigned char portLayoutPadConst[1 << 16] = {2};

/* (kept by the linker: read once at start-up by nobody but this) */
int portLayoutPadTouch(void) {
    return portLayoutPad[0] + portLayoutPadConst[0];
}
#else
typedef int portLayoutPadUnused; /* (an empty file is not ISO C) */
#endif
