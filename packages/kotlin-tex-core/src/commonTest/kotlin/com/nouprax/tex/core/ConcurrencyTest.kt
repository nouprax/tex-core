package com.nouprax.tex.core

import com.nouprax.tex.core.model.CompileMode
import com.nouprax.tex.core.model.CompileOptions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withContext
import kotlin.test.Test
import kotlin.test.assertEquals

class ConcurrencyTest {
    @Test
    fun parallelCompilesShareNoState() =
        runTest {
            val dumps =
                withContext(Dispatchers.Default) {
                    (0 until 8)
                        .map {
                            async {
                                Document
                                    .compile(
                                        "a\\,b\\:c\\;d\\!e\\quad f\\qquad g\\ h",
                                        CompileOptions(CompileMode.MATH_DISPLAY),
                                    ).dump()
                            }
                        }.awaitAll()
                }
            assertEquals(1, dumps.toSet().size)
        }
}
