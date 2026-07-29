package com.nouprax.tex.core

internal actual fun cCompile(
    source: ByteArray,
    mode: Int,
): ByteArray {
    AndroidNativeLoader.ensureLoaded()
    return JvmNative.compile(source, mode)
}

private object AndroidNativeLoader {
    private val loaded: Unit = load()

    fun ensureLoaded() = loaded

    private fun load() {
        if (System.getProperty("java.vm.name").orEmpty().contains("Dalvik", ignoreCase = true)) {
            System.loadLibrary("tex_core_kotlin")
            return
        }

        // Host-JVM execution (Robolectric and other local unit tests).
        // Prefer an explicit native build, then the JVM artifact's resource
        // layout, and otherwise explain both remedies.
        val explicit = System.getProperty("tex.core.hostNativeLibrary")
        if (explicit != null) {
            System.load(explicit)
            return
        }
        val os = System.getProperty("os.name").orEmpty().lowercase()
        val architecture = System.getProperty("os.arch").orEmpty().lowercase()
        val platform =
            when {
                os.contains("mac") && architecture in setOf("aarch64", "arm64") -> "macos-arm64"
                os.contains("linux") && architecture in setOf("x86_64", "amd64") -> "linux-x64"
                else -> null
            }
        val filename = System.mapLibraryName("tex_core_kotlin")
        val resource = platform?.let { "/com/nouprax/tex/core/native/$it/$filename" }
        val stream = resource?.let { AndroidNativeLoader::class.java.getResourceAsStream(it) }
        if (stream == null) {
            throw IllegalStateException(
                "TeX Core's Android artifact is running on a host JVM without its native library. " +
                    "Either set -Dtex.core.hostNativeLibrary=/path/to/$filename to a host build of " +
                    "tex_core_kotlin, or put a host build on the test classpath at " +
                    "${resource ?: "com/nouprax/tex/core/native/<platform>/$filename"}.",
            )
        }
        val directory =
            java.nio.file.Files
                .createTempDirectory("tex-core-")
        val library = directory.resolve(filename)
        directory.toFile().deleteOnExit()
        stream.use {
            java.nio.file.Files
                .copy(it, library)
        }
        library.toFile().deleteOnExit()
        System.load(library.toAbsolutePath().toString())
    }
}

internal object JvmNative {
    external fun compile(
        source: ByteArray,
        mode: Int,
    ): ByteArray
}
