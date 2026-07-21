package com.nouprax.tex.core.model

/**
 * Half-open byte range `[begin, end)` into the compile call's source.
 *
 * Ranges are data obtained from a compiled tree, valid for that compile
 * call's input; the schema contract deliberately does not promise that they
 * are storage-resident node properties across revisions.
 */
public data class SourceRange(
    val begin: Long,
    val end: Long,
)
