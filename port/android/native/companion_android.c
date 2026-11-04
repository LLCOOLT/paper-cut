/* The second screen's Java side (port/android/.../TtydCompanion.java): its
 * native methods, on the app's UI thread. The snapshot is read as text and a
 * tap only joins the queue the game reads as input (port/include/port/companion.h). */
#include <jni.h>
#include <stdlib.h>
#include "port/companion.h"
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
