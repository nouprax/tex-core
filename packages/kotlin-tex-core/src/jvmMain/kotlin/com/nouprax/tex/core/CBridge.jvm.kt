package com.nouprax.tex.core

import java.nio.file.Files
import java.nio.file.Path

internal actual fun cCompile(
    source: ByteArray,
    mode: Int,
): ByteArray {
    DesktopNativeLoader.ensureLoaded()
    return JvmNative.compile(source, mode)
}

internal object JvmNative {
    external fun compile(
        source: ByteArray,
        mode: Int,
    ): ByteArray
}

private object DesktopNativeLoader {
    private val loaded: Unit = load()

    fun ensureLoaded() = loaded

    private fun load() {
        val os = System.getProperty("os.name").lowercase()
        val architecture = System.getProperty("os.arch").lowercase()
        // Exactly the tuples the release publishes payloads for (the same
        // support table as `desktopPlatform` in build.gradle.kts): claiming
        // more would fail at load time on consumer machines instead of
        // here, with a clear message.
        val platform =
            when {
                os.contains("mac") && architecture in setOf("aarch64", "arm64") -> "macos-arm64"

                os.contains("linux") && architecture in setOf("x86_64", "amd64") -> "linux-x64"

                else -> throw UnsupportedOperationException(
                    "unsupported native platform: $os/$architecture (supported: macos-arm64, linux-x64)",
                )
            }
        val filename = System.mapLibraryName("tex_core_kotlin")
        val resource = "/com/nouprax/tex/core/native/$platform/$filename"
        val directory = Files.createTempDirectory("tex-core-")
        val library = directory.resolve(filename)

        // deleteOnExit removes entries in reverse registration order, so the
        // directory must be registered before its child.
        directory.toFile().deleteOnExit()
        requireNotNull(DesktopNativeLoader::class.java.getResourceAsStream(resource)) {
            "native library is missing for $platform"
        }.use { Files.copy(it, library) }
        library.toFile().deleteOnExit()
        loadBundledLibrary(library)
    }

    @Suppress("UnsafeDynamicallyLoadedCode")
    private fun loadBundledLibrary(library: Path) {
        // loadLibrary cannot address a native library extracted from this JAR.
        System.load(library.toAbsolutePath().toString())
    }
}
