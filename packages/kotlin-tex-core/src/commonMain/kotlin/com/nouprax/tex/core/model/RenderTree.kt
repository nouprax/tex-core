package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderDumper

/**
 * An immutable compiled document: the render tree of schema version 5.
 *
 * Trees are plain values — safely shareable across threads, structurally
 * equatable, and detached from the native engine the moment
 * `Document.compile` returns.
 */
public data class RenderTree(
    /** The root horizontal box; its range spans the whole compile input. */
    val root: HBox,
) {
    /**
     * Renders the canonical dump (`docs/specs/render-tree-dump.md`):
     * byte-identical across platforms for the same compiled tree.
     */
    public fun dump(): String = RenderDumper.dump(this)
}
