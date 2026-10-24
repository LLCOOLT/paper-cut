#ifndef PORT_AURORA_PC_H
#define PORT_AURORA_PC_H

/* Pump Aurora events until a new frame can be started (exits on quit). */
void portAuroraBeginFrame(void);
/* Open the user's disc (port/src/aurora/disc_aurora.c): the argument,
 * $TTYD_DISC, the last one chosen, or the file dialog. 0 if there is none. */
int portAuroraOpenDisc(int argc, char* argv[], const char* userPath);

#endif
