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

/* A remembered path (the launcher's library.txt and last_disc.txt). The app's container moves
 * (/var/mobile/Containers/Data/Application/<UUID>/ changes with a reinstall or an update), so a path in
 * it is kept relative to it: Store writes "~/Documents/x.iso" for one inside the container (any other
 * as it is); Resolve gives back the full path today, also for an absolute one an older build kept
 * (its part after the old container, under this one), and when that file isn't there, the same file
 * name in Documents if it is there. Both into out (the path as given when out is too small). */
void portIosStorePath(const char* path, char* out, size_t size);
void portIosResolvePath(const char* stored, char* out, size_t size);
/* a big file in Documents (a disc image) left out of iCloud and computer backups */
void portIosExcludeFromBackup(const char* path);
/* A file the open picker gave (asCopy: the system's copy in the app's tmp/ inbox), read or copied in
 * by now: deleted, so ROMs and save imports don't pile up there. A path outside tmp/ is left alone. */
void portIosPickedDone(const char* path);

/* The screen, for its real size (port/src/aurora/layout_aurora.c, portIosScreenPpi): the model
 * (hw.machine, "iPhone18,1"; the simulator's $SIMULATOR_MODEL_IDENTIFIER) into model, 1 in *pad on an
 * iPad, UIScreen's scale and nativeScale, and its short side in points. 0 if there is no screen yet. */
int portIosScreenInfo(char* model, size_t size, int* pad, int* scale, float* nativeScale, float* shortSidePts);

/* port/src/aurora/layout_aurora.c: the allowed orientations changed
 * (0 any, 1 landscape, 2 portrait). */
void portIosRefreshOrientation(int orientation);
/* A screenshot's PNG bytes into Photos (port/src/aurora/shot_aurora.c): 1 under way (the outcome
 * comes later, to portShotPhotosDone), 0 when it can't be (not an image, no access to Photos). */
int portIosSaveToPhotos(const unsigned char* png, size_t len);
/* shot_aurora.c: Photos' answer for a picture portIosSaveToPhotos started (1 added, 0 not: the access
 * refused when asked, or the library's error), for the status line; any thread. */
void portShotPhotosDone(int ok);

#ifdef __cplusplus
}
#endif

#endif
