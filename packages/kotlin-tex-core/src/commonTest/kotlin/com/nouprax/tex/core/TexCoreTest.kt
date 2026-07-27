package com.nouprax.tex.core

import com.nouprax.tex.core.model.CompileException
import com.nouprax.tex.core.model.CompileMode
import com.nouprax.tex.core.model.CompileOptions
import com.nouprax.tex.core.model.CompileStatus
import com.nouprax.tex.core.model.Glyph
import com.nouprax.tex.core.model.GlyphFamily
import com.nouprax.tex.core.model.GlyphStyle
import com.nouprax.tex.core.model.HBox
import com.nouprax.tex.core.model.Kern
import com.nouprax.tex.core.model.RenderNode
import com.nouprax.tex.core.model.SourceRange
import com.nouprax.tex.core.walker.RenderVisitor
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class TexCoreTest {
    @Test
    fun optionsDefaultToDocumentMode() {
        assertEquals(CompileMode.DOCUMENT, CompileOptions().mode)
    }

    @Test
    fun compileReturnsDetachedValueTree() {
        val tree = Document.compile("x", CompileOptions(CompileMode.MATH_INLINE))
        assertEquals(SourceRange(0, 1), tree.root.src)
        assertEquals(1, tree.root.children.size)
        val glyph = assertIs<Glyph>(tree.root.children[0])
        assertEquals('x'.code, glyph.codepoint)
        assertEquals(GlyphStyle.ITALIC, glyph.style)
        assertEquals(GlyphFamily.MAIN, glyph.family)
        assertEquals(10.0, glyph.size)
        assertEquals(0.0, glyph.x)
        assertEquals(glyph.width + glyph.italic, tree.root.width)
    }

    @Test
    fun equalSourcesCompileToEqualValues() {
        val first = Document.compile("a b")
        val second = Document.compile("a b")
        assertEquals(first, second)
        assertEquals(first.hashCode(), second.hashCode())
        assertNotEquals(first, Document.compile("a c"))
    }

    @Test
    fun dumpIsCanonicalAndDeterministic() {
        val tree = Document.compile("")
        assertEquals(
            "render-tree 2\nhbox x=0.0pt y=0.0pt width=0.0pt ascent=0.0pt descent=0.0pt src=0..0\n",
            tree.dump(),
        )
        assertEquals(tree.dump(), tree.dump())
    }

    @Test
    fun unsupportedInputThrowsStructuredError() {
        val failure =
            assertFailsWith<CompileException> {
                Document.compile("\\frac x y", CompileOptions(CompileMode.MATH_INLINE))
            }
        assertEquals(CompileStatus.UNSUPPORTED, failure.status)
        assertEquals(SourceRange(0, 5), failure.range)
        assertEquals("unsupported command \\frac", failure.errorMessage)
    }

    @Test
    fun invalidBytesThrowEncodingErrorNeverReplace() {
        val failure =
            assertFailsWith<CompileException> {
                Document.compile(byteArrayOf(0x61, -1))
            }
        assertEquals(CompileStatus.INVALID_UTF8, failure.status)
        assertEquals(SourceRange(1, 2), failure.range)
        assertEquals("invalid UTF-8 sequence", failure.errorMessage)
    }

    @Test
    fun modeChangesTheAcceptedSurface() {
        Document.compile("a+b", CompileOptions(CompileMode.MATH_INLINE))
        val failure =
            assertFailsWith<CompileException> {
                Document.compile("a+b")
            }
        assertEquals(CompileStatus.UNSUPPORTED, failure.status)
    }

    @Test
    fun visitorsDispatchExhaustively() {
        val tree = Document.compile("a b")
        val counter = KindCounter()
        tree.root.accept(counter)
        assertEquals(1, counter.boxes)
        assertEquals(2, counter.glyphs)
        assertEquals(1, counter.kerns)
    }

    @Test
    fun childrenStayInSourceOrder() {
        val tree = Document.compile("a \\quad b\\ c\\,d\\quad\n")
        val begins = tree.root.children.map { it.src.begin }
        assertEquals(begins.sorted(), begins)
        assertTrue(tree.root.children.any { it is Kern })
    }
}

private class KindCounter : RenderVisitor<Unit> {
    var boxes = 0
    var glyphs = 0
    var kerns = 0

    override fun visit(node: HBox) {
        boxes += 1
        for (child in node.children) {
            child.accept(this)
        }
    }

    override fun visit(node: Glyph) {
        glyphs += 1
    }

    override fun visit(node: Kern) {
        kerns += 1
    }
}

internal fun flatten(node: RenderNode): List<RenderNode> =
    when (node) {
        is HBox -> listOf(node) + node.children.flatMap(::flatten)
        else -> listOf(node)
    }
