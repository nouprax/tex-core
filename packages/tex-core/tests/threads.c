/* Concurrency contract (plan decision D6): no process-global mutable state,
 * so concurrent compiles must be independent and deterministic. This suite
 * gives the TSan preset real work. POSIX threads; the suite is only added
 * on POSIX hosts. */

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
        for (int index = 0; index < TXC_THREADS; index++) {
            workers[index].source = workloads[workload].source;
            workers[index].mode = workloads[workload].mode;
            workers[index].expected = expected;
            workers[index].mismatches = 0;
            workers[index].failures = 0;
            txc_check(
                &test,
                pthread_create(&threads[index], NULL, txc_worker_main, &workers[index]) == 0,
                "thread %d starts",
                index
            );
        }
        for (int index = 0; index < TXC_THREADS; index++) {
            pthread_join(threads[index], NULL);
            txc_check(&test, workers[index].failures == 0, "thread %d compiles cleanly", index);
            txc_check(&test, workers[index].mismatches == 0, "thread %d dumps identically", index);
        }
        tex_core_dump_free(expected);
    }
    return txc_test_finish(&test, "threads");
}
