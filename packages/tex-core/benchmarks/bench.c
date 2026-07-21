/* Benchmark executable (CTest label `benchmark`, serial): reports
 * informational numbers and never gates on them — regressions surface
 * through CI trend review, not flaky wall-clock asserts. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tex_core.h"

static const char TXC_BENCH_PHRASE[] = "The quick brown fox \\quad jumps over the lazy dog 0123456789. \\, ";
#define TXC_BENCH_TARGET_BYTES 65536
#define TXC_BENCH_ITERATIONS 64

/* Standard C11 wall clock: POSIX monotonic clocks do not exist on MSVC,
 * and these informational numbers do not warrant a per-platform timer. */
static double txc_now(void) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static int txc_bench(const char *name, const char *source, size_t length, tex_core_mode mode) {
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;

    double start = txc_now();
    size_t dump_bytes = 0;
    for (int iteration = 0; iteration < TXC_BENCH_ITERATIONS; iteration++) {
        tex_core_render_tree *tree = NULL;
        tex_core_error error;
        if (tex_core_document_compile((const uint8_t *)source, length, &options, &tree, &error) != TEX_CORE_STATUS_OK) {
            fprintf(stderr, "benchmark %s: compile failed: %s\n", name, error.message);
            return 1;
        }
        char *dump = NULL;
        size_t dump_length = 0;
        if (tex_core_render_tree_dump(tree, &dump, &dump_length) != TEX_CORE_STATUS_OK) {
            tex_core_render_tree_free(tree);
            fprintf(stderr, "benchmark %s: dump failed\n", name);
            return 1;
        }
        dump_bytes = dump_length;
        tex_core_dump_free(dump);
        tex_core_render_tree_free(tree);
    }
    double elapsed = txc_now() - start;

    printf(
        "benchmark %s source_bytes=%zu dump_bytes=%zu iterations=%d total_seconds=%.3f ms_per_iteration=%.3f\n",
        name,
        length,
        dump_bytes,
        TXC_BENCH_ITERATIONS,
        elapsed,
        elapsed * 1000.0 / TXC_BENCH_ITERATIONS
    );
    return 0;
}

int main(void) {
    size_t phrase = strlen(TXC_BENCH_PHRASE);
    size_t repeats = TXC_BENCH_TARGET_BYTES / phrase;
    char *document = malloc(repeats * phrase + 1);
    if (document == NULL) {
        fputs("benchmark: workload allocation failed\n", stderr);
        return 1;
    }
    for (size_t index = 0; index < repeats; index++) {
        memcpy(document + index * phrase, TXC_BENCH_PHRASE, phrase);
    }
    document[repeats * phrase] = '\0';

    int failures = 0;
    failures += txc_bench("compile-document", document, repeats * phrase, TEX_CORE_MODE_DOCUMENT);
    failures += txc_bench("compile-math-inline", document, repeats * phrase, TEX_CORE_MODE_MATH_INLINE);
    failures += txc_bench("compile-trivial", "x", 1, TEX_CORE_MODE_MATH_INLINE);
    free(document);
    return failures == 0 ? 0 : 1;
}
