/* Allocation-failure injection: every allocation point in compile and dump
 * must fail cleanly — structured TEX_CORE_STATUS_ALLOCATION_FAILED, NULL
 * outputs, and nothing leaked (the sanitizer presets verify the latter).
 * Uses the internal txc_allocation_limit seam (not public API). */

#include <string.h>

#include "harness.h"
#include "memory.h"
#include "tex_core.h"

#define TXC_ALLOCATION_CAP 256

static void txc_sweep(txc_test *test, const char *source, tex_core_mode mode) {
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;

    long compile_successes = 0;
    for (long limit = 0; limit < TXC_ALLOCATION_CAP; limit++) {
        txc_allocation_limit(limit);
        tex_core_render_tree *tree = NULL;
        tex_core_error error;
        tex_core_status status =
            tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, &error);
        txc_allocation_limit(-1);

        if (status == TEX_CORE_STATUS_OK) {
            /* Once compile fits the budget, sweep dump under the same
             * injection before releasing. */
            txc_allocation_limit(limit == 0 ? 0 : limit / 2);
            char *dump = NULL;
            tex_core_status dump_status = tex_core_render_tree_dump(tree, &dump, NULL);
            txc_allocation_limit(-1);
            if (dump_status == TEX_CORE_STATUS_OK) {
                txc_check(test, dump != NULL, "dump output set on success for \"%s\"", source);
            } else {
                txc_check(
                    test,
                    dump_status == TEX_CORE_STATUS_ALLOCATION_FAILED,
                    "dump failure status for \"%s\"",
                    source
                );
                txc_check(test, dump == NULL, "dump stays NULL on failure for \"%s\"", source);
            }
            tex_core_dump_free(dump);
            tex_core_render_tree_free(tree);
            compile_successes += 1;
            break;
        }

        txc_check(
            test,
            status == TEX_CORE_STATUS_ALLOCATION_FAILED,
            "\"%s\" limit %ld: status %d, expected allocation failure",
            source,
            limit,
            (int)status
        );
        txc_check(test, tree == NULL, "\"%s\" limit %ld: tree stays NULL", source, limit);
        txc_check(
            test,
            error.status == TEX_CORE_STATUS_ALLOCATION_FAILED,
            "\"%s\" limit %ld: error status",
            source,
            limit
        );
        txc_check_text(test, error.message, "allocation failed", "allocation-failure message");
    }
    txc_check(test, compile_successes == 1, "\"%s\": compile succeeds within the allocation cap", source);

    /* And a full dump sweep against an uninjected tree. */
    tex_core_render_tree *tree = NULL;
    tex_core_status status = tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, NULL);
    txc_check(test, status == TEX_CORE_STATUS_OK, "\"%s\": uninjected compile", source);
    long dump_successes = 0;
    for (long limit = 0; limit < TXC_ALLOCATION_CAP; limit++) {
        txc_allocation_limit(limit);
        char *dump = NULL;
        tex_core_status dump_status = tex_core_render_tree_dump(tree, &dump, NULL);
        txc_allocation_limit(-1);
        if (dump_status == TEX_CORE_STATUS_OK) {
            tex_core_dump_free(dump);
            dump_successes += 1;
            break;
        }
        txc_check(test, dump_status == TEX_CORE_STATUS_ALLOCATION_FAILED, "\"%s\" dump limit %ld", source, limit);
        txc_check(test, dump == NULL, "\"%s\" dump limit %ld: output stays NULL", source, limit);
    }
    txc_check(test, dump_successes == 1, "\"%s\": dump succeeds within the allocation cap", source);
    tex_core_render_tree_free(tree);
}

int main(void) {
    txc_test test = {0, 0};
    txc_sweep(&test, "", TEX_CORE_MODE_DOCUMENT);
    txc_sweep(&test, "x", TEX_CORE_MODE_MATH_INLINE);
    txc_sweep(&test, "abc \\quad xyz \\, tail", TEX_CORE_MODE_DOCUMENT);
    txc_sweep(&test, "The quick brown fox jumps over the lazy dog 0123456789.", TEX_CORE_MODE_DOCUMENT);
    /* Classed math atoms, symbol commands, and inter-atom spacing kerns
     * allocate in parse and layout; sweep both math modes over them. */
    txc_sweep(&test, "\\alpha+(\\beta-1)=\\gamma, x", TEX_CORE_MODE_MATH_DISPLAY);
    txc_sweep(&test, "a\\,+b\\;=c", TEX_CORE_MODE_MATH_INLINE);
    /* Scripts allocate their boxes; fractions allocate the construct, its
     * part boxes, the bar, and the null-delimiter kerns. */
    txc_sweep(&test, "x_1^\\frac{a+b}{2}+\\tfrac12", TEX_CORE_MODE_MATH_DISPLAY);
    /* Fences allocate the construct, both delimiter nodes or assemblies,
     * and the spliced box; \binom adds the sized parentheses. */
    txc_sweep(&test, "\\left[\\binom{n}{k}\\right]+\\bigl(x\\bigr)", TEX_CORE_MODE_MATH_DISPLAY);
    /* Radicals allocate the construct, the sign, the bar, the index box,
     * and its flanking kerns. */
    txc_sweep(&test, "\\sqrt[3]{x+1}+\\sqrt2", TEX_CORE_MODE_MATH_DISPLAY);
    /* Operators allocate the size-face glyph, the limit boxes, and the
     * assembly; function names allocate their letter runs. */
    txc_sweep(&test, "\\sum_{i=1}^{n}\\lim_{x}\\int\\limits_a^b", TEX_CORE_MODE_MATH_DISPLAY);
    /* Accents allocate the construct, the accent glyph, and the box. */
    txc_sweep(&test, "\\hat{x}+\\widehat{y+z}^2", TEX_CORE_MODE_MATH_DISPLAY);
    /* Style switches rewrite in place; \text allocates its word list. */
    txc_sweep(&test, "\\mathbf{ab1}+\\text{on it}^2", TEX_CORE_MODE_MATH_DISPLAY);
    txc_allocation_limit(-1);
    return txc_test_finish(&test, "allocfail");
}
