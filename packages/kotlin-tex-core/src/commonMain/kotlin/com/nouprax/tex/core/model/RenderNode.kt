package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderVisitor

/**
 * One node of the render tree. The sealed hierarchy is exactly the schema's
 * node kinds, so `when` expressions over a node are exhaustive by
 * construction. Nodes are immutable values with structural equality and no
 * parent pointers.
 */
public sealed interface RenderNode {
    /** The source bytes this node was compiled from. */
    public val src: SourceRange

    /** Dispatches this node to the matching [RenderVisitor] overload. */
    public fun <R> accept(visitor: RenderVisitor<R>): R
}
