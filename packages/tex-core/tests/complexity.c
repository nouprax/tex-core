/* Compile-complexity regression gate.
 *
 * Each case compares time per input byte at a small and a large endpoint.
 * Every endpoint is compiled once before adaptive timed sampling, so a hot
 * small endpoint is never compared with a cold large endpoint. Short samples
 * use the median of three >=25 ms buckets; when the first post-warmup bucket
 * already contains one long compile, that complete compile is the sample.
 *
 * The gate checks an asymptotic ratio, never an absolute wall-clock limit. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tex_core.h"

#define TXC_COMPLEXITY_REPEATS 3
#define TXC_MIN_SAMPLE_NS 25000000u
#define TXC_MAX_NORMALIZED_SLOWDOWN 4.0

typedef struct {
    const char *name;
    tex_core_mode mode;
    const char *pattern;
    size_t small_bytes;
    size_t large_bytes;
} txc_complexity_case;

static const txc_complexity_case TXC_COMPLEXITY_CASES[] = {
    {"document", TEX_CORE_MODE_DOCUMENT, "The 42 rows total 3.14 units. ", 4u * 1024u, 1024u * 1024u},
    {"math", TEX_CORE_MODE_MATH_DISPLAY, "a+b+c+d+", 4u * 1024u, 64u * 1024u},
};

static uint64_t txc_now_ns(void) {
    struct timespec now;
#if defined(_WIN32)
    timespec_get(&now, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &now);
#endif
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static int txc_compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static char *txc_source(const txc_complexity_case *test_case, size_t length) {
    size_t pattern_length = strlen(test_case->pattern);
    char *source = malloc(length + 1);
    if (source == NULL) {
        return NULL;
    }
    for (size_t offset = 0; offset < length; offset++) {
        source[offset] = test_case->pattern[offset % pattern_length];
    }
    if (test_case->mode != TEX_CORE_MODE_DOCUMENT) {
        source[length - 1] = 'a';
    }
    source[length] = '\0';
    return source;
}

static int txc_compile_once(const char *source, size_t length, const tex_core_options *options, const char *endpoint) {
    tex_core_render_tree *tree = NULL;
    tex_core_error error;
    tex_core_status status = tex_core_document_compile((const uint8_t *)source, length, options, &tree, &error);
    if (status != TEX_CORE_STATUS_OK) {
        fprintf(stderr, "complexity %s: compile failed: %s\n", endpoint, error.message);
        return 1;
    }
    tex_core_render_tree_free(tree);
    return 0;
}

static int txc_measure(
    const char *source,
    size_t length,
    const tex_core_options *options,
    const char *endpoint,
    double *seconds
) {
    double samples[TXC_COMPLEXITY_REPEATS];

    /* Warm every endpoint once outside the timer. The adaptive policy below
     * therefore classifies and measures only post-warmup work. */
    if (txc_compile_once(source, length, options, endpoint) != 0) {
        return 1;
    }

    for (size_t repeat = 0; repeat < TXC_COMPLEXITY_REPEATS; repeat++) {
        uint64_t started = txc_now_ns();
        uint64_t elapsed;
        size_t iterations = 0;
        do {
            if (txc_compile_once(source, length, options, endpoint) != 0) {
                return 1;
            }
            iterations++;
            elapsed = txc_now_ns() - started;
        } while (elapsed < TXC_MIN_SAMPLE_NS);

        samples[repeat] = (double)elapsed / (1e9 * (double)iterations);
        /* Classify from the first post-warmup bucket. A single compile is
         * already a long sample; later single-compile buckets can be scheduler
         * outliers and remain covered by the median path. */
        if (repeat == 0 && iterations == 1) {
            *seconds = samples[repeat];
            return 0;
        }
    }

    qsort(samples, TXC_COMPLEXITY_REPEATS, sizeof(*samples), txc_compare_double);
    *seconds = samples[TXC_COMPLEXITY_REPEATS / 2];
    return 0;
}

static int txc_run(const txc_complexity_case *test_case) {
    char *small = txc_source(test_case, test_case->small_bytes);
    char *large = txc_source(test_case, test_case->large_bytes);
    if (small == NULL || large == NULL) {
        free(small);
        free(large);
        fprintf(stderr, "complexity %s: source allocation failed\n", test_case->name);
        return 1;
    }

    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = test_case->mode;

    double small_seconds = 0.0;
    double large_seconds = 0.0;
    int failed = txc_measure(small, test_case->small_bytes, &options, "small", &small_seconds) != 0 ||
                 txc_measure(large, test_case->large_bytes, &options, "large", &large_seconds) != 0;
    free(small);
    free(large);
    if (failed) {
        return 1;
    }

    double small_per_byte = small_seconds / (double)test_case->small_bytes;
    double large_per_byte = large_seconds / (double)test_case->large_bytes;
    double normalized_slowdown = large_per_byte / small_per_byte;
    printf(
        "complexity case=%s small_bytes=%zu large_bytes=%zu "
        "small_seconds=%.9f large_seconds=%.9f normalized_slowdown=%.3f\n",
        test_case->name,
        test_case->small_bytes,
        test_case->large_bytes,
        small_seconds,
        large_seconds,
        normalized_slowdown
    );
    if (normalized_slowdown > TXC_MAX_NORMALIZED_SLOWDOWN) {
        fprintf(
            stderr,
            "complexity %s: per-byte cost grew %.3fx (limit %.1fx)\n",
            test_case->name,
            normalized_slowdown,
            TXC_MAX_NORMALIZED_SLOWDOWN
        );
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    size_t case_count = sizeof(TXC_COMPLEXITY_CASES) / sizeof(*TXC_COMPLEXITY_CASES);
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t index = 0; index < case_count; index++) {
            puts(TXC_COMPLEXITY_CASES[index].name);
        }
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: %s --list|CASE\n", argv[0]);
        return 2;
    }
    for (size_t index = 0; index < case_count; index++) {
        if (strcmp(argv[1], TXC_COMPLEXITY_CASES[index].name) == 0) {
            return txc_run(&TXC_COMPLEXITY_CASES[index]);
        }
    }
    fprintf(stderr, "unknown complexity case: %s\n", argv[1]);
    return 2;
}
