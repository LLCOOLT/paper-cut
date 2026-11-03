#ifndef PORT_ANDROID_PC_H
#define PORT_ANDROID_PC_H

/* Android glue (port/android/native/android_pc.c), called from aurora_main. */

/* First thing: stdout/stderr to logcat (an app's go nowhere), the debugging
 * environment file, and the system's crash handlers remembered for later. */
void portAndroidEarlyInit(void);
/* After aurora_initialize and portCrashInit: crash reports to logcat (then the
 * system's tombstone), and the game paused as the app goes to the background. */
void portAndroidInit(void);

#endif
