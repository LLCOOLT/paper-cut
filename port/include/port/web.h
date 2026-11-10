#ifndef PORT_WEB_H
#define PORT_WEB_H
/* The browser build (port/src/web/web_pc.c). */
#include <dolphin/os.h>

/* The disc image the page was given (Module.papercutDisc), opened; FALSE if
 * there is none or Aurora can't read it. Paths "web:<name>" stand for it. */
BOOL portWebOpenDisc(void);

/* The next game frame is due at when (OSGetTime); portWebYield waits for it. */
void portWebPaceTo(OSTime when);

/* Hands the frame just drawn to the browser and waits for its next one (and
 * for the time set by portWebPaceTo). Before each frame begins. */
void portWebYield(void);

#endif
