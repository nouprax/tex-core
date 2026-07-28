package com.nouprax.tex.core.model

import com.nouprax.tex.core.walker.RenderVisitor
import com.nouprax.tex.core.wire.readOnlySnapshot

/**
 * A horizontal box: the box node of the schema; the compile root is always
 * an `HBox`. Measures are absolute points and exact integer multiples of
 * 2^-16 pt (see the schema contract, `docs/specs/render-tree.md`).
 *
 * The children list is snapshotted behind a read-only view on every
 * construction path — including [copy] — so no caller-held alias can
 * change a box after it is built, and equality, hash codes, and dumps
 * stay stable across threads.
 */
public class HBox(
    /** Offset of the box's reference point within its parent, in points. */
    public val x: Double,
    /** Baseline shift within the parent; positive is up. */
    public val y: Double,
    /** The advance of the box's content in points. */
    public val width: Double,
    /** The maximum extent above the baseline over the children, in points. */
    public val ascent: Double,
    /** The maximum extent below the baseline over the children, in points. */
    public val descent: Double,
    override val src: SourceRange,
    children: List<RenderNode>,
) : RenderNode {
    /** Child nodes in source order. */
    public val children: List<RenderNode> = readOnlySnapshot(children)

    override fun <R> accept(visitor: RenderVisitor<R>): R = visitor.visit(this)

    /** Returns a box with the given fields replaced, value-style. */
    public fun copy(
        x: Double = this.x,
        y: Double = this.y,
        width: Double = this.width,
        ascent: Double = this.ascent,
        descent: Double = this.descent,
        src: SourceRange = this.src,
        children: List<RenderNode> = this.children,
    ): HBox = HBox(x, y, width, ascent, descent, src, children)

    override fun equals(other: Any?): Boolean =
        this === other ||
            (
                other is HBox &&
                    x == other.x &&
                    y == other.y &&
                    width == other.width &&
                    ascent == other.ascent &&
                    descent == other.descent &&
                    src == other.src &&
                    children == other.children
            )

    override fun hashCode(): Int {
        var result = x.hashCode()
        result = 31 * result + y.hashCode()
        result = 31 * result + width.hashCode()
        result = 31 * result + ascent.hashCode()
        result = 31 * result + descent.hashCode()
        result = 31 * result + src.hashCode()
        result = 31 * result + children.hashCode()
        return result
    }

    override fun toString(): String =
        "HBox(x=$x, y=$y, width=$width, ascent=$ascent, descent=$descent, src=$src, children=$children)"
}
