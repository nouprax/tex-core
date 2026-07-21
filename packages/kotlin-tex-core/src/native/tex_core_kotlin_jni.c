/* JNI adapter over the bridge: one entry point, byte arrays in and out.
 * Used by the JVM desktop payload and the Android runtime alike. */

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>

#include "tex_core_kotlin_bridge.h"

JNIEXPORT jbyteArray JNICALL Java_com_nouprax_tex_core_JvmNative_compile(JNIEnv *env, jclass type,
                                                                         jbyteArray source, jint mode) {
    (void)type;
    jsize length = source != NULL ? (*env)->GetArrayLength(env, source) : 0;
    jbyte *bytes = NULL;
    if (length > 0) {
        bytes = (*env)->GetByteArrayElements(env, source, NULL);
        if (bytes == NULL) {
            return NULL;
        }
    }

    uint8_t *payload = NULL;
    size_t payload_length = 0;
    bool ok = tex_core_kotlin_compile((const uint8_t *)bytes, (size_t)length, (uint32_t)mode, &payload,
                                      &payload_length);
    if (bytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, source, bytes, JNI_ABORT);
    }
    if (!ok) {
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/OutOfMemoryError"),
                         "native render-tree copy failed");
        return NULL;
    }

    jbyteArray result = (*env)->NewByteArray(env, (jsize)payload_length);
    if (result != NULL) {
        (*env)->SetByteArrayRegion(env, result, 0, (jsize)payload_length, (const jbyte *)payload);
    }
    tex_core_kotlin_free(payload);
    return result;
}
