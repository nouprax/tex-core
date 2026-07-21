package consumer

import com.nouprax.tex.core.Document
import com.nouprax.tex.core.model.CompileMode
import com.nouprax.tex.core.model.CompileOptions
import com.nouprax.tex.core.model.Glyph
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs

class KmpConsumerTest {
    @Test
    fun rootMetadataSelectsTheJvmVariant() {
        val tree = Document.compile("x", CompileOptions(CompileMode.MATH_INLINE))
        assertEquals(1, tree.root.children.size)
        assertIs<Glyph>(tree.root.children[0])
        assertEquals(tree.dump(), tree.dump())
    }
}
