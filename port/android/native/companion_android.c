/* The second screen's Java side (port/android/.../TtydCompanion.java): its
 * native methods, on the app's UI thread. The snapshot is read as text and a
 * tap only joins the queue the game reads as input (port/include/port/companion.h). */
#include <jni.h>
#include <stdlib.h>
#include "port/companion.h"
#include "port/companion_menu.h"
#include "port/tr.h"

JNIEXPORT jboolean JNICALL Java_dev_ttyd_port_TtydCompanion_enabled(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return portCompanionSetting() != 0;
}

JNIEXPORT void JNICALL Java_dev_ttyd_port_TtydCompanion_setActive(JNIEnv* env, jclass cls, jboolean active) {
    (void)env;
    (void)cls;
    portCompanionSetActive(active ? 1 : 0);
}

JNIEXPORT jstring JNICALL Java_dev_ttyd_port_TtydCompanion_snapshot(JNIEnv* env, jclass cls) {
    static char sText[24576];
    (void)cls;
    portCompanionText(sText, sizeof(sText));
    return (*env)->NewStringUTF(env, sText);
}

JNIEXPORT void JNICALL Java_dev_ttyd_port_TtydCompanion_tap(JNIEnv* env, jclass cls, jint kind, jint arg, jint target) {
    (void)env;
    (void)cls;
    portCompanionTap(kind, arg, target);
}

JNIEXPORT jstring JNICALL Java_dev_ttyd_port_TtydCompanion_tr(JNIEnv* env, jclass cls, jstring english) {
    const char* en;
    jstring out;
    (void)cls;
    if (english == NULL) {
        return NULL;
    }
    en = (*env)->GetStringUTFChars(env, english, NULL);
    if (en == NULL) {
        return english;
    }
    out = (*env)->NewStringUTF(env, portTr(en)); /* made before en is let go: it may be en itself */
    (*env)->ReleaseStringUTFChars(env, english, en);
    return out;
}

/* --- the second screen's Start menu (port/include/port/companion_menu.h) --- */

JNIEXPORT jboolean JNICALL Java_dev_ttyd_port_TtydCompanion_menuReady(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return portCompanionMenuReady() != 0;
}

/* The picture into out (ARGB, PORT_COMPANION_MENU_WIDTH x _HEIGHT) when newer than
 * seen; returns its serial (0: none yet). */
JNIEXPORT jint JNICALL Java_dev_ttyd_port_TtydCompanion_menuPicture(JNIEnv* env, jclass cls, jintArray out, jint seen) {
    static unsigned char sRgba[PORT_COMPANION_MENU_WIDTH * PORT_COMPANION_MENU_HEIGHT * 4];
    static jint sArgb[PORT_COMPANION_MENU_WIDTH * PORT_COMPANION_MENU_HEIGHT];
    unsigned now = (unsigned)seen;
    unsigned serial;
    int i;
    (void)cls;
    serial = portCompanionMenuPicture(sRgba, &now);
    if (serial != 0 && now != (unsigned)seen && out != NULL &&
        (*env)->GetArrayLength(env, out) >= PORT_COMPANION_MENU_WIDTH * PORT_COMPANION_MENU_HEIGHT) {
        for (i = 0; i < PORT_COMPANION_MENU_WIDTH * PORT_COMPANION_MENU_HEIGHT; i++) {
            const unsigned char* p = sRgba + i * 4;
            sArgb[i] = (jint)(0xFF000000u | ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2]);
        }
        (*env)->SetIntArrayRegion(env, out, 0, PORT_COMPANION_MENU_WIDTH * PORT_COMPANION_MENU_HEIGHT, sArgb);
    }
    return (jint)serial;
}

JNIEXPORT void JNICALL Java_dev_ttyd_port_TtydCompanion_menuTap(JNIEnv* env, jclass cls, jfloat x, jfloat y) {
    (void)env;
    (void)cls;
    portCompanionMenuTap(x, y);
}

JNIEXPORT void JNICALL Java_dev_ttyd_port_TtydCompanion_menuButton(JNIEnv* env, jclass cls, jint button) {
    (void)env;
    (void)cls;
    portCompanionMenuButton(button);
}

/* The device has a second display of its own: the setting shows. */
JNIEXPORT void JNICALL Java_dev_ttyd_port_TtydCompanion_setDualScreen(JNIEnv* env, jclass cls, jboolean dual) {
    (void)env;
    (void)cls;
    portCompanionSetDualScreen(dual ? 1 : 0);
}
