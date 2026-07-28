package consumer

import com.nouprax.tex.core.Document

fun main() {
    val tree = Document.compile("The 42 rows, 3.14 total.\n")
    check(tree.root.children.isNotEmpty())
    check(tree.dump().startsWith("render-tree 4\n"))
}
