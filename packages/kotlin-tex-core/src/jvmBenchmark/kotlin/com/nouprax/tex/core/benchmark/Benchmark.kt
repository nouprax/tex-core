package com.nouprax.tex.core.benchmark

import com.nouprax.tex.core.Document
import com.nouprax.tex.core.model.CompileMode
import com.nouprax.tex.core.model.CompileOptions
import kotlin.time.measureTime

private const val WARMUP_COUNT = 3
private const val REPEAT_COUNT = 10

private fun benchmark(
    workload: String,
    source: String,
    mode: CompileMode,
) {
    val options = CompileOptions(mode)
    repeat(WARMUP_COUNT) { Document.compile(source, options) }

    val durations =
        (0 until REPEAT_COUNT)
            .map { measureTime { Document.compile(source, options) } }
            .sorted()
    val medianNanoseconds = durations[REPEAT_COUNT / 2].inWholeNanoseconds
    val peakRssKib =
        java.lang.management.ManagementFactory
            .getMemoryMXBean()
            .heapMemoryUsage.used / 1024
    println(
        "benchmark runtime=kotlin-jvm boundary=native_compile_and_value_copy workload=$workload " +
            "bytes=${source.encodeToByteArray().size} warmup=$WARMUP_COUNT repeats=$REPEAT_COUNT " +
            "median_ns=$medianNanoseconds heap_used_kib=$peakRssKib",
    )
}

public fun main() {
    val paragraph = "The 42 rows total 3.14 units, and the galley keeps flowing.\n"
    benchmark("large_document", paragraph.repeat(2_000), CompileMode.DOCUMENT)
    benchmark("math_spacing", "a\\,b\\:c\\;d\\!e\\quad f\\qquad g\\ h".repeat(200), CompileMode.MATH_DISPLAY)
}
