/* Conformance runner (CTest label `conformance`). Each CTest invocation is
 * one case of specs/render-tree/manifest.json, enumerated by CMake:
 *
 *   conformance <input.tex> <expected.tree> <mode> <outcome>
 *
 * A `tree` case compiles the input through the public API and compares the
 * canonical dump (docs/specs/render-tree-dump.md) byte for byte with the
 * reviewed golden, dumping twice to assert dump determinism. An `error`
 * case expects compile to fail and compares the corpus error record
 * (specs/render-tree/README.md) built from the public error value. Goldens
 * are never rewritten by tests. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "tex_core.h"

static uint8_t *txc_read_file(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        return NULL;
    }
    uint8_t *data = NULL;
    size_t used = 0;
    size_t capacity = 0;
    for (;;) {
        if (capacity - used < 4096) {
            capacity = capacity > 0 ? capacity * 2 : 8192;
            uint8_t *grown = realloc(data, capacity + 1);
            if (grown == NULL) {
                free(data);
                fclose(stream);
                return NULL;
            }
            data = grown;
        }
        size_t read = fread(data + used, 1, capacity - used, stream);
        used += read;
        if (read == 0) {
            break;
        }
    }
    int failed = ferror(stream);
    fclose(stream);
    if (failed) {
        free(data);
        return NULL;
    }
    data[used] = '\0';
    *length = used;
    return data;
}

static const char *txc_status_name(tex_core_status status) {
    switch (status) {
    case TEX_CORE_STATUS_OK:
        return "ok";
    case TEX_CORE_STATUS_INVALID_ARGUMENT:
        return "invalid-argument";
    case TEX_CORE_STATUS_INVALID_UTF8:
        return "invalid-utf8";
    case TEX_CORE_STATUS_UNSUPPORTED:
        return "unsupported";
    case TEX_CORE_STATUS_ALLOCATION_FAILED:
        return "allocation-failed";
    }
    return "unknown";
}

/* Renders the corpus error record for a failed compile. */
static void txc_error_record(const tex_core_error *error, char *record, size_t capacity) {
    if (error->has_range) {
        snprintf(
            record,
            capacity,
            "render-error 2\nerror status=%s src=%zu..%zu message=%s\n",
            txc_status_name(error->status),
            error->range.begin,
            error->range.end,
            error->message
        );
    } else {
        snprintf(
            record,
            capacity,
            "render-error 2\nerror status=%s src=none message=%s\n",
            txc_status_name(error->status),
            error->message
        );
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fputs("usage: conformance <input.tex> <expected.tree> <mode> <outcome>\n", stderr);
        return 2;
    }
    const char *input_path = argv[1];
    const char *expected_path = argv[2];
    const char *mode_name = argv[3];
    const char *outcome = argv[4];

    tex_core_options options;
    tex_core_options_init(&options);
    if (strcmp(mode_name, "document") == 0) {
        options.mode = TEX_CORE_MODE_DOCUMENT;
    } else if (strcmp(mode_name, "mathInline") == 0) {
        options.mode = TEX_CORE_MODE_MATH_INLINE;
    } else if (strcmp(mode_name, "mathDisplay") == 0) {
        options.mode = TEX_CORE_MODE_MATH_DISPLAY;
    } else {
        fprintf(stderr, "conformance: unknown mode: %s\n", mode_name);
        return 2;
    }

    size_t source_length = 0;
    size_t expected_length = 0;
    uint8_t *source = txc_read_file(input_path, &source_length);
    uint8_t *expected = txc_read_file(expected_path, &expected_length);
    if (source == NULL || expected == NULL) {
        fprintf(stderr, "conformance: cannot read %s\n", source == NULL ? input_path : expected_path);
        free(source);
        free(expected);
        return 2;
    }

    txc_test test = {0, 0};
    tex_core_render_tree *tree = NULL;
    tex_core_error error;
    tex_core_status status = tex_core_document_compile(source, source_length, &options, &tree, &error);

    if (strcmp(outcome, "tree") == 0) {
        txc_check(&test, status == TEX_CORE_STATUS_OK, "compile succeeds: %s", error.message);
        if (status == TEX_CORE_STATUS_OK) {
            char *dump = NULL;
            size_t dump_length = 0;
            txc_check(
                &test,
                tex_core_render_tree_dump(tree, &dump, &dump_length) == TEX_CORE_STATUS_OK,
                "dump succeeds"
            );
            if (dump != NULL) {
                txc_check_size(&test, dump_length, expected_length, "dump length matches the golden");
                txc_check_text(&test, dump, (const char *)expected, "dump matches the golden");

                /* Dump determinism: a second dump must be byte-identical. */
                char *second = NULL;
                size_t second_length = 0;
                txc_check(
                    &test,
                    tex_core_render_tree_dump(tree, &second, &second_length) == TEX_CORE_STATUS_OK &&
                        second_length == dump_length && memcmp(dump, second, dump_length) == 0,
                    "dump is deterministic"
                );
                tex_core_dump_free(second);
            }
            tex_core_dump_free(dump);
        }
    } else if (strcmp(outcome, "error") == 0) {
        txc_check(&test, status != TEX_CORE_STATUS_OK, "compile fails");
        txc_check(&test, tree == NULL, "no tree on failure");
        char record[TEX_CORE_ERROR_MESSAGE_CAPACITY + 96];
        txc_error_record(&error, record, sizeof(record));
        txc_check_text(&test, record, (const char *)expected, "error record matches the golden");
    } else {
        fprintf(stderr, "conformance: unknown outcome: %s\n", outcome);
        free(source);
        free(expected);
        return 2;
    }

    tex_core_render_tree_free(tree);
    free(source);
    free(expected);
    return txc_test_finish(&test, "conformance");
}
