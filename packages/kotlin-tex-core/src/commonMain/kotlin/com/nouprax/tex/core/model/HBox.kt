package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderVisitor

/**
 * A horizontal box: the compile root. Measures are absolute points and
 * exact integer multiples of 2^-16 pt (see the schema contract,
 * `docs/specs/render-tree.md`).
 */
public data class HBox(
    /** The advance of the box's content in points. */
    val width: Double,
    /** The maximum extent above the baseline over the children, in points. */
    val ascent: Double,
    /** The maximum extent below the baseline over the children, in points. */
    val descent: Double,
    override val src: SourceRange,
    /** Child nodes in source order. */
    val children: List<RenderNode>,
) : RenderNode {
    override fun <R> accept(visitor: RenderVisitor<R>): R = visitor.visit(this)
}
