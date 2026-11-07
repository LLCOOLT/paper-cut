#pragma once
/* Tests: the port's menus walked through by themselves, for pictures of every
 * screen in every language (port/src/aurora/menu_crawl_aurora.c).
 *
 * $TTYD_MENU_CRAWL=1: once the Start menu's Settings tab is open (the title
 * menu's Settings), every front-page button, category page, row, Modes hub
 * button's page, the versus set-up and the Bindings page are shown in turn,
 * each choice of each row measured ($TTYD_TEXT_FIT), a picture of each view
 * saved to $TTYD_SHOT_DIR, then the game ends ("[crawl] done", exit 0).
 * =online: the same through the Online tab's rows (the title menu's Online).
 * Nothing is chosen or changed; real input is ignored while it runs. */
#ifdef __cplusplus
extern "C" {
#endif

struct WindowWork;

/* "settings", "online", or NULL when off */
const char* portMenuCrawl(void);
/* a picture of this frame, as name.png (letters, digits, '-' and '_' kept; others '_') */
void portMenuCrawlShot(const char* name);
/* a page that has the pad, walked down its rows: each frame, wp's pad set (down every few frames,
 * downs times, a picture every 6 rows, name_NN); 1 while going, 0 once done */
int portMenuCrawlPad(struct WindowWork* wp, const char* name, int downs);
/* each frame once the crawl is over: the game ends a little later (the last picture written) */
void portMenuCrawlDone(void);

#ifdef __cplusplus
}
#endif
