package com.nouprax.tex.core

import java.lang.reflect.InvocationTargetException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs

class JvmImmutabilityTest {
    @Test
    fun decodedChildrenRejectMutationThroughTheJavaView() {
        val tree = Document.compile("a b")
        val dump = tree.dump()

        // A Kotlin downcast to MutableList is rejected outright…
        assertFailsWith<ClassCastException> {
            @Suppress("UNCHECKED_CAST")
            tree.root.children as MutableList<Any?>
        }

        // …and the raw java.util.List view throws on every mutation entry
        // point instead of editing the decoded snapshot.
        val clear = java.util.List::class.java.getMethod("clear")
        val failure = assertFailsWith<InvocationTargetException> { clear.invoke(tree.root.children) }
        assertIs<UnsupportedOperationException>(failure.cause)
        assertEquals(dump, tree.dump())
    }
}

class HBoxSnapshotTest {
    @Test
    fun publicConstructionSnapshotsChildren() {
        val tree = Document.compile("a b")
        val alias = mutableListOf<com.nouprax.tex.core.model.RenderNode>()
        val box = tree.root.copy(children = alias)
        val dump =
            com.nouprax.tex.core.model
                .RenderTree(box)
                .dump()
        val hash = box.hashCode()

        // A caller-held alias must not reach the published box: neither its
        // children, nor its dump, equality, or hash may change afterward.
        alias += tree.root.children.first()
        assertEquals(0, box.children.size)
        assertEquals(
            dump,
            com.nouprax.tex.core.model
                .RenderTree(box)
                .dump(),
        )
        assertEquals(hash, box.hashCode())
        assertEquals(box, box.copy())

        // The snapshot behind copy() is as read-only as a decoded list.
        val clear = java.util.List::class.java.getMethod("clear")
        val failure = assertFailsWith<InvocationTargetException> { clear.invoke(box.children) }
        assertIs<UnsupportedOperationException>(failure.cause)
    }
}
