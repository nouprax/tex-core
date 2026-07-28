package com.nouprax.tex.core

import com.nouprax.tex.core.wire.WireDecoder
import kotlin.test.Test
import kotlin.test.assertFailsWith

/**
 * Handcrafted malformed TXC1 payloads must fail closed: a decoder that
 * defaulted unknown discriminants would turn an engine/binding protocol
 * mismatch into a plausible but semantically wrong public tree.
 */
class WireDecoderTest {
    private class Writer {
        private val bytes = mutableListOf<Byte>()

        fun u32(value: Int) {
            repeat(4) { index -> bytes.add(((value shr (8 * index)) and 0xFF).toByte()) }
        }

        fun u64(value: Long) {
            repeat(8) { index -> bytes.add(((value shr (8 * index)) and 0xFF).toByte()) }
        }

        fun f64(value: Double) {
            u64(value.toRawBits())
        }

        fun raw(count: Int) {
            repeat(count) { bytes.add(0) }
        }

        fun toByteArray(): ByteArray = bytes.toByteArray()
    }

    private fun node(
        kind: Int,
        codepoint: Int = 0,
        style: Int = 0,
        family: Int = 0,
        childCount: Int = 0,
    ): Writer.() -> Unit =
        {
            u32(kind)
            repeat(6) { f64(0.0) }
            u32(codepoint)
            u32(style)
            u32(family)
            f64(0.0)
            u64(0)
            u64(0)
            u32(childCount)
        }

    private fun success(
        nodeCount: Int = 1,
        body: Writer.() -> Unit,
    ): ByteArray {
        val writer = Writer()
        writer.u32(0)
        writer.u32(nodeCount)
        writer.body()
        return writer.toByteArray()
    }

    @Test
    fun unknownNodeKindFailsClosed() {
        assertFailsWith<IllegalStateException> { WireDecoder.decode(success { node(kind = 9)() }) }
    }

    @Test
    fun unknownGlyphFamilyFailsClosed() {
        assertFailsWith<IllegalStateException> { WireDecoder.decode(success { node(kind = 2, family = 99)() }) }
    }

    @Test
    fun unknownGlyphStyleFailsClosed() {
        assertFailsWith<IllegalStateException> { WireDecoder.decode(success { node(kind = 2, style = 7)() }) }
    }

    @Test
    fun nonScalarCodepointFailsClosed() {
        assertFailsWith<IllegalArgumentException> {
            WireDecoder.decode(success { node(kind = 2, codepoint = 0xD800)() })
        }
    }

    @Test
    fun leafWithChildrenFailsClosed() {
        assertFailsWith<IllegalArgumentException> {
            WireDecoder.decode(success { node(kind = 3, childCount = 1)() })
        }
    }

    @Test
    fun nodeCountMismatchFailsClosed() {
        assertFailsWith<IllegalArgumentException> {
            WireDecoder.decode(success(nodeCount = 2) { node(kind = 1)() })
        }
    }

    @Test
    fun trailingBytesFailClosed() {
        assertFailsWith<IllegalArgumentException> {
            WireDecoder.decode(
                success {
                    node(kind = 1)()
                    raw(1)
                },
            )
        }
    }

    @Test
    fun errorRecordTrailingBytesFailClosed() {
        val writer = Writer()
        writer.u32(3)
        writer.u32(0)
        writer.u64(0)
        writer.u64(0)
        writer.u32(0)
        writer.raw(1)
        assertFailsWith<IllegalArgumentException> { WireDecoder.decode(writer.toByteArray()) }
    }

    @Test
    fun unknownStatusFailsClosed() {
        val writer = Writer()
        writer.u32(250)
        writer.u32(0)
        writer.u64(0)
        writer.u64(0)
        writer.u32(0)
        assertFailsWith<IllegalStateException> { WireDecoder.decode(writer.toByteArray()) }
    }
}
