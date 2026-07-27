package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderVisitor

/**
 * A leaf rule: a solid rectangle. Its reference point sits at its left
 * edge on the parent's baseline shifted by [y]; the ink extends [ascent]
 * up, [descent] down, and [width] right. Today produced only as the
 * fraction bar.
 */
public data class Rule(
    /** Horizontal offset of the reference point from the parent's, in points. */
    val x: Double,
    /** Vertical offset from the parent baseline, positive upward, in points. */
    val y: Double,
    /** The ink width in points. */
    val width: Double,
    /** Ink extent above the reference point in points. */
    val ascent: Double,
    /** Ink extent below the reference point in points. */
    val descent: Double,
    override val src: SourceRange,
) : RenderNode {
    override fun <R> accept(visitor: RenderVisitor<R>): R = visitor.visit(this)
}
