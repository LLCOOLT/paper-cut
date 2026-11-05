#pragma once
/* The "what's new" page: this version's release notes as a compact list
 * (tools/port/whats_new.py makes it at build time from
 * docs/release_notes/<version>.md), in the game's own windows.
 *
 *  - The first start after an update (a release build whose version is newer
 *    than the one last shown, whats_new_seen in settings.txt; not a first
 *    start ever): over the title screen, the file select, or the field once Mario has the
 *    controls, until A or B. The game gets no input while it's up (so a
 *    replay or another game sees nothing pressed). Not online, not while a
 *    replay plays, not in tests ($TTYD_FAST, $TTYD_MAP) unless
 *    $TTYD_WHATS_NEW=1; $TTYD_WHATS_NEW=0 never.
 *  - Again any time: the Start menu's Settings tab, General, "What's new"
 *    (a page over the tab: port/src/aurora/whats_new_aurora.c), and the
 *    settings page's General tab. */
#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_WHATS_NEW_SECTION, PORT_WHATS_NEW_SUBHEAD, PORT_WHATS_NEW_ITEM };

typedef struct PortWhatsNewLine {
    int kind;           /* PORT_WHATS_NEW_* */
    const char* utf8;   /* the settings page's */
    const char* latin1; /* the US and EU releases' font */
    const char* ascii;  /* the JP release's font */
} PortWhatsNewLine;

/* the generated list (build/generated/whats_new.c): the notes' version (this
 * build's, or the newest notes for a dev build) and the build's own version */
extern const char portWhatsNewVersion[];
extern const char portWhatsNewBuild[];
extern const PortWhatsNewLine portWhatsNewLines[];
extern const int portWhatsNewCount;

/* the Start menu's page (port/include/port/pause_forms.h's PortPausePage) */
struct PortPausePage;
extern const struct PortPausePage gPortWhatsNewPage;

/* each game frame (makeKey, src/system.c): the first start's window */
void portWhatsNewFrame(void);
/* each pad read (DEMOPadRead, before online and replays): while the window
 * is up, its own keys read and the game's input emptied */
struct PADStatus;
void portWhatsNewPad(struct PADStatus* pads);
/* the window is up now */
int portWhatsNewShowing(void);

#ifdef __cplusplus
}
#endif
