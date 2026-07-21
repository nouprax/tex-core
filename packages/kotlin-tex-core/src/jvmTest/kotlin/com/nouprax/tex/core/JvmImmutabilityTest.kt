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
