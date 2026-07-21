package com.nouprax.tex.core

internal actual fun cCompile(
    source: ByteArray,
    mode: Int,
): ByteArray {
    AndroidNativeLoader.ensureLoaded()
    return JvmNative.compile(source, mode)
}

private object AndroidNativeLoader {
    private val loaded: Unit =
        if (System.getProperty("java.vm.name").orEmpty().contains("Dalvik", ignoreCase = true)) {
            System.loadLibrary("tex_core_kotlin")
        } else {
            // Android host tests run on a desktop JVM: load the desktop JNI
            // payload the build points at instead of an packaged library.
            System.load(requireNotNull(System.getProperty("tex.core.hostNativeLibrary")))
        }

    fun ensureLoaded() = loaded
}

internal object JvmNative {
    external fun compile(
        source: ByteArray,
        mode: Int,
    ): ByteArray
}
