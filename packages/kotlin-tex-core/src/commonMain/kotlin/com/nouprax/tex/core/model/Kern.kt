package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderVisitor

/** A leaf kern: a fixed horizontal advance. [width] may be negative. */
public data class Kern(
    /** Horizontal offset of the reference point from the parent's, in points. */
    val x: Double,
    /** The advance in points; negative widths move the cursor backward. */
    val width: Double,
    override val src: SourceRange,
) : RenderNode {
    override fun <R> accept(visitor: RenderVisitor<R>): R = visitor.visit(this)
}
