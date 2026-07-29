/* Benchmark executable (CTest label `benchmark`, serial): reports
 * informational numbers and never gates on them — regressions surface
 * through the PR metrics trend (scripts/collect-pr-metrics.mjs picks up
 * the `runtime=c` lines), not flaky wall-clock asserts.
 *
 * Methodology: a monotonic clock, warmup iterations, and the median of
 * repeated runs; compile (parse+layout+free) and canonical dump are timed
 * as separate boundaries so a regression is attributable. The two
 * workloads mirror the Swift/Kotlin/ES benchmark corpus so the trend
 * report compares like with like. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tex_core.h"

#define TXC_BENCH_WARMUP 3
#define TXC_BENCH_REPEATS 10

/* Monotonic on POSIX; MSVC has no CLOCK_MONOTONIC, so it falls back to the
 * C11 wall clock there (informational numbers on the one platform without
 * a portable monotonic timespec source). */
static uint64_t txc_now_ns(void) {
    struct timespec now;
#if defined(_WIN32)
    timespec_get(&now, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &now);
#endif
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static int txc_compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static uint64_t txc_median(uint64_t *samples, size_t count) {
    qsort(samples, count, sizeof(*samples), txc_compare_u64);
    return samples[count / 2];
}

static int txc_bench(const char *workload, const char *source, tex_core_mode mode) {
    size_t length = strlen(source);
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;

    uint64_t compile_ns[TXC_BENCH_REPEATS];
    uint64_t dump_ns[TXC_BENCH_REPEATS];
    for (int iteration = 0; iteration < TXC_BENCH_WARMUP + TXC_BENCH_REPEATS; iteration++) {
        tex_core_render_tree *tree = NULL;
        tex_core_error error;
        uint64_t start = txc_now_ns();
        if (tex_core_document_compile((const uint8_t *)source, length, &options, &tree, &error) != TEX_CORE_STATUS_OK) {
            fprintf(stderr, "benchmark %s: compile failed: %s\n", workload, error.message);
            return 1;
        }
        uint64_t compiled = txc_now_ns();
        char *dump = NULL;
        size_t dump_length = 0;
        if (tex_core_render_tree_dump(tree, &dump, &dump_length) != TEX_CORE_STATUS_OK) {
            tex_core_render_tree_free(tree);
            fprintf(stderr, "benchmark %s: dump failed\n", workload);
            return 1;
        }
        uint64_t dumped = txc_now_ns();
        tex_core_dump_free(dump);
        tex_core_render_tree_free(tree);
        if (iteration >= TXC_BENCH_WARMUP) {
            compile_ns[iteration - TXC_BENCH_WARMUP] = compiled - start;
            dump_ns[iteration - TXC_BENCH_WARMUP] = dumped - compiled;
        }
    }

    /* One collector-visible line per workload (`median_ns` is the compile
     * boundary, comparable with the other runtimes' compile numbers); the
     * dump boundary rides along under its own key so the collector never
     * confuses the two. */
    printf(
        "benchmark runtime=c boundary=native_compile workload=%s workload_version=1 "
        "bytes=%zu warmup=%d repeats=%d "
        "median_ns=%llu dump_median_ns=%llu\n",
        workload,
        length,
        TXC_BENCH_WARMUP,
        TXC_BENCH_REPEATS,
        (unsigned long long)txc_median(compile_ns, TXC_BENCH_REPEATS),
        (unsigned long long)txc_median(dump_ns, TXC_BENCH_REPEATS)
    );
    return 0;
}

static char *txc_repeat(const char *phrase, size_t repeats) {
    size_t phrase_length = strlen(phrase);
    char *source = malloc(phrase_length * repeats + 1);
    if (source == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < repeats; index++) {
        memcpy(source + index * phrase_length, phrase, phrase_length);
    }
    source[phrase_length * repeats] = '\0';
    return source;
}

int main(void) {
    /* The shared benchmark corpus (see the ES/Swift/Kotlin benchmarks). */
    char *document = txc_repeat("The 42 rows total 3.14 units, and the galley keeps flowing.\n", 2000);
    char *math = txc_repeat("a\\,b\\:c\\;d\\!e\\quad f\\qquad g\\ h", 200);
    if (document == NULL || math == NULL) {
        free(document);
        free(math);
        fputs("benchmark: workload allocation failed\n", stderr);
        return 1;
    }

    int failures = 0;
    failures += txc_bench("large_document", document, TEX_CORE_MODE_DOCUMENT);
    failures += txc_bench("math_spacing", math, TEX_CORE_MODE_MATH_DISPLAY);
    free(document);
    free(math);
    return failures == 0 ? 0 : 1;
}
