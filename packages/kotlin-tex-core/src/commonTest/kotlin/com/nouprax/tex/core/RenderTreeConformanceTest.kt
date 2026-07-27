package com.nouprax.tex.core

import com.nouprax.tex.core.model.CompileException
import com.nouprax.tex.core.model.CompileStatus
import com.nouprax.tex.core.model.Glyph
import com.nouprax.tex.core.model.HBox
import com.nouprax.tex.core.model.Kern
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * Replays every case of the shared render-tree corpus
 * (`specs/render-tree/manifest.json`) through the public API. Tree cases
 * compare the canonical dump byte for byte with the reviewed golden; error
 * cases compare the corpus error record rebuilt from the public exception.
 * The case data is generated at build time from the manifest — never a
 * checked-in copy.
 */
class RenderTreeConformanceTest {
    @Test
    fun everyManifestCaseMatchesTheSharedSpec() {
        assertTrue(renderTreeCases.isNotEmpty())
        for (case in renderTreeCases) {
            when (case.outcome) {
                RenderTreeOutcome.TREE -> {
                    val tree = Document.compile(case.source, case.options)
                    assertEquals(case.expected, tree.dump(), case.name)
                    assertEquals(tree.dump(), tree.dump(), case.name)
                }

                RenderTreeOutcome.ERROR -> {
                    try {
                        Document.compile(case.source, case.options)
                        fail("${case.name}: compile unexpectedly succeeded")
                    } catch (failure: CompileException) {
                        assertEquals(case.expected, errorRecord(failure), case.name)
                    }
                }
            }
        }
    }

    @Test
    fun theCorpusReachesEveryPublicNodeKind() {
        val kinds = mutableSetOf<String>()
        for (case in renderTreeCases) {
            if (case.outcome != RenderTreeOutcome.TREE) continue
            for (node in flatten(Document.compile(case.source, case.options).root)) {
                kinds +=
                    when (node) {
                        is HBox -> "hbox"
                        is Glyph -> "glyph"
                        is Kern -> "kern"
                    }
            }
        }
        assertEquals(setOf("hbox", "glyph", "kern"), kinds)
    }

    private fun errorRecord(failure: CompileException): String {
        val status =
            when (failure.status) {
                CompileStatus.INVALID_ARGUMENT -> "invalid-argument"
                CompileStatus.INVALID_UTF8 -> "invalid-utf8"
                CompileStatus.UNSUPPORTED -> "unsupported"
                CompileStatus.ALLOCATION_FAILED -> "allocation-failed"
            }
        val src = failure.range?.let { "${it.begin}..${it.end}" } ?: "none"
        return "render-error 2\nerror status=$status src=$src message=${failure.errorMessage}\n"
    }
}
