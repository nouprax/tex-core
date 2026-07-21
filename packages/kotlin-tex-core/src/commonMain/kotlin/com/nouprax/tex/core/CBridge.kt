package com.nouprax.tex.core

/**
 * Runs one native compile and returns the TXC1 payload. Each platform
 * supplies the transport: JNI on JVM and Android, cinterop on the native
 * targets.
 */
internal expect fun cCompile(
    source: ByteArray,
    mode: Int,
): ByteArray
