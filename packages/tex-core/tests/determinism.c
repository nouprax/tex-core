/* Dump determinism: repeated compiles of the same source yield byte-equal
 * canonical dumps in every mode. (Cross-platform byte equality rides on the
 * integer scaled-point arithmetic and is enforced across the CI matrix.) */

#include <string.h>

#include "harness.h"
#include "tex_core.h"

static const char *const TXC_SAMPLES[] = {
    "",
    "x",
    "fghjpqy",
    "The quick brown fox jumps over the lazy dog 0123456789.",
    "a\\quad b\\qquad c",
    "\\,\\:\\;\\!\\ x",
    "words  with\truns\nand lines",
};

/* Math-only samples: classed atoms, symbol commands, and inter-atom
 * spacing are math-mode surface, so these compile in the two math modes
 * only. */
static const char *const TXC_MATH_SAMPLES[] = {
    "-a+(-b)=-c*-d, -e*(f-)<g-=h-, k-",
    "\\alpha\\pm\\beta\\le\\gamma\\to\\delta",
    "(x, y; z)!?",
    "a\\,+b\\;=c",
    "\\lbrace\\langle x|y\\rangle\\rbrace\\{z\\}",
};

static char *txc_dump_once(txc_test *test, const char *source, tex_core_mode mode) {
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;
    tex_core_render_tree *tree = NULL;
    tex_core_error error;
    tex_core_status status =
        tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, &error);
    txc_check(test, status == TEX_CORE_STATUS_OK, "compile \"%s\": %s", source, error.message);
    if (status != TEX_CORE_STATUS_OK) {
        return NULL;
    }
    char *dump = NULL;
    size_t length = 0;
    txc_check(test, tex_core_render_tree_dump(tree, &dump, &length) == TEX_CORE_STATUS_OK, "dump \"%s\"", source);
    if (dump != NULL) {
        txc_check(test, length == strlen(dump), "dump length for \"%s\"", source);
        txc_check(test, length > 0 && dump[length - 1] == '\n', "dump ends with a newline for \"%s\"", source);
    }
    tex_core_render_tree_free(tree);
    return dump;
}

int main(void) {
    txc_test test = {0, 0};
    static const tex_core_mode modes[] =
        {TEX_CORE_MODE_DOCUMENT, TEX_CORE_MODE_MATH_INLINE, TEX_CORE_MODE_MATH_DISPLAY};
    for (size_t sample = 0; sample < sizeof(TXC_SAMPLES) / sizeof(TXC_SAMPLES[0]); sample++) {
        const char *source = TXC_SAMPLES[sample];
        for (size_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
            char *first = txc_dump_once(&test, source, modes[mode]);
            char *second = txc_dump_once(&test, source, modes[mode]);
            txc_check(
                &test,
                first != NULL && second != NULL && strcmp(first, second) == 0,
                "repeated dumps differ for \"%s\" in mode %d",
                source,
                (int)modes[mode]
            );
            tex_core_dump_free(first);
            tex_core_dump_free(second);
        }
    }
    for (size_t sample = 0; sample < sizeof(TXC_MATH_SAMPLES) / sizeof(TXC_MATH_SAMPLES[0]); sample++) {
        const char *source = TXC_MATH_SAMPLES[sample];
        for (size_t mode = 1; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
            char *first = txc_dump_once(&test, source, modes[mode]);
            char *second = txc_dump_once(&test, source, modes[mode]);
            txc_check(
                &test,
                first != NULL && second != NULL && strcmp(first, second) == 0,
                "repeated dumps differ for \"%s\" in mode %d",
                source,
                (int)modes[mode]
            );
            tex_core_dump_free(first);
            tex_core_dump_free(second);
        }
    }
    return txc_test_finish(&test, "determinism");
}
