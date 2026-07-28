package com.nouprax.tex.core

import com.nouprax.tex.core.model.CompileOptions
import com.nouprax.tex.core.model.RenderTree
import com.nouprax.tex.core.model.toNativeMode
import com.nouprax.tex.core.wire.WireDecoder

/**
 * The compile entry point: UTF-8 LaTeX source in, an immutable render tree
 * out. The native engine validates, parses, and lays out the source; the
 * returned tree is a detached Kotlin value and no native handle survives
 * the call. This library never draws.
 */
public object Document {
    /** Compiles a Kotlin string (always valid UTF-8). */
    @kotlin.jvm.JvmStatic
    @kotlin.jvm.JvmOverloads
    public fun compile(
        source: String,
        options: CompileOptions = CompileOptions(),
    ): RenderTree = compile(source.encodeToByteArray(), options)

    /**
     * Compiles exact source bytes. The engine owns encoding validation:
     * invalid UTF-8 is a structured
     * [com.nouprax.tex.core.model.CompileException], never a silent
     * replacement, so byte inputs round-trip the full error contract.
     */
    @kotlin.jvm.JvmStatic
    @kotlin.jvm.JvmOverloads
    public fun compile(
        source: ByteArray,
        options: CompileOptions = CompileOptions(),
    ): RenderTree = WireDecoder.decode(cCompile(source, options.toNativeMode()))
}
