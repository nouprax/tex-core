package com.nouprax.tex.core.wire

import com.nouprax.tex.core.model.CompileException
import com.nouprax.tex.core.model.CompileStatus
import com.nouprax.tex.core.model.Glyph
import com.nouprax.tex.core.model.GlyphFamily
import com.nouprax.tex.core.model.GlyphStyle
import com.nouprax.tex.core.model.HBox
import com.nouprax.tex.core.model.Kern
import com.nouprax.tex.core.model.RenderNode
import com.nouprax.tex.core.model.RenderTree
import com.nouprax.tex.core.model.Rule
import com.nouprax.tex.core.model.SourceRange

/**
 * Decodes the TXC1 payload written by `tex_core_kotlin_bridge.c` — the
 * in-process transport between the native bridge and this decoder, never a
 * public serialization format. A status record becomes a thrown
 * [CompileException]; a preorder node stream becomes the immutable value
 * tree.
 */
internal object WireDecoder {
    fun decode(payload: ByteArray): RenderTree {
        val reader = WireReader(payload)
        val status = reader.u32()
        if (status != 0) {
            val hasRange = reader.u32() != 0
            val begin = reader.u64()
            val end = reader.u64()
            val message = reader.text(reader.u32())
            throw CompileException(
                status = decodeStatus(status),
                range = if (hasRange) SourceRange(begin, end) else null,
                errorMessage = message,
            )
        }
        reader.u32() // node count; the preorder walk carries the structure
        val root = reader.node()
        require(root is HBox) { "wire payload root must be an hbox" }
        require(reader.exhausted()) { "wire payload has trailing bytes" }
        return RenderTree(root)
    }

    private fun decodeStatus(status: Int): CompileStatus =
        when (status) {
            2 -> CompileStatus.INVALID_UTF8
            3 -> CompileStatus.UNSUPPORTED
            4 -> CompileStatus.ALLOCATION_FAILED
            else -> CompileStatus.INVALID_ARGUMENT
        }
}

private class WireReader(
    private val payload: ByteArray,
) {
    private var position = 0

    fun exhausted(): Boolean = position == payload.size

    fun u32(): Int {
        var value = 0
        repeat(4) { index -> value = value or ((payload[position + index].toInt() and 0xFF) shl (8 * index)) }
        position += 4
        return value
    }

    fun u64(): Long {
        var value = 0L
        repeat(8) { index -> value = value or ((payload[position + index].toLong() and 0xFF) shl (8 * index)) }
        position += 8
        return value
    }

    fun f64(): Double = Double.fromBits(u64())

    fun text(length: Int): String {
        val value = payload.decodeToString(position, position + length)
        position += length
        return value
    }

    fun node(): RenderNode {
        val kind = u32()
        val x = f64()
        val y = f64()
        val width = f64()
        val ascent = f64()
        val descent = f64()
        val italic = f64()
        val codepoint = u32()
        val style = u32()
        val family = u32()
        val size = f64()
        val src = SourceRange(u64(), u64())
        val childCount = u32()
        return when (kind) {
            2 -> {
                Glyph(
                    x = x,
                    y = y,
                    codepoint = codepoint,
                    style = if (style == 1) GlyphStyle.ITALIC else GlyphStyle.UPRIGHT,
                    family =
                        when (family) {
                            1 -> GlyphFamily.SIZE1
                            2 -> GlyphFamily.SIZE2
                            3 -> GlyphFamily.SIZE3
                            4 -> GlyphFamily.SIZE4
                            5 -> GlyphFamily.BOLD
                            6 -> GlyphFamily.TEXTIT
                            7 -> GlyphFamily.CAL
                            8 -> GlyphFamily.BB
                            9 -> GlyphFamily.SANS
                            10 -> GlyphFamily.MONO
                            else -> GlyphFamily.MAIN
                        },
                    size = size,
                    width = width,
                    ascent = ascent,
                    descent = descent,
                    italic = italic,
                    src = src,
                )
            }

            3 -> {
                Kern(x = x, width = width, src = src)
            }

            4 -> {
                Rule(x = x, y = y, width = width, ascent = ascent, descent = descent, src = src)
            }

            1 -> {
                HBox(
                    x = x,
                    y = y,
                    width = width,
                    ascent = ascent,
                    descent = descent,
                    src = src,
                    children = immutableList(childCount) { node() },
                )
            }

            else -> {
                error("wire payload contains an unknown node kind: $kind")
            }
        }
    }
}
