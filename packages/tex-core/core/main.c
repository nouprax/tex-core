/* tex-core CLI: a repository-owned diagnostic adapter over the public API,
 * for debugging and CI evidence. Reads LaTeX source from a file argument or
 * stdin and writes the canonical render-tree dump to stdout. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tex_core.h"

static void txc_cli_usage(FILE *stream) {
    fputs(
        "Usage: tex-core [--mode document|math-inline|math-display] [--version] [file]\n"
        "\n"
        "Compiles UTF-8 LaTeX source from `file` (or stdin) and writes the\n"
        "canonical render-tree dump to stdout.\n",
        stream
    );
}

static int txc_cli_read(FILE *stream, uint8_t **source, size_t *length) {
    uint8_t *data = NULL;
    size_t used = 0;
    size_t capacity = 0;
    for (;;) {
        if (capacity - used < 4096) {
            if (capacity > SIZE_MAX / 2) {
                free(data);
                return -1;
            }
            capacity = capacity > 0 ? capacity * 2 : 8192;
            uint8_t *grown = realloc(data, capacity);
            if (grown == NULL) {
                free(data);
                return -1;
            }
            data = grown;
        }
        size_t read = fread(data + used, 1, capacity - used, stream);
        used += read;
        if (read == 0) {
            if (ferror(stream)) {
                free(data);
                return -1;
            }
            break;
        }
    }
    *source = data;
    *length = used;
    return 0;
}

int main(int argc, char *argv[]) {
    tex_core_options options;
    tex_core_options_init(&options);
    const char *path = NULL;

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        if (strcmp(argument, "--version") == 0) {
            printf("tex-core %s\n", tex_core_version_string());
            return 0;
        }
        if (strcmp(argument, "--help") == 0) {
            txc_cli_usage(stdout);
            return 0;
        }
        if (strcmp(argument, "--mode") == 0) {
            if (index + 1 >= argc) {
                fputs("tex-core: --mode requires a value\n", stderr);
                return 2;
            }
            const char *mode = argv[++index];
            if (strcmp(mode, "document") == 0) {
                options.mode = TEX_CORE_MODE_DOCUMENT;
            } else if (strcmp(mode, "math-inline") == 0) {
                options.mode = TEX_CORE_MODE_MATH_INLINE;
            } else if (strcmp(mode, "math-display") == 0) {
                options.mode = TEX_CORE_MODE_MATH_DISPLAY;
            } else {
                fprintf(stderr, "tex-core: unknown mode: %s\n", mode);
                return 2;
            }
            continue;
        }
        if (argument[0] == '-' && argument[1] != '\0') {
            fprintf(stderr, "tex-core: unknown option: %s\n", argument);
            txc_cli_usage(stderr);
            return 2;
        }
        if (path != NULL) {
            fputs("tex-core: only one input file is supported\n", stderr);
            return 2;
        }
        path = argument;
    }

    FILE *stream = stdin;
    if (path != NULL && strcmp(path, "-") != 0) {
        stream = fopen(path, "rb");
        if (stream == NULL) {
            fprintf(stderr, "tex-core: cannot open %s\n", path);
            return 2;
        }
    }
    uint8_t *source;
    size_t length;
    int read_result = txc_cli_read(stream, &source, &length);
    if (stream != stdin) {
        fclose(stream);
    }
    if (read_result != 0) {
        fputs("tex-core: reading input failed\n", stderr);
        return 2;
    }

    tex_core_render_tree *tree;
    tex_core_error error;
    tex_core_status status = tex_core_document_compile(source, length, &options, &tree, &error);
    free(source);
    if (status != TEX_CORE_STATUS_OK) {
        if (error.has_range) {
            fprintf(
                stderr,
                "tex-core: error: %s (bytes %zu..%zu)\n",
                error.message,
                error.range.begin,
                error.range.end
            );
        } else {
            fprintf(stderr, "tex-core: error: %s\n", error.message);
        }
        return 1;
    }

    char *dump;
    size_t dump_length;
    status = tex_core_render_tree_dump(tree, &dump, &dump_length);
    if (status != TEX_CORE_STATUS_OK) {
        tex_core_render_tree_free(tree);
        fputs("tex-core: error: allocation failed\n", stderr);
        return 1;
    }
    size_t written = fwrite(dump, 1, dump_length, stdout);
    tex_core_dump_free(dump);
    tex_core_render_tree_free(tree);
    /* A truncated dump must not exit 0: scripts treat the CLI's status as
     * the compile verdict, so short writes, closed pipes, and deferred
     * flush errors all have to surface here. */
    if (written != dump_length || fflush(stdout) != 0 || ferror(stdout)) {
        fputs("tex-core: writing output failed\n", stderr);
        return 1;
    }
    return 0;
}
