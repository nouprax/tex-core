package com.nouprax.tex.core.walker

import com.nouprax.tex.core.model.Glyph
import com.nouprax.tex.core.model.HBox
import com.nouprax.tex.core.model.Kern

/**
 * An exhaustive typed visitor over render nodes: one `visit` overload per
 * schema node kind, so adding a kind is a source-breaking schema change,
 * never a silently unhandled case.
 */
public interface RenderVisitor<R> {
    /** Visits a horizontal box. */
    public fun visit(node: HBox): R

    /** Visits a glyph. */
    public fun visit(node: Glyph): R

    /** Visits a kern. */
    public fun visit(node: Kern): R
}
