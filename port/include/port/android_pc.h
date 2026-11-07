#ifndef PORT_ANDROID_PC_H
#define PORT_ANDROID_PC_H

/* Android glue (port/android/native/android_pc.c), called from aurora_main. */

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* First thing: stdout/stderr to logcat (an app's go nowhere), the debugging
 * environment file, and the system's crash handlers remembered for later. */
void portAndroidEarlyInit(void);
/* After aurora_initialize and portCrashInit: crash reports to logcat (then the
 * system's tombstone), and the game paused as the app goes to the background. */
void portAndroidInit(void);

/* A content:// URI (what the system's file picker gives, through SDL's file
 * dialog) is no file path: stat() and SDL_GetPathInfo can't see it, though
 * SDL_IOFromFile opens it. 1 if it can be read now, with its name as the file
 * manager shows it (name may be NULL) and its size (size may be NULL); 0 if
 * it is gone or the app's access to it was lost. Any thread. */
int portAndroidContentInfo(const char* uri, char* name, size_t nameSize, unsigned long long* size);

/* Updates (port/settings/features/update_check.cpp, TtydActivity.installApk):
 * the downloaded, checked APK at path handed to Android's own installer,
 * which asks the player (never a silent install). 0: Android's dialog is
 * coming; 1: the app may not install apps yet, so Android's "Install unknown
 * apps" screen for it was opened; -1: it failed. Any thread. */
int portAndroidInstallApk(const char* path);
/* What became of that install, once: 0 nothing (yet); 2 refused, signed with
 * another key; 3 the player closed Android's dialog; 4 another failure (why
 * in message). Any thread. */
int portAndroidInstallResult(char* message, size_t messageSize);

/* 1 if path is a content:// URI (portAndroidContentInfo's), not a file path. */
#define PORT_ANDROID_IS_CONTENT_URI(path) ((path) != NULL && strncmp((path), "content://", 10) == 0)

#ifdef __cplusplus
}
#endif

#endif
