package consumer

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nouprax.tex.core.Document
import com.nouprax.tex.core.model.CompileMode
import com.nouprax.tex.core.model.CompileOptions
import com.nouprax.tex.core.model.Glyph
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AndroidConsumerTest {
    @Test
    fun aarSelectsAndLoadsTheDeviceNativeLibrary() {
        val tree = Document.compile("x", CompileOptions(CompileMode.MATH_INLINE))
        val glyph = tree.root.children.single() as Glyph
        assertEquals('x'.code, glyph.codepoint)
    }
}
