/* Concurrency contract (no process-global mutable state): concurrent
 * compiles must be independent and deterministic, and one immutable tree
 * must be safely readable from many threads at once. This suite gives the
 * TSan preset real work on both halves of that contract. POSIX threads;
 * the suite is only added on POSIX hosts. */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "tex_core.h"

#define TXC_THREADS 8
#define TXC_ITERATIONS 200

typedef struct txc_worker {
    const char *source;
    tex_core_mode mode;
    const char *expected;
    int mismatches;
    int failures;
} txc_worker;

static char *txc_dump_source(const char *source, tex_core_mode mode) {
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;
    tex_core_render_tree *tree = NULL;
    if (tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, NULL) !=
        TEX_CORE_STATUS_OK) {
        return NULL;
    }
    char *dump = NULL;
    if (tex_core_render_tree_dump(tree, &dump, NULL) != TEX_CORE_STATUS_OK) {
        dump = NULL;
    }
    tex_core_render_tree_free(tree);
    return dump;
}

static void *txc_worker_main(void *argument) {
    txc_worker *worker = argument;
    for (int iteration = 0; iteration < TXC_ITERATIONS; iteration++) {
        char *dump = txc_dump_source(worker->source, worker->mode);
        if (dump == NULL) {
            worker->failures += 1;
        } else if (strcmp(dump, worker->expected) != 0) {
            worker->mismatches += 1;
        }
        tex_core_dump_free(dump);
    }
    return NULL;
}

/* One shared immutable tree read by every worker at once: each iteration
 * traverses the tree through the public node views and re-dumps it, so
 * TSan observes genuinely concurrent reads of the same nodes. */
typedef struct txc_reader {
    const tex_core_render_tree *tree;
    const char *expected;
    int mismatches;
    int failures;
} txc_reader;

static size_t txc_count_nodes(const tex_core_node *node) {
    size_t count = 1;
    size_t children = tex_core_node_child_count(node);
    for (size_t index = 0; index < children; index++) {
        count += txc_count_nodes(tex_core_node_child(node, index));
    }
    (void)tex_core_node_get_kind(node);
    (void)tex_core_node_frame(node);
    (void)tex_core_node_glyph(node);
    (void)tex_core_node_range(node);
    return count;
}

static void *txc_reader_main(void *argument) {
    txc_reader *reader = argument;
    size_t expected_nodes = txc_count_nodes(tex_core_render_tree_root(reader->tree));
    for (int iteration = 0; iteration < TXC_ITERATIONS; iteration++) {
        if (txc_count_nodes(tex_core_render_tree_root(reader->tree)) != expected_nodes) {
            reader->failures += 1;
        }
        char *dump = NULL;
        if (tex_core_render_tree_dump(reader->tree, &dump, NULL) != TEX_CORE_STATUS_OK) {
            reader->failures += 1;
        } else if (strcmp(dump, reader->expected) != 0) {
            reader->mismatches += 1;
        }
        tex_core_dump_free(dump);
    }
    return NULL;
}

int main(void) {
    txc_test test = {0, 0};
    static const struct {
        const char *source;
        tex_core_mode mode;
    } workloads[] = {
        {"abc \\quad xyz", TEX_CORE_MODE_DOCUMENT},
        {"fghjpqy", TEX_CORE_MODE_MATH_INLINE},
    };

    for (size_t workload = 0; workload < sizeof(workloads) / sizeof(workloads[0]); workload++) {
        char *expected = txc_dump_source(workloads[workload].source, workloads[workload].mode);
        txc_check(&test, expected != NULL, "reference dump for \"%s\"", workloads[workload].source);
        if (expected == NULL) {
            continue;
        }

        txc_worker workers[TXC_THREADS];
        pthread_t threads[TXC_THREADS];
        bool started[TXC_THREADS];
        for (int index = 0; index < TXC_THREADS; index++) {
            workers[index].source = workloads[workload].source;
            workers[index].mode = workloads[workload].mode;
            workers[index].expected = expected;
            workers[index].mismatches = 0;
            workers[index].failures = 0;
            started[index] = pthread_create(&threads[index], NULL, txc_worker_main, &workers[index]) == 0;
            txc_check(&test, started[index], "thread %d starts", index);
        }
        for (int index = 0; index < TXC_THREADS; index++) {
            if (!started[index]) {
                continue;
            }
            pthread_join(threads[index], NULL);
            txc_check(&test, workers[index].failures == 0, "thread %d compiles cleanly", index);
            txc_check(&test, workers[index].mismatches == 0, "thread %d dumps identically", index);
        }
        tex_core_dump_free(expected);
    }

    /* Shared-tree scenario: compile once, then read the same tree from
     * every thread concurrently — the strongest published guarantee. */
    {
        const char *source = "\\frac{a+b}{2} \\sqrt{x^2}";
        tex_core_options options;
        tex_core_options_init(&options);
        options.mode = TEX_CORE_MODE_MATH_INLINE;
        tex_core_render_tree *tree = NULL;
        txc_check(
            &test,
            tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, NULL) ==
                TEX_CORE_STATUS_OK,
            "shared tree compiles"
        );
        if (tree != NULL) {
            char *expected = NULL;
            txc_check(
                &test,
                tex_core_render_tree_dump(tree, &expected, NULL) == TEX_CORE_STATUS_OK,
                "shared tree reference dump"
            );
            txc_reader readers[TXC_THREADS];
            pthread_t threads[TXC_THREADS];
            bool started[TXC_THREADS];
            for (int index = 0; index < TXC_THREADS; index++) {
                readers[index].tree = tree;
                readers[index].expected = expected;
                readers[index].mismatches = 0;
                readers[index].failures = 0;
                started[index] = pthread_create(&threads[index], NULL, txc_reader_main, &readers[index]) == 0;
                txc_check(&test, started[index], "reader %d starts", index);
            }
            for (int index = 0; index < TXC_THREADS; index++) {
                if (!started[index]) {
                    continue;
                }
                pthread_join(threads[index], NULL);
                txc_check(&test, readers[index].failures == 0, "reader %d traverses cleanly", index);
                txc_check(&test, readers[index].mismatches == 0, "reader %d dumps identically", index);
            }
            tex_core_dump_free(expected);
            tex_core_render_tree_free(tree);
        }
    }
    return txc_test_finish(&test, "threads");
}
