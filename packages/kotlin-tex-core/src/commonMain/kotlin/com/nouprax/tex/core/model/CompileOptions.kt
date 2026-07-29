package com.nouprax.tex.core.model

/** The input form of one compile call (schema `options.mode`). */
public enum class CompileMode {
    /** A self-contained LaTeX file or block. */
    DOCUMENT,

    /** A bare inline math fragment with the delimiters already stripped. */
    MATH_INLINE,

    /** A bare display math fragment with the delimiters already stripped. */
    MATH_DISPLAY,
}

/** Options of one compile call. Every field has a frozen default. */
public data class CompileOptions
    @kotlin.jvm.JvmOverloads
    constructor(
        /** The input form; the default is [CompileMode.DOCUMENT]. */
        val mode: CompileMode = CompileMode.DOCUMENT,
    )

internal fun CompileOptions.toNativeMode(): Int =
    when (mode) {
        CompileMode.DOCUMENT -> 0
        CompileMode.MATH_INLINE -> 1
        CompileMode.MATH_DISPLAY -> 2
    }
