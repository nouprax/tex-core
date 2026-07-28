package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderVisitor

/** The face style a glyph is drawn in (schema `style`). */
public enum class GlyphStyle {
    /** The upright text and math face. */
    UPRIGHT,

    /** The math italic letter face. */
    ITALIC,
}

/** The font family a glyph belongs to (schema `family`). */
public enum class GlyphFamily {
    /** The default face pair embedded in the engine. */
    MAIN,

    /** The first delimiter size-variant face. */
    SIZE1,

    /** The second delimiter size-variant face. */
    SIZE2,

    /** The third delimiter size-variant face. */
    SIZE3,

    /** The fourth delimiter size-variant face. */
    SIZE4,
}

/**
 * A leaf glyph, named by Unicode codepoint + style + family + size — never
 * by a private glyph ID.
 */
public data class Glyph(
    /** Horizontal offset of the reference point from the parent's, in points. */
    val x: Double,
    /** Vertical offset from the parent baseline, positive upward, in points. */
    val y: Double,
    /** The Unicode scalar value this glyph renders. */
    val codepoint: Int,
    /** The face style. */
    val style: GlyphStyle,
    /** The font family. */
    val family: GlyphFamily,
    /** The em size in points. */
    val size: Double,
    /** The glyph advance in points. */
    val width: Double,
    /** Extent above the baseline in points. */
    val ascent: Double,
    /** Extent below the baseline in points. */
    val descent: Double,
    /** The italic correction in points; included in a math Ord glyph's advance. */
    val italic: Double,
    override val src: SourceRange,
) : RenderNode {
    override fun <R> accept(visitor: RenderVisitor<R>): R = visitor.visit(this)
}
