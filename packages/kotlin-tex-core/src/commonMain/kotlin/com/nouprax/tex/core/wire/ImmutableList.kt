package com.nouprax.tex.core.wire

/**
 * A genuinely read-only list for decoded tree values. Kotlin's `List`
 * interface erases to `java.util.List` on the JVM, so a decoded
 * `ArrayList` could be downcast and mutated from Java, breaking the
 * detached-immutable-tree contract. `AbstractList` throws on every
 * mutation entry point, so the snapshot behind a decoded node cannot
 * change after `Document.compile` returns.
 */
internal class ReadOnlyList<out Element>(
    private val snapshot: kotlin.collections.List<Element>,
) : AbstractList<Element>() {
    override val size: Int
        get() = snapshot.size

    override fun get(index: Int): Element = snapshot[index]
}

internal fun <Element> immutableList(
    size: Int,
    initializer: (Int) -> Element,
): kotlin.collections.List<Element> = ReadOnlyList(kotlin.collections.List(size, initializer))

/**
 * Snapshots a caller-provided list behind the read-only wrapper. Lists the
 * decoder already wrapped are adopted as-is; anything else is copied, so a
 * mutable alias held by the caller can never change a published tree.
 */
internal fun <Element> readOnlySnapshot(list: kotlin.collections.List<Element>): kotlin.collections.List<Element> =
    if (list is ReadOnlyList<Element>) list else ReadOnlyList(list.toList())
