@file:OptIn(kotlinx.cinterop.ExperimentalForeignApi::class)

package com.nouprax.tex.core

import com.nouprax.tex.core.internal.nativebridge.tex_core_kotlin_compile
import com.nouprax.tex.core.internal.nativebridge.tex_core_kotlin_free
import kotlinx.cinterop.CPointerVar
import kotlinx.cinterop.UByteVar
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.alloc
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.readBytes
import kotlinx.cinterop.reinterpret
import kotlinx.cinterop.usePinned
import kotlinx.cinterop.value
import platform.posix.size_tVar

internal actual fun cCompile(
    source: ByteArray,
    mode: Int,
): ByteArray =
    memScoped {
        val output = alloc<CPointerVar<UByteVar>>()
        val outputLength = alloc<size_tVar>()
        val ok =
            if (source.isEmpty()) {
                tex_core_kotlin_compile(null, 0u, mode.toUInt(), output.ptr, outputLength.ptr)
            } else {
                source.usePinned { pinned ->
                    tex_core_kotlin_compile(
                        pinned.addressOf(0).reinterpret(),
                        source.size.toULong(),
                        mode.toUInt(),
                        output.ptr,
                        outputLength.ptr,
                    )
                }
            }
        if (!ok) throw OutOfMemoryError("native render-tree copy failed")
        val pointer = requireNotNull(output.value)
        try {
            pointer.readBytes(outputLength.value.toInt())
        } finally {
            tex_core_kotlin_free(pointer)
        }
    }
