#ifndef PORT_IOS_PC_H
#define PORT_IOS_PC_H

/* iOS glue (port/ios/ios_app.m; iOS builds only). Plain C: no game or SDK
 * types, so both the game-side C and the Objective-C file can include it. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The app's folders, created when missing (no trailing slash):
 * - Documents: shown in the Files app (On My iPhone/iPad > the app) and in
 *   Finder's file sharing; the disc image goes here.
 * - Documents/Saves: Aurora's user folder (memory card .gci files, settings),
 *   so saves can be copied to and from Dolphin with the Files app.
 * - Library/Caches/ttyd: Aurora's pipeline caches (not backed up). */
const char* portIosDocumentsDir(void);
const char* portIosSavesDir(void);
const char* portIosCacheDir(void);

/* Documents/env.txt: the debugging switches of docs/PC_PORT.md (TTYD_TRACE,
 * TTYD_MAP, TTYD_INPUT, ...), one NAME=value per line (# comments), set
 * unless already in the environment; "{docs}" in a value is the Documents
 * folder. As Android's env.txt (iOS apps get no environment of their own):
 * copy it in with the Files app, Finder or `xcrun devicectl device copy to`.
 * Call before anything reads them. */
void portIosLoadEnv(void);

/* The system document picker. Begin presents it (0 if it can't); Poll returns
 * -1 while it is open, 0 if it was cancelled, 1 with the chosen file's path
 * in out. The file is imported: the picker's copy is moved into Documents, so
 * it is found there on the next launch. Keep pumping events (aurora_update)
 * between polls. */
int portIosPickDiscBegin(void);
int portIosPickDiscPoll(char* out, size_t size);

/* port/src/aurora/layout_aurora.c: the allowed orientations changed
 * (0 any, 1 landscape, 2 portrait). */
void portIosRefreshOrientation(int orientation);
/* A screenshot's PNG bytes into Photos (port/src/aurora/shot_aurora.c). */
int portIosSaveToPhotos(const unsigned char* png, size_t len);

#ifdef __cplusplus
}
#endif

#endif
