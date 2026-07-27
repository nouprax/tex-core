package com.nouprax.tex.core.walker

import com.nouprax.tex.core.model.Glyph
import com.nouprax.tex.core.model.GlyphFamily
import com.nouprax.tex.core.model.GlyphStyle
import com.nouprax.tex.core.model.HBox
import com.nouprax.tex.core.model.Kern
import com.nouprax.tex.core.model.RenderTree
import com.nouprax.tex.core.model.SourceRange

/**
 * Renders the canonical dump through the public visitor path. The output is
 * byte-deterministic: measures print with Knuth's `print_scaled` algorithm
 * over the exact scaled-point values the engine computed, so a Kotlin dump
 * of a tree equals the C dump of the same compile byte for byte.
 */
internal object RenderDumper {
    fun dump(tree: RenderTree): String {
        val visitor = DumpVisitor()
        tree.root.accept(visitor)
        return visitor.output.toString()
    }
}

private class DumpVisitor : RenderVisitor<Unit> {
    val output = StringBuilder("render-tree 2\n")
    private var depth = 0

    override fun visit(node: HBox) {
        indent()
        output
            .append("hbox x=")
            .append(measure(node.x))
            .append(" y=")
            .append(measure(node.y))
            .append(" width=")
            .append(measure(node.width))
            .append(" ascent=")
            .append(measure(node.ascent))
            .append(" descent=")
            .append(measure(node.descent))
            .append(" src=")
            .append(range(node.src))
            .append('\n')
        depth += 1
        for (child in node.children) {
            child.accept(this)
        }
        depth -= 1
    }

    override fun visit(node: Glyph) {
        indent()
        output
            .append("glyph x=")
            .append(measure(node.x))
            .append(" y=")
            .append(measure(node.y))
            .append(" cp=")
            .append(codepoint(node.codepoint))
            .append(" style=")
            .append(
                when (node.style) {
                    GlyphStyle.UPRIGHT -> "upright"
                    GlyphStyle.ITALIC -> "italic"
                },
            ).append(" family=")
            .append(
                when (node.family) {
                    GlyphFamily.MAIN -> "main"
                },
            ).append(" size=")
            .append(measure(node.size))
            .append(" width=")
            .append(measure(node.width))
            .append(" ascent=")
            .append(measure(node.ascent))
            .append(" descent=")
            .append(measure(node.descent))
            .append(" italic=")
            .append(measure(node.italic))
            .append(" src=")
            .append(range(node.src))
            .append('\n')
    }

    override fun visit(node: Kern) {
        indent()
        output
            .append("kern x=")
            .append(measure(node.x))
            .append(" width=")
            .append(measure(node.width))
            .append(" src=")
            .append(range(node.src))
            .append('\n')
    }

    private fun indent() {
        repeat(depth) { output.append("  ") }
    }

    private fun range(src: SourceRange): String = "${src.begin}..${src.end}"

    private fun codepoint(value: Int): String = "U+" + value.toString(16).uppercase().padStart(4, '0')

    /**
     * Knuth's `print_scaled` (TeX §103) over the recovered integer scaled
     * value: published measures are exact multiples of 2^-16 pt, so the
     * recovery is lossless. Half-away-from-zero over `toLong` truncation
     * guards a stray ulp with integer-only arithmetic.
     */
    private fun measure(points: Double): String {
        val scaled = points * 65536.0
        var value = (if (scaled < 0) scaled - 0.5 else scaled + 0.5).toLong()
        val text = StringBuilder()
        if (value < 0) {
            text.append('-')
            value = -value
        }
        text.append(value / 65536)
        text.append('.')
        var fraction = 10 * (value % 65536) + 5
        var delta = 10L
        do {
            if (delta > 65536) {
                fraction += 32768 - delta / 2
            }
            text.append(fraction / 65536)
            fraction = 10 * (fraction % 65536)
            delta *= 10
        } while (fraction > delta)
        text.append("pt")
        return text.toString()
    }
}
