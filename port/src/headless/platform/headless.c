/* Headless platform backend: no window, no input. Lets the game run (and
 * be tested) before a windowed backend exists. */
#include <stdlib.h>
#include "port/platform.h"

void portPlatformInit(void) {}

BOOL portPlatformPoll(void) {
    return TRUE;
}

void portPlatformPresent(void) {}

BOOL portPlatformReadPad(s32 chan, PADStatus* status) {
    return chan == 0; /* a controller in port 1 with nothing pressed */
}

/* Frame captures: nothing is drawn headless. */
void portShotRequest(const char* name) {
    (void)name;
}

BOOL portShotPending(void) {
    return FALSE;
}

void portShotFrame(void) {}

void portInterpSceneDrawn(void) {}
/* text to speech (port/include/port/speech.h): the settings page's, not here */
void portSpeechMessage(const void* window, const char* text) {
    (void)window;
    (void)text;
}
void portSpeechAppend(const void* window, const char* text) {
    (void)window;
    (void)text;
}
void portSpeechPage(const void* window, int page) {
    (void)window;
    (void)page;
}
void portSpeechClosed(const void* window) {
    (void)window;
}
void portSpeechMenu(const char* text) {
    (void)text;
}
/* captions for sound cues (port/include/port/captions.h): the settings page's, not here */
void portCaptionSfx(const char* name, const float* pos) {
    (void)name;
    (void)pos;
}

BOOL portInterpInBetween(void) {
    return FALSE;
}

void* portInterpScratchAlloc(u32 size) {
    (void)size;
    return NULL;
}

void portInterpKeep(void* p, unsigned int n) {
    (void)p;
    (void)n;
}
