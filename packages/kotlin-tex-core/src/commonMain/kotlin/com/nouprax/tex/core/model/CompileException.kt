package com.nouprax.tex.core.model

/** The failure category of a compile call, mirroring `tex_core_status`. */
public enum class CompileStatus {
    /** A caller contract violation; unreachable through these entry points. */
    INVALID_ARGUMENT,

    /** The source is not valid UTF-8. */
    INVALID_UTF8,

    /** The source uses a construct outside the shipped surface. */
    UNSUPPORTED,

    /** The engine could not allocate memory. */
    ALLOCATION_FAILED,
}

/**
 * The structured fail-fast compile error (plan decision D4): a failed
 * compile produces no tree, only this exception naming the offending bytes.
 */
public class CompileException(
    /** The failure category. */
    public val status: CompileStatus,
    /** The source bytes the error names, when the failure has a location. */
    public val range: SourceRange?,
    /** The deterministic ASCII description, e.g. `unsupported command \frac`. */
    public val errorMessage: String,
) : Exception(
        if (range != null) "$errorMessage (bytes ${range.begin}..${range.end})" else errorMessage,
    )
